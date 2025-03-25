/* vim: set syntax=c expandtab sw=2 softtabstop=2 autoindent smartindent smarttab : */
/*
 * ArduStim - Arbitrary wheel pattern generator
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
 #include "main.h"
 #include "enums.h"
 #include "comms.h"
 #include "hal.h"
 #include "wheelDefs.h"
 
 // DO NOT define global variables here - they should be defined in common.cpp
 // and just referenced here as extern declarations
 extern struct configTable config;     // Holds settings like wheel type, RPM mode, etc.
 extern struct status currentStatus;   // Tracks current RPM and related values
 
 // Control variables - local to this file
 uint32_t sweepTimeCounter = 0;        // Timer for RPM sweep mode
 uint8_t sweepDirection = ASCENDING;   // Direction of RPM sweep (ASCENDING or DESCENDING)
 uint8_t outputInvertMask = 0x00;      // Mask for inverting output pins
 volatile bool cacheNeedsRefresh = false; // Flag to indicate cache needs refresh
 
 // Global variables that are only used by this file
 // These could be made static to limit their scope
 volatile uint16_t edgeCounter = 0;      // Counts edges in the wheel pattern
 volatile uint32_t cycleStartTime = 0;   // Start time of current cycle (for crank angle calculation)
 volatile uint32_t cycleDuration = 0;    // Duration of the last complete cycle
 
 /**
  * @brief Initialize the lookahead cache with upcoming wheel pattern values
  * @param startEdge First edge index to cache
  */
 void initLookaheadCache(uint16_t startEdge) {
   extern wheels Wheels[];
   extern uint8_t lookaheadCache[];
   extern volatile uint16_t cacheStartEdge;
   
   cacheStartEdge = startEdge;
   
   for (uint8_t i = 0; i < LOOKAHEAD_CACHE_SIZE && (startEdge + i) < Wheels[config.wheel].wheel_max_edges; i++) {
     lookaheadCache[i] = decodeWheelPattern(Wheels[config.wheel].edge_states_ptr, startEdge + i);
   }
 }

 // Forward declarations
 #if defined(ESP32) || defined(ESP32C6)
 void IRAM_ATTR onTimer(void);
 #else
 void onTimer(void);
 #endif
 
 /**
  * @brief Setup function to initialize the system
  *
  * Configures serial communication, pins, timers, and interrupts based on the platform.
  */
 void setup() {
   storageHalInit();          // Init Storage HAL
   storageHalLoadConfig(&config);   // Load config using HAL
   serialHalInit();             // Init Serial using HAL
   gpioHalInit();             // Init GPIO using HAL
   adcHalInit();              // Init ADC using HAL
 
   // Initialize cycle start time
   cycleStartTime = micros();
   
   // Initialize lookahead cache
   initLookaheadCache(0);
 
   timerHalInit(currentStatus.rpm, onTimer); // Init Timer HAL, pass onTimer callback
 
   sei(); // Enable interrupts after setup
   resetNewOCR1A(currentStatus.rpm); // Set initial RPM, using HAL Timer functions internally
 }
 
 /**
  * @brief Timer interrupt handler - executed at each edge transition
  * 
  * This function is called by the timer interrupt at precisely timed intervals
  * to generate wheel patterns. It sets the output pins based on wheel pattern.
  */
 void onTimer() {
   extern wheels Wheels[];
   extern uint8_t lookaheadCache[];
   extern volatile uint16_t cacheStartEdge;
   
   uint8_t state;
   
   // Get state from the lookahead cache
   uint16_t cacheIndex = edgeCounter - cacheStartEdge;
   
   if (cacheIndex < LOOKAHEAD_CACHE_SIZE) {
     // Fast path: value is in cache
     state = lookaheadCache[cacheIndex];
   } else {
     // We've moved beyond the cache - get current value directly
     state = decodeWheelPattern(Wheels[config.wheel].edge_states_ptr, edgeCounter);
     cacheNeedsRefresh = true;  // Schedule full refresh in main loop
   }
   
   // Apply output inversion
   state ^= outputInvertMask;
 
   // Set outputs using HAL
   gpioHalSetOutput(PRIMARY_OUTPUT_PIN, (state & 1));
   gpioHalSetOutput(SECONDARY_OUTPUT_PIN, (state & 2));
   gpioHalSetOutput(TERTIARY_OUTPUT_PIN, (state & 4));
   gpioHalSetOutput(KNOCK_OUTPUT_PIN, (state & 8));
 
   // Increment edge counter (fixed volatile warning)
   edgeCounter = edgeCounter + 1;
   if (edgeCounter >= Wheels[config.wheel].wheel_max_edges) {
     edgeCounter = 0;
     cycleDuration = micros() - cycleStartTime;
     cycleStartTime = micros();
     cacheNeedsRefresh = true;  // Schedule cache refresh for new cycle
   }
 }
 
 /**
  * @brief Updates RPM based on the current mode
  * @param rpm Pointer to the RPM value to update
  */
 void updateRpmBasedOnMode(uint16_t *rpm) {
   switch (config.mode) {
     case POT_RPM:
       *rpm = adcHalReadChannel(0) << TMP_RPM_SHIFT;
       if (*rpm > TMP_RPM_CAP) *rpm = TMP_RPM_CAP;
       break;
       
     case LINEAR_SWEPT_RPM:
       if (micros() > (sweepTimeCounter + config.sweep_interval)) {
         sweepTimeCounter = micros();
         if (sweepDirection == ASCENDING) {
           *rpm = currentStatus.base_rpm + 1;
           if (*rpm >= config.sweep_high_rpm) sweepDirection = DESCENDING;
         } else {
           *rpm = currentStatus.base_rpm - 1;
           if (*rpm <= config.sweep_low_rpm) sweepDirection = ASCENDING;
         }
       }
       break;
       
     case FIXED_RPM:
       *rpm = config.fixed_rpm;
       break;
   }
 }
 
 /**
  * @brief Main loop function
  *
  * Handles serial commands, updates RPM based on mode, and applies compression modifier.
  */
 void loop() {
   uint16_t tmpRpm = currentStatus.base_rpm;
 
   // Check if cache needs refreshing (deferred from ISR)
   if (cacheNeedsRefresh) {
     initLookaheadCache(edgeCounter);
     cacheNeedsRefresh = false;
   }
 
   // Process HAL tasks (including communication)
   halDoWork();
 
   // Handle RPM control based on mode
   updateRpmBasedOnMode(&tmpRpm);
 
   // Apply compression effects
   currentStatus.base_rpm = tmpRpm;
   currentStatus.compressionModifier = calculateCompressionModifier();
   if (currentStatus.compressionModifier >= currentStatus.base_rpm) {
     currentStatus.compressionModifier = 0;
   }
   
   // Set final RPM
   setRPM(currentStatus.base_rpm - currentStatus.compressionModifier);
 }
 
 /**
  * @brief Calculates the compression modifier based on crank angle
  *
  * Simulates engine compression effects by reducing RPM based on a sine wave pattern.
  * @return uint16_t The compression modifier value (0 if disabled or above threshold)
  */
 uint16_t calculateCompressionModifier() {
   extern const uint8_t sin_100_180[] PROGMEM;
   extern const uint8_t sin_100_90[] PROGMEM;
   extern const uint8_t sin_100_120[] PROGMEM;
   
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
   extern wheels Wheels[];
   
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
   if (currentStatus.rpm != newRPM) timerHalSetRpm(newRPM); // Update timer if RPM changes
   currentStatus.rpm = newRPM; // Store final RPM
 }
 
 /**
  * @brief Reset timer compare value for a new RPM
  * @param rpm Target RPM value
  */
 void resetNewOCR1A(uint32_t rpm) {
   timerHalSetRpm(rpm);
 }
 
 /**
  * @brief Main entry point for the program
  * Required for standard C++ applications
  */
 int main(void) {
   init(); // Arduino initialization
   setup();
   
   while(true) {
     loop();
   }
   
   return 0;
 }