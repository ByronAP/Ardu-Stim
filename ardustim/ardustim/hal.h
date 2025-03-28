/* vim: set syntax=c expandtab sw=2 softtabstop=2 autoindent smartindent smarttab : */
/*
 * ArduStim - Hardware Abstraction Layer definitions
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
#ifndef __HAL_H__
#define __HAL_H__

#include <stdint.h>
#include <Arduino.h>
#include "enums.h"
#include "wifiHal.h"
#include "bleHal.h"

#if defined(__AVR__)
#include <avr/interrupt.h>
#include <EEPROM.h> // For AVR Storage HAL
#elif defined(ESP32) && !defined(ESP32C6)
#include <esp_timer.h>
#include <Preferences.h> // For ESP32 Storage HAL
#endif

// Forward declaration
struct configTable; // Defined in globals.h

/**
 * @brief Gets if the hardware supports any wireless communication
 */
wirelessComm getWirelessSupportType();

/**
 * @brief Validates configuration values and corrects them if needed
 * @param config Pointer to configuration structure to validate
 */
void validateConfiguration(struct configTable *config);

// --- Timer HAL ---
/**
 * Function pointer type for timer callbacks
 */
typedef void (*timerCallbackPtr)(void);

/**
 * @brief Perform periodic HAL tasks
 * This function handles all periodic tasks including command processing,
 * hardware monitoring, etc.
 */
void halDoWork();

/**
 * @brief Initialize the timer hardware
 * @param initialRpm Initial RPM value to set
 * @param callback Function to call on timer interrupt
 */
void timerHalInit(uint32_t initialRpm, timerCallbackPtr callback);

/**
 * @brief Set the timer frequency based on RPM
 * @param rpm Target RPM
 */
void timerHalSetRpm(uint32_t rpm);

/**
 * @brief Start the timer
 */
void timerHalStart();

/**
 * @brief Stop the timer
 */
void timerHalStop();

#if defined(__AVR__) // AVR-specific Timer HAL functions
/**
 * @brief Get bit shift value for a given prescaler setting
 * @param prescalerBits Pointer to prescaler bits
 * @return Bit shift amount
 */
uint8_t getBitshiftFromPrescaler(uint8_t *prescalerBits);

/**
 * @brief Determine prescaler settings based on timer value
 * @param potentialOcValue Pointer to timer value
 * @param prescaler Pointer to store prescaler value
 * @param bitshift Pointer to store bit shift value
 */
void getPrescalerBits(uint32_t *potentialOcValue, uint8_t *prescaler, uint8_t *bitshift);
#endif

// --- Storage HAL ---
/**
 * @brief Initialize storage system
 */
void storageHalInit();

/**
 * @brief Load configuration from storage
 * @param config Pointer to configuration structure to fill
 */
void storageHalLoadConfig(struct configTable *config);

/**
 * @brief Save configuration to storage
 * @param config Pointer to configuration structure to save
 */
void storageHalSaveConfig(const struct configTable *config);

// --- ADC HAL ---
/**
 * @brief Initialize analog-to-digital converter
 */
void adcHalInit();

/**
 * @brief Read value from ADC channel
 * @param channel Channel to read
 * @return ADC reading (0-1023 for AVR, 0-4095 for ESP32)
 */
uint16_t adcHalReadChannel(uint8_t channel);

// --- GPIO HAL ---
/**
 * @brief Initialize GPIO pins
 */
void gpioHalInit();

/**
 * @brief Set GPIO output state
 * @param pin Pin number to set
 * @param state Pin state (true=HIGH, false=LOW)
 */
void gpioHalSetOutput(int pin, bool state);

// --- Serial HAL ---
/**
 * @brief Initialize serial communication
 */
void serialHalInit();

/**
 * @brief Check if serial data is available
 * @return true if data is available
 */
bool serialHalAvailable();

/**
 * @brief Read a byte from serial
 * @return Byte read from serial
 */
uint8_t serialHalReadByte();

/**
 * @brief Write a byte to serial
 * @param byte Byte to write
 */
void serialHalWriteByte(uint8_t byte);

/**
 * @brief Print string to serial
 * @param str String to print
 */
void serialHalPrint(const char *str);

/**
 * @brief Print string to serial with newline
 * @param str String to print
 */
void serialHalPrintln(const char *str);

#endif // __HAL_H__