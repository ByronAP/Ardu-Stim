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
volatile uint64_t new_timer_interval_us;
gptimer_handle_t timer = NULL; // Define timer handle here, initialized in esp32/timer_esp32.cpp and used in ISR

static timer_callback_ptr esp32_timer_callback;

bool __attribute__((section(".iram1.0"))) esp32_timer_isr_callback(gptimer_handle_t timer, const gptimer_alarm_event_data_t *edata, void *user_ctx) {
    if (esp32_timer_callback) {
        esp32_timer_callback();
    }
    if (new_timer_interval_us != edata->alarm_value) {
        gptimer_alarm_config_t alarm_config;
        memset(&alarm_config, 0, sizeof(alarm_config));
        alarm_config.alarm_count = new_timer_interval_us;
        alarm_config.reload_count = 0;
        alarm_config.flags.auto_reload_on_alarm = true;
        gptimer_set_alarm_action(timer, &alarm_config);
    }
    return true;
}

void timer_hal_init(uint32_t initial_rpm, timer_callback_ptr callback) {
    esp32_timer_callback = callback;
    gptimer_config_t timer_config = {
        .clk_src = GPTIMER_CLK_SRC_DEFAULT,
        .direction = GPTIMER_COUNT_UP,
        .resolution_hz = 1000000, // 1 MHz, 1 tick = 1 us
    };
    ESP_ERROR_CHECK(gptimer_new_timer(&timer_config, &timer));
    gptimer_event_callbacks_t timer_callbacks = {
        .on_alarm = esp32_timer_isr_callback,
    };
    ESP_ERROR_CHECK(gptimer_register_event_callbacks(timer, &timer_callbacks, NULL));

    timer_hal_set_rpm(initial_rpm);

    gptimer_alarm_config_t alarm_config = {
        .alarm_count = new_timer_interval_us,
        .reload_count = 0,
    };
    alarm_config.flags.auto_reload_on_alarm = true;
    ESP_ERROR_CHECK(gptimer_set_alarm_action(timer, &alarm_config));

    ESP_ERROR_CHECK(gptimer_enable(timer));
    timer_hal_start(); // Start timer after initialization
    ::timer = timer; // Assign to global timer variable
}

void timer_hal_set_rpm(uint32_t rpm) {
    if (rpm < 10) return;
    uint64_t f_interrupt = ((uint64_t)rpm * Wheels[config.wheel].wheel_max_edges) / 60ULL;
    if (f_interrupt == 0) f_interrupt = 1;
    new_timer_interval_us = 1000000ULL / f_interrupt;
    if (new_timer_interval_us < 1) new_timer_interval_us = 1;
}

void timer_hal_start() { ESP_ERROR_CHECK(gptimer_start(timer)); }
void timer_hal_stop() { ESP_ERROR_CHECK(gptimer_stop(timer)); }


// --- Storage HAL Implementation for ESP32 ---
Preferences preferences;

void storage_hal_init() { preferences.begin("ardustim", false); }

void storage_hal_load_config(struct configTable *config) {
    preferences.getBytes("config", config, sizeof(*config));
    if (config->version != VERSION) { // Version mismatch or first run
        config->version = VERSION; config->wheel = 5; currentStatus.rpm = 3000;
        currentStatus.base_rpm = 3000; config->mode = POT_RPM; config->fixed_rpm = 3500;
        config->sweep_high_rpm = 6000; config->sweep_low_rpm = 1000; config->sweep_interval = 1000;
        config->useCompression = false; config->compressionType = COMPRESSION_TYPE_4CYL_4STROKE;
        config->compressionRPM = 400; config->compressionOffset = 0;
        storage_hal_save_config(config);
    }
}

void storage_hal_save_config(const struct configTable *config) {
    preferences.putBytes("config", config, sizeof(*config));
    // TODO: do we need to ensure the data is written to flash somehow?
}

// --- ADC HAL Implementation for ESP32 ---
void adc_hal_init() {} // ESP32 ADC is initialized by default. No explicit init needed.

uint16_t adc_hal_read_channel(uint8_t channel) {
    if (channel == 0) { return analogRead(ADC_PIN); }
    return 0; // Placeholder for other channels if needed
}

// --- GPIO HAL Implementation for ESP32 ---
void gpio_hal_init() {
    pinMode(PRIMARY_OUTPUT_PIN, OUTPUT); pinMode(SECONDARY_OUTPUT_PIN, OUTPUT);
    pinMode(TERTIARY_OUTPUT_PIN, OUTPUT); pinMode(KNOCK_OUTPUT_PIN, OUTPUT);
}

void gpio_hal_set_output(int pin, bool state) {
    digitalWrite(pin, state ? HIGH : LOW);
}

// --- Serial HAL Implementation for ESP32 ---
void serial_hal_init() { Serial.begin(115200); }
bool serial_hal_available() { return Serial.available() > 0; }
uint8_t serial_hal_read_byte() { return Serial.read(); }
void serial_hal_write_byte(uint8_t byte) { Serial.write(byte); }
void serial_hal_print(const char *str) { Serial.print(str); }
void serial_hal_println(const char *str) { Serial.println(str); }