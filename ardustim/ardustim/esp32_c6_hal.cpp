#include "hal.h"
#include "globals.h"
#include "enums.h"
#include <esp_timer.h>
#include <driver/gptimer.h>
#include <Arduino.h>
#include <Preferences.h>

// --- Timer HAL Implementation for ESP32-C6 ---
extern wheels Wheels[]; // Defined in globals.h
extern struct configTable config; // Defined in globals.h

volatile uint64_t new_timer_interval_us;
gptimer_handle_t c6_timer = NULL;
static timer_callback_ptr esp32c6_timer_callback;

// Track stats for debugging
static uint32_t last_rpm_set = 0;
static uint32_t rpm_update_count = 0;

// Simple ISR that calls the callback
bool IRAM_ATTR esp32c6_timer_isr_callback(gptimer_handle_t timer, const gptimer_alarm_event_data_t *edata, void *user_ctx) {
  // Call the callback with minimal overhead
  if (esp32c6_timer_callback) {
    esp32c6_timer_callback();
  }
  
  // Toggle GPIO 5 occasionally to show activity
  static uint32_t count = 0;
  if (++count % 1000 == 0) {
    static bool led_state = false;
    led_state = !led_state;
    digitalWrite(5, led_state);
  }
  
  return true; // Keep auto-reload enabled
}

void timer_hal_init(uint32_t initial_rpm, timer_callback_ptr callback) {
  // Set up debug pin
  pinMode(5, OUTPUT);
  digitalWrite(5, HIGH); // Turn on at init
  
  // Store callback
  esp32c6_timer_callback = callback;
  
  // Force a reasonable initial RPM
  if (initial_rpm < 100) initial_rpm = 1000;
  last_rpm_set = initial_rpm;
  
  // Calculate timer interval from RPM
  uint32_t edges_per_minute = initial_rpm * Wheels[config.wheel].wheel_max_edges;
  uint32_t edges_per_second = edges_per_minute / 60;
  if (edges_per_second < 1) edges_per_second = 1;
  new_timer_interval_us = 1000000 / edges_per_second;
  
  // Safety checks
  if (new_timer_interval_us < 1000) new_timer_interval_us = 1000; // Max 1kHz
  if (new_timer_interval_us > 100000) new_timer_interval_us = 100000; // Min 10Hz
  
  // Create and configure timer
  gptimer_config_t timer_config = {
    .clk_src = GPTIMER_CLK_SRC_DEFAULT,
    .direction = GPTIMER_COUNT_UP,
    .resolution_hz = 1000000, // 1 MHz, 1 tick = 1 us
  };
  
  // Create timer with error handling
  esp_err_t err = gptimer_new_timer(&timer_config, &c6_timer);
  if (err != ESP_OK) {
    // If creation fails, indicate error with LED and return
    for (int i = 0; i < 10; i++) {
      digitalWrite(5, HIGH);
      delay(50);
      digitalWrite(5, LOW);
      delay(50);
    }
    return;
  }
  
  // Register callback with timer
  gptimer_event_callbacks_t callbacks = {
    .on_alarm = esp32c6_timer_isr_callback,
  };
  err = gptimer_register_event_callbacks(c6_timer, &callbacks, NULL);
  if (err != ESP_OK) return;
  
  // Configure timer alarm
  gptimer_alarm_config_t alarm_config = {
    .alarm_count = new_timer_interval_us,
    .reload_count = 0,
  };
  alarm_config.flags.auto_reload_on_alarm = true;
  
  err = gptimer_set_alarm_action(c6_timer, &alarm_config);
  if (err != ESP_OK) return;
  
  // Enable timer
  err = gptimer_enable(c6_timer);
  if (err != ESP_OK) return;
  
  // Timer will be started by timer_hal_start() when ready
  digitalWrite(5, LOW); // Turn off LED to indicate init complete
}

void timer_hal_set_rpm(uint32_t rpm) {
  // Ensure minimum RPM and track changes
  if (rpm < 10) rpm = 10;
  
  // Debug: toggle LED when RPM changes significantly
  if (abs((int32_t)rpm - (int32_t)last_rpm_set) > 50) {
    digitalWrite(5, HIGH);
    delay(1);  // Brief flash
    digitalWrite(5, LOW);
    
    // Track last set value
    last_rpm_set = rpm;
    rpm_update_count++;
  }
  
  // Calculate new interval
  uint32_t edges_per_minute = rpm * Wheels[config.wheel].wheel_max_edges;
  uint32_t edges_per_second = edges_per_minute / 60;
  
  if (edges_per_second < 1) edges_per_second = 1;
  uint64_t temp_interval = 1000000 / edges_per_second;
  
  // Safety checks
  if (temp_interval < 1000) temp_interval = 1000; // Max 1kHz to prevent overload
  if (temp_interval > 100000) temp_interval = 100000; // Min 10Hz for visibility
  
  // Update alarm if necessary - ALWAYS update to ensure RPM changes take effect
  if (c6_timer != NULL) {
    new_timer_interval_us = temp_interval;
    
    // Configure new alarm
    gptimer_alarm_config_t alarm_config = {
      .alarm_count = new_timer_interval_us,
      .reload_count = 0,
    };
    alarm_config.flags.auto_reload_on_alarm = true;
    
    // Stop timer temporarily
    esp_err_t err = gptimer_stop(c6_timer);
    if (err != ESP_OK) return;
    
    // Update alarm configuration
    err = gptimer_set_alarm_action(c6_timer, &alarm_config);
    if (err != ESP_OK) return;
    
    // Restart timer
    err = gptimer_start(c6_timer);
    if (err != ESP_OK) return;
  }
}

void timer_hal_start() {
  if (c6_timer) {
    esp_err_t err = gptimer_start(c6_timer);
    if (err == ESP_OK) {
      // Flash LED to show timer started
      for (int i = 0; i < 3; i++) {
        digitalWrite(5, HIGH);
        delay(50);
        digitalWrite(5, LOW);
        delay(50);
      }
    }
  }
}

void timer_hal_stop() {
  if (c6_timer) {
    gptimer_stop(c6_timer);
  }
}

// --- Storage HAL Implementation for ESP32-C6 ---
Preferences preferences;

void storage_hal_init() {
  preferences.begin("ardustim", false);
}

void storage_hal_load_config(struct configTable *config) {
  size_t read_size = preferences.getBytes("config", config, sizeof(*config));
  
  // Check if we got a valid config
  if (read_size != sizeof(*config) || config->version != VERSION) {
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
    
    // Save defaults
    storage_hal_save_config(config);
  }
}

void storage_hal_save_config(const struct configTable *config) {
  preferences.putBytes("config", config, sizeof(*config));
}

// --- ADC HAL Implementation for ESP32-C6 ---
void adc_hal_init() {
  // Basic Arduino analogRead approach
  pinMode(ADC_PIN, INPUT);
}

uint16_t adc_hal_read_channel(uint8_t channel) {
  if (channel == 0) {
    // Remap ADC range (0-4095) to RPM range (0-8000)
    // This helps with potentiometer control
    uint16_t raw_value = analogRead(ADC_PIN);
    return map(raw_value, 0, 4095, 0, 8000);
  }
  return 0;
}

// --- GPIO HAL Implementation for ESP32-C6 ---
void gpio_hal_init() {
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
  
  // Flash all pins as a test
  for (int i = 0; i < 5; i++) {
    digitalWrite(PRIMARY_OUTPUT_PIN, HIGH);
    digitalWrite(SECONDARY_OUTPUT_PIN, HIGH);
    digitalWrite(TERTIARY_OUTPUT_PIN, HIGH);
    digitalWrite(KNOCK_OUTPUT_PIN, HIGH);
    delay(100);
    digitalWrite(PRIMARY_OUTPUT_PIN, LOW);
    digitalWrite(SECONDARY_OUTPUT_PIN, LOW);
    digitalWrite(TERTIARY_OUTPUT_PIN, LOW);
    digitalWrite(KNOCK_OUTPUT_PIN, LOW);
    delay(100);
  }
}

void gpio_hal_set_output(int pin, bool state) {
  digitalWrite(pin, state ? HIGH : LOW);
}

// --- Serial HAL Implementation for ESP32-C6 ---
void serial_hal_init() { Serial.begin(115200); }
bool serial_hal_available() { return Serial.available() > 0; }
uint8_t serial_hal_read_byte() { return Serial.read(); }
void serial_hal_write_byte(uint8_t byte) { Serial.write(byte); }
void serial_hal_print(const char *str) { Serial.print(str); }
void serial_hal_println(const char *str) { Serial.println(str); }