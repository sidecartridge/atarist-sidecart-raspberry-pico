#include "include/constants.h"

// GPIO constants for SELECT button.
const uint32_t SELECT_GPIO = 5;

// GPIO constants for the read address from the bus
const uint READ_ADDR_GPIO_BASE = 6;    // Start of the GPIOs for the address
const uint READ_ADDR_PIN_COUNT = 16;   // Number of GPIOs for the address
const uint READ_SIGNAL_GPIO_BASE = 27; // GPIO signal for READ
const uint READ_SIGNAL_PIN_COUNT = 1;

// Write data to the bus
const uint WRITE_DATA_GPIO_BASE = READ_ADDR_GPIO_BASE;         // Start of the GPIOs for the data to write
const uint WRITE_DATA_PIN_COUNT = READ_ADDR_PIN_COUNT;         // Number of GPIOs for the data
const uint WRITE_SIGNAL_GPIO_BASE = READ_SIGNAL_GPIO_BASE + 1; // GPIO signal for WRITE
const uint WRITE_SIGNAL_PIN_COUNT = 1;

// Frequency constants.
const float SAMPLE_DIV_FREQ = 1.f;                     // Sample frequency division factor.
const uint32_t RP2040_CLOCK_FREQ_KHZ = 125000 + 25000; // Clock frequency in KHz (150MHz).

// FLASH and RAM sections constants.
const uint8_t ROM_BANKS = 2;                                                    // Number of ROM banks to emulate
const uint32_t FLASH_ROM_LOAD_OFFSET = 0xE0000;                                 // Offset start in FLASH reserved for ROMs. Survives a reset or poweroff.
const uint32_t FLASH_ROM4_LOAD_OFFSET = FLASH_ROM_LOAD_OFFSET;                  // First 64KB block
const uint32_t FLASH_ROM3_LOAD_OFFSET = FLASH_ROM_LOAD_OFFSET + 0x10000;        // Second 64KB block
const uint32_t ROM_IN_RAM_ADDRESS = 0x20020000;                                 // Address in RAM where the ROMs are loaded. Not survive a reset or poweroff.
const uint32_t ROMS_START_ADDRESS = ROM_IN_RAM_ADDRESS;                         // Address in RAM where the ROMs are loaded. Not survive a reset or poweroff.
const uint32_t ROM4_START_ADDRESS = ROMS_START_ADDRESS;                         // First 64KB block of ROM in rp2040 RAM. Not survive a reset or poweroff.
const uint32_t ROM3_START_ADDRESS = ROM_IN_RAM_ADDRESS + 0x10000;               // Second 64KB block of ROM in rp2040 RAM. Not survive a reset or poweroff.
const uint32_t ROM_SIZE_BYTES = 0x10000;                                        // 64KBytes
const uint32_t ROM_SIZE_WORDS = ROM_SIZE_BYTES / 2;                             // 32KWords
const uint32_t ROM_SIZE_LONGWORDS = ROM_SIZE_BYTES / 4;                         // 16KLongWords
const uint32_t CONFIG_FLASH_SIZE = 4096;                                        // Size of your reserved flash memory 4Kbytes
const uint32_t CONFIG_FLASH_OFFSET = FLASH_ROM_LOAD_OFFSET - CONFIG_FLASH_SIZE; // Offset FLASH where the config is stored. Survives a reset or poweroff.
const uint32_t CONFIG_VERSION = 0x00000001;                                     // Version of the config. Used to check if the config is compatible with the current code.
const uint32_t CONFIG_MAGIC = 0x12340000;                                       // Magic number to check if the config exists in FLASH.

// Configurator constants.
const uint32_t CONFIGURATOR_SHARED_MEMORY_SIZE_BYTES = 4096; // Size of the shared memory between the configurator and the emulator.

// Network constants.
const uint32_t NETWORK_MAGIC = 0x12340001; // Magic number to check if the network scan data exists in FLASH.

MorseCode morseAlphabet[] = {
    {'A', ".-"}, {'B', "-..."}, {'C', "-.-."}, {'D', "-.."}, {'E', "."}, {'F', "..-."}, {'G', "--."}, {'H', "...."}, {'I', ".."}, {'J', ".---"}, {'K', "-.-"}, {'L', ".-.."}, {'M', "--"}, {'N', "-."}, {'O', "---"}, {'P', ".--."}, {'Q', "--.-"}, {'R', ".-."}, {'S', "..."}, {'T', "-"}, {'U', "..-"}, {'V', "...-"}, {'W', ".--"}, {'X', "-..-"}, {'Y', "-.--"}, {'Z', "--.."}, {'0', "-----"}, {'1', ".----"}, {'2', "..---"}, {'3', "...--"}, {'4', "....-"}, {'5', "....."}, {'6', "-...."}, {'7', "--..."}, {'8', "---.."}, {'9', "----."}, {'\0', NULL} // Sentinel value to mark end of array
};
