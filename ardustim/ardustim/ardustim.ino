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
 #include "storage.h"
 #include "wheel_defs.h"
 
 // Platform-specific headers
 #if defined(__AVR__)
 #include <avr/pgmspace.h>
 #include <avr/interrupt.h>
 #elif defined(ESP32)
 #include <esp_timer.h>
 #include <Preferences.h>
 #endif
 
 // Pin definitions
 volatile uint8_t analog_port = 0;
 #if defined(__AVR__)
 #define ADC_PIN A0             // Analog input for RPM potentiometer
 #define PRIMARY_OUTPUT_PIN 8   // PB0 - Primary output (crank)
 #define SECONDARY_OUTPUT_PIN 9 // PB1 - Secondary output (cam1)
 #define TERTIARY_OUTPUT_PIN 10 // PB2 - Tertiary output (cam2)
 #define KNOCK_OUTPUT_PIN 11    // PB3 - Knock signal
 #elif defined(ESP32)
 #define ADC_PIN 34             // GPIO 34 for ADC input (RPM pot)
 #define PRIMARY_OUTPUT_PIN 2   // GPIO 2 - Primary output
 #define SECONDARY_OUTPUT_PIN 4 // GPIO 4 - Secondary output
 #define TERTIARY_OUTPUT_PIN 5  // GPIO 5 - Tertiary output
 #define KNOCK_OUTPUT_PIN 18    // GPIO 18 - Knock signal
 #endif

struct configTable config;
struct status currentStatus;

// Sensitive variables for ISRs
volatile uint16_t adc0;            // POT RPM reading
volatile uint16_t adc1;            // Reserved for future use
volatile bool adc0_read_complete = false;
volatile bool adc1_read_complete = false;
volatile bool reset_prescaler = false;
volatile uint8_t output_invert_mask = 0x00; // No inversion by default
volatile uint8_t prescaler_bits = 0;
volatile uint8_t last_prescaler_bits = 0;
volatile uint16_t new_OCR1A = 5000; // Default for AVR timer
#if defined(ESP32)
uint32_t apb_frequency = 80000000UL; // APB clock typically 80MHz for ESP32
volatile uint64_t new_timer_ticks = 1000; // Default for ESP32 timer
hw_timer_t *timer = NULL;          // ESP32 hardware timer
#endif
volatile uint16_t edge_counter = 0;
volatile uint32_t cycleStartTime = micros();
volatile uint32_t cycleDuration = 0;

uint32_t sweep_time_counter = 0;
uint8_t sweep_direction = ASCENDING;

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

// ESP32 ISR definition
#if defined(ESP32)
void IRAM_ATTR onTimer() {
  uint8_t state = pgm_read_byte(&Wheels[config.wheel].edge_states_ptr[edge_counter]) ^ output_invert_mask;
  digitalWrite(PRIMARY_OUTPUT_PIN, (state & 1) ? HIGH : LOW);
  digitalWrite(SECONDARY_OUTPUT_PIN, (state & 2) ? HIGH : LOW);
  digitalWrite(TERTIARY_OUTPUT_PIN, (state & 4) ? HIGH : LOW);
  digitalWrite(KNOCK_OUTPUT_PIN, (state & 8) ? HIGH : LOW);
  edge_counter++;
  if (edge_counter == Wheels[config.wheel].wheel_max_edges) {
    edge_counter = 0;
    cycleDuration = micros() - cycleStartTime;
    cycleStartTime = micros();
  }
  timerAlarmWrite(timer, new_timer_ticks, true);
}
#endif

/* Initialization */
void setup() {
  loadConfig();
  serialSetup();

  cli(); // Disable interrupts during setup

  // Pin setup
  pinMode(PRIMARY_OUTPUT_PIN, OUTPUT);
  pinMode(SECONDARY_OUTPUT_PIN, OUTPUT);
  pinMode(TERTIARY_OUTPUT_PIN, OUTPUT);
  pinMode(KNOCK_OUTPUT_PIN, OUTPUT);

  // Platform-specific timer setup
  #if defined(__AVR__)
  // AVR Timer1 setup for pattern generation
  TCCR1A = 0;
  TCCR1B = 0;
  TCNT1 = 0;
  OCR1A = 1000; // Initial value (8000 RPM for 60-2)
  TCCR1B |= (1 << WGM12); // CTC mode
  TCCR1B |= (1 << CS10);  // Prescaler 1
  TIMSK1 |= (1 << OCIE1A); // Enable compare interrupt

  // ADC setup for AVR (interrupt-driven)
  ADMUX &= B11011111;  // Right-adjust result
  ADMUX |= B01000000;  // Reference voltage AVcc
  ADMUX &= B11110000;  // Clear MUX bits
  ADCSRA |= B10000000; // Enable ADC
  ADCSRA |= B00100000; // Enable auto-trigger
  ADCSRB &= B11111000; // Free-running mode
  ADCSRA |= B00000111; // Prescaler 128
  ADCSRA |= B00001000; // Enable ADC interrupt
  ADCSRA |= B01000000; // Start conversion
  #elif defined(ESP32)
  // ESP32 timer setup
  apb_frequency = getApbFrequency(); // Just in case it is different than the default
  timer = timerBegin(0, 80, true); // Timer 0, prescaler 80, count up
  timerAttachInterrupt(timer, &onTimer, true);
  timerAlarmWrite(timer, 1000, true); // Initial value, updated later
  timerAlarmEnable(timer);

  // Preferences initialization for ESP32
  Preferences preferences;
  preferences.begin("ardustim", false);
  #endif

  sei(); // Enable interrupts
  reset_new_OCR1A(currentStatus.rpm); // Set initial RPM
}

#if defined(__AVR__)
//! ADC ISR for alternating between ADC pins 0 and 1
/*!
 * Reads ADC ports 0 and 1 alternately. Port 0 is RPM, Port 1 is for
 * future fun (possible wheel selection)
 */
// AVR ADC ISR
ISR(ADC_vect) {
  if (analog_port == 0) {
    adc0 = ADCL | (ADCH << 8);
    adc0_read_complete = true;
  }
}

// AVR Timer1 ISR
ISR(TIMER1_COMPA_vect) {
  PORTB = output_invert_mask ^ pgm_read_byte(&Wheels[config.wheel].edge_states_ptr[edge_counter]);
  edge_counter++;
  if (edge_counter == Wheels[config.wheel].wheel_max_edges) {
    edge_counter = 0;
    cycleDuration = micros() - cycleStartTime;
    cycleStartTime = micros();
  }
  if (reset_prescaler) {
    TCCR1B &= ~((1 << CS10) | (1 << CS11) | (1 << CS12));
    TCCR1B |= prescaler_bits;
    reset_prescaler = false;
  }
  OCR1A = new_OCR1A;
}
#endif



void loop() {
  uint16_t tmp_rpm = currentStatus.base_rpm;

  if (Serial.available() > 0) {
    commandParser();
  }

  if (config.mode == POT_RPM) {
    #if defined(__AVR__)
    if (adc0_read_complete == true) {
      adc0_read_complete = false;
      tmp_rpm = adc0 << TMP_RPM_SHIFT;
      if (tmp_rpm > TMP_RPM_CAP) tmp_rpm = TMP_RPM_CAP;
    }
    #elif defined(ESP32)
    adc0 = analogRead(ADC_PIN); // Polled ADC reading
    tmp_rpm = adc0 << TMP_RPM_SHIFT;
    if (tmp_rpm > TMP_RPM_CAP) tmp_rpm = TMP_RPM_CAP;
    #endif
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
  setRPM(currentStatus.base_rpm - currentStatus.compressionModifier);
}

uint16_t calculateCompressionModifier()
{
  if( (currentStatus.base_rpm > config.compressionRPM) || (config.useCompression != true) ) { return 0; }
  //if( currentStatus.base_rpm > 400 ) { return 0;}

  uint16_t crankAngle = calculateCurrentCrankAngle();
  uint16_t modAngle = crankAngle;

  uint16_t compressionModifier = 0;
  switch(config.compressionType)
  {
    case COMPRESSION_TYPE_2CYL_4STROKE:
      modAngle = crankAngle / 2;
      compressionModifier = pgm_read_byte(&sin_100_180[modAngle]);
      break;
    case COMPRESSION_TYPE_4CYL_4STROKE:
      modAngle = (crankAngle % 180) ;
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
      modAngle = (crankAngle % 180) ;
      compressionModifier = pgm_read_byte(&sin_100_180[modAngle]);
      break;
  }

  //RPM scaler - Varies the amplitude of the compression modifier based on how far below the compression RPM point we are. Eg:
  //If the compression RPM value is 400
  //At 300rpm the amplitude will be 75%
  //At 200rpm the amplitude will be 50%
  //At 100rpm the amplitude will be 25% etc
  //Base RPM must be below 650 to prevent overflow
  if(config.compressionDynamic && (currentStatus.base_rpm < 655U) ) { compressionModifier = (compressionModifier * currentStatus.base_rpm) / config.compressionRPM; }
  
  return compressionModifier;
}

uint16_t calculateCurrentCrankAngle()
{
  if(cycleDuration == 0) { return 0; }

  uint32_t cycleTime = micros() - cycleStartTime;
  if( Wheels[config.wheel].wheel_degrees == 720 ) { cycleTime = cycleTime * 2; } 
  
  uint16_t tmpCrankAngle = ((cycleTime * 360U) / cycleDuration);
  tmpCrankAngle += config.compressionOffset;
  while(tmpCrankAngle > 360) { tmpCrankAngle -= 360; }

  return tmpCrankAngle;
}

void setRPM(uint16_t newRPM) {
  if (newRPM < 10) return;
  if (currentStatus.rpm != newRPM) reset_new_OCR1A(newRPM);
  currentStatus.rpm = newRPM;
}

void reset_new_OCR1A(uint32_t new_rpm) {
  if (new_rpm < 10) return;
  #if defined(__AVR__)
  uint32_t tmp = (uint32_t)(8000000.0 / (Wheels[config.wheel].rpm_scaler * (float)new_rpm));
  uint8_t tmp_prescaler_bits;
  uint8_t bitshift;
  get_prescaler_bits(&tmp, &tmp_prescaler_bits, &bitshift);
  new_OCR1A = (uint16_t)(tmp >> bitshift);
  prescaler_bits = tmp_prescaler_bits;
  reset_prescaler = true;
  #elif defined(ESP32)
  uint64_t f_interrupt = ((uint64_t)new_rpm * Wheels[config.wheel].wheel_max_edges) / 60ULL;
  if (f_interrupt == 0) f_interrupt = 1;
  new_timer_ticks = (apb_frequency / 80) / f_interrupt;
  if (new_timer_ticks < 1) new_timer_ticks = 1;
  #endif
}


uint8_t get_bitshift_from_prescaler(uint8_t *prescaler_bits)
{
  switch (*prescaler_bits)
  {
    case PRESCALE_1024:
    return 10;
    case PRESCALE_256:
    return 8;
    case PRESCALE_64:
    return 6;
    case PRESCALE_8:
    return 3;
    case PRESCALE_1:
    return 0;
  }
  return 0;
}

//! Gets prescaler enum and bitshift based on OC value
void get_prescaler_bits(uint32_t *potential_oc_value, uint8_t *prescaler, uint8_t *bitshift)
{
  if (*potential_oc_value >= 16777216)
  {
    *prescaler = PRESCALE_1024;
    *bitshift = 10;
  }
  else if (*potential_oc_value >= 4194304)
  {
    *prescaler = PRESCALE_256;
    *bitshift = 8;
  }
  else if (*potential_oc_value >= 524288)
  {
    *prescaler = PRESCALE_64;
    *bitshift = 6;
  }
  else if (*potential_oc_value >= 65536)
  {
    *prescaler = PRESCALE_8;
    *bitshift = 3;
  }
  else
  {
    *prescaler = PRESCALE_1;
    *bitshift = 0;
  }
}
