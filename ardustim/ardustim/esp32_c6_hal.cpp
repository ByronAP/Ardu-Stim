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

volatile uint64_t timer_interval_us;
static gptimer_handle_t esp32c6_timer = NULL;
static timer_callback_ptr esp32c6_timer_callback;

// Timer ISR callback function
bool IRAM_ATTR esp32c6_timer_isr_callback(gptimer_handle_t timer, const gptimer_alarm_event_data_t *edata, void *user_ctx) {
  // Call the callback with minimal overhead
  if (esp32c6_timer_callback) {
    esp32c6_timer_callback();
  }
  return true; // Keep auto-reload enabled
}

void timer_hal_init(uint32_t initial_rpm, timer_callback_ptr callback) {
  // Store callback function pointer
  esp32c6_timer_callback = callback;
  
  // Ensure a reasonable initial RPM
  if (initial_rpm < 100) initial_rpm = 1000;
  
  // Calculate timer interval from RPM
  uint32_t edges_per_minute = initial_rpm * Wheels[config.wheel].wheel_max_edges;
  uint32_t edges_per_second = edges_per_minute / 60;
  if (edges_per_second < 1) edges_per_second = 1;
  timer_interval_us = 1000000 / edges_per_second;
  
  // Apply safety limits
  if (timer_interval_us < 1000) timer_interval_us = 1000; // Max 1kHz
  if (timer_interval_us > 100000) timer_interval_us = 100000; // Min 10Hz
  
  // Create and configure timer
  gptimer_config_t timer_config = {
    .clk_src = GPTIMER_CLK_SRC_DEFAULT,
    .direction = GPTIMER_COUNT_UP,
    .resolution_hz = 1000000, // 1 MHz, 1 tick = 1 us
  };
  
  // Create timer
  gptimer_new_timer(&timer_config, &esp32c6_timer);
  
  // Register callback with timer
  gptimer_event_callbacks_t callbacks = {
    .on_alarm = esp32c6_timer_isr_callback,
  };
  gptimer_register_event_callbacks(esp32c6_timer, &callbacks, NULL);
  
  // Configure timer alarm
  gptimer_alarm_config_t alarm_config = {
    .alarm_count = timer_interval_us,
    .reload_count = 0,
  };
  alarm_config.flags.auto_reload_on_alarm = true;
  
  gptimer_set_alarm_action(esp32c6_timer, &alarm_config);
  
  // Enable timer
  gptimer_enable(esp32c6_timer);
  
  // Timer will be started by timer_hal_start() when ready
}

void timer_hal_set_rpm(uint32_t rpm) {
  // Ensure minimum RPM
  if (rpm < 10) rpm = 10;
  
  // Calculate new interval
  uint32_t edges_per_minute = rpm * Wheels[config.wheel].wheel_max_edges;
  uint32_t edges_per_second = edges_per_minute / 60;
  
  if (edges_per_second < 1) edges_per_second = 1;
  uint64_t new_interval = 1000000 / edges_per_second;
  
  // Apply safety limits
  if (new_interval < 1000) new_interval = 1000; // Max 1kHz to prevent overload
  if (new_interval > 100000) new_interval = 100000; // Min 10Hz for visibility
  
  // Update alarm if timer exists and interval has changed
  if (esp32c6_timer != NULL && new_interval != timer_interval_us) {
    timer_interval_us = new_interval;
    
    // Configure new alarm
    gptimer_alarm_config_t alarm_config = {
      .alarm_count = timer_interval_us,
      .reload_count = 0,
    };
    alarm_config.flags.auto_reload_on_alarm = true;
    
    // Update timer configuration (stop, set new alarm, start)
    gptimer_stop(esp32c6_timer);
    gptimer_set_alarm_action(esp32c6_timer, &alarm_config);
    gptimer_start(esp32c6_timer);
  }
}

void timer_hal_start() {
  if (esp32c6_timer) {
    gptimer_start(esp32c6_timer);
  }
}

void timer_hal_stop() {
  if (esp32c6_timer) {
    gptimer_stop(esp32c6_timer);
  }
}

// --- Storage HAL Implementation for ESP32-C6 ---
static Preferences preferences;

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
  pinMode(ADC_PIN, INPUT);
}

uint16_t adc_hal_read_channel(uint8_t channel) {
  if (channel == 0) {
    // Remap ADC range (0-4095) to RPM range (0-8000)
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
}

void gpio_hal_set_output(int pin, bool state) {
  digitalWrite(pin, state ? HIGH : LOW);
}

// --- Serial HAL Implementation for ESP32-C6 ---
void serial_hal_init() { 
  Serial.begin(115200); 
}

bool serial_hal_available() { 
  return Serial.available() > 0; 
}

uint8_t serial_hal_read_byte() { 
  return Serial.read(); 
}

void serial_hal_write_byte(uint8_t byte) { 
  Serial.write(byte); 
}

void serial_hal_print(const char *str) { 
  Serial.print(str); 
}

void serial_hal_println(const char *str) { 
  Serial.println(str); 
}