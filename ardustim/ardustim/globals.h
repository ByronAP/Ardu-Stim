/* vim: set syntax=c expandtab sw=2 softtabstop=2 autoindent smartindent smarttab : */
/*
 * ArduStim - Global definitions and structures
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

 #ifndef __GLOBALS_H__
 #define __GLOBALS_H__
 
 #include "Arduino.h"
 
 /**
  * Size of the lookahead cache for wheel pattern decoding
  */
 #define LOOKAHEAD_CACHE_SIZE 24   // Small lookahead cache
 extern uint8_t lookaheadCache[LOOKAHEAD_CACHE_SIZE];
 extern volatile uint16_t cacheStartEdge;  // First edge in the cache
 
 #include "wheelDefs.h"

 #define VERSION 2 // Configuration version number
 
 // Settings for RPM calculation and limits
 #define TMP_RPM_SHIFT 4 // Shift factor for ADC to RPM conversion (x16, 0-16384 RPM)
 #define TMP_RPM_CAP 9000 // Maximum RPM via potentiometer (capped from 16384)
 #define EEPROM_LAST_MODE 100 // Unused EEPROM address
 
 // Pin definitions - Conditional defines based on platform
 #if defined(__AVR__)
 // AVR specific pin definitions
 #define ADC_PIN             A0   // Analog input for RPM potentiometer
 #define PRIMARY_OUTPUT_PIN   8   // PB0 - Primary output (crank)
 #define SECONDARY_OUTPUT_PIN 9   // PB1 - Secondary output (cam1)
 #define TERTIARY_OUTPUT_PIN 10   // PB2 - Tertiary output (cam2)
 #define KNOCK_OUTPUT_PIN    11   // PB3 - Knock signal
 #elif defined(ESP32) && !defined(ESP32C6)
 // ESP32 specific pin definitions
 #define ADC_PIN             34   // GPIO 34 for ADC input (RPM pot)
 #define PRIMARY_OUTPUT_PIN   2   // GPIO 2 - Primary output
 #define SECONDARY_OUTPUT_PIN 4   // GPIO 4 - Secondary output
 #define TERTIARY_OUTPUT_PIN  5   // GPIO 5 - Tertiary output
 #define KNOCK_OUTPUT_PIN     18  // GPIO 18 - Knock signal
 #elif defined(ESP32C6)
 // ESP32-C6 specific pin definitions
 #define ADC_PIN              1   // GPIO 1 for ADC input (RPM pot) - ADC1_CH0
 #define PRIMARY_OUTPUT_PIN   6   // GPIO 6 - Primary output (crank)
 #define SECONDARY_OUTPUT_PIN 7   // GPIO 7 - Secondary output (cam1)
 #define TERTIARY_OUTPUT_PIN  8   // GPIO 8 - Tertiary output (cam2)
 #define KNOCK_OUTPUT_PIN     9   // GPIO 9 - Knock signal
 #endif
 
 // Compression type definitions
 #define COMPRESSION_TYPE_1CYL_4STROKE 0 // Not supported
 #define COMPRESSION_TYPE_2CYL_4STROKE 1
 #define COMPRESSION_TYPE_3CYL_4STROKE 2 // Not supported
 #define COMPRESSION_TYPE_4CYL_4STROKE 3
 #define COMPRESSION_TYPE_6CYL_4STROKE 4
 #define COMPRESSION_TYPE_8CYL_4STROKE 5
 
 /**
  * Configuration structure for persistent settings
  */
 struct configTable {
     uint8_t version;            // Configuration version
     uint8_t wheel;              // Current wheel pattern index
     uint8_t mode;               // RPM control mode
     uint16_t fixed_rpm;         // Fixed RPM value
     uint16_t sweep_low_rpm;     // Low RPM for sweep mode
     uint16_t sweep_high_rpm;    // High RPM for sweep mode
     uint16_t sweep_interval;    // Sweep interval in microseconds
     bool useCompression;        // Enable compression simulation
     uint8_t compressionType;    // Engine type for compression
     uint16_t compressionRPM;    // RPM threshold for compression
     uint16_t compressionOffset; // Angle offset for compression
     bool compressionDynamic;    // Scale compression with RPM
 } __attribute__ ((packed));
 extern struct configTable config;
 
 /**
  * Status structure for runtime values
  */
 struct status {
     uint16_t base_rpm;          // RPM before compression modifier
     uint16_t compressionModifier; // Compression effect on RPM
     uint16_t rpm;               // Final RPM
 };
 extern struct status currentStatus;
 
 /**
  * Wheel structure definition
  */ 
 typedef struct _wheels wheels;
 struct _wheels {
     const char *decoder_name PROGMEM;      // Friendly name of the wheel
     const unsigned char *edge_states_ptr PROGMEM; // Edge state array
     const float rpm_scaler;                // RPM scaling factor
     const uint16_t wheel_max_edges;        // Number of edges in pattern
     const uint16_t wheel_degrees;          // Degrees per cycle (360 or 720)
 };
 
 // Sin wave tables for compression simulation
 extern const uint8_t sin_100_180[] PROGMEM;
 extern const uint8_t sin_100_90[] PROGMEM;
 extern const uint8_t sin_100_120[] PROGMEM;
 
 extern wheels Wheels[MAX_WHEELS]; // Wheel pattern definitions

 // Global variables for timer and ADC
 extern volatile uint16_t newOCR1A;
 extern volatile uint8_t prescalerBits;
 extern volatile bool resetPrescaler;
 extern volatile uint16_t adc0;
 extern volatile uint16_t adc1;
 extern volatile bool adc0ReadComplete;
 extern volatile bool adc1ReadComplete;
 extern volatile uint8_t analogPort;
 
 #endif