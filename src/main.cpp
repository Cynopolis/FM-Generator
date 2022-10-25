#include <Arduino.h>
// fm modulation
// pin to read in analog audio signal
#define sound_in_pin A0
// pin to output fm audio signal
#define FM_out_pin 2
#define sync_pin 2

/**
 * @brief Read the given analog channel. THIS DOES NOT WORK UNLESS THE ADC CHANNEL IS INITIALIZED
 * @param channel the channel to read
 */
uint32_t fastAnalogRead(adc_channel_num_t analogChannel){
  uint32_t analog_val = 0;

  analog_val = adc_get_channel_value(ADC, analogChannel);
	return analog_val;
}

/**
  * @brief set up the ADC to read a 22kHz audio signal
  * @param analogChannel the analog channel to configure
*/
void setupADC(adc_channel_num_t analogChannel){
  

  // disable write protection so we can configure the ADC
  adc_set_writeprotect(ADC, 0);
  // Configure the ADC to read at 1 MHz
  adc_init(ADC, SystemCoreClock, 1000000, 8);
  // enable free running mode
  ADC->ADC_MR |= ADC_MR_FREERUN_ON;
  // enable the channel
  adc_enable_channel(ADC, analogChannel);
  analogRead(analogChannel);
  //adc_start(ADC);
}



static inline uint32_t mapResolution(uint32_t value, uint32_t from, uint32_t to) {
	if (from == to)
		return value;
	if (from > to)
		return value >> (from-to);
	else
		return value << (to-from);
}

static uint8_t TCChanEnabled[] = {0, 0, 0, 0, 0, 0, 0, 0, 0};

static Tc *channelToTC[] = {
			TC0, TC0, TC0, TC0, TC0, TC0,
			TC1, TC1, TC1, TC1, TC1, TC1,
			TC2, TC2, TC2, TC2, TC2, TC2 };

static const uint32_t channelToChNo[] = { 0, 0, 1, 1, 2, 2, 0, 0, 1, 1, 2, 2, 0, 0, 1, 1, 2, 2 };

static void TC_SetCMR_ChannelA(Tc *tc, uint32_t chan, uint32_t v)
{
	tc->TC_CHANNEL[chan].TC_CMR = (tc->TC_CHANNEL[chan].TC_CMR & 0xFFF0FFFF) | v;
}

static void TC_SetCMR_ChannelB(Tc *tc, uint32_t chan, uint32_t v)
{
	tc->TC_CHANNEL[chan].TC_CMR = (tc->TC_CHANNEL[chan].TC_CMR & 0xF0FFFFFF) | v;
}

void setupPWM(uint32_t ulPin){
    uint32_t frequency = 42000;
    // We use MCLK/2 as clock.
		const uint32_t TC = VARIANT_MCK / 2 / frequency;

		// Map value to Timer ranges 0..255 => 0..TC
		uint32_t ulValue = 127;
		ulValue = ulValue * TC;
		ulValue = ulValue / TC_MAX_DUTY_CYCLE;

		// Setup Timer for this pin
		ETCChannel channel = g_APinDescription[ulPin].ulTCChannel;
		
		static const uint32_t channelToAB[]   = { 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0 };
		uint32_t chA  = channelToAB[channel];
		static const uint32_t channelToId[] = { 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8 };
		uint32_t chNo = channelToChNo[channel];
		Tc *chTC = channelToTC[channel];
		uint32_t interfaceID = channelToId[channel];

		if (!TCChanEnabled[interfaceID]) {
			pmc_enable_periph_clk(TC_INTERFACE_ID + interfaceID);
			TC_Configure(chTC, chNo,
				TC_CMR_TCCLKS_TIMER_CLOCK1 |
				TC_CMR_WAVE |         // Waveform mode
				TC_CMR_WAVSEL_UP_RC | // Counter running up and reset when equals to RC
				TC_CMR_EEVT_XC0 |     // Set external events from XC0 (this setup TIOB as output)
				TC_CMR_ACPA_CLEAR | TC_CMR_ACPC_CLEAR |
				TC_CMR_BCPB_CLEAR | TC_CMR_BCPC_CLEAR);
			TC_SetRC(chTC, chNo, TC);
		}

    if (chA) {
      TC_SetRA(chTC, chNo, ulValue);
      TC_SetCMR_ChannelA(chTC, chNo, TC_CMR_ACPA_CLEAR | TC_CMR_ACPC_SET);
    } else {
      TC_SetRB(chTC, chNo, ulValue);
      TC_SetCMR_ChannelB(chTC, chNo, TC_CMR_BCPB_CLEAR | TC_CMR_BCPC_SET);
    }

		if ((g_pinStatus[ulPin] & 0xF) != PIN_STATUS_PWM) {
			PIO_Configure(g_APinDescription[ulPin].pPort,
					g_APinDescription[ulPin].ulPinType,
					g_APinDescription[ulPin].ulPin,
					g_APinDescription[ulPin].ulPinConfiguration);
			g_pinStatus[ulPin] = (g_pinStatus[ulPin] & 0xF0) | PIN_STATUS_PWM;
		}
		if (!TCChanEnabled[interfaceID]) {
			TC_Start(chTC, chNo);
			TCChanEnabled[interfaceID] = 1;
		}
}

void setPWMFrequency(uint32_t frequency, uint32_t pwmPin) {
  /* Configure ul_channel */
  ETCChannel channel = g_APinDescription[pwmPin].ulTCChannel;
  Tc *chTC = channelToTC[channel];
  uint32_t chNo = channelToChNo[channel];
  uint32_t TC = VARIANT_MCK / 2 / frequency;

  TC_SetRC(chTC, chNo, TC);
  TC_SetRA(chTC, chNo, 127*TC/255);

  // read counter value
  uint32_t ulValue = TC_GetCV(chTC, chNo);
  
  // reset the counter
  // TC_Start(chTC, chNo);

}

void setup() {
  Serial.begin(115200);
  Serial.println("Starting setup.");

  // put your setup code here, to run once:
  pinMode(sound_in_pin, INPUT);
  pinMode(sync_pin, OUTPUT);
  digitalWrite(sync_pin, LOW);

  // setup PWM
  Serial.println("Setting up PWM");
  setupPWM(FM_out_pin);

  // set up ADC channel 0
  Serial.println("Setting up ADC");
  setupADC(ADC_CHANNEL_0);

  Serial.println("Finished setting up");
  delay(3000);
}

// sine wave lookup table that goes from 0 to 441. There are 997 points
int sine_wave[997] = {2048,2060,2073,2086,2099,2112,2125,2138,2151,2164,2176,2189,2202,2215,2228,2241,2254,2266,2279,2292,2305,2318,2330,2343,2356,2369,2381,2394,2407,2420,2432,2445,2458,2470,2483,2495,2508,2521,2533,2546,2558,2571,2583,2596,2608,2620,2633,2645,2657,2670,2682,2694,2707,2719,2731,2743,2755,2767,2779,2791,2803,2815,2827,2839,2851,2863,2875,2887,2898,2910,2922,2933,2945,2957,2968,2980,2991,3003,3014,3025,3037,3048,3059,3070,3081,3093,3104,3115,3126,3137,3148,3158,3169,3180,3191,3201,3212,3223,3233,3244,3254,3265,3275,3285,3295,3306,3316,3326,3336,3346,3356,3366,3376,3385,3395,3405,3415,3424,3434,3443,3452,3462,3471,3480,3490,3499,3508,3517,3526,3535,3544,3552,3561,3570,3578,3587,3595,3604,3612,3620,3629,3637,3645,3653,3661,3669,3677,3684,3692,3700,3707,3715,3722,3730,3737,3744,3752,3759,3766,3773,3780,3786,3793,3800,3807,3813,3820,3826,3832,3839,3845,3851,3857,3863,3869,3875,3881,3886,3892,3898,3903,3909,3914,3919,3924,3929,3934,3939,3944,3949,3954,3959,3963,3968,3972,3976,3981,3985,3989,3993,3997,4001,4005,4009,4012,4016,4019,4023,4026,4029,4033,4036,4039,4042,4045,4047,4050,4053,4055,4058,4060,4063,4065,4067,4069,4071,4073,4075,4077,4078,4080,4081,4083,4084,4086,4087,4088,4089,4090,4091,4092,4092,4093,4093,4094,4094,4095,4095,4095,4095,4095,4095,4095,4094,4094,4094,4093,4093,4092,4091,4090,4089,4088,4087,4086,4085,4084,4082,4081,4079,4078,4076,4074,4072,4070,4068,4066,4064,4061,4059,4057,4054,4052,4049,4046,4043,4040,4037,4034,4031,4028,4025,4021,4018,4014,4010,4007,4003,3999,3995,3991,3987,3983,3979,3974,3970,3965,3961,3956,3952,3947,3942,3937,3932,3927,3922,3916,3911,3906,3900,3895,3889,3884,3878,3872,3866,3860,3854,3848,3842,3836,3829,3823,3816,3810,3803,3797,3790,3783,3776,3769,3762,3755,3748,3741,3733,3726,3719,3711,3704,3696,3688,3681,3673,3665,3657,3649,3641,3633,3624,3616,3608,3599,3591,3583,3574,3565,3557,3548,3539,3530,3521,3512,3503,3494,3485,3476,3467,3457,3448,3438,3429,3419,3410,3400,3390,3381,3371,3361,3351,3341,3331,3321,3311,3301,3290,3280,3270,3259,3249,3238,3228,3217,3207,3196,3185,3175,3164,3153,3142,3131,3120,3109,3098,3087,3076,3065,3054,3042,3031,3020,3008,2997,2985,2974,2962,2951,2939,2928,2916,2904,2892,2881,2869,2857,2845,2833,2821,2809,2797,2785,2773,2761,2749,2737,2725,2713,2700,2688,2676,2664,2651,2639,2627,2614,2602,2589,2577,2564,2552,2539,2527,2514,2502,2489,2477,2464,2451,2439,2426,2413,2401,2388,2375,2362,2350,2337,2324,2311,2298,2286,2273,2260,2247,2234,2221,2209,2196,2183,2170,2157,2144,2131,2118,2106,2093,2080,2067,2054,2041,2028,2015,2002,1989,1977,1964,1951,1938,1925,1912,1899,1886,1874,1861,1848,1835,1822,1809,1797,1784,1771,1758,1745,1733,1720,1707,1694,1682,1669,1656,1644,1631,1618,1606,1593,1581,1568,1556,1543,1531,1518,1506,1493,1481,1468,1456,1444,1431,1419,1407,1395,1382,1370,1358,1346,1334,1322,1310,1298,1286,1274,1262,1250,1238,1226,1214,1203,1191,1179,1167,1156,1144,1133,1121,1110,1098,1087,1075,1064,1053,1041,1030,1019,1008,997,986,975,964,953,942,931,920,910,899,888,878,867,857,846,836,825,815,805,794,784,774,764,754,744,734,724,714,705,695,685,676,666,657,647,638,628,619,610,601,592,583,574,565,556,547,538,530,521,512,504,496,487,479,471,462,454,446,438,430,422,414,407,399,391,384,376,369,362,354,347,340,333,326,319,312,305,298,292,285,279,272,266,259,253,247,241,235,229,223,217,211,206,200,195,189,184,179,173,168,163,158,153,148,143,139,134,130,125,121,116,112,108,104,100,96,92,88,85,81,77,74,70,67,64,61,58,55,52,49,46,43,41,38,36,34,31,29,27,25,23,21,19,17,16,14,13,11,10,9,8,7,6,5,4,3,2,2,1,1,1,0,0,0,0,0,0,0,1,1,2,2,3,3,4,5,6,7,8,9,11,12,14,15,17,18,20,22,24,26,28,30,32,35,37,40,42,45,48,50,53,56,59,62,66,69,72,76,79,83,86,90,94,98,102,106,110,114,119,123,127,132,136,141,146,151,156,161,166,171,176,181,186,192,197,203,209,214,220,226,232,238,244,250,256,263,269,275,282,288,295,302,309,315,322,329,336,343,351,358,365,373,380,388,395,403,411,418,426,434,442,450,458,466,475,483,491,500,508,517,525,534,543,551,560,569,578,587,596,605,615,624,633,643,652,661,671,680,690,700,710,719,729,739,749,759,769,779,789,800,810,820,830,841,851,862,872,883,894,904,915,926,937,947,958,969,980,991,1002,1014,1025,1036,1047,1058,1070,1081,1092,1104,1115,1127,1138,1150,1162,1173,1185,1197,1208,1220,1232,1244,1256,1268,1280,1292,1304,1316,1328,1340,1352,1364,1376,1388,1401,1413,1425,1438,1450,1462,1475,1487,1499,1512,1524,1537,1549,1562,1574,1587,1600,1612,1625,1637,1650,1663,1675,1688,1701,1714,1726,1739,1752,1765,1777,1790,1803,1816,1829,1841,1854,1867,1880,1893,1906,1919,1931,1944,1957,1970,1983,1996,2009,2022,2035};

uint32_t passed_time = 0;
int FM_factor;
uint32_t sig_frequency = 42000; // the period of the signal in microseconds
uint32_t timer = 0;

void loop() {
  while(true){
    // Read the message signal and map it to a value from 90 to -90 us.
    // 90 microseconds corresponds to half of 22 kHz (an audio input signal).
    FM_factor = fastAnalogRead(ADC_CHANNEL_0);
    FM_factor = map(FM_factor, 0, 4095, 1000, -1000);
    //modulate the carrier signal with the message signal by changing the reset value of the timer
    // TC_SetRC(TC2, 0, 997 + FM_factor);
    sig_frequency = 42000 + FM_factor;
    setPWMFrequency(sig_frequency, FM_out_pin);
  }
  

}