/* vim: set syntax=c expandtab sw=2 softtabstop=2 autoindent smartindent smarttab : */
/*
 * ArduStim - WiFi connectivity for ESP32 platforms
 *
 * Copyright 2025
 * 
 * Ardu-Stim software is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

 #include "wifiHal.h"

 #if defined(ESP32) // Only compile for ESP32 platforms
 
 // Global variables for WiFi support
 WiFiServer tcpServer(TCP_SERVER_PORT);
 TCPClientWrapper tcpClients[MAX_TCP_CLIENTS];
 WiFiConnectionState wifiState = WIFI_DISABLED;
 unsigned long lastWiFiAttempt = 0;
 
 /**
  * @brief Process incoming data and handle command parsing
  */
 void TCPClientWrapper::processData() {
   if (!connected) return;
   
   // Read available data
   while (client.available()) {
     char c = client.read();
     lastActivity = millis();
     
     // Handle line breaks and command processing
     if (c == '\n' || c == '\r') {
       if (inputBuffer.length() > 0) {
         // Create a stream wrapper for the buffer
         MemoryStream memStream(inputBuffer.c_str(), inputBuffer.length());
         
         // Process command using the same parser as serial
         bool cmdProcessed = commandParser(&memStream);
         
         // Send response back to client if any
         if (memStream.available()) {
           String response = "";
           while (memStream.available()) {
             response += (char)memStream.read();
           }
           sendData(response.c_str());
         }
         
         inputBuffer = "";
       }
     } else {
       // Add character to buffer
       inputBuffer += c;
     }
   }
 }
 
 /**
  * @brief Send data to client
  * @param data Data to send
  * @param len Length of data
  */
 void TCPClientWrapper::sendData(const char* data, size_t len) {
   if (connected) {
     client.write(data, len);
     lastActivity = millis();
   }
 }
 
 /**
  * @brief Send data to client (null-terminated)
  * @param data Data to send
  */
 void TCPClientWrapper::sendData(const char* data) {
   if (connected) {
     client.print(data);
     lastActivity = millis();
   }
 }
 
 /**
  * @brief Update client state
  */
 void TCPClientWrapper::update() {
   isConnected(); // Check connection status
 }
 
 /**
  * @brief Initialize WiFi functionality
  */
 void wifiHalInit() {
   wifiState = WIFI_DISABLED;
   
   // Check if WiFi is enabled in config
   if (!config.wifiEnabled) {
     return;
   }
   
   // Ensure SSID isn't empty
   if (strlen((char*)config.wifiSSID) == 0) {
     return;
   }
   
   // Initialize WiFi in station mode
   WiFi.mode(WIFI_STA);
   WiFi.setAutoReconnect(true);
   
   // Start connection attempt
   wifiState = WIFI_CONNECTING;
   WiFi.begin((char*)config.wifiSSID, (char*)config.wifiPassword);
   lastWiFiAttempt = millis();
   
   // Set hostname to "ardustim"
   WiFi.setHostname("ardustim");
   
   // Wait up to 5 seconds for initial connection attempt
   unsigned long startAttempt = millis();
   while (WiFi.status() != WL_CONNECTED && millis() - startAttempt < 5000) {
     delay(100);
   }
   
   // Check connection result
   if (WiFi.status() == WL_CONNECTED) {
     wifiState = WIFI_CONNECTED;
     Serial.println("WiFi connected");
     // Start mDNS responder
     if (MDNS.begin("ardustim")) {
       MDNS.addService("telnet", "tcp", TCP_SERVER_PORT);
     }
     
     // Start TCP server
     tcpServer.begin();
     tcpServer.setNoDelay(true);
   } else {
     wifiState = WIFI_DISCONNECTED;
      Serial.println("WiFi connection failed");
   }
 }
 
 /**
  * @brief Perform periodic WiFi maintenance tasks
  */
 void wifiHalDoWork() {
   // Skip if WiFi is disabled
   if (!config.wifiEnabled) {
     if (wifiState != WIFI_DISABLED) {
       wifiHalDisconnect();
       wifiState = WIFI_DISABLED;
     }
     return;
   }
   
   // Handle WiFi state
   switch (wifiState) {
     case WIFI_DISCONNECTED:
       // Retry connection after delay
       if (millis() - lastWiFiAttempt > WIFI_RETRY_DELAY) {
         wifiState = WIFI_CONNECTING;
         WiFi.begin((char*)config.wifiSSID, (char*)config.wifiPassword);
         lastWiFiAttempt = millis();
       }
       break;
       
     case WIFI_CONNECTING:
       // Check connection result
       if (WiFi.status() == WL_CONNECTED) {
         wifiState = WIFI_CONNECTED;
         
         // Start mDNS responder
         if (MDNS.begin("ardustim")) {
           MDNS.addService("telnet", "tcp", TCP_SERVER_PORT);
         }
         
         // Start TCP server
         tcpServer.begin();
         tcpServer.setNoDelay(true);
       } else if (millis() - lastWiFiAttempt > WIFI_RETRY_DELAY) {
         // Retry failed connection after delay
         WiFi.disconnect();
         wifiState = WIFI_DISCONNECTED;
       }
       break;
       
     case WIFI_CONNECTED:
       // Check if we're still connected
       if (WiFi.status() != WL_CONNECTED) {
         wifiState = WIFI_DISCONNECTED;
         lastWiFiAttempt = millis();
       } else {
         // Process TCP clients
         wifiHalHandleNewClients();
         wifiHalProcessClientData();
       }
       break;
       
     default:
       break;
   }
 }
 
 /**
  * @brief Check if WiFi is connected
  * @return bool True if connected
  */
 bool wifiHalIsConnected() {
   return (wifiState == WIFI_CONNECTED && WiFi.status() == WL_CONNECTED);
 }
 
 /**
  * @brief Disconnect WiFi and cleanup resources
  */
 void wifiHalDisconnect() {
   // Stop TCP server and disconnect clients
   tcpServer.end();
   for (int i = 0; i < MAX_TCP_CLIENTS; i++) {
     tcpClients[i].disconnect();
   }
   
   // Stop mDNS
   MDNS.end();
   
   // Disconnect WiFi
   WiFi.disconnect(true);
   wifiState = WIFI_DISABLED;
 }
 
 /**
  * @brief Get current IP address as string
  * @return String IP address or empty string if not connected
  */
 String wifiHalGetIP() {
   if (wifiHalIsConnected()) {
     return WiFi.localIP().toString();
   }
   return "";
 }
 
 /**
  * @brief Handle new client connections
  */
 void wifiHalHandleNewClients() {
   // Check for new clients
   if (tcpServer.hasClient()) {
     // Use accept() instead of available() to avoid deprecation warning
     WiFiClient newClient = tcpServer.accept();
     
     // Find a free slot
     int freeSlot = -1;
     for (int i = 0; i < MAX_TCP_CLIENTS; i++) {
       if (!tcpClients[i].isConnected()) {
         freeSlot = i;
         break;
       }
     }
     
     // Add client if slot is available, otherwise reject
     if (freeSlot >= 0) {
       tcpClients[freeSlot] = TCPClientWrapper(newClient);
       
       // Send welcome message
       tcpClients[freeSlot].sendData("ArduStim Telnet Server\r\n");
     } else {
       // No free slots, reject client
       newClient.stop();
     }
   }
 }
 
 /**
  * @brief Process data from all connected clients
  */
 void wifiHalProcessClientData() {
   for (int i = 0; i < MAX_TCP_CLIENTS; i++) {
     if (tcpClients[i].isConnected()) {
       tcpClients[i].update();
       tcpClients[i].processData();
       
       // Disconnect inactive clients
       if (millis() - tcpClients[i].getLastActivity() > TCP_CLIENT_TIMEOUT) {
         tcpClients[i].disconnect();
       }
     }
   }
 }
 
 /**
  * @brief Broadcast a message to all connected clients
  * @param data Data to broadcast
  */
 void wifiHalBroadcast(const char* data) {
   for (int i = 0; i < MAX_TCP_CLIENTS; i++) {
     if (tcpClients[i].isConnected()) {
       tcpClients[i].sendData(data);
     }
   }
 }
 
 #else
  /**
  * @brief Stub function for non-ESP32 platforms
  */
 void wifiHalInit() {
   // No-op for non-ESP32 platforms
  }

 #endif