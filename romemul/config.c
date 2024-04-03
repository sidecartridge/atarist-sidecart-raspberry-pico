#include "include/config.h"

// We should define ALWAYS the default entries with valid values.
// DONT FORGET TO CHANGE MAX_ENTRIES if the number of value changes!
static ConfigEntry defaultEntries[MAX_ENTRIES] = {
    {PARAM_BOOT_FEATURE, TYPE_STRING, "CONFIGURATOR"},
    {PARAM_CONFIGURATOR_DARK, TYPE_BOOL, "false"},
    {"DELAY_ROM_EMULATION", TYPE_BOOL, "false"},
    {PARAM_DOWNLOAD_TIMEOUT_SEC, TYPE_INT, "60"},
    {PARAM_FLOPPIES_FOLDER, TYPE_STRING, "/floppies"},
    {PARAM_FLOPPY_BOOT_ENABLED, TYPE_BOOL, "true"},
    {PARAM_FLOPPY_BUFFER_TYPE, TYPE_INT, "0"},
    {PARAM_FLOPPY_DB_URL, TYPE_STRING, "http://ataristdb.sidecartridge.com"},
    {"FLOPPY_IMAGE_A", TYPE_STRING, ""},
    {"FLOPPY_IMAGE_B", TYPE_STRING, ""},
    {PARAM_FLOPPY_XBIOS_ENABLED, TYPE_BOOL, "true"},
    {PARAM_GEMDRIVE_BUFF_TYPE, TYPE_INT, "0"},
    {PARAM_GEMDRIVE_DRIVE, TYPE_STRING, "C"},
    {PARAM_GEMDRIVE_FOLDERS, TYPE_STRING, "/hd"},
    {PARAM_GEMDRIVE_RTC, TYPE_BOOL, "true"},
    {PARAM_GEMDRIVE_TIMEOUT_SEC, TYPE_INT, "45"},
    {"HOSTNAME", TYPE_STRING, "sidecart"},
    {PARAM_LASTEST_RELEASE_URL, TYPE_STRING, LATEST_RELEASE_URL},
    {PARAM_MENU_REFRESH_SEC, TYPE_INT, "3"},
    {PARAM_NETWORK_STATUS_SEC, TYPE_INT, NETWORK_POLL_INTERVAL_STR},
    {PARAM_ROMS_FOLDER, TYPE_STRING, "/roms"},
    {PARAM_ROMS_YAML_URL, TYPE_STRING, "http://roms.sidecartridge.com/roms.json"},
    {"RTC_NTP_SERVER_HOST", TYPE_STRING, "pool.ntp.org"},
    {"RTC_NTP_SERVER_PORT", TYPE_INT, "123"},
    {"RTC_TYPE", TYPE_STRING, "SIDECART"},
    {"RTC_UTC_OFFSET", TYPE_STRING, "+1"},
    {"SAFE_CONFIG_REBOOT", TYPE_BOOL, "true"},
    {PARAM_WIFI_SCAN_SECONDS, TYPE_INT, WIFI_SCAN_POLL_COUNTER_STR},
    {PARAM_WIFI_PASSWORD, TYPE_STRING, ""},
    {PARAM_WIFI_SSID, TYPE_STRING, ""},
    {PARAM_WIFI_AUTH, TYPE_INT, ""},
    {PARAM_WIFI_COUNTRY, TYPE_STRING, ""}};

ConfigData configData;

static ConfigEntry read_entry(uint8_t **addressOffset)
{
    ConfigEntry entry;

    void *address = *addressOffset;
    void *entry_addr = &entry;
    size_t entry_size = sizeof(ConfigEntry);

    memcpy(&entry, *addressOffset, sizeof(ConfigEntry));
    return entry;
}

static void load_default_entries()
{
    configData.magic = CONFIG_MAGIC | CONFIG_VERSION;
    configData.count = 0; // Reset count

    for (size_t i = 0; i < MAX_ENTRIES; i++)
    {
        if (strlen(defaultEntries[i].key) > (MAX_KEY_LENGTH - 1))
        {
            DPRINTF("WARNING: MAX_KEY_LENGTH is %d but key %s is %d characters long.\n", MAX_KEY_LENGTH, defaultEntries[i].key, strlen(defaultEntries[i].key));
        }
        ConfigEntry tmpEntry = defaultEntries[i];
        //        tmpEntry.key[MAX_KEY_LENGTH - 1] = '\0'; // Ensure null-termination
        configData.entries[i] = tmpEntry;
        configData.count++;
    }

    if (configData.count != MAX_ENTRIES)
    {
        DPRINTF("WARNING: MAX_ENTRIES is %d but %d entries were loaded.\n", MAX_ENTRIES, configData.count);
    }
}

static void replace_bad_domain_entries()
{
    for (size_t i = 0; i < MAX_ENTRIES; i++)
    {

        if (strcmp(configData.entries[i].value, "http://ataristdb.sidecart.xyz") == 0)
        {
            strcpy(configData.entries[i].value, "http://ataristdb.sidecartridge.com");
        }
        if (strcmp(configData.entries[i].value, "http://roms.sidecart.xyz/roms.json") == 0)
        {
            strcpy(configData.entries[i].value, "http://roms.sidecartridge.com/roms.json");
        }
        if (strcmp(configData.entries[i].value, "http://atarist.sidecart.xyz/beta.txt") == 0)
        {
            strcpy(configData.entries[i].value, "http://atarist.sidecartridge.com/beta.txt");
        }
        if (strcmp(configData.entries[i].value, "http://atarist.sidecart.xyz/version.txt") == 0)
        {
            strcpy(configData.entries[i].value, "http://atarist.sidecartridge.com/version.txt");
        }
    }
}

void load_all_entries()
{
    uint8_t *currentAddress = (uint8_t *)(CONFIG_FLASH_OFFSET + XIP_BASE);

    // First, load default entries
    load_default_entries();

    uint8_t count = 0;

    const uint32_t magic = *(uint32_t *)currentAddress;
    currentAddress += sizeof(uint32_t);

    if (magic != (CONFIG_MAGIC | CONFIG_VERSION))
    {
        // No config found in FLASH. Use default values
        DPRINTF("No config found in FLASH. Using default values.\n");
        return;
    }

    while (count < MAX_ENTRIES)
    {
        //        ConfigEntry entry = read_entry(&currentAddress);
        ConfigEntry entry;
        memcpy(&entry, currentAddress, sizeof(ConfigEntry));

        currentAddress += sizeof(ConfigEntry);

        // Check for the end of the config entries
        if (entry.key[0] == '\0')
        {
            break; // Exit the loop if we encounter a key length of 0 (end of entries)
        }

        // Check if this key already exists in our loaded default entries
        char keyStr[MAX_KEY_LENGTH + 1] = {0};
        strncpy(keyStr, entry.key, MAX_KEY_LENGTH);
        ConfigEntry *existingEntry = find_entry(keyStr);
        if (existingEntry)
        {
            *existingEntry = entry;
        }
        // No else part here since we know every memory entry has a default
        count++;
    }
    replace_bad_domain_entries();
}

ConfigEntry *find_entry(const char key[MAX_KEY_LENGTH])
{
    for (size_t i = 0; i < configData.count; i++)
    {
        if (strncmp(configData.entries[i].key, key, MAX_KEY_LENGTH) == 0)
        {
            return &configData.entries[i];
        }
    }
    DPRINTF("Key %s not found.\n", key);
    return NULL;
}

// ConfigEntry* entry = findConfigEntry("desired_key");
// if (entry != NULL) {
//     // Access the entry's data using entry->value, entry->dataType, etc.
// } else {
//     // Entry with the desired key was not found.
// }

static int add_entry(const char key[MAX_KEY_LENGTH], DataType dataType, char value[MAX_STRING_VALUE_LENGTH])
{
    if (configData.count > MAX_ENTRIES)
    {
        // No room left for more entries
        return -1;
    }

    // Check if the key already exists
    for (size_t i = 0; i < configData.count; i++)
    {
        if (strncmp(configData.entries[i].key, key, MAX_KEY_LENGTH) == 0)
        {
            // Key already exists. Update its value and dataType
            configData.entries[i].dataType = dataType;
            strncpy(configData.entries[i].value, value, MAX_STRING_VALUE_LENGTH - 1);
            configData.entries[i].value[MAX_STRING_VALUE_LENGTH - 1] = '\0'; // Ensure null-termination
            return 0;                                                        // Successfully updated existing entry
        }
    }

    // If key doesn't exist, add a new entry
    strncpy(configData.entries[configData.count].key, key, MAX_KEY_LENGTH);
    if (strlen(configData.entries[configData.count].key) < MAX_KEY_LENGTH)
    {
        configData.entries[configData.count].key[strlen(configData.entries[configData.count].key)] = '\0'; // Null terminate just in case
    }
    configData.entries[configData.count].dataType = dataType;
    strncpy(configData.entries[configData.count].value, value, MAX_STRING_VALUE_LENGTH - 1);
    configData.entries[configData.count].value[MAX_STRING_VALUE_LENGTH - 1] = '\0'; // Ensure null-termination
    configData.count++;

    return 0; // Successfully added new entry
}

int put_bool(const char key[MAX_KEY_LENGTH], bool value)
{
    return add_entry(key, TYPE_BOOL, value ? "true" : "false");
}

int put_string(const char key[MAX_KEY_LENGTH], const char *value)
{
    char configValue[MAX_STRING_VALUE_LENGTH];
    strncpy(configValue, value, MAX_STRING_VALUE_LENGTH - 1);
    configValue[MAX_STRING_VALUE_LENGTH - 1] = '\0'; // Ensure null termination
    return add_entry(key, TYPE_STRING, configValue);
}

int put_integer(const char key[MAX_KEY_LENGTH], int value)
{
    char configValue[MAX_STRING_VALUE_LENGTH];
    snprintf(configValue, sizeof(configValue), "%d", value);
    // Set \0 at the end of the configValue string
    configValue[MAX_STRING_VALUE_LENGTH - 1] = '\0';
    return add_entry(key, TYPE_INT, configValue);
}

// Disable remove_entry. We don't need it for this project.
//
// int remove_entry(const char *key)
// {
//     int indexToRemove = -1;

//     // Search for the key
//     for (size_t i = 0; i < configData.count; i++)
//     {
//         if (strcmp(configData.entries[i].key, key) == 0)
//         {
//             indexToRemove = i;
//             break;
//         }
//     }

//     if (indexToRemove == -1)
//     {
//         return -1; // Key not found
//     }

//     // If the entry is not the last one, move subsequent entries up to overwrite it
//     for (size_t i = indexToRemove; i < configData.count - 1; i++)
//     {
//         configData.entries[i] = configData.entries[i + 1];
//     }

//     // Decrease the count of entries
//     configData.count--;

//     return 0; // Successfully removed the entry
// }

int write_all_entries()
{

    uint8_t *address = (uint8_t *)(CONFIG_FLASH_OFFSET + XIP_BASE);

    // Ensure we don't exceed the reserved space
    if (configData.count * sizeof(ConfigEntry) > CONFIG_FLASH_SIZE)
    {
        return -1; // Error: Config size exceeds reserved space
    }
    print_config_table();
    DPRINTF("Writing %d entries to FLASH.\n", configData.count);
    DPRINTF("Size of ConfigData: %d\n", sizeof(ConfigData));
    DPRINTF("Size of ConfigEntry: %d\n", sizeof(ConfigEntry));
    DPRINTF("Size of entries: %d\n", configData.count * sizeof(ConfigEntry));

    uint32_t ints = save_and_disable_interrupts();

    // Erase the content before writing the configuration
    // overwriting it's not enough
    flash_range_erase(CONFIG_FLASH_OFFSET, CONFIG_FLASH_SIZE); // 4 Kbytes

    // Transfer config to FLASH
    flash_range_program(CONFIG_FLASH_OFFSET, (uint8_t *)&configData, sizeof(configData));

    restore_interrupts(ints);

    return 0; // Successful write
}

int reset_config_default()
{
    uint32_t ints = save_and_disable_interrupts();

    // Erase the content before writing the configuration
    // overwriting it's not enough
    flash_range_erase(CONFIG_FLASH_OFFSET, CONFIG_FLASH_SIZE); // 4 Kbytes

    restore_interrupts(ints);

    load_default_entries();

    write_all_entries();
    return 0; // Successful write
}

// uint8_t *destAddress = /* some memory location */;
// writeConfigToMemory(destAddress);

void clear_config(void)
{
    memset(&configData, 0, sizeof(ConfigData));
}

size_t get_config_size()
{
    return sizeof(configData);
}

void print_config_table()
{
    DPRINTF("+----------------------+--------------------------------+----------+\n");
    DPRINTF("|         Key          |             Value              |   Type   |\n");
    DPRINTF("+----------------------+--------------------------------+----------+\n");

    for (size_t i = 0; i < configData.count; i++)
    {
        char valueStr[32]; // Buffer to format the value

        switch (configData.entries[i].dataType)
        {
        case TYPE_INT:
        case TYPE_STRING:
        case TYPE_BOOL:
            snprintf(valueStr, sizeof(valueStr), "%s", configData.entries[i].value);
            break;
        default:
            snprintf(valueStr, sizeof(valueStr), "Unknown");
            break;
        }

        char *typeStr;
        switch (configData.entries[i].dataType)
        {
        case TYPE_INT:
            typeStr = "INT";
            break;
        case TYPE_STRING:
            typeStr = "STRING";
            break;
        case TYPE_BOOL:
            typeStr = "BOOL";
            break;
        default:
            typeStr = "UNKNOWN";
            break;
        }
        char keyStr[21] = {0};
        strncpy(keyStr, configData.entries[i].key, MAX_KEY_LENGTH);
        DPRINTF("| %-20s | %-30s | %-8s |\n", keyStr, valueStr, typeStr);
    }

    DPRINTF("+--------------------------------+--------------------------------+----------+\n");
}

void swap_data(uint16_t *dest_ptr_word)
{
    for (int j = 0; j < MAX_KEY_LENGTH; j += 2)
    {
        uint16_t value = *(uint16_t *)(dest_ptr_word);
        *(uint16_t *)(dest_ptr_word)++ = (value << 8) | (value >> 8);
    }
    dest_ptr_word++; // Bypass type definition
    for (int j = 0; j < MAX_STRING_VALUE_LENGTH; j += 2)
    {
        uint16_t value = *(uint16_t *)(dest_ptr_word);
        *(uint16_t *)(dest_ptr_word)++ = (value << 8) | (value >> 8);
    }
}

void select_button_action(bool safe_config_reboot, bool write_config_only_once)
{
    if (safe_config_reboot)
    {
        if (write_config_only_once)
        {
            DPRINTF("SELECT button pressed. Configurator will start after power cycling the computer.\n");
            // Do not reboot if the user has disabled it
            put_string(PARAM_BOOT_FEATURE, "CONFIGURATOR");
            write_all_entries();
        }
    }
    else
    {

        DPRINTF("SELECT button pressed. Launch configurator.\n");
        watchdog_reboot(0, SRAM_END, 10);
        while (1)
            ;
    }
}

/**
 * @brief   Blinks an LED to represent a given character in Morse code.
 *
 * @param   ch  The character to blink in Morse code.
 *
 * @details This function searches for the provided character in the
 *          `morseAlphabet` structure array to get its Morse code representation.
 *          If found, it then blinks an LED in the pattern of dots and dashes
 *          corresponding to the Morse code of the character. The LED blinks are
 *          separated by time intervals defined by constants such as DOT_DURATION_MS,
 *          DASH_DURATION_MS, SYMBOL_GAP_MS, and CHARACTER_GAP_MS.
 *
 * @return  void
 */
void blink_morse(char ch)
{
    void blink_morse_container()
    {
        const char *morseCode = NULL;
        // Search for character's Morse code
        for (int i = 0; morseAlphabet[i].character != '\0'; i++)
        {
            if (morseAlphabet[i].character == ch)
            {
                morseCode = morseAlphabet[i].morse;
                break;
            }
        }

        // If character not found in Morse alphabet, return
        if (!morseCode)
            return;

        // Blink pattern
        for (int i = 0; morseCode[i] != '\0'; i++)
        {
            cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 1);
            if (morseCode[i] == '.')
            {
                // Short blink for dot
                sleep_ms(DOT_DURATION_MS);
            }
            else
            {
                // Long blink for dash
                sleep_ms(DASH_DURATION_MS);
            }
            cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 0);
            // Gap between symbols
            sleep_ms(SYMBOL_GAP_MS);
        }
    }
    blink_morse_container();
}

/**
 * Swaps the bytes in each word (16-bit) of a given block of memory.
 * This function is used for changing the endianness of data
 *
 * @param dest_ptr_word A pointer to the memory block where words will be swapped.
 * @param size_in_bytes The total size of the memory block in bytes.
 *
 * Note: The function assumes that 'size_in_bytes' is an even number, as it processes
 *       16-bit words. If 'size_in_bytes' is odd, the last byte will not be processed.
 */
void swap_words(void *dest_ptr_word, uint16_t size_in_bytes)
{
    uint16_t *word_ptr = (uint16_t *)dest_ptr_word;
    uint16_t total_words = size_in_bytes / 2;
    for (uint16_t j = 0; j < total_words; ++j)
    {
        uint16_t value = word_ptr[j];              // Read the current word once
        word_ptr[j] = (value << 8) | (value >> 8); // Swap the bytes and write back
    }
}

void null_words(void *dest_ptr_word, uint16_t size_in_bytes)
{
    memset(dest_ptr_word, 0, size_in_bytes);
}

int copy_firmware_to_RAM(uint16_t *emulROM, int emulROM_length)
{
    // Need to initialize the ROM4 section with the firmware data
    extern uint16_t __rom_in_ram_start__;
    // uint16_t *rom4_dest = &__rom_in_ram_start__;
    // volatile uint16_t *rom4_src = emulROM;
    // for (int i = 0; i < emulROM_length; i++)
    // {
    //     uint16_t value = *rom4_src++;
    //     *rom4_dest++ = value;
    // }
    memcpy(&__rom_in_ram_start__, emulROM, emulROM_length * sizeof(uint16_t));
    DPRINTF("Emulation firmware copied to RAM.\n");
    return 0;
}

int erase_firmware_from_RAM()
{
    // Need to initialize the ROM4 section with the firmware data
    extern uint16_t __rom_in_ram_start__;
    volatile uint32_t *rom4_dest = (uint32_t *)&__rom_in_ram_start__;
    for (int i = 0; i < ROM_SIZE_LONGWORDS * ROM_BANKS; i++)
    {
        *rom4_dest++ = 0x0;
    }
    DPRINTF("RAM for the firmware zeroed.\n");
    return 0;
}

void blink_error()
{
    // If we are here, something went wrong. Flash 'E' in morse code until pressed SELECT or RESET.
    while (1)
    {
        blink_morse('E');
        sleep_ms(1000);
        // If SELECT button is pressed, launch the configurator
        if (gpio_get(5) != 0)
        {
            // Ignore safe reboot here
            select_button_action(false, true);
        }
    }
}

// Don't forget to free the returned string after use!
char *bin_2_str(int number)
{
    int numBits = sizeof(number) * 8;
    char *binaryStr = malloc(numBits + 1); // Allocate memory for the binary string plus the null terminator
    if (binaryStr == NULL)
    {
        return NULL; // Return NULL if memory allocation fails
    }

    unsigned int mask = 1 << (numBits - 1);
    for (int i = 0; i < numBits; i++)
    {
        binaryStr[i] = (number & mask) ? '1' : '0'; // Use bitwise AND to check if the current bit is 1 or 0 and store in the string
        mask >>= 1;                                 // Shift the mask one bit to the right
    }
    binaryStr[numBits] = '\0'; // Null-terminate the string

    return binaryStr;
}

// Change endianess of a 32 bit value by swapping the words in the longword in memory
void set_and_swap_longword(uint32_t memory_address, uint32_t longword_value)
{
    uint16_t *address = (uint16_t *)(memory_address);
    address[0] = (longword_value >> 16) & 0xFFFF; // Most significant 16 bits
    address[1] = longword_value & 0xFFFF;         // Least significant 16 bit
}
