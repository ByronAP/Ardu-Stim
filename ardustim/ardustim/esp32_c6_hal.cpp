#include "hal.h"
#include "globals.h"
#include "enums.h"
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
volatile uint64_t new_timer_interval_us;
gptimer_handle_t c6_timer = NULL; // Define timer handle here

static timer_callback_ptr esp32c6_timer_callback;

bool __attribute__((section(".iram1.0"))) esp32c6_timer_isr_callback(gptimer_handle_t timer, const gptimer_alarm_event_data_t *edata, void *user_ctx) {
    if (esp32c6_timer_callback) {
        esp32c6_timer_callback();
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
    esp32c6_timer_callback = callback;
    gptimer_config_t timer_config = {
        .clk_src = GPTIMER_CLK_SRC_DEFAULT,
        .direction = GPTIMER_COUNT_UP,
        .resolution_hz = 1000000, // 1 MHz, 1 tick = 1 us
    };
    ESP_ERROR_CHECK(gptimer_new_timer(&timer_config, &c6_timer));
    gptimer_event_callbacks_t timer_callbacks = {
        .on_alarm = esp32c6_timer_isr_callback,
    };
    ESP_ERROR_CHECK(gptimer_register_event_callbacks(c6_timer, &timer_callbacks, NULL));

    timer_hal_set_rpm(initial_rpm);

    gptimer_alarm_config_t alarm_config = {
        .alarm_count = new_timer_interval_us,
        .reload_count = 0,
    };
    alarm_config.flags.auto_reload_on_alarm = true;
    ESP_ERROR_CHECK(gptimer_set_alarm_action(c6_timer, &alarm_config));

    ESP_ERROR_CHECK(gptimer_enable(c6_timer));
    timer_hal_start(); // Start timer after initialization
}

void timer_hal_set_rpm(uint32_t rpm) {
    if (rpm < 10) return;
    uint64_t f_interrupt = ((uint64_t)rpm * Wheels[config.wheel].wheel_max_edges) / 60ULL;
    if (f_interrupt == 0) f_interrupt = 1;
    new_timer_interval_us = 1000000ULL / f_interrupt;
    if (new_timer_interval_us < 1) new_timer_interval_us = 1;
}

void timer_hal_start() { ESP_ERROR_CHECK(gptimer_start(c6_timer)); }
void timer_hal_stop() { ESP_ERROR_CHECK(gptimer_stop(c6_timer)); }

// --- Storage HAL Implementation for ESP32-C6 ---
Preferences c6_preferences;

void storage_hal_init() { c6_preferences.begin("ardustim", false); }

void storage_hal_load_config(struct configTable *config) {
    c6_preferences.getBytes("config", config, sizeof(*config));
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
    c6_preferences.putBytes("config", config, sizeof(*config));
}

// --- ADC HAL Implementation for ESP32-C6 ---
// ESP32-C6 uses new ADC driver API in IDF 5.x
adc_oneshot_unit_handle_t adc1_handle;
adc_channel_t adc_channel;
adc_cali_handle_t adc_cali_handle = NULL;
bool do_calibration = false;

void adc_hal_init() {
    // Initialize ADC
    adc_oneshot_unit_init_cfg_t init_config = {
        .unit_id = ADC_UNIT_1,
        .ulp_mode = ADC_ULP_MODE_DISABLE,
    };
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&init_config, &adc1_handle));
    
    // Configure the ADC channel
    adc_oneshot_chan_cfg_t config;
    config.bitwidth = ADC_BITWIDTH_DEFAULT;
    config.atten = ADC_ATTEN_DB_12;  // Using ADC_ATTEN_DB_12 instead of deprecated ADC_ATTEN_DB_11
    
    // Map the GPIO pin to ADC channel
    // For ESP32-C6, convert GPIO to ADC channel
    adc_channel = (adc_channel_t)ADC_PIN;
    ESP_ERROR_CHECK(adc_oneshot_config_channel(adc1_handle, adc_channel, &config));
    
    // Setup calibration if supported
    adc_cali_handle = NULL;
    do_calibration = false;
    
    // Check if calibration scheme is supported
    adc_cali_scheme_ver_t scheme_mask;
    if (adc_cali_check_scheme(&scheme_mask) == ESP_OK) {
        if (scheme_mask & ADC_CALI_SCHEME_VER_CURVE_FITTING) {
            adc_cali_curve_fitting_config_t cali_config;
            cali_config.unit_id = ADC_UNIT_1;
            cali_config.bitwidth = ADC_BITWIDTH_DEFAULT;
            cali_config.atten = ADC_ATTEN_DB_12;  // Using ADC_ATTEN_DB_12 instead of deprecated ADC_ATTEN_DB_11
            
            ESP_ERROR_CHECK(adc_cali_create_scheme_curve_fitting(&cali_config, &adc_cali_handle));
            do_calibration = true;
        }
    }
}

uint16_t adc_hal_read_channel(uint8_t channel) {
    if (channel == 0) {
        int raw = 0;
        int voltage = 0;
        
        ESP_ERROR_CHECK(adc_oneshot_read(adc1_handle, adc_channel, &raw));
        
        // Convert to calibrated value if available
        if (do_calibration && adc_cali_handle != NULL) {
            ESP_ERROR_CHECK(adc_cali_raw_to_voltage(adc_cali_handle, raw, &voltage));
            // Map voltage to ADC range (0-4095)
            return voltage * 4095 / 3300; // 3300mV is max voltage
        }
        
        return raw;
    }
    return 0; // Placeholder for other channels if needed
}

// --- GPIO HAL Implementation for ESP32-C6 ---
void gpio_hal_init() {
    pinMode(PRIMARY_OUTPUT_PIN, OUTPUT); pinMode(SECONDARY_OUTPUT_PIN, OUTPUT);
    pinMode(TERTIARY_OUTPUT_PIN, OUTPUT); pinMode(KNOCK_OUTPUT_PIN, OUTPUT);
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