/* vim: set syntax=c expandtab sw=2 softtabstop=2 autoindent smartindent smarttab : */
/*
 * ArduStim - Serial communications handlers
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
  * @brief Initializes serial communication
  * Sets up the serial port and menu for the serial user interface
  */
 void serialSetup()
 {
   serialHalInit();
   cmdPending = false;
   serialHalPrintln("Serial Setup Complete");
 }
 
 /**
  * @brief Handles wheel pattern command
  * Sends the current wheel pattern and wheel degrees
  */
 void handleWheelPatternCommand() {
   uint16_t wheelMaxEdges = Wheels[config.wheel].wheel_max_edges;
 
   // Send edge states as comma-separated values
   for(uint16_t x = 0; x < wheelMaxEdges; x++) {
     if(x != 0) {
       serialHalPrint(",");
     }
 
     // Use decodeWheelPattern instead of direct access to handle RLE correctly
     byte tempByte = decodeWheelPattern(Wheels[config.wheel].edge_states_ptr, x);
     serialHalPrint(String(tempByte).c_str());
   }
 
   serialHalPrintln("");
 
   // Send the number of degrees the wheel runs over (360 or 720 typically)
   serialHalPrintln(String(Wheels[config.wheel].wheel_degrees).c_str());
 }
 
 /**
  * @brief Handles board type query
  * Reports the current board type (AVR, ESP32, etc.)
  */
 void handleBoardTypeCommand() {
   #if defined(__AVR__)
   serialHalPrintln("AVR");
   #elif defined(ESP32) && !defined(ESP32C6)
   serialHalPrintln("ESP32");
   #elif defined(ESP32C6)
   serialHalPrintln("ESP32C6");
   #else
   serialHalPrintln("Unknown");
   #endif
 }
 
 /**
 * @brief Parses incoming serial commands
 */
void commandParser()
{
  static unsigned long waitStartTime = 0;
  const unsigned long WAIT_TIMEOUT = 1000; // 1 second timeout for waiting on parameters
  char buf[80];
  byte tmp_wheel;
  void* pntConfig = &config;

  if (cmdPending == false) {
    if (serialHalAvailable()) {
      currentCommand = serialHalReadByte();
      
      // Ignore newlines and carriage returns
      if (currentCommand == '\n' || currentCommand == '\r') {
        return; // Ignore newline/carriage return
      }
      
      cmdPending = true;
      waitStartTime = millis(); // Start timer when we get a command
    } else {
      return; // No data available
    }
  } else {
    return; // Already processing a command
  }

  switch (currentCommand)
  {
    case (byte)'b': // Report board type
      handleBoardTypeCommand();
      break;

    case (byte)'c':// Receive full config
      {
        // Non-blocking wait with timeout
        int requiredBytes = static_cast<int>(sizeof(struct configTable) - 1);
        while(serialHalAvailable() < requiredBytes) {
          if (millis() - waitStartTime > WAIT_TIMEOUT) {
            serialHalPrintln("Error: Timeout waiting for config data");
            cmdPending = false;
            return;
          }
          delay(1); // Small delay to prevent CPU hogging
        }
        
        for(uint8_t x=1; x<(sizeof(struct configTable)); x++) {
          *((uint8_t *)pntConfig + x) = serialHalReadByte();
        }
      }
      break;

    case (byte)'C': // Send current config
      for(uint8_t x=0; x<sizeof(struct configTable); x++) {
        serialHalWriteByte(*((uint8_t *)pntConfig + x));
      }
      break;
      
    case (byte)'L': // Send the list of wheel names      
      for(byte x=0;x<MAX_WHEELS;x++) {
        strcpy_P(buf,Wheels[x].decoder_name);
        serialHalPrintln(buf);
      }
      break;

    case (byte)'n': // Send number of wheels
      serialHalPrintln(String(MAX_WHEELS).c_str());
      break;

    case (byte)'N': // Send current wheel index
      serialHalPrintln(String(config.wheel).c_str());
      break;
    
    case (byte)'p': // Send current wheel size (edges)
      serialHalPrintln(String(Wheels[config.wheel].wheel_max_edges).c_str());
      break;

    case (byte)'P': // Send current wheel pattern
      handleWheelPatternCommand();
      break;

    case (byte)'R': // Send current RPM
      serialHalPrintln(String(currentStatus.rpm).c_str());
      break;

    case (byte)'r': // Set sweep mode parameters
      {
        // Non-blocking wait with timeout
        while(serialHalAvailable() < 6) {
          if (millis() - waitStartTime > WAIT_TIMEOUT) {
            serialHalPrintln("Error: Timeout waiting for sweep parameters");
            cmdPending = false;
            return;
          }
          delay(1);
        }
        
        config.mode = LINEAR_SWEPT_RPM;
        config.sweep_low_rpm = word(serialHalReadByte(), serialHalReadByte());
        config.sweep_high_rpm = word(serialHalReadByte(), serialHalReadByte());
        config.sweep_interval = word(serialHalReadByte(), serialHalReadByte());
      }
      break;

    case (byte)'s': // Save config to storage
      storageHalSaveConfig(&config);
      break;

    case (byte)'S': // Set current wheel
      {
        // Non-blocking wait with timeout
        while(serialHalAvailable() < 1) {
          if (millis() - waitStartTime > WAIT_TIMEOUT) {
            serialHalPrintln("Error: Timeout waiting for wheel selection");
            cmdPending = false;
            return;
          }
          delay(1);
        }
        
        tmp_wheel = serialHalReadByte();
        if(tmp_wheel < MAX_WHEELS) {
          config.wheel = tmp_wheel;
          displayNewWheel();
        }
      }
      break;

    case (byte)'X': // Test: Switch to next wheel
      selectNextWheelCb();
      strcpy_P(buf,Wheels[config.wheel].decoder_name);
      serialHalPrintln(buf);
      break;

    default:
      break;
  }
  cmdPending = false;
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
 
 /* SerialUI Callbacks */
 /**
  * @brief Toggles inversion of the primary output
  */
 void toggleInvertPrimaryCb()
 {
   extern uint8_t outputInvertMask;
   outputInvertMask ^= 0x01; /* Flip crank invert mask bit */
 }
 
 /**
  * @brief Toggles inversion of the secondary output
  */
 void toggleInvertSecondaryCb()
 {
   extern uint8_t outputInvertMask;
   outputInvertMask ^= 0x02; /* Flip cam invert mask bit */
 }
 
 /**
  * @brief Updates system for a new wheel selection
  */
 void displayNewWheel()
 {
   resetNewOCR1A(currentStatus.rpm);
   edgeCounter = 0; // Reset to beginning of the wheel pattern
 
   // Initialize cache with new wheel pattern
   initLookaheadCache(0);
 }
 
 /**
  * @brief Selects the next wheel in the list
  */
 void selectNextWheelCb()
 {
   if (config.wheel == (MAX_WHEELS-1))
     config.wheel = 0;
   else
     config.wheel++;
 
   displayNewWheel();
 }
 
 /**
  * @brief Selects the previous wheel in the list
  */
 void selectPreviousWheelCb()
 {
   if (config.wheel == 0)
     config.wheel = MAX_WHEELS-1;
   else
     config.wheel--;
 
   displayNewWheel();
 }