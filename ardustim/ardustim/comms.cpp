/* vim: set syntax=c expandtab sw=2 softtabstop=2 autoindent smartindent smarttab : */
/*
 * ArduStim - Command processing
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
 */

 #include "globals.h"
 #include "main.h"
 #include "hal.h"
 #include "enums.h"
 #include "comms.h"
 #include "wheelDefs.h"
 #if defined(__AVR__)
 #include <avr/pgmspace.h>
 #include <util/delay.h>
 #endif
 #if defined(ESP32)
 #include <esp_heap_caps.h>
 #endif
 #include <math.h>
  
 /* External Global Variables */
 extern wheels Wheels[];
  
 /* Volatile variables (USED in ISR's) */
 extern volatile bool normal;
 extern volatile uint16_t edgeCounter;
 extern volatile uint16_t newOCR1A;
  
 // Command processing variables
 bool cmdPending;
 byte currentCommand;
  
 /**
  * @brief Returns a string representation of the board type
  * @return const char* String with board type
  */
 const char* getBoardTypeString() {
   #if defined(__AVR__)
   return "AVR";
   #elif defined(ESP32) && !defined(ESP32C6)
   return "ESP32";
   #elif defined(ESP32C6)
   return "ESP32C6";
   #else
   return "Unknown";
   #endif
 }
 
 /**
  * @brief Sends the current wheel pattern and wheel degrees
  * @param stream Stream to send the data to
  */
 void handleWheelPatternCommand(Stream* stream) {
   uint16_t wheelMaxEdges = Wheels[config.wheel].wheel_max_edges;
  
   // Send edge states as comma-separated values
   for(uint16_t x = 0; x < wheelMaxEdges; x++) {
     if(x != 0) {
       stream->print(",");
     }
  
     // Use decodeWheelPattern instead of direct access to handle RLE correctly
     byte tempByte = decodeWheelPattern(Wheels[config.wheel].edge_states_ptr, x);
     stream->print(String(tempByte).c_str());
   }
  
   stream->println("");
  
   // Send the number of degrees the wheel runs over (360 or 720 typically)
   stream->println(String(Wheels[config.wheel].wheel_degrees).c_str());
 }
  
 /**
  * @brief Parses incoming commands from a stream
  * @param stream Stream to read from and write to
  * @return bool True if a command was processed
  */
 bool commandParser(Stream* stream)
 {
   static unsigned long waitStartTime = 0;
   const unsigned long WAIT_TIMEOUT = 1000; // 1 second timeout for waiting on parameters
   char buf[80];
   byte tmp_wheel;
   void* pntConfig = &config;
   bool cmdProcessed = false;
 
   if (cmdPending == false) {
     if (stream->available()) {
       currentCommand = stream->read();
       
       // Ignore newlines and carriage returns
       if (currentCommand == '\n' || currentCommand == '\r') {
         return false;
       }
       
       cmdPending = true;
       waitStartTime = millis(); // Start timer when we get a command
     } else {
       return false; // No data available
     }
   } else {
     return false; // Already processing a command
   }
 
   switch (currentCommand)
   {
     case (byte)'b': // Report board type
       stream->println(getBoardTypeString());
       cmdProcessed = true;
       break;
 
     case (byte)'c':// Receive full config
       {
         // Non-blocking wait with timeout
         int requiredBytes = static_cast<int>(sizeof(struct configTable) - 1);
         while(stream->available() < requiredBytes) {
           if (millis() - waitStartTime > WAIT_TIMEOUT) {
             stream->println("Error: Timeout waiting for config data");
             cmdPending = false;
             return false;
           }
           delay(1); // Small delay to prevent CPU hogging
         }
         
         for(uint8_t x=1; x<(sizeof(struct configTable)); x++) {
           *((uint8_t *)pntConfig + x) = stream->read();
         }
         cmdProcessed = true;
       }
       break;
 
     case (byte)'C': // Send current config
       for(uint8_t x=0; x<sizeof(struct configTable); x++) {
         stream->write(*((uint8_t *)pntConfig + x));
       }
       cmdProcessed = true;
       break;
       
     case (byte)'L': // Send the list of wheel names      
       for(byte x=0;x<MAX_WHEELS;x++) {
         strcpy_P(buf,Wheels[x].decoder_name);
         stream->println(buf);
       }
       cmdProcessed = true;
       break;
 
     case (byte)'n': // Send number of wheels
       stream->println(String(MAX_WHEELS).c_str());
       cmdProcessed = true;
       break;
 
     case (byte)'N': // Send current wheel index
       stream->println(String(config.wheel).c_str());
       cmdProcessed = true;
       break;
     
     case (byte)'p': // Send current wheel size (edges)
       stream->println(String(Wheels[config.wheel].wheel_max_edges).c_str());
       cmdProcessed = true;
       break;
 
     case (byte)'P': // Send current wheel pattern
       handleWheelPatternCommand(stream);
       cmdProcessed = true;
       break;
 
     case (byte)'R': // Send current RPM
       stream->println(String(currentStatus.rpm).c_str());
       cmdProcessed = true;
       break;
 
     case (byte)'r': // Set sweep mode parameters
       {
         // Non-blocking wait with timeout
         while(stream->available() < 6) {
           if (millis() - waitStartTime > WAIT_TIMEOUT) {
             stream->println("Error: Timeout waiting for sweep parameters");
             cmdPending = false;
             return false;
           }
           delay(1);
         }
         
         config.mode = LINEAR_SWEPT_RPM;
         config.sweep_low_rpm = word(stream->read(), stream->read());
         config.sweep_high_rpm = word(stream->read(), stream->read());
         config.sweep_interval = word(stream->read(), stream->read());
         cmdProcessed = true;
       }
       break;
 
     case (byte)'s': // Save config to storage
       storageHalSaveConfig(&config);
       cmdProcessed = true;
       break;
 
     case (byte)'S': // Set current wheel
       {
         // Non-blocking wait with timeout
         while(stream->available() < 1) {
           if (millis() - waitStartTime > WAIT_TIMEOUT) {
             stream->println("Error: Timeout waiting for wheel selection");
             cmdPending = false;
             return false;
           }
           delay(1);
         }
         
         tmp_wheel = stream->read();
         if(tmp_wheel < MAX_WHEELS) {
           config.wheel = tmp_wheel;
           displayNewWheel();
           cmdProcessed = true;
         }
       }
       break;
 
    case (byte) 'w': // Get wireless communication type
       stream->println(getWirelessSupportType());
       cmdProcessed = true;
       break;

     case (byte)'X': // Test: Switch to next wheel
       selectNextWheelCb();
       strcpy_P(buf,Wheels[config.wheel].decoder_name);
       stream->println(buf);
       cmdProcessed = true;
       break;
 
     default:
       // Unknown command - no action taken
       break;
   }
   cmdPending = false;
   return cmdProcessed;
 }
  
 /**
  * @brief Returns the amount of free RAM available
  *
  * For AVR, calculates free RAM using stack and heap pointers.
  * For ESP32, uses ESP.getFreeHeap() to get free heap size.
  *
  * @return uint32_t Free RAM in bytes
  */
 uint32_t freeRam() {
   #if defined(__AVR__)
     extern int __heap_start, *__brkval;
     int v;
     int freeMemory;
     if (__brkval == 0) {
       freeMemory = (int) &v - (int) &__heap_start;
     } else {
       freeMemory = (int) &v - (int) __brkval;
     }
     return (uint32_t) freeMemory;
   #elif defined(ESP32)
     return heap_caps_get_free_size(MALLOC_CAP_8BIT);
   #else
     return 0;  // Unsupported platform
   #endif
 }
  
 /* Callbacks - These remain unchanged */
 void toggleInvertPrimaryCb()
 {
   extern uint8_t outputInvertMask;
   outputInvertMask ^= 0x01; /* Flip crank invert mask bit */
 }
  
 void toggleInvertSecondaryCb()
 {
   extern uint8_t outputInvertMask;
   outputInvertMask ^= 0x02; /* Flip cam invert mask bit */
 }
  
 void displayNewWheel()
 {
   resetNewOCR1A(currentStatus.rpm);
   edgeCounter = 0; // Reset to beginning of the wheel pattern
  
   // Initialize cache with new wheel pattern
   initLookaheadCache(0);
 }
  
 void selectNextWheelCb()
 {
   if (config.wheel == (MAX_WHEELS-1))
     config.wheel = 0;
   else
     config.wheel++;
  
   displayNewWheel();
 }
  
 void selectPreviousWheelCb()
 {
   if (config.wheel == 0)
     config.wheel = MAX_WHEELS-1;
   else
     config.wheel--;
  
   displayNewWheel();
 }