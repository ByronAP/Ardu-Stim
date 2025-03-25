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
#include "enums.h"
#include <esp_timer.h>
#include <driver/gptimer.h>
#include <Preferences.h>
#include <Arduino.h> // Needed for digitalWrite, analogRead, Serial, HIGH, LOW, pinMode

// --- Timer HAL Implementation for ESP32 ---
extern wheels Wheels[]; // Defined in globals.h
extern struct configTable config; // Defined in globals.h
extern struct status currentStatus; // Define for access to currentStatus

volatile uint64_t newTimerIntervalUs;
gptimer_handle_t timer = NULL; 

static timerCallbackPtr esp32TimerCallback;

/**
 * @brief Timer interrupt callback for ESP32
 * 
 * This function is called when the timer triggers. It executes the callback
 * and updates the timer interval if needed.
 * 
 * @param timer Timer handle
 * @param edata Event data for the timer alarm
 * @param user_ctx User context pointer (unused)
 * @return bool True to continue with auto-reload
 */
bool IRAM_ATTR esp32TimerIsrCallback(gptimer_handle_t timer, const gptimer_alarm_event_data_t *edata, void *user_ctx) {
    if (esp32TimerCallback) {
        esp32TimerCallback();
    }
    if (newTimerIntervalUs != edata->alarm_value) {
        gptimer_alarm_config_t alarmConfig;
        memset(&alarmConfig, 0, sizeof(alarmConfig));
        alarmConfig.alarm_count = newTimerIntervalUs;
        alarmConfig.reload_count = 0;
        alarmConfig.flags.auto_reload_on_alarm = true;
        gptimer_set_alarm_action(timer, &alarmConfig);
    }
    return true;
}

/**
 * @brief Initialize the ESP32 timer
 * @param initialRpm Initial RPM value to set
 * @param callback Function to call on timer interrupt
 */
void timerHalInit(uint32_t initialRpm, timerCallbackPtr callback) {
    esp32TimerCallback = callback;
    gptimer_config_t timerConfig = {
        .clk_src = GPTIMER_CLK_SRC_DEFAULT,
        .direction = GPTIMER_COUNT_UP,
        .resolution_hz = 1000000, // 1 MHz, 1 tick = 1 us
    };
    ESP_ERROR_CHECK(gptimer_new_timer(&timerConfig, &timer));
    gptimer_event_callbacks_t timerCallbacks = {
        .on_alarm = esp32TimerIsrCallback,
    };
    ESP_ERROR_CHECK(gptimer_register_event_callbacks(timer, &timerCallbacks, NULL));

    timerHalSetRpm(initialRpm);

    gptimer_alarm_config_t alarmConfig = {
        .alarm_count = newTimerIntervalUs,
        .reload_count = 0,
    };
    alarmConfig.flags.auto_reload_on_alarm = true;
    ESP_ERROR_CHECK(gptimer_set_alarm_action(timer, &alarmConfig));

    ESP_ERROR_CHECK(gptimer_enable(timer));
    timerHalStart(); // Start timer after initialization
}

/**
 * @brief Set timer frequency based on RPM
 * @param rpm Target RPM value
 */
void timerHalSetRpm(uint32_t rpm) {
    if (rpm < 10) return;
    uint64_t fInterrupt = ((uint64_t)rpm * Wheels[config.wheel].wheel_max_edges) / 60ULL;
    if (fInterrupt == 0) fInterrupt = 1;
    newTimerIntervalUs = 1000000ULL / fInterrupt;
    if (newTimerIntervalUs < 1) newTimerIntervalUs = 1;
}

/**
 * @brief Start the timer
 */
void timerHalStart() { 
    ESP_ERROR_CHECK(gptimer_start(timer)); 
}

/**
 * @brief Stop the timer
 */
void timerHalStop() { 
    ESP_ERROR_CHECK(gptimer_stop(timer)); 
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
 * @return ADC reading (0-4095)
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