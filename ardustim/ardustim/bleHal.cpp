/* vim: set syntax=c expandtab sw=2 softtabstop=2 autoindent smartindent smarttab : */
/*
 * ArduStim - BLE connectivity implementation for ESP32 platforms
 *
 * Copyright 2025
 *
 * Ardu-Stim software is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

 #include "bleHal.h"

 #if defined(ESP32) // Only compile for ESP32 platforms
 
 // --- Global BLE Variables ---
 BLEServer*          pServer = NULL;
 BLECharacteristic*  pTxCharacteristic = NULL; // For sending data to client
 BLECharacteristic*  pRxCharacteristic = NULL; // For receiving data from client
 bool                deviceConnected = false;
 bool                oldDeviceConnected = false;
 String              rxBuffer = "";            // Buffer for incoming BLE data
 uint32_t            lastBleActivity = 0;      // For potential timeouts (optional)
 size_t              bleMtu = 20;              // Default MTU - 3 overhead
 
 // --- BLE Callback Classes ---
 
 // Handles server connection and disconnection events
 class MyServerCallbacks: public BLEServerCallbacks {
     void onConnect(BLEServer* pServer, esp_ble_gatts_cb_param_t *param) {
       deviceConnected = true;

       if (bleMtu < 20) bleMtu = 20; // Ensure minimum MTU
       Serial.println("BLE Client Connected");
       lastBleActivity = millis();
     };
 
     void onDisconnect(BLEServer* pServer) {
       deviceConnected = false;
       Serial.println("BLE Client Disconnected");
       // Restart advertising to allow new connections
       // Delay slightly before restarting advertising to allow stack cleanup
       delay(100); // Adjust delay if needed
       pServer->getAdvertising()->start();
       Serial.println("BLE Advertising restarted");
     }
 };
 
 // Handles incoming data on the RX Characteristic
 class MyRxCallbacks: public BLECharacteristicCallbacks {
     void onWrite(BLECharacteristic *pCharacteristic) {
       std::string rxValue = pCharacteristic->getValue();
       if (rxValue.length() > 0) {
         // Append received data to buffer
         for (int i = 0; i < rxValue.length(); i++) {
           rxBuffer += (char)rxValue[i];
         }
         // Serial.print("BLE Received: "); // Debugging
         // Serial.println(rxBuffer.c_str());
         lastBleActivity = millis();
       }
     }
 };
 
 
 // --- BLECommandStream Implementation ---
 
 BLECommandStream::BLECommandStream(const char* inputData, size_t inputLen) :
     _inputBuffer(inputData),
     _inputLength(inputLen),
     _inputPos(0),
     _responseBuffer(""),
     _maxPacketSize(bleMtu) // Use the negotiated MTU
 {}
 
 // Read from the input buffer provided at construction
 int BLECommandStream::available() {
     return _inputLength - _inputPos;
 }
 
 int BLECommandStream::read() {
     if (_inputPos < _inputLength) {
         return _inputBuffer[_inputPos++];
     }
     return -1;
 }
 
 int BLECommandStream::peek() {
     if (_inputPos < _inputLength) {
         return _inputBuffer[_inputPos];
     }
     return -1;
 }
 
 // Write to the internal response buffer
 size_t BLECommandStream::write(uint8_t data) {
     _responseBuffer += (char)data;
     return 1;
 }
 
 // Send the buffered response over BLE TX Characteristic
 void BLECommandStream::flush() {
     if (_responseBuffer.length() > 0) {
         sendData((const uint8_t*)_responseBuffer.c_str(), _responseBuffer.length());
         _responseBuffer = ""; // Clear buffer after sending
     }
 }
 
 // Private helper to send data over BLE, handling fragmentation
 void BLECommandStream::sendData(const uint8_t* data, size_t len) {
     if (deviceConnected && pTxCharacteristic != NULL) {
         size_t bytesSent = 0;
         while (bytesSent < len) {
             size_t chunkSize = len - bytesSent;
             if (chunkSize > _maxPacketSize) {
                 chunkSize = _maxPacketSize;
             } 
             pTxCharacteristic->setValue((uint8_t*)(data + bytesSent), chunkSize);
             pTxCharacteristic->notify();
             bytesSent += chunkSize;
             // Small delay between packets might be needed for some clients
             // delay(2); // Adjust if needed, often not necessary
         }
         lastBleActivity = millis();
     }
 }
 
 // --- BLE HAL Functions ---
 
 void bleHalInit() {
     if (!config.bluetoothEnabled) {
         Serial.println("BLE disabled in configuration.");
         return;
     }
 
     Serial.println("Initializing BLE...");
 
     // Create the BLE Device
     BLEDevice::init("ArduStim"); // Set device name
 
     // Create the BLE Server
     pServer = BLEDevice::createServer();
     pServer->setCallbacks(new MyServerCallbacks());
 
     // Create the BLE Service (NUS)
     BLEService *pService = pServer->createService(SERVICE_UUID);
 
     // Create BLE Characteristics
     // TX Characteristic (Server to Client)
     pTxCharacteristic = pService->createCharacteristic(
                       CHARACTERISTIC_UUID_TX,
                       BLECharacteristic::PROPERTY_NOTIFY // Allow notifications
                     );
     pTxCharacteristic->addDescriptor(new BLE2902()); // Standard descriptor for notifications
 
     // RX Characteristic (Client to Server)
     pRxCharacteristic = pService->createCharacteristic(
                        CHARACTERISTIC_UUID_RX,
                        BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_WRITE_NR // Allow writes (with or without response)
                      );
     pRxCharacteristic->setCallbacks(new MyRxCallbacks()); // Set callback for incoming data
 
     // Start the service
     pService->start();
 
     // Start advertising
     BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
     pAdvertising->addServiceUUID(SERVICE_UUID); // Advertise our service
     pAdvertising->setScanResponse(true);
     //pAdvertising->setMinPreferred(0x06);  // functions that help with iPhone connections issue
     //pAdvertising->setMinPreferred(0x12);
     pAdvertising->start();
 
     deviceConnected = false;
     oldDeviceConnected = false;
     Serial.println("BLE Initialized and Advertising started.");
 }
 
 void bleHalDoWork() {
     // Check if BLE should be enabled/disabled based on config
     if (!config.bluetoothEnabled) {
         if (pServer != NULL) { // If BLE was previously initialized
            bleHalDisconnect(); // Cleanly disconnect and stop BLE
            // Consider deinitializing BLEDevice if necessary, often stopping server/adv is enough
            // BLEDevice::deinit(); // Use with caution, might affect other BLE functionalities if added later
            pServer = NULL; // Mark as uninitialized
            pTxCharacteristic = NULL;
            pRxCharacteristic = NULL;
            Serial.println("BLE Disabled.");
         }
         return;
     } else if (pServer == NULL) {
         // BLE is enabled in config but not initialized yet
         bleHalInit();
         if (pServer == NULL) return; // Initialization failed
     }
 
 
     // Handle new connection state changes (optional logging/actions)
     if (deviceConnected && !oldDeviceConnected) {
         oldDeviceConnected = true;
         // Actions on connection if needed (already logged in callback)
     }
     if (!deviceConnected && oldDeviceConnected) {
         oldDeviceConnected = false;
         // Actions on disconnection if needed (already logged in callback)
         // Advertising is restarted in the disconnect callback
     }
 
     // Process received data if connected
     if (deviceConnected && rxBuffer.length() > 0) {
         // Look for a newline character to delimit commands
         int newlinePos = rxBuffer.indexOf('\n');
         if (newlinePos == -1) {
            newlinePos = rxBuffer.indexOf('\r'); // Also check for carriage return
         }
 
         if (newlinePos != -1) {
             // Extract the command (including potential preceding \r)
             String commandLine = rxBuffer.substring(0, newlinePos);
             rxBuffer.remove(0, newlinePos + 1); // Remove command and newline from buffer
 
             // Trim leading/trailing whitespace/CRLF just in case
             commandLine.trim();
 
             if (commandLine.length() > 0) {
                 //Serial.print("Processing BLE Command: "); // Debug
                 //Serial.println(commandLine);
 
                 // Create stream objects for command parser
                 BLECommandStream bleStream(commandLine.c_str(), commandLine.length());
 
                 // Call the central command parser
                 bool processed = commandParser(&bleStream);
 
                 // Flush the response buffer (sends data over BLE)
                 bleStream.flush();
 
                 // Serial.print("BLE Command Processed: "); // Debug
                 // Serial.println(processed);
             }
         }
         // Optional: Add a check for buffer overflow if commands are very long or frequent without newlines
         // if (rxBuffer.length() > 512) { rxBuffer = ""; Serial.println("BLE RX Buffer Overflow!"); }
     }
 
     // Optional: Add timeout for inactive clients?
     // if (deviceConnected && (millis() - lastBleActivity > BLE_CLIENT_TIMEOUT_MS)) {
     //    Serial.println("BLE Client inactive, disconnecting.");
     //    pServer->disconnect(pServer->getConnId()); // Disconnect the client
     // }
 }
 
 bool bleHalIsConnected() {
     return deviceConnected;
 }
 
 // Disconnect client and stop advertising/server
 void bleHalDisconnect() {
     if (pServer != NULL) {
         // Disconnect any connected clients
         uint16_t connId = pServer->getConnId(); // Get current connection ID
         if (connId != 0xffff) { // Check if valid connection ID exists
              pServer->disconnect(connId);
              Serial.println("Forcing BLE client disconnection.");
         }
 
         // Stop advertising (important!)
         BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
         if (pAdvertising) {
              pAdvertising->stop();
              Serial.println("BLE Advertising stopped.");
         }
 
         // Potentially stop the server/service? Usually not needed if just stopping advertising
         // If completely stopping BLE:
         // pServer->removeService(pServer->getServiceByUUID(SERVICE_UUID)); // Maybe?
     }
     deviceConnected = false;
     oldDeviceConnected = false;
 }
 
 // Send arbitrary data (e.g., status updates) - USE WITH CAUTION regarding MTU
 void bleHalSendData(const char* data) {
     bleHalSendData((const uint8_t*)data, strlen(data));
 }
 
 void bleHalSendData(const uint8_t* data, size_t len) {
     if (deviceConnected && pTxCharacteristic != NULL) {
         BLECommandStream tempStream("", 0); // Use helper from stream class
         tempStream.sendData(data, len);
         //Serial.print("Sent Async BLE Data, len="); // Debug
         //Serial.println(len);
     }
 }
 
 
 #endif // ESP32 || ESP32C6