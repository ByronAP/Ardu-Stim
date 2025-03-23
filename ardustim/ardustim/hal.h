#ifndef __HAL_H__
#define __HAL_H__

#include <stdint.h>
#include <Arduino.h>

#if defined(__AVR__)
#include <avr/interrupt.h>
#include <EEPROM.h> // For AVR Storage HAL
#elif defined(ESP32) && !defined(ESP32C6)
#include <esp_timer.h>
#include <driver/gptimer.h>
#include <Preferences.h> // For ESP32 Storage HAL
#elif defined(ESP32C6)
#include <esp_timer.h>
#include <driver/gptimer.h>
#include <esp_adc/adc_oneshot.h>
#include <esp_adc/adc_cali.h>
#include <esp_adc/adc_cali_scheme.h>
#include <Preferences.h> // For ESP32 Storage HAL
#endif

// --- Timer HAL ---
typedef void (*timer_callback_ptr)(void);
void timer_hal_init(uint32_t initial_rpm, timer_callback_ptr callback);
void timer_hal_set_rpm(uint32_t rpm);
void timer_hal_start();
void timer_hal_stop();
#if defined(__AVR__) // AVR-specific Timer HAL functions
uint8_t get_bitshift_from_prescaler(uint8_t *prescaler_bits);
void get_prescaler_bits(uint32_t *potential_oc_value, uint8_t *prescaler, uint8_t *bitshift);
#endif

// --- Storage HAL ---
struct configTable; // Forward declaration - configTable struct is defined in globals.h
void storage_hal_init();
void storage_hal_load_config(struct configTable *config);
void storage_hal_save_config(const struct configTable *config);

// --- ADC HAL ---
void adc_hal_init();
uint16_t adc_hal_read_channel(uint8_t channel);

// --- GPIO HAL ---
void gpio_hal_init();
void gpio_hal_set_output(int pin, bool state);

// --- Serial HAL ---
void serial_hal_init();
bool serial_hal_available();
uint8_t serial_hal_read_byte();
void serial_hal_write_byte(uint8_t byte);
void serial_hal_print(const char *str);
void serial_hal_println(const char *str);

#endif // __HAL_H__