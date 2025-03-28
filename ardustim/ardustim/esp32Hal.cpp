/* vim: set syntax=c expandtab sw=2 softtabstop=2 autoindent smartindent smarttab : */
/*
 * ArduStim - ESP32 Hardware Abstraction Layer implementation
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
#include "hal.h"
#include "globals.h"
#include "comms.h"
#include "bleHal.h" // Include BLE HAL
#include "enums.h"
#include <Preferences.h>
#include <Arduino.h> // Needed for digitalWrite, analogRead, Serial, HIGH, LOW, pinMode
#include <esp_timer.h> // USE THIS for timers compatible with Arduino Core 2.x / IDF 4.4

wirelessComm getWirelessSupportType() {
  return WIFI_AND_BLUETOOTH;
}


// --- Timer HAL Implementation for ESP32 using esp_timer ---
extern wheels Wheels[]; // Defined in globals.h
extern struct configTable config; // Defined in globals.h
extern struct status currentStatus; // Define for access to currentStatus

volatile uint64_t newTimerIntervalUs = 1000; // Default interval
esp_timer_handle_t periodic_timer; // Timer handle

static timerCallbackPtr esp32TimerCallback;

/**
 * @brief Timer interrupt callback for ESP32 (using esp_timer)
 *
 * This function is called periodically by the esp_timer framework.
 *
 * @param arg User context pointer (unused in this case)
 */
void IRAM_ATTR esp32TimerIsrCallbackWrapper(void* arg) {
    if (esp32TimerCallback) {
        esp32TimerCallback();
    }
}

/**
 * @brief Initialize the ESP32 timer (using esp_timer)
 * @param initialRpm Initial RPM value to set
 * @param callback Function to call on timer interrupt
 */
void timerHalInit(uint32_t initialRpm, timerCallbackPtr callback) {
    esp32TimerCallback = callback;

    // Calculate initial interval
    timerHalSetRpm(initialRpm); // This calculates newTimerIntervalUs

    // Configure the timer
    const esp_timer_create_args_t periodic_timer_args = {
            .callback = &esp32TimerIsrCallbackWrapper,
            .name = "ardustim_timer" // Optional name for debugging
    };

    ESP_ERROR_CHECK(esp_timer_create(&periodic_timer_args, &periodic_timer));

    // Start the timer immediately
    timerHalStart();
}

/**
 * @brief Set timer frequency based on RPM (using esp_timer)
 * @param rpm Target RPM value
 */
void timerHalSetRpm(uint32_t rpm) {
    if (rpm < 10) rpm = 10; // Enforce minimum RPM

    // Calculate the desired interrupt frequency (interrupts per second)
    // Use floating point for intermediate calculation to avoid premature truncation
    float fInterruptHz = ((float)rpm * Wheels[config.wheel].wheel_max_edges) / 60.0f;

    // Avoid division by zero or extremely low frequencies
    if (fInterruptHz < 0.01f) fInterruptHz = 0.01f;

    // Calculate the interval in microseconds
    // Use 64-bit unsigned integer to avoid overflow with high frequencies
    uint64_t intervalUs = (uint64_t)(1000000.0f / fInterruptHz);

    // Apply safety limits (e.g., minimum interval to prevent ISR overload)
    // Max 50kHz interrupt rate (20us interval) might be a reasonable limit
    const uint64_t minIntervalUs = 20;
    if (intervalUs < minIntervalUs) intervalUs = minIntervalUs;

    // Apply a maximum interval if needed (e.g., 1 second)
    const uint64_t maxIntervalUs = 1000000;
    if (intervalUs > maxIntervalUs) intervalUs = maxIntervalUs;

    // Store the new interval
    newTimerIntervalUs = intervalUs;

    // If timer is already running, restart it with the new interval
    if (periodic_timer != NULL && esp_timer_is_active(periodic_timer)) {
        timerHalStop();
        timerHalStart();
    }
}


/**
 * @brief Start the timer (using esp_timer)
 */
void timerHalStart() {
    if (periodic_timer != NULL) {
       // Ensure it's stopped before starting to apply new interval
       esp_timer_stop(periodic_timer);
       ESP_ERROR_CHECK(esp_timer_start_periodic(periodic_timer, newTimerIntervalUs));
    }
}

/**
 * @brief Stop the timer (using esp_timer)
 */
void timerHalStop() {
     if (periodic_timer != NULL && esp_timer_is_active(periodic_timer)) {
        ESP_ERROR_CHECK(esp_timer_stop(periodic_timer));
    }
}

// --- Storage HAL Implementation for ESP32 ---
Preferences preferences;

/**
 * @brief Initialize the storage system
 */
void storageHalInit() {
    preferences.begin("ardustim", false);
}

/**
 * @brief Load configuration from NVS
 * @param config Pointer to configuration structure to fill
 */
void storageHalLoadConfig(struct configTable *config) {
    size_t readSize = preferences.getBytes("config", config, sizeof(*config));

    if (readSize != sizeof(*config) || config->version != VERSION) {
        // Set default values
        config->version = VERSION;
        config->wheel = 5; // Default to 60-2 with cam
        currentStatus.rpm = 3000;
        currentStatus.base_rpm = 3000;
        config->mode = POT_RPM;
        config->fixed_rpm = 3500;
        config->sweep_high_rpm = 6000;
        config->sweep_low_rpm = 1000;
        config->sweep_interval = 1000;
        config->useCompression = false;
        config->compressionType = COMPRESSION_TYPE_4CYL_4STROKE;
        config->compressionRPM = 400;
        config->compressionOffset = 0;
        config->compressionDynamic = false;
        config->wifiEnabled = false;
        config->bluetoothEnabled = false; // Default BLE disabled
        config->bluetoothPin = 0;
        memset(config->wifiSSID, 0, sizeof(config->wifiSSID));
        memset(config->wifiPassword, 0, sizeof(config->wifiPassword));

        // Save defaults
        storageHalSaveConfig(config);
    }

    // Validate configuration - function is declared in hal.h
    validateConfiguration(config);
}

/**
 * @brief Save configuration to NVS
 * @param config Pointer to configuration structure to save
 */
void storageHalSaveConfig(const struct configTable *config) {
    preferences.putBytes("config", config, sizeof(*config));
}

// --- ADC HAL Implementation for ESP32 ---
/**
 * @brief Initialize the ADC
 */
void adcHalInit() {
    // ESP32 ADC is initialized by default
    pinMode(ADC_PIN, INPUT);
}

/**
 * @brief Read value from ADC channel
 * @param channel Channel to read
 * @return ADC reading (0-4095 converted to 0-1023 for compatibility)
 */
uint16_t adcHalReadChannel(uint8_t channel) {
    if (channel == 0) {
        uint16_t rawValue = analogRead(ADC_PIN);
        // Scale to match AVR ADC range for RPM calculation
        return rawValue >> 2; // Convert 12-bit (0-4095) to 10-bit (0-1023)
    }
    return 0;
}

// --- GPIO HAL Implementation for ESP32 ---
/**
 * @brief Initialize GPIO pins
 */
void gpioHalInit() {
    pinMode(PRIMARY_OUTPUT_PIN, OUTPUT);
    pinMode(SECONDARY_OUTPUT_PIN, OUTPUT);
    pinMode(TERTIARY_OUTPUT_PIN, OUTPUT);
    pinMode(KNOCK_OUTPUT_PIN, OUTPUT);
}

/**
 * @brief Set GPIO output state
 * @param pin Pin number to set
 * @param state Pin state (true=HIGH, false=LOW)
 */
void gpioHalSetOutput(int pin, bool state) {
    digitalWrite(pin, state ? HIGH : LOW);
}

// --- Serial HAL Implementation for ESP32 ---
void serialHalInit() { Serial.begin(115200); }
bool serialHalAvailable() { return Serial.available() > 0; }
uint8_t serialHalReadByte() { return Serial.read(); }
void serialHalWriteByte(uint8_t byte) { Serial.write(byte); }
void serialHalPrint(const char *str) { Serial.print(str); }
void serialHalPrintln(const char *str) { Serial.println(str); }

/**
 * @brief Perform periodic tasks
 */
void halDoWork() {
    // Process serial commands
    if (Serial.available() > 0) {
      commandParser(&Serial);
    }

    // Process WiFi tasks
    wifiHalDoWork();

    // Process BLE tasks
    bleHalDoWork();
  }