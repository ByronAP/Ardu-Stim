/* vim: set syntax=c expandtab sw=2 softtabstop=2 autoindent smartindent smarttab : */
/*
 * ArduStim - BLE connectivity for ESP32 platforms
 *
 * Copyright 2025
 *
 * Ardu-Stim software is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

 #ifndef __BLE_HAL_H__
 #define __BLE_HAL_H__
 
 #include <Arduino.h>
 #include "globals.h"
 #include "comms.h"
 
 #if defined(ESP32) && !defined(ESP32C6) // Only include BLE specifics for ESP32 platforms
 
 #include <BLEDevice.h>
 #include <BLEServer.h>
 #include <BLEUtils.h>
 #include <BLE2902.h> // For characteristic descriptors
 
 // Define Nordic UART Service (NUS) UUIDs
 #define SERVICE_UUID           "6E400001-B5A3-F393-E0A9-E50E24DCCA9E"
 #define CHARACTERISTIC_UUID_RX "6E400002-B5A3-F393-E0A9-E50E24DCCA9E" // Client writes commands here
 #define CHARACTERISTIC_UUID_TX "6E400003-B5A3-F393-E0A9-E50E24DCCA9E" // Server sends responses/notifications here
 
 // --- BLECommandStream Class ---
 // A Stream implementation that reads from a provided input buffer
 // and buffers output data to be sent over BLE TX characteristic on flush.
 class BLECommandStream : public Stream {
 private:
     const char*   _inputBuffer;
     size_t        _inputLength;
     size_t        _inputPos;
     String        _responseBuffer;
     size_t        _maxPacketSize; // Max BLE packet size
 
 public:
     BLECommandStream(const char* inputData, size_t inputLen);
 
     // Stream interface methods (Input)
     virtual int available() override;
     virtual int read() override;
     virtual int peek() override;
 
     // Print interface methods (Output)
     virtual size_t write(uint8_t data) override;
     using Print::write; // Inherit other write methods
 
     // Method to send the buffered output over BLE
     virtual void flush() override;
     
     // Helper to send data (handles fragmentation)
     void sendData(const uint8_t* data, size_t len);
 };
 
 
 // --- Function Prototypes ---
 
 /**
  * @brief Initialize BLE functionality (Server, Service, Characteristics)
  */
 void bleHalInit();
 
 /**
  * @brief Perform periodic BLE tasks (handle connections, process data)
  */
 void bleHalDoWork();
 
 /**
  * @brief Check if a BLE client is currently connected
  * @return bool True if connected, false otherwise
  */
 bool bleHalIsConnected();
 
 /**
  * @brief Disconnect any connected client and stop BLE services cleanly
  */
 void bleHalDisconnect();
 
 /**
  * @brief Send data over the BLE TX characteristic (for potential async messages)
  * @param data C-string data to send
  */
 void bleHalSendData(const char* data);
 
 /**
  * @brief Send data over the BLE TX characteristic (for potential async messages)
  * @param data Pointer to data buffer
  * @param len Length of data buffer
  */
 void bleHalSendData(const uint8_t* data, size_t len);
 
 
 #else // !defined(ESP32)
 
 // Provide empty stubs for non-ESP platforms to allow compilation
 inline void bleHalInit() { /* No BLE on this platform */ }
 inline void bleHalDoWork() { /* No BLE on this platform */ }
 inline bool bleHalIsConnected() { return false; }
 inline void bleHalDisconnect() { /* No BLE on this platform */ }
 inline void bleHalSendData(const char* data) { /* No BLE on this platform */ }
 inline void bleHalSendData(const uint8_t* data, size_t len) { /* No BLE on this platform */ }
 
 #endif // ESP32 || ESP32C6
 
 #endif // __BLE_HAL_H__