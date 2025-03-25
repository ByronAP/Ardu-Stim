/* vim: set syntax=c expandtab sw=2 softtabstop=2 autoindent smartindent smarttab : */
/*
 * ArduStim - Common functions and data shared across platforms
 *
 * Copyright 2014 David J. Andruczyk
 * 
 * Ardu-Stim software is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * ArduStim software is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with any ArduStim software.  If not, see http://www.gnu.org/licenses/
 *
 */
#include "globals.h"
#include "hal.h"
#include "enums.h"

// Define global configuration and status structures
struct configTable config;
struct status currentStatus;

// Global variables for wheel pattern generation
uint8_t lookaheadCache[LOOKAHEAD_CACHE_SIZE];
volatile uint16_t cacheStartEdge = 0;

// Global variables for timer and ADC
volatile uint16_t newOCR1A;
volatile uint8_t prescalerBits;
volatile bool resetPrescaler;
volatile uint16_t adc0;
volatile uint16_t adc1;
volatile bool adc0ReadComplete = false;
volatile bool adc1ReadComplete = false;
volatile uint8_t analogPort;

// Sin wave tables for compression simulation
// A sin wave of amplitude 100 with a complete cycle in 180 degrees (1 entry per degree)
const uint8_t sin_100_180[] PROGMEM = {
  0,0,0,0,0,1,1,1,2,2,3,4,4,5,6,7,8,9,10,11,
  12,13,14,15,17,18,19,21,22,24,25,27,28,30,
  31,33,35,36,38,40,41,43,45,47,48,50,52,53,
  55,57,59,60,62,64,65,67,69,70,72,73,75,76,
  78,79,81,82,83,85,86,87,88,89,90,91,92,93,
  94,95,96,96,97,98,98,99,99,99,100,100,100,
  100,100,100,100,100,100,99,99,99,98,98,97,
  96,96,95,94,93,92,91,90,89,88,87,86,85,83,
  82,81,79,78,76,75,73,72,70,69,67,65,64,62,
  60,59,57,55,53,52,50,48,47,45,43,41,40,38,
  36,35,33,31,30,28,27,25,24,22,21,19,18,17,
  15,14,13,12,11,10,9,8,7,6,5,4,4,3,2,2,1,1,
  1,0,0,0,0
};

// A sin wave of amplitude 100 with a complete cycle in 90 degrees (1 entry per degree)
const uint8_t sin_100_90[] PROGMEM = {
  0,0,0,1,2,3,4,6,8,10,12,14,17,19,22,25,28,
  31,35,38,41,45,48,52,55,59,62,65,69,72,75,
  78,81,83,86,88,90,92,94,96,97,98,99,100,100,
  100,100,100,99,98,97,96,94,92,90,88,86,83,81,
  78,75,72,69,65,62,59,55,52,48,45,41,38,35,31,
  28,25,22,19,17,14,12,10,8,6,4,3,2,1,0,0
};

// A sin wave of amplitude 100 with a complete cycle in 120 degrees
const uint8_t sin_100_120[] PROGMEM = {
  0,0,0,1,1,2,2,3,4,5,7,8,10,11,13,15,17,19,
  21,23,25,27,30,32,35,37,40,42,45,47,50,53,
  55,58,60,63,65,68,70,73,75,77,79,81,83,85,
  87,89,90,92,93,95,96,97,98,98,99,99,100,100,
  100,100,100,99,99,98,98,97,96,95,93,92,90,89,
  87,85,83,81,79,77,75,73,70,68,65,63,60,58,55,
  53,50,47,45,42,40,37,35,32,30,27,25,23,21,19,
  17,15,13,11,10,8,7,5,4,3,2,2,1,1,0,0
};

/**
 * @brief Validates configuration values and corrects them if needed
 * @param config Pointer to configuration structure to validate
 */
void validateConfiguration(struct configTable *config) {
  if(config->wheel >= MAX_WHEELS) {
    config->wheel = 5; // Default to 60-2 with cam
  }
  
  if(config->mode >= MAX_MODES) {
    config->mode = POT_RPM;
  }
  
  if(currentStatus.rpm > 15000) {
    currentStatus.rpm = 4000;
  }
  
  if(currentStatus.base_rpm > 15000) {
    currentStatus.base_rpm = 4000;
  }
  
  if(config->compressionType > COMPRESSION_TYPE_8CYL_4STROKE) {
    config->compressionType = COMPRESSION_TYPE_4CYL_4STROKE;
  }
  
  if(config->compressionRPM > 1000) {
    config->compressionRPM = 400;
  }
  
  if(config->compressionOffset > 359) {
    config->compressionOffset = 0;
  }
}

// Global wheel pattern definitions
wheels Wheels[MAX_WHEELS] = {
  /* Pointer to friendly name string, pointer to edge array, RPM Scaler, Number of edges in the array, whether the number of edges covers 360 or 720 degrees */
 { dizzy_four_cylinder_friendly_name, dizzy_four_cylinder, 0.03333, 4, 360 },
 { dizzy_six_cylinder_friendly_name, dizzy_six_cylinder, 0.05, 6, 360 },
 { dizzy_eight_cylinder_friendly_name, dizzy_eight_cylinder, 0.06667, 8, 360 },
 { sixty_minus_two_friendly_name, sixty_minus_two, 1.0, 120, 360 },
 { sixty_minus_two_with_cam_friendly_name, sixty_minus_two_with_cam, 1.0, 240, 720 },
 { sixty_minus_two_with_halfmoon_cam_friendly_name, sixty_minus_two_with_halfmoon_cam, 1.0, 240, 720 },
 { thirty_six_minus_one_friendly_name, thirty_six_minus_one, 0.6, 72, 360 },
 { twenty_four_minus_one_friendly_name, twenty_four_minus_one, 0.5, 48, 360 },
 { four_minus_one_with_cam_friendly_name, four_minus_one_with_cam, 0.06667, 16, 720 },
 { eight_minus_one_friendly_name, eight_minus_one, 0.13333, 16, 360 },
 { six_minus_one_with_cam_friendly_name, six_minus_one_with_cam, 0.15, 36, 720 },
 { twelve_minus_one_with_cam_friendly_name, twelve_minus_one_with_cam, 0.6, 144, 720 },
 { fourty_minus_one_friendly_name, fourty_minus_one, 0.66667, 80, 360 },
 { dizzy_four_trigger_return_friendly_name, dizzy_four_trigger_return, 0.15, 9, 720 },
 { oddfire_vr_friendly_name, oddfire_vr, 0.2, 24, 360 },
 { optispark_lt1_friendly_name, optispark_lt1, 3.0, 720, 720 },
 { twelve_minus_three_friendly_name, twelve_minus_three, 0.4, 48, 360 },
 { thirty_six_minus_two_two_two_friendly_name, thirty_six_minus_two_two_two, 0.6, 72, 360 },
 { thirty_six_minus_two_two_two_h6_friendly_name, thirty_six_minus_two_two_two_h6, 0.6, 72, 360 },
 { thirty_six_minus_two_two_two_with_cam_friendly_name, thirty_six_minus_two_two_two_with_cam, 0.6, 144, 720 },
 { fourty_two_hundred_wheel_friendly_name, fourty_two_hundred_wheel, 0.6, 72, 360 },
 { thirty_six_minus_one_with_cam_fe3_friendly_name, thirty_six_minus_one_with_cam_fe3, 0.6, 144, 720 },
 { six_g_seventy_two_with_cam_friendly_name, six_g_seventy_two_with_cam, 0.6, 144, 720 },
 { buell_oddfire_cam_friendly_name, buell_oddfire_cam, 0.33333, 80, 720 },
 { gm_ls1_crank_and_cam_friendly_name, gm_ls1_crank_and_cam, 6.0, 720, 720 },
 { gm_ls_58X_crank_and_4x_cam_friendly_name, GM_LS_58X_crank_and_4x_cam, 1.0, 240, 720},
 { lotus_thirty_six_minus_one_one_one_one_friendly_name, lotus_thirty_six_minus_one_one_one_one, 0.6, 72, 360 },
 { honda_rc51_with_cam_friendly_name, honda_rc51_with_cam, 0.2, 48, 720 },
 { thirty_six_minus_one_with_second_trigger_friendly_name, thirty_six_minus_one_with_second_trigger, 0.6, 144, 720 },
 { chrysler_ngc_thirty_six_plus_two_minus_two_with_ngc4_cam_friendly_name, chrysler_ngc_thirty_six_plus_two_minus_two_with_ngc4_cam, 3.0, 720, 720 },
 { chrysler_ngc_thirty_six_minus_two_plus_two_with_ngc6_cam_friendly_name, chrysler_ngc_thirty_six_minus_two_plus_two_with_ngc6_cam, 3.0, 720, 720 },
 { chrysler_ngc_thirty_six_minus_two_plus_two_with_ngc8_cam_friendly_name, chrysler_ngc_thirty_six_minus_two_plus_two_with_ngc8_cam, 3.0, 720, 720 },
 { weber_iaw_with_cam_friendly_name, weber_iaw_with_cam, 1.2, 144, 720 },
 { fiat_one_point_eight_sixteen_valve_with_cam_friendly_name, fiat_one_point_eight_sixteen_valve_with_cam, 3.0, 720, 720 },
 { three_sixty_nissan_cas_friendly_name, three_sixty_nissan_cas, 3.0, 720, 720 },
 { twenty_four_minus_two_with_second_trigger_friendly_name, twenty_four_minus_two_with_second_trigger, 0.3, 72, 720 },
 { yamaha_eight_tooth_with_cam_friendly_name, yamaha_eight_tooth_with_cam, 0.26667, 64, 720 },
 { gm_four_tooth_with_cam_friendly_name, gm_four_tooth_with_cam, 0.06666, 8, 720 },
 { gm_six_tooth_with_cam_friendly_name, gm_six_tooth_with_cam, 0.1, 12, 720 },
 { gm_eight_tooth_with_cam_friendly_name, gm_eight_tooth_with_cam, 0.13333, 16, 720 },
 { volvo_d12acd_with_cam_friendly_name, volvo_d12acd_with_cam, 4.0, 480, 720 },
 { mazda_thirty_six_minus_two_two_two_with_six_tooth_cam_friendly_name, mazda_thirty_six_minus_two_two_two_with_six_tooth_cam, 1.5, 360, 720 },
 { mitsubishi_4g63_4_2_friendly_name, mitsubishi_4g63_4_2, 0.6, 144, 720 },
 { audi_135_with_cam_friendly_name, audi_135_with_cam, 1.5, 1080, 720 },
 { honda_d17_no_cam_friendly_name, honda_d17_no_cam, 0.6, 144, 720 },
 { mazda_323_au_friendly_name, mazda_323_au, 1, 30, 720 },
 { daihatsu_3cyl_friendly_name, daihatsu_3cyl, 0.8, 144, 360 },
 { miata_9905_friendly_name, miata_9905, 0.6, 144, 720 },
 { twelve_with_cam_friendly_name, twelve_with_cam, 0.6, 144, 720 },
 { twenty_four_with_cam_friendly_name, twenty_four_with_cam, 0.6, 144, 720 },
 { subaru_six_seven_name_friendly_name, subaru_six_seven, 3.0, 720, 720 },
 { gm_seven_x_friendly_name, gm_seven_x, 1.502, 180, 720 },
 { four_twenty_a_friendly_name, four_twenty_a, 0.6, 144, 720 },
 { ford_st170_friendly_name, ford_st170, 3.0, 720, 720 },
 { mitsubishi_3A92_friendly_name, mitsubishi_3A92, 0.6, 144, 720 },
 { Toyota_4AGE_CAS_friendly_name, toyota_4AGE_CAS, 0.333, 144, 720 },
 { Toyota_4AGZE_friendly_name, toyota_4AGZE, 0.333, 144, 720 },
 { Suzuki_DRZ400_friendly_name, suzuki_DRZ400, 0.6, 72, 360},
 { Jeep_2000_4cyl_friendly_name, jeep_2000_4cyl, 1.5, 360, 720},
 { Jeep_2000_6cyl_friendly_name, jeep_2000_6cyl, 1.5, 360, 720 },
 { BMW_N20_friendly_name, bmw_n20, 1.0, 240, 720},
 { VIPER9602_friendly_name, viper9602wheel, 1.0, 240, 720},
 { thirty_six_minus_two_with_second_trigger_friendly_name, thirty_six_minus_two_with_second_trigger, 0.6, 144, 720 },
 { GM_40_Tooth_Trans_OSS_friendly_name, GM40toothOSS, 1.0, 80, 360 },
};