#include "hal.h" // Include the combined HAL header
#include "globals.h"       // For global variables like `Wheels`, `config`, `currentStatus`
#include "enums.h"        // For enums like `PRESCALE_1`, etc.
#include "wheel_defs.h"   // For `wheels` struct and wheel pattern definitions
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

// --- Timer HAL Implementation for AVR ---
extern wheels Wheels[]; // Defined in globals.h
extern struct configTable config; // Defined in globals.h
extern volatile uint16_t new_OCR1A; // Defined in ardustim.ino.txt
extern volatile uint8_t prescaler_bits; // Defined in ardustim.ino.txt
extern volatile bool reset_prescaler; // Defined in ardustim.ino.txt

static timer_callback_ptr avr_timer_callback;

ISR(TIMER1_COMPA_vect) {
    if (avr_timer_callback) {
        avr_timer_callback();
    }
    if (reset_prescaler) {
        TCCR1B &= ~((1 << CS10) | (1 << CS11) | (1 << CS12));
        TCCR1B |= prescaler_bits;
        reset_prescaler = false;
    }
    OCR1A = new_OCR1A;
}

void timer_hal_init(uint32_t initial_rpm, timer_callback_ptr callback) {
    avr_timer_callback = callback;
    TCCR1A = 0;
    TCCR1B = 0;
    TCNT1 = 0;
    OCR1A = 1000;
    TCCR1B |= (1 << WGM12);
    TCCR1B |= (1 << CS10);
    TIMSK1 |= (1 << OCIE1A);
    timer_hal_set_rpm(initial_rpm);
}

void timer_hal_set_rpm(uint32_t rpm) {
    if (rpm < 10) return;
    uint32_t tmp = (uint32_t)(8000000.0 / (Wheels[config.wheel].rpm_scaler * (float)rpm));
    uint8_t tmp_prescaler_bits;
    uint8_t bitshift;
    get_prescaler_bits(&tmp, &tmp_prescaler_bits, &bitshift);
    new_OCR1A = (uint16_t)(tmp >> bitshift);
    prescaler_bits = tmp_prescaler_bits;
    reset_prescaler = true;
}

void timer_hal_start() {} // No-op for AVR in this setup
void timer_hal_stop() { TCCR1B &= ~((1 << CS10) | (1 << CS11) | (1 << CS12)); }

uint8_t get_bitshift_from_prescaler(uint8_t *prescaler_bits) {
    switch (*prescaler_bits) {
        case PRESCALE_1024: return 10;
        case PRESCALE_256:  return 8;
        case PRESCALE_64:   return 6;
        case PRESCALE_8:    return 3;
        case PRESCALE_1:    return 0;
        default:            return 0;
    }
}
void get_prescaler_bits(uint32_t *potential_oc_value, uint8_t *prescaler, uint8_t *bitshift) {
    if (*potential_oc_value >= 16777216) {
        *prescaler = PRESCALE_1024;
        *bitshift = 10;
    } else if (*potential_oc_value >= 4194304) {
        *prescaler = PRESCALE_256;
        *bitshift = 8;
    } else if (*potential_oc_value >= 524288) {
        *prescaler = PRESCALE_64;
        *bitshift = 6;
    } else if (*potential_oc_value >= 65536) {
        *prescaler = PRESCALE_8;
        *bitshift = 3;
    } else {
        *prescaler = PRESCALE_1;
        *bitshift = 0;
    }
}

// --- Storage HAL Implementation for AVR ---
void storage_hal_init() {} // No specific init needed for AVR EEPROM

void storage_hal_load_config(struct configTable *config) {
    if(EEPROM.read(EEPROM_VERSION) == 255) {
        config->version = VERSION; config->wheel = 5; currentStatus.rpm = 3000; currentStatus.base_rpm = 3000;
        config->mode = POT_RPM; config->fixed_rpm = 3500; config->sweep_high_rpm = 6000;
        config->sweep_low_rpm = 1000; config->sweep_interval = 1000; config->useCompression = false;
        config->compressionType = COMPRESSION_TYPE_4CYL_4STROKE; config->compressionRPM = 400; config->compressionOffset = 0;
        storage_hal_save_config(config);
    } else {
        config->version =        EEPROM.read(EEPROM_VERSION); config->wheel =          EEPROM.read(EEPROM_WHEEL);
        config->mode =           EEPROM.read(EEPROM_RPM_MODE); currentStatus.rpm =      word(EEPROM.read(EEPROM_CURRENT_RPM), EEPROM.read(EEPROM_CURRENT_RPM+1));
        config->fixed_rpm =      word(EEPROM.read(EEPROM_FIXED_RPM), EEPROM.read(EEPROM_FIXED_RPM+1)); config->sweep_low_rpm =  word(EEPROM.read(EEPROM_SWEEP_RPM_MIN), EEPROM.read(EEPROM_SWEEP_RPM_MIN+1));
        config->sweep_high_rpm = word(EEPROM.read(EEPROM_SWEEP_RPM_MAX), EEPROM.read(EEPROM_SWEEP_RPM_MAX+1)); config->sweep_interval = word(EEPROM.read(EEPROM_SWEEP_RPM_INT), EEPROM.read(EEPROM_SWEEP_RPM_INT+1));
        config->useCompression = EEPROM.read(EEPROM_USE_COMPRESSION); config->compressionType =EEPROM.read(EEPROM_COMPRESSION_TYPE);
        config->compressionRPM = word(EEPROM.read(EEPROM_COMPRESSION_RPM), EEPROM.read(EEPROM_COMPRESSION_RPM+1)); config->compressionOffset =word(EEPROM.read(EEPROM_COMPRESSION_OFFSET), EEPROM.read(EEPROM_COMPRESSION_OFFSET+1));

        if(config->wheel >= MAX_WHEELS) { config->wheel = 5; } if(config->mode >= MAX_MODES) { config->mode = POT_RPM; }
        if(currentStatus.rpm > 15000) { currentStatus.rpm = 4000; } if(currentStatus.base_rpm > 15000) { currentStatus.base_rpm = 4000; }
        if(config->compressionType > COMPRESSION_TYPE_8CYL_4STROKE) { config->compressionType = COMPRESSION_TYPE_4CYL_4STROKE; }
        if(config->compressionRPM > 1000) { config->compressionRPM = 400; } if(config->compressionOffset > 359) { config->compressionOffset = 0; }
    }
}

void storage_hal_save_config(const struct configTable *config) {
    EEPROM.update(EEPROM_VERSION, VERSION); EEPROM.update(EEPROM_WHEEL, config->wheel);
    EEPROM.update(EEPROM_RPM_MODE, config->mode); EEPROM.update(EEPROM_CURRENT_RPM, highByte(currentStatus.rpm));
    EEPROM.update(EEPROM_CURRENT_RPM+1, lowByte(currentStatus.rpm)); EEPROM.update(EEPROM_FIXED_RPM, highByte(config->fixed_rpm));
    EEPROM.update(EEPROM_FIXED_RPM+1, lowByte(config->fixed_rpm)); EEPROM.update(EEPROM_SWEEP_RPM_MIN, highByte(config->sweep_low_rpm));
    EEPROM.update(EEPROM_SWEEP_RPM_MIN+1, lowByte(config->sweep_low_rpm)); EEPROM.update(EEPROM_SWEEP_RPM_MAX, highByte(config->sweep_high_rpm));
    EEPROM.update(EEPROM_SWEEP_RPM_MAX+1, lowByte(config->sweep_high_rpm)); EEPROM.update(EEPROM_SWEEP_RPM_INT, highByte(config->sweep_interval));
    EEPROM.update(EEPROM_SWEEP_RPM_INT+1, lowByte(config->sweep_interval)); EEPROM.update(EEPROM_USE_COMPRESSION, config->useCompression);
    EEPROM.update(EEPROM_COMPRESSION_TYPE, config->compressionType); EEPROM.update(EEPROM_COMPRESSION_RPM, highByte(config->compressionRPM));
    EEPROM.update(EEPROM_COMPRESSION_RPM+1, lowByte(config->compressionRPM)); EEPROM.update(EEPROM_COMPRESSION_OFFSET, highByte(config->compressionOffset));
    EEPROM.update(EEPROM_COMPRESSION_OFFSET+1, lowByte(config->compressionOffset));
}

// --- ADC HAL Implementation for AVR ---
extern volatile uint16_t adc0; // Defined in ardustim.ino.txt
extern volatile uint16_t adc1; // Defined in ardustim.ino.txt
extern volatile bool adc0_read_complete; // Defined in ardustim.ino.txt
extern volatile bool adc1_read_complete; // Defined in ardustim.ino.txt
extern volatile uint8_t analog_port; // Defined in ardustim.ino.txt

ISR(ADC_vect) {
    if (analog_port == 0) {
        adc0 = ADCL | (ADCH << 8);
        adc0_read_complete = true;
    }
}

void adc_hal_init() {
    ADMUX &= B11011111; ADMUX |= B01000000; ADMUX &= B11110000;
    ADCSRA |= B10000000; ADCSRA |= B00100000; ADCSRB &= B11111000;
    ADCSRA |= B00000111; ADCSRA |= B00001000; ADCSRA |= B01000000;
}

uint16_t adc_hal_read_channel(uint8_t channel) {
    if (channel == 0) {
        analog_port = 0; ADCSRA |= B01000000; while (!adc0_read_complete);
        adc0_read_complete = false; return adc0;
    }
    return 0;
}

// --- GPIO HAL Implementation for AVR ---
void gpio_hal_init() {
    pinMode(PRIMARY_OUTPUT_PIN, OUTPUT); pinMode(SECONDARY_OUTPUT_PIN, OUTPUT);
    pinMode(TERTIARY_OUTPUT_PIN, OUTPUT); pinMode(KNOCK_OUTPUT_PIN, OUTPUT);
}

void gpio_hal_set_output(int pin, bool state) {
    digitalWrite(pin, state ? HIGH : LOW);
}

// --- Serial HAL Implementation for AVR ---
void serial_hal_init() { Serial.begin(115200); }
bool serial_hal_available() { return Serial.available() > 0; }
uint8_t serial_hal_read_byte() { return Serial.read(); }
void serial_hal_write_byte(uint8_t byte) { Serial.write(byte); }
void serial_hal_print(const char *str) { Serial.print(str); }
void serial_hal_println(const char *str) { Serial.println(str); }