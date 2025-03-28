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

 #ifndef __WIFI_HAL_H__
 #define __WIFI_HAL_H__
 
 #include <Arduino.h>
 #include "enums.h"
 #include "globals.h"
 #include "comms.h"
 
 #if defined(ESP32)
 #include <WiFi.h>
 #include <ESPmDNS.h>
 #include <WiFiClient.h>

 
 // WiFi configuration 
 #define WIFI_RETRY_DELAY 20000  // 20 seconds between connection attempts
 #define TCP_SERVER_PORT 23      // Standard telnet port
 #define MAX_TCP_CLIENTS 2       // Maximum simultaneous clients
 #define TCP_CLIENT_TIMEOUT 1800000 // 30 minutes timeout for inactive clients
 
 // Stream wrapper class for in-memory command processing
 class MemoryStream : public Stream {
 private:
   const char* buffer;
   size_t bufferLength;
   size_t readPosition;
   String responseBuffer;
 
 public:
   MemoryStream(const char* buf, size_t len) : 
     buffer(buf), 
     bufferLength(len), 
     readPosition(0), 
     responseBuffer("") {}
 
   virtual int available() override {
     return responseBuffer.length() - readPosition;
   }
 
   virtual int read() override {
     if (readPosition < responseBuffer.length()) {
       return responseBuffer.charAt(readPosition++);
     }
     return -1;
   }
 
   virtual int peek() override {
     if (readPosition < responseBuffer.length()) {
       return responseBuffer.charAt(readPosition);
     }
     return -1;
   }
 
   virtual void flush() override {
     // No-op for memory stream
   }
 
   virtual size_t write(uint8_t data) override {
     responseBuffer += (char)data;
     return 1;
   }
 
   using Print::write; // Pull in the base class implementation
 };
 
 // TCP Client wrapper class for handling telnet-like connections
 class TCPClientWrapper {
 private:
   WiFiClient client;
   unsigned long lastActivity;
   String inputBuffer;
   bool connected;
 
 public:
   TCPClientWrapper() : connected(false), lastActivity(0), inputBuffer("") {}
   
   TCPClientWrapper(WiFiClient newClient) : 
     client(newClient), 
     connected(true), 
     lastActivity(millis()), 
     inputBuffer("") {}
 
   bool isConnected() {
     if (connected && !client.connected()) {
       disconnect();
     }
     return connected;
   }
 
   void disconnect() {
     if (connected) {
       client.stop();
       connected = false;
     }
   }
 
   void processData();
   void sendData(const char* data, size_t len);
   void sendData(const char* data);
   void update();
   unsigned long getLastActivity() { return lastActivity; }
 };
 
 // Function prototypes
 void wifiHalInit();
 void wifiHalDoWork();
 bool wifiHalIsConnected();
 void wifiHalDisconnect();
 String wifiHalGetIP();
 void wifiHalHandleNewClients();
 void wifiHalProcessClientData();
 void wifiHalBroadcast(const char* data);
 
 #else
 void wifiHalInit();
 #endif
 #endif // __WIFI_HAL_H__