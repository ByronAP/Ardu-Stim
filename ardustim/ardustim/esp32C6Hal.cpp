/* vim: set syntax=c expandtab sw=2 softtabstop=2 autoindent smartindent smarttab : */
/*
 * ArduStim - ESP32-C6 Hardware Abstraction Layer implementation
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
#include "comms.h"
#include <esp_timer.h>
#include <driver/gptimer.h>
#include <esp_adc/adc_oneshot.h>
#include <esp_adc/adc_cali.h>
#include <esp_adc/adc_cali_scheme.h>
#include <Preferences.h>
#include <Arduino.h>

// --- Timer HAL Implementation for ESP32-C6 ---
extern wheels Wheels[]; // Defined in globals.h
extern struct configTable config; // Defined in globals.h
extern struct status currentStatus; // Defined in globals.h

volatile uint64_t timerIntervalUs;
static gptimer_handle_t esp32c6Timer = NULL;
static timerCallbackPtr esp32c6TimerCallback;

/**
 * @brief Timer interrupt callback for ESP32-C6
 * 
 * Called when the timer triggers. Executes the callback function.
 * 
 * @param timer Timer handle
 * @param edata Event data for the timer alarm
 * @param user_ctx User context pointer (unused)
 * @return bool True to continue with auto-reload
 */
bool IRAM_ATTR esp32c6TimerIsrCallback(gptimer_handle_t timer, const gptimer_alarm_event_data_t *edata, void *user_ctx) {
  // Call the callback with minimal overhead
  if (esp32c6TimerCallback) {
    esp32c6TimerCallback();
  }
  return true; // Keep auto-reload enabled
}

/**
 * @brief Initialize the ESP32-C6 timer
 * @param initialRpm Initial RPM value to set
 * @param callback Function to call on timer interrupt
 */
void timerHalInit(uint32_t initialRpm, timerCallbackPtr callback) {
  // Store callback function pointer
  esp32c6TimerCallback = callback;
  
  // Ensure a reasonable initial RPM
  if (initialRpm < 100) initialRpm = 1000;
  
  // Calculate timer interval from RPM
  uint32_t edgesPerMinute = initialRpm * Wheels[config.wheel].wheel_max_edges;
  uint32_t edgesPerSecond = edgesPerMinute / 60;
  if (edgesPerSecond < 1) edgesPerSecond = 1;
  timerIntervalUs = 1000000 / edgesPerSecond;
  
  // Apply safety limits
  if (timerIntervalUs < 1000) timerIntervalUs = 1000; // Max 1kHz
  if (timerIntervalUs > 100000) timerIntervalUs = 100000; // Min 10Hz
  
  // Create and configure timer
  gptimer_config_t timerConfig = {
    .clk_src = GPTIMER_CLK_SRC_DEFAULT,
    .direction = GPTIMER_COUNT_UP,
    .resolution_hz = 1000000, // 1 MHz, 1 tick = 1 us
  };
  
  // Create timer
  gptimer_new_timer(&timerConfig, &esp32c6Timer);
  
  // Register callback with timer
  gptimer_event_callbacks_t callbacks = {
    .on_alarm = esp32c6TimerIsrCallback,
  };
  gptimer_register_event_callbacks(esp32c6Timer, &callbacks, NULL);
  
  // Configure timer alarm
  gptimer_alarm_config_t alarmConfig = {
    .alarm_count = timerIntervalUs,
    .reload_count = 0,
  };
  alarmConfig.flags.auto_reload_on_alarm = true;
  
  gptimer_set_alarm_action(esp32c6Timer, &alarmConfig);
  
  // Enable timer
  gptimer_enable(esp32c6Timer);
  
  // Timer will be started by timerHalStart() when ready
}

/**
 * @brief Set timer frequency based on RPM
 * @param rpm Target RPM value
 */
void timerHalSetRpm(uint32_t rpm) {
  // Ensure minimum RPM
  if (rpm < 10) rpm = 10;
  
  // Calculate new interval
  uint32_t edgesPerMinute = rpm * Wheels[config.wheel].wheel_max_edges;
  uint32_t edgesPerSecond = edgesPerMinute / 60;
  
  if (edgesPerSecond < 1) edgesPerSecond = 1;
  uint64_t newInterval = 1000000 / edgesPerSecond;
  
  // Apply safety limits
  if (newInterval < 1000) newInterval = 1000; // Max 1kHz to prevent overload
  if (newInterval > 100000) newInterval = 100000; // Min 10Hz for visibility
  
  // Update alarm if timer exists and interval has changed
  if (esp32c6Timer != NULL && newInterval != timerIntervalUs) {
    timerIntervalUs = newInterval;
    
    // Configure new alarm
    gptimer_alarm_config_t alarmConfig = {
      .alarm_count = timerIntervalUs,
      .reload_count = 0,
    };
    alarmConfig.flags.auto_reload_on_alarm = true;
    
    // Update timer configuration (stop, set new alarm, start)
    gptimer_stop(esp32c6Timer);
    gptimer_set_alarm_action(esp32c6Timer, &alarmConfig);
    gptimer_start(esp32c6Timer);
  }
}

/**
 * @brief Start the timer
 */
void timerHalStart() {
  if (esp32c6Timer) {
    gptimer_start(esp32c6Timer);
  }
}

/**
 * @brief Stop the timer
 */
void timerHalStop() {
  if (esp32c6Timer) {
    gptimer_stop(esp32c6Timer);
  }
}

// --- Storage HAL Implementation for ESP32-C6 ---
static Preferences preferences;

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
  
  // Check if we got a valid config
  if (readSize != sizeof(*config) || config->version != VERSION) {
    // Set default values
    config->version = VERSION;
    config->wheel = 5; // Default to 60-2 with cam
    currentStatus.rpm = 1000;
    currentStatus.base_rpm = 1000;
    config->mode = FIXED_RPM;
    config->fixed_rpm = 1000;
    config->sweep_high_rpm = 6000;
    config->sweep_low_rpm = 1000;
    config->sweep_interval = 1000;
    config->useCompression = false;
    config->compressionType = COMPRESSION_TYPE_4CYL_4STROKE;
    config->compressionRPM = 400;
    config->compressionOffset = 0;
    config->compressionDynamic = false;
    config->wifiEnabled = false;
    config->bluetoothEnabled = false;
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

// --- ADC HAL Implementation for ESP32-C6 ---
static adc_oneshot_unit_handle_t adcHandle;
static bool adcInitialized = false;

/**
 * @brief Initialize the ADC
 */
void adcHalInit() {
  if (adcInitialized) return;
  
  // Initialize ADC
  adc_oneshot_unit_init_cfg_t adcConfig = {
    .unit_id = ADC_UNIT_1,
  };
  ESP_ERROR_CHECK(adc_oneshot_new_unit(&adcConfig, &adcHandle));
  
  // Configure ADC channel using individual assignments to avoid designator order issues
  adc_oneshot_chan_cfg_t channelConfig;
  channelConfig.bitwidth = ADC_BITWIDTH_12;
  channelConfig.atten = ADC_ATTEN_DB_12; // Use non-deprecated value
  
  ESP_ERROR_CHECK(adc_oneshot_config_channel(adcHandle, ADC_CHANNEL_0, &channelConfig));
  
  adcInitialized = true;
  pinMode(ADC_PIN, INPUT);
}

/**
 * @brief Read value from ADC channel
 * @param channel Channel to read
 * @return ADC reading (0-4095 converted to 0-1023 for compatibility)
 */
uint16_t adcHalReadChannel(uint8_t channel) {
  if (channel == 0) {
    int rawValue = analogRead(ADC_PIN);
    // Scale to match AVR ADC range for RPM calculation
    return rawValue >> 2; // Convert 12-bit (0-4095) to 10-bit (0-1023)
  }
  return 0;
}

// --- GPIO HAL Implementation for ESP32-C6 ---
/**
 * @brief Initialize GPIO pins
 */
void gpioHalInit() {
  // Configure pins
  pinMode(PRIMARY_OUTPUT_PIN, OUTPUT);
  pinMode(SECONDARY_OUTPUT_PIN, OUTPUT);
  pinMode(TERTIARY_OUTPUT_PIN, OUTPUT);
  pinMode(KNOCK_OUTPUT_PIN, OUTPUT);
  
  // Set initial states
  digitalWrite(PRIMARY_OUTPUT_PIN, LOW);
  digitalWrite(SECONDARY_OUTPUT_PIN, LOW);
  digitalWrite(TERTIARY_OUTPUT_PIN, LOW);
  digitalWrite(KNOCK_OUTPUT_PIN, LOW);
}

/**
 * @brief Set GPIO output state
 * @param pin Pin number to set
 * @param state Pin state (true=HIGH, false=LOW)
 */
void gpioHalSetOutput(int pin, bool state) {
  digitalWrite(pin, state ? HIGH : LOW);
}

// --- Serial HAL Implementation for ESP32-C6 ---
void serialHalInit() { 
  Serial.begin(115200); 
}

bool serialHalAvailable() { 
  return Serial.available() > 0; 
}

uint8_t serialHalReadByte() { 
  return Serial.read(); 
}

void serialHalWriteByte(uint8_t byte) { 
  Serial.write(byte); 
}

void serialHalPrint(const char *str) { 
  Serial.print(str); 
}

void serialHalPrintln(const char *str) { 
  Serial.println(str); 
}

/**
 * @brief Perform periodic tasks
 */
void halDoWork() {
  // Process serial commands
  if (Serial.available() > 0) {
    commandParser(&Serial);
  }
}