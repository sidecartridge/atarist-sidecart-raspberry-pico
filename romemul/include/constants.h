/**
 * File: constants.h
 * Author: Diego Parrilla Santamaría
 * Date: July 2023
 * Copyright: 2023 - GOODDATA LABS SL
 * Description: Constants used throughout the project.
 */

#pragma once

#include <stdint.h>
#include "pico/stdlib.h"

// GPIO constants for SELECT button.
extern const uint32_t SELECT_GPIO;

// GPIO constants for the read address from the bus
extern const uint READ_ADDR_GPIO_BASE;
extern const uint READ_ADDR_PIN_COUNT;
extern const uint READ_SIGNAL_GPIO_BASE;
extern const uint READ_SIGNAL_PIN_COUNT;

// Write data to the bus
extern const uint WRITE_DATA_GPIO_BASE;
extern const uint WRITE_DATA_PIN_COUNT;
extern const uint WRITE_SIGNAL_GPIO_BASE;
extern const uint WRITE_SIGNAL_PIN_COUNT;

// Frequency constants.
extern const float SAMPLE_DIV_FREQ;
extern const uint32_t RP2040_CLOCK_FREQ_KHZ;

// Frequency constants.
#define SAMPLE_DIV_FREQ (1.f)                  // Sample frequency division factor.
#define RP2040_CLOCK_FREQ_KHZ (125000 + 35000) // Clock frequency in KHz (160MHz).

// Voltage constants.
#define RP2040_VOLTAGE VREG_VOLTAGE_1_10 // Voltage in 1.10 Volts.
#define VOLTAGE_VALUES   \
    (const char *[])     \
    {                    \
        "NOT VALID",     \
            "NOT VALID", \
            "NOT VALID", \
            "NOT VALID", \
            "NOT VALID", \
            "NOT VALID", \
            "0.85v",     \
            "0.90v",     \
            "0.95v",     \
            "1.00v",     \
            "1.05v",     \
            "1.10v",     \
            "1.15v",     \
            "1.20v",     \
            "1.25v",     \
            "1.30v",     \
            "NOT VALID", \
            "NOT VALID", \
            "NOT VALID", \
            "NOT VALID", \
            "NOT VALID"  \
    }

// FLASH and RAM sections constants.
extern const uint8_t ROM_BANKS;
extern const uint32_t FLASH_ROM_LOAD_OFFSET;
extern const uint32_t FLASH_ROM4_LOAD_OFFSET;
extern const uint32_t FLASH_ROM3_LOAD_OFFSET;
extern const uint32_t ROM_IN_RAM_ADDRESS;
extern const uint32_t ROMS_START_ADDRESS;
extern const uint32_t ROM4_START_ADDRESS;
extern const uint32_t ROM3_START_ADDRESS;
extern const uint32_t ROM_SIZE_BYTES;
extern const uint32_t ROM_SIZE_WORDS;
extern const uint32_t ROM_SIZE_LONGWORDS;
extern const uint32_t CONFIG_FLASH_OFFSET;
extern const uint32_t CONFIG_FLASH_SIZE;
extern const uint32_t CONFIG_VERSION;
extern const uint32_t CONFIG_MAGIC;
extern const uint32_t NETWORK_MAGIC;

// Atari ST constants.
extern const uint32_t ATARI_ROM4_START_ADDRESS;
extern const uint32_t ATARI_ROM3_START_ADDRESS;

// Configurator constants.
extern const uint32_t CONFIGURATOR_SHARED_MEMORY_SIZE_BYTES;

// Configuration files constants.
extern const char WIFI_PASS_FILE_NAME[];
extern const char ROM_RESCUE_MODE_FILE_NAME[];

// Morse code
#define DOT_DURATION_MS 150
#define DASH_DURATION_MS 450
#define SYMBOL_GAP_MS 150
#define CHARACTER_GAP_MS 700

// Custom errors for the filesystem
#define FR_FILE_EXISTS 100
#define FR_CANNOT_OPEN_FILE_FOR_WRITE 101

typedef struct
{
    char character;
    const char *morse;
} MorseCode;

extern MorseCode morseAlphabet[]; // This is a declaration, not a definition.

extern const char *GEMDOS_CALLS[];