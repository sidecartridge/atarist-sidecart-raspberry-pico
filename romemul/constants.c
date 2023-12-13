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

// Atari ST constants.
const uint32_t ATARI_ROM4_START_ADDRESS = 0xFA0000; // Start address of the Atari ST ROM4
const uint32_t ATARI_ROM3_START_ADDRESS = 0xFB0000; // Start address of the Atari ST ROM3

// Configurator constants.
const uint32_t CONFIGURATOR_SHARED_MEMORY_SIZE_BYTES = (64 * 1024); // Size of the offset in the shared memory to store the config.

// Network constants.
const uint32_t NETWORK_MAGIC = 0x12340001; // Magic number to check if the network scan data exists in FLASH.

MorseCode morseAlphabet[] = {
    {'A', ".-"}, {'B', "-..."}, {'C', "-.-."}, {'D', "-.."}, {'E', "."}, {'F', "..-."}, {'G', "--."}, {'H', "...."}, {'I', ".."}, {'J', ".---"}, {'K', "-.-"}, {'L', ".-.."}, {'M', "--"}, {'N', "-."}, {'O', "---"}, {'P', ".--."}, {'Q', "--.-"}, {'R', ".-."}, {'S', "..."}, {'T', "-"}, {'U', "..-"}, {'V', "...-"}, {'W', ".--"}, {'X', "-..-"}, {'Y', "-.--"}, {'Z', "--.."}, {'0', "-----"}, {'1', ".----"}, {'2', "..---"}, {'3', "...--"}, {'4', "....-"}, {'5', "....."}, {'6', "-...."}, {'7', "--..."}, {'8', "---.."}, {'9', "----."}, {'\0', NULL} // Sentinel value to mark end of array
};

// Config files constants
const char WIFI_PASS_FILE_NAME[] = "/.wifipass";        // File name for the wifi password
const char ROM_RESCUE_MODE_FILE_NAME[] = "/.romrescue"; // File name for the rom rescue mode

// The GEMDOS calls
const char *GEMDOS_CALLS[93] = {
    "Pterm0",   // 0x00
    "Conin",    // 0x01
    "Cconout",  // 0x02
    "Cauxin",   // 0x03
    "Cauxout",  // 0x04
    "Cprnout",  // 0x05
    "Crawio",   // 0x06
    "Crawcin",  // 0x07
    "Cnecin",   // 0x08
    "Cconws",   // 0x09
    "Cconrs",   // 0x0A
    "Cconis",   // 0x0B
    "",         // 0x0C
    "",         // 0x0D
    "Dsetdrv",  // 0x0E
    "",         // 0x0F
    "Cconos",   // 0x10
    "Cprnos",   // 0x11
    "Cauxis",   // 0x12
    "Cauxos",   // 0x13
    "Maddalt",  // 0x14
    "",         // 0x15
    "",         // 0x16
    "",         // 0x17
    "",         // 0x18
    "Dgetdrv",  // 0x19
    "Fsetdta",  // 0x1A
    "",         // 0x1B
    "",         // 0x1C
    "",         // 0x1D
    "",         // 0x1E
    "",         // 0x1F
    "Super",    // 0x20
    "",         // 0x21
    "",         // 0x22
    "",         // 0x23
    "",         // 0x24
    "",         // 0x25
    "",         // 0x26
    "",         // 0x27
    "",         // 0x28
    "",         // 0x29
    "Tgetdate", // 0x2A
    "Tsetdate", // 0x2B
    "Tgettime", // 0x2C
    "Tsettime", // 0x2D
    "",         // 0x2E
    "Fgetdta",  // 0x2F
    "Sversion", // 0x30
    "Ptermres", // 0x31
    "",         // 0x32
    "",         // 0x33
    "",         // 0x34
    "",         // 0x35
    "Dfree",    // 0x36
    "",         // 0x37
    "",         // 0x38
    "Dcreate",  // 0x39
    "Ddelete",  // 0x3A
    "Dsetpath", // 0x3B
    "Fcreate",  // 0x3C
    "Fopen",    // 0x3D
    "Fclose",   // 0x3E
    "Fread",    // 0x3F
    "Fwrite",   // 0x40
    "Fdelete",  // 0x41
    "Fseek",    // 0x42
    "Fattrib",  // 0x43
    "Mxalloc",  // 0x44
    "Fdup",     // 0x45
    "Fforce",   // 0x46
    "Dgetpath", // 0x47
    "Malloc",   // 0x48
    "Mfree",    // 0x49
    "Mshrink",  // 0x4A
    "Pexec",    // 0x4B
    "Pterm",    // 0x4C
    "",         // 0x4D
    "Fsfirst",  // 0x4E
    "Fsnext",   // 0x4F
    "",         // 0x50
    "",         // 0x51
    "",         // 0x52
    "",         // 0x53
    "",         // 0x54
    "",         // 0x55
    "Frename",  // 0x56
    "Fdatime",  // 0x57
    "",         // 0x58
    "",         // 0x59
    "",         // 0x5A
    "",         // 0x5B
    "Flock",    // 0x5C
};
