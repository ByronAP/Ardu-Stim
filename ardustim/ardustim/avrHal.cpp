/* vim: set syntax=c expandtab sw=2 softtabstop=2 autoindent smartindent smarttab : */
/*
 * ArduStim - AVR Hardware Abstraction Layer implementation
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
#include "hal.h" // Include the combined HAL header
#include "globals.h"       // For global variables like `Wheels`, `config`, `currentStatus`
#include "comms.h"         // For `commandParser` function
#include "enums.h"        // For enums like `PRESCALE_1`, etc.
#include "wheelDefs.h"   // For `wheels` struct and wheel pattern definitions
#include <avr/interrupt.h> // For AVR ISRs
#include <EEPROM.h>        // For AVR EEPROM

// EEPROM address definitions for AVR
#define EEPROM_VERSION          1
#define EEPROM_WHEEL            2
#define EEPROM_RPM_MODE         3
#define EEPROM_CURRENT_RPM      4
#define EEPROM_SWEEP_RPM_MIN    6
#define EEPROM_SWEEP_RPM_MAX    8
#define EEPROM_SWEEP_RPM_INT    10
#define EEPROM_FIXED_RPM        12
#define EEPROM_USE_COMPRESSION  14
#define EEPROM_COMPRESSION_TYPE 15
#define EEPROM_COMPRESSION_RPM  16
#define EEPROM_COMPRESSION_OFFSET 18
#define EEPROM_COMPRESSION_DYNAMIC 20

wirelessComm getWirelessComm() {
    return NONE; // No wireless on AVR
}

// --- Timer HAL Implementation for AVR ---
extern wheels Wheels[]; // Defined in ardustim.ino
static timerCallbackPtr avrTimerCallback;

/**
 * @brief Timer interrupt service routine for AVR
 * 
 * This ISR is executed on each timer compare match. It calls the timer
 * callback function and updates the timer compare value.
 */
ISR(TIMER1_COMPA_vect) {
    if (avrTimerCallback) {
        avrTimerCallback();
    }
    if (resetPrescaler) {
        TCCR1B &= ~((1 << CS10) | (1 << CS11) | (1 << CS12));
        TCCR1B |= prescalerBits;
        resetPrescaler = false;
    }
    OCR1A = newOCR1A;
}

/**
 * @brief ADC interrupt service routine for AVR
 * 
 * This ISR is executed when an ADC conversion completes.
 */
ISR(ADC_vect) {
    if (analogPort == 0) {
        adc0 = ADCL | (ADCH << 8);
        adc0ReadComplete = true;
    }
}

/**
 * @brief Initialize the AVR timer
 * @param initialRpm Initial RPM to set
 * @param callback Function to call on timer interrupt
 */
void timerHalInit(uint32_t initialRpm, timerCallbackPtr callback) {
    avrTimerCallback = callback;
    TCCR1A = 0;
    TCCR1B = 0;
    TCNT1 = 0;
    OCR1A = 1000;
    TCCR1B |= (1 << WGM12);
    TCCR1B |= (1 << CS10);
    TIMSK1 |= (1 << OCIE1A);
    timerHalSetRpm(initialRpm);
}

/**
 * @brief Set the timer frequency based on RPM
 * @param rpm Target RPM value
 */
void timerHalSetRpm(uint32_t rpm) {
    if (rpm < 10) return;
    uint32_t tmp = (uint32_t)(8000000.0 / (Wheels[config.wheel].rpm_scaler * (float)rpm));
    uint8_t tmpPrescalerBits;
    uint8_t bitshift;
    getPrescalerBits(&tmp, &tmpPrescalerBits, &bitshift);
    newOCR1A = (uint16_t)(tmp >> bitshift);
    prescalerBits = tmpPrescalerBits;
    resetPrescaler = true;
}

/**
 * @brief Start the timer
 */
void timerHalStart() {} // No-op for AVR in this setup

/**
 * @brief Stop the timer
 */
void timerHalStop() { TCCR1B &= ~((1 << CS10) | (1 << CS11) | (1 << CS12)); }

/**
 * @brief Get bit shift value for prescaler
 * @param prescalerBits Pointer to prescaler bits
 * @return Bit shift amount
 */
uint8_t getBitshiftFromPrescaler(uint8_t *prescalerBits) {
    switch (*prescalerBits) {
        case PRESCALE_1024: return 10;
        case PRESCALE_256:  return 8;
        case PRESCALE_64:   return 6;
        case PRESCALE_8:    return 3;
        case PRESCALE_1:    return 0;
        default:            return 0;
    }
}

/**
 * @brief Determine prescaler settings based on timer value
 * @param potentialOcValue Pointer to timer value
 * @param prescaler Pointer to store prescaler value
 * @param bitshift Pointer to store bit shift value
 */
void getPrescalerBits(uint32_t *potentialOcValue, uint8_t *prescaler, uint8_t *bitshift) {
    if (*potentialOcValue >= 16777216) {
        *prescaler = PRESCALE_1024;
        *bitshift = 10;
    } else if (*potentialOcValue >= 4194304) {
        *prescaler = PRESCALE_256;
        *bitshift = 8;
    } else if (*potentialOcValue >= 524288) {
        *prescaler = PRESCALE_64;
        *bitshift = 6;
    } else if (*potentialOcValue >= 65536) {
        *prescaler = PRESCALE_8;
        *bitshift = 3;
    } else {
        *prescaler = PRESCALE_1;
        *bitshift = 0;
    }
}

// --- Storage HAL Implementation for AVR ---
/**
 * @brief Initialize the storage system
 */
void storageHalInit() {} // No specific init needed for AVR EEPROM

/**
 * @brief Load configuration from EEPROM
 * @param config Pointer to configuration structure to fill
 */
void storageHalLoadConfig(struct configTable *config) {
    if(EEPROM.read(EEPROM_VERSION) == 255) {
        // Set default configuration if EEPROM is empty
        config->version = VERSION;
        config->wheel = 5;
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
        config->compressionDynamic = false; // Initialize dynamic compression
        // No wireless on AVR
        config->wifiEnabled = false;
        config->bluetoothEnabled = false;
        config->bluetoothPin = 0;
        memset(config->wifiSSID, 0, sizeof(config->wifiSSID));
        memset(config->wifiPassword, 0, sizeof(config->wifiPassword));
        
        // Save default configuration
        storageHalSaveConfig(config);
    } else {
        // Load existing configuration
        config->version = EEPROM.read(EEPROM_VERSION);
        config->wheel = EEPROM.read(EEPROM_WHEEL);
        config->mode = EEPROM.read(EEPROM_RPM_MODE);
        currentStatus.rpm = word(EEPROM.read(EEPROM_CURRENT_RPM), EEPROM.read(EEPROM_CURRENT_RPM+1));
        config->fixed_rpm = word(EEPROM.read(EEPROM_FIXED_RPM), EEPROM.read(EEPROM_FIXED_RPM+1));
        config->sweep_low_rpm = word(EEPROM.read(EEPROM_SWEEP_RPM_MIN), EEPROM.read(EEPROM_SWEEP_RPM_MIN+1));
        config->sweep_high_rpm = word(EEPROM.read(EEPROM_SWEEP_RPM_MAX), EEPROM.read(EEPROM_SWEEP_RPM_MAX+1));
        config->sweep_interval = word(EEPROM.read(EEPROM_SWEEP_RPM_INT), EEPROM.read(EEPROM_SWEEP_RPM_INT+1));
        config->useCompression = EEPROM.read(EEPROM_USE_COMPRESSION);
        config->compressionType = EEPROM.read(EEPROM_COMPRESSION_TYPE);
        config->compressionRPM = word(EEPROM.read(EEPROM_COMPRESSION_RPM), EEPROM.read(EEPROM_COMPRESSION_RPM+1));
        config->compressionOffset = word(EEPROM.read(EEPROM_COMPRESSION_OFFSET), EEPROM.read(EEPROM_COMPRESSION_OFFSET+1));
        config->compressionDynamic = EEPROM.read(EEPROM_COMPRESSION_DYNAMIC);
        // No wireless on AVR
        config->wifiEnabled = false;
        config->bluetoothEnabled = false;
        config->bluetoothPin = 0;
        memset(config->wifiSSID, 0, sizeof(config->wifiSSID));
        memset(config->wifiPassword, 0, sizeof(config->wifiPassword));

        // Validate loaded configuration
        validateConfiguration(config);
    }
}

/**
 * @brief Save configuration to EEPROM
 * @param config Pointer to configuration structure to save
 */
void storageHalSaveConfig(const struct configTable *config) {
    EEPROM.update(EEPROM_VERSION, VERSION); 
    EEPROM.update(EEPROM_WHEEL, config->wheel);
    EEPROM.update(EEPROM_RPM_MODE, config->mode); 
    EEPROM.update(EEPROM_CURRENT_RPM, highByte(currentStatus.rpm));
    EEPROM.update(EEPROM_CURRENT_RPM+1, lowByte(currentStatus.rpm)); 
    EEPROM.update(EEPROM_FIXED_RPM, highByte(config->fixed_rpm));
    EEPROM.update(EEPROM_FIXED_RPM+1, lowByte(config->fixed_rpm)); 
    EEPROM.update(EEPROM_SWEEP_RPM_MIN, highByte(config->sweep_low_rpm));
    EEPROM.update(EEPROM_SWEEP_RPM_MIN+1, lowByte(config->sweep_low_rpm)); 
    EEPROM.update(EEPROM_SWEEP_RPM_MAX, highByte(config->sweep_high_rpm));
    EEPROM.update(EEPROM_SWEEP_RPM_MAX+1, lowByte(config->sweep_high_rpm)); 
    EEPROM.update(EEPROM_SWEEP_RPM_INT, highByte(config->sweep_interval));
    EEPROM.update(EEPROM_SWEEP_RPM_INT+1, lowByte(config->sweep_interval)); 
    EEPROM.update(EEPROM_USE_COMPRESSION, config->useCompression);
    EEPROM.update(EEPROM_COMPRESSION_TYPE, config->compressionType); 
    EEPROM.update(EEPROM_COMPRESSION_RPM, highByte(config->compressionRPM));
    EEPROM.update(EEPROM_COMPRESSION_RPM+1, lowByte(config->compressionRPM)); 
    EEPROM.update(EEPROM_COMPRESSION_OFFSET, highByte(config->compressionOffset));
    EEPROM.update(EEPROM_COMPRESSION_OFFSET+1, lowByte(config->compressionOffset));
    EEPROM.update(EEPROM_COMPRESSION_DYNAMIC, config->compressionDynamic);
    // No wireless on AVR so no need to save those settings
}

// --- ADC HAL Implementation for AVR ---
/**
 * @brief Initialize ADC for AVR
 */
void adcHalInit() {
    ADMUX &= B11011111; 
    ADMUX |= B01000000; 
    ADMUX &= B11110000;
    ADCSRA |= B10000000; 
    ADCSRA |= B00100000; 
    ADCSRB &= B11111000;
    ADCSRA |= B00000111; 
    ADCSRA |= B00001000; 
    ADCSRA |= B01000000;
}

/**
 * @brief Read value from ADC channel
 * @param channel Channel to read
 * @return ADC reading (0-1023)
 */
uint16_t adcHalReadChannel(uint8_t channel) {
    if (channel == 0) {
        analogPort = 0; 
        ADCSRA |= B01000000; 
        while (!adc0ReadComplete);
        adc0ReadComplete = false; 
        return adc0;
    }
    return 0;
}

// --- GPIO HAL Implementation for AVR ---
/**
 * @brief Initialize GPIO pins for AVR
 */
void gpioHalInit() {
    pinMode(PRIMARY_OUTPUT_PIN, OUTPUT); 
    pinMode(SECONDARY_OUTPUT_PIN, OUTPUT);
    pinMode(TERTIARY_OUTPUT_PIN, OUTPUT); 
    pinMode(KNOCK_OUTPUT_PIN, OUTPUT);
}

/**
 * @brief Set GPIO output state for AVR
 * @param pin Pin number to set
 * @param state Pin state (true=HIGH, false=LOW)
 */
void gpioHalSetOutput(int pin, bool state) {
    digitalWrite(pin, state ? HIGH : LOW);
}

// --- Serial HAL Implementation for AVR ---
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
    // Check for serial commands
    if (Serial.available() > 0) {
      commandParser(&Serial);
    }
  }