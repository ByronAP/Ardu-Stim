/* vim: set syntax=c expandtab sw=2 softtabstop=2 autoindent smartindent smarttab : */
/*
 * Arbritrary wheel pattern generator
 *
 * copyright 2014 David J. Andruczyk
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
 #include "ardustim.h"
 #include "enums.h"
 #include "comms.h"
 #include "hal.h"
 #include "wheel_defs.h"

// Global configuration and status structures
struct configTable config;    // Holds settings like wheel type, RPM mode, etc.
struct status currentStatus;  // Tracks current RPM and related values

volatile uint16_t edge_counter = 0;      // Counts edges in the wheel pattern
volatile uint32_t cycleStartTime = micros(); // Start time of current cycle (for crank angle calculation)
volatile uint32_t cycleDuration = 0;     // Duration of the last complete cycle

uint32_t sweep_time_counter = 0; // Timer for RPM sweep mode
uint8_t sweep_direction = ASCENDING; // Direction of RPM sweep (ASCENDING or DESCENDING)
uint8_t output_invert_mask = 0x00; // Mask for inverting output pins

// Lookahead cache for wheel pattern decoding
uint8_t lookahead_cache[LOOKAHEAD_CACHE_SIZE];
volatile uint16_t cache_start_edge = 0;  // First edge in the cache

// Array of wheel definitions for various crankshaft/camshaft patterns
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
  { Suzuki_DRZ400_friendly_name, suzuki_DRZ400,0.6, 72, 360},
  { Jeep_2000_4cyl_friendly_name, jeep_2000_4cyl, 1.5, 360, 720},
  { Jeep_2000_6cyl_friendly_name, jeep_2000_6cyl, 1.5, 360, 720 },
  { BMW_N20_friendly_name, bmw_n20, 1.0, 240, 720},
  { VIPER9602_friendly_name, viper9602wheel, 1.0, 240, 720},
  { thirty_six_minus_two_with_second_trigger_friendly_name, thirty_six_minus_two_with_second_trigger, 0.6, 144, 720 },
  { GM_40_Tooth_Trans_OSS_friendly_name, GM40toothOSS, 1.0, 80, 360 },
};

// Global variables for timer and ADC - Definitions for AVR
volatile uint16_t new_OCR1A;
volatile uint8_t prescaler_bits;
volatile bool reset_prescaler;
volatile uint16_t adc0;
volatile uint16_t adc1;
volatile bool adc0_read_complete = false;
volatile bool adc1_read_complete = false;
volatile uint8_t analog_port;

void onTimer(); // Forward declaration - now just calls the HAL GPIO output function

/**
 * @brief Setup function to initialize the system
 *
 * Configures serial communication, pins, timers, and interrupts based on the platform.
 */
void setup() {
  storage_hal_init();          // Init Storage HAL
  storage_hal_load_config(&config);   // Load config using HAL
  serialSetup();               // Init Serial using HAL (in comms.cpp)
  gpio_hal_init();             // Init GPIO using HAL
  adc_hal_init();              // Init ADC using HAL

  // Initialize lookahead cache
  cache_start_edge = 0;
  for (uint8_t i = 0; i < LOOKAHEAD_CACHE_SIZE && i < Wheels[config.wheel].wheel_max_edges; i++) {
    lookahead_cache[i] = decode_wheel_pattern(Wheels[config.wheel].edge_states_ptr, i);
  }

  timer_hal_init(currentStatus.rpm, onTimer); // Init Timer HAL, pass onTimer callback

  sei(); // Enable interrupts after setup
  reset_new_OCR1A(currentStatus.rpm); // Set initial RPM, using HAL Timer functions internally
}

void onTimer() {
    uint8_t state;
  
    // Get state from the lookahead cache
    uint16_t cache_index = edge_counter - cache_start_edge;
    
    if (cache_index < LOOKAHEAD_CACHE_SIZE) {
      // Fast path: value is in cache
      state = lookahead_cache[cache_index];
    } else {
      // We've moved beyond the cache - refresh it
      cache_start_edge = edge_counter;
      
      // Fill cache with next LOOKAHEAD_CACHE_SIZE values
      for (uint8_t i = 0; i < LOOKAHEAD_CACHE_SIZE && (edge_counter + i) < Wheels[config.wheel].wheel_max_edges; i++) {
        lookahead_cache[i] = decode_wheel_pattern(Wheels[config.wheel].edge_states_ptr, edge_counter + i);
      }
      
      // Now use the first value
      state = lookahead_cache[0];
    }
    
    // Apply output inversion
    state ^= output_invert_mask;

  gpio_hal_set_output(PRIMARY_OUTPUT_PIN, (state & 1));    // Use GPIO HAL to set outputs
  gpio_hal_set_output(SECONDARY_OUTPUT_PIN, (state & 2));  // Use GPIO HAL
  gpio_hal_set_output(TERTIARY_OUTPUT_PIN, (state & 4));   // Use GPIO HAL
  gpio_hal_set_output(KNOCK_OUTPUT_PIN, (state & 8));      // Use GPIO HAL

  edge_counter = edge_counter + 1; // Increment edge counter
  if (edge_counter >= Wheels[config.wheel].wheel_max_edges) {
      edge_counter = 0;
      cycleDuration = micros() - cycleStartTime;
      cycleStartTime = micros();

      // Reset cache for new cycle
    cache_start_edge = 0;
    
    // Pre-fill cache for the next cycle
    for (uint8_t i = 0; i < LOOKAHEAD_CACHE_SIZE && i < Wheels[config.wheel].wheel_max_edges; i++) {
      lookahead_cache[i] = decode_wheel_pattern(Wheels[config.wheel].edge_states_ptr, i);
    }
  }
}

/**
 * @brief Main loop function
 *
 * Handles serial commands, updates RPM based on mode, and applies compression modifier.
 */
void loop() {
  uint16_t tmp_rpm = currentStatus.base_rpm;

  if (serial_hal_available()) { // Use Serial HAL for serial input
      commandParser(); // Process serial commands (in comms.cpp)
  }

  if (config.mode == POT_RPM) {
      tmp_rpm = adc_hal_read_channel(0) << TMP_RPM_SHIFT; // Use ADC HAL to read pot
      if (tmp_rpm > TMP_RPM_CAP) tmp_rpm = TMP_RPM_CAP;
  } else if (config.mode == LINEAR_SWEPT_RPM) {
      if (micros() > (sweep_time_counter + config.sweep_interval)) {
          sweep_time_counter = micros();
          if (sweep_direction == ASCENDING) {
              tmp_rpm = currentStatus.base_rpm + 1;
              if (tmp_rpm >= config.sweep_high_rpm) sweep_direction = DESCENDING;
          } else {
              tmp_rpm = currentStatus.base_rpm - 1;
              if (tmp_rpm <= config.sweep_low_rpm) sweep_direction = ASCENDING;
          }
      }
  } else if (config.mode == FIXED_RPM) {
      tmp_rpm = config.fixed_rpm;
  }

  currentStatus.base_rpm = tmp_rpm;
  currentStatus.compressionModifier = calculateCompressionModifier();
  if (currentStatus.compressionModifier >= currentStatus.base_rpm) {
      currentStatus.compressionModifier = 0;
  }
  setRPM(currentStatus.base_rpm - currentStatus.compressionModifier); // Set RPM - uses HAL Timer
}

/**
 * @brief Calculates the compression modifier based on crank angle
 *
 * Simulates engine compression effects by reducing RPM based on a sine wave pattern.
 * @return uint16_t The compression modifier value (0 if disabled or above threshold)
 */
uint16_t calculateCompressionModifier() {
  if (!config.useCompression || currentStatus.base_rpm > config.compressionRPM) {
      return 0; // No modifier if disabled or RPM exceeds threshold
  }

  uint16_t crankAngle = calculateCurrentCrankAngle(); // Get current crank angle
  uint16_t modAngle = crankAngle;
  uint16_t compressionModifier = 0;

  // Adjust modifier based on engine type
  switch (config.compressionType) {
      case COMPRESSION_TYPE_2CYL_4STROKE:
          modAngle = crankAngle / 2;
          compressionModifier = pgm_read_byte(&sin_100_180[modAngle]);
          break;
      case COMPRESSION_TYPE_4CYL_4STROKE:
          modAngle = crankAngle % 180;
          compressionModifier = pgm_read_byte(&sin_100_180[modAngle]);
          break;
      case COMPRESSION_TYPE_6CYL_4STROKE:
          modAngle = crankAngle % 120;
          compressionModifier = pgm_read_byte(&sin_100_120[modAngle]);
          break;
      case COMPRESSION_TYPE_8CYL_4STROKE:
          modAngle = crankAngle % 90;
          compressionModifier = pgm_read_byte(&sin_100_90[modAngle]);
          break;
      default:
          modAngle = crankAngle % 180;
          compressionModifier = pgm_read_byte(&sin_100_180[modAngle]);
          break;
  }

  // Scale modifier dynamically based on RPM if enabled
  if (config.compressionDynamic && currentStatus.base_rpm < 655U) {
      compressionModifier = (compressionModifier * currentStatus.base_rpm) / config.compressionRPM;
  }
  return compressionModifier;
}

/**
* @brief Calculates the current crank angle
*
* Uses cycle timing to determine the current position in the wheel pattern.
* @return uint16_t Crank angle in degrees (0-360)
*/
uint16_t calculateCurrentCrankAngle() {
  if (cycleDuration == 0) return 0; // No valid cycle yet

  uint32_t cycleTime = micros() - cycleStartTime;
  if (Wheels[config.wheel].wheel_degrees == 720) cycleTime *= 2; // Adjust for 720-degree patterns

  uint16_t tmpCrankAngle = ((cycleTime * 360U) / cycleDuration); // Calculate angle
  tmpCrankAngle += config.compressionOffset; // Apply offset
  while (tmpCrankAngle > 360) tmpCrankAngle -= 360; // Normalize to 0-360
  return tmpCrankAngle;
}

/**
* @brief Sets the RPM and adjusts the timer
*
* Updates the timer interrupt frequency based on the desired RPM.
* @param newRPM The target RPM value
*/
void setRPM(uint16_t newRPM) {
  if (newRPM < 10) return; // Minimum RPM threshold
  if (currentStatus.rpm != newRPM) timer_hal_set_rpm(newRPM); // Update timer if RPM changes
  currentStatus.rpm = newRPM; // Store final RPM
}

// Function to reset new_OCR1A - Definition for AVR
void reset_new_OCR1A(uint32_t rpm) {
    timer_hal_set_rpm(rpm);
}