#include "include/config.h"

// We should define ALWAYS the default entries with valid values.
// DONT FORGET TO CHANGE MAX_ENTRIES if the number of value changes!
static ConfigEntry defaultEntries[MAX_ENTRIES] = {
    {"BOOT_FEATURE", TYPE_STRING, "CONFIGURATOR"},
    {"HOSTNAME", TYPE_STRING, "sidecart"},
    {"FLOPPIES_FOLDER", TYPE_STRING, "/floppies"},
    {"FLOPPY_IMAGE_A", TYPE_STRING, "gfa.st"},
    {"FLOPPY_IMAGE_B", TYPE_STRING, ""},
    {"ROMS_FOLDER", TYPE_STRING, "/roms"},
    {"ROMS_YAML_URL", TYPE_STRING, "http://roms.sidecart.xyz/roms.json"},
    {"WIFI_PASSWORD", TYPE_STRING, ""},
    {"WIFI_SSID", TYPE_STRING, ""},
    {"WIFI_AUTH", TYPE_INT, ""}};

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
        configData.entries[i] = defaultEntries[i];
        configData.count++;
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
        printf("No config found in FLASH. Using default values.\n");
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
        ConfigEntry *existingEntry = find_entry(entry.key);
        if (existingEntry)
        {
            // Overwrite the default value
            *existingEntry = entry;
        }
        // No else part here since we know every memory entry has a default
        count++;
    }
}

ConfigEntry *find_entry(const char key[MAX_KEY_LENGTH])
{
    for (size_t i = 0; i < configData.count; i++)
    {
        if (strcmp(configData.entries[i].key, key) == 0)
        {
            return &configData.entries[i];
        }
    }
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
        if (strcmp(configData.entries[i].key, key) == 0)
        {
            // Key already exists. Update its value and dataType
            configData.entries[i].dataType = dataType;
            strncpy(configData.entries[i].value, value, MAX_STRING_VALUE_LENGTH - 1);
            configData.entries[i].value[MAX_STRING_VALUE_LENGTH - 1] = '\0'; // Ensure null-termination
            return 0;                                                        // Successfully updated existing entry
        }
    }

    // If key doesn't exist, add a new entry
    strncpy(configData.entries[configData.count].key, key, MAX_KEY_LENGTH - 1);
    configData.entries[configData.count].key[MAX_KEY_LENGTH - 1] = '\0'; // Null terminate just in case
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

    uint32_t ints = save_and_disable_interrupts();

    uint8_t *address = (uint8_t *)(CONFIG_FLASH_OFFSET + XIP_BASE);

    // Ensure we don't exceed the reserved space
    if (configData.count * sizeof(ConfigEntry) > CONFIG_FLASH_SIZE)
    {
        return -1; // Error: Config size exceeds reserved space
    }

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
    printf("+--------------------------------+--------------------------------+----------+\n");
    printf("|              Key               |             Value              |   Type   |\n");
    printf("+--------------------------------+--------------------------------+----------+\n");

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

        printf("| %-30s | %-30s | %-8s |\n", configData.entries[i].key, valueStr, typeStr);
    }

    printf("+--------------------------------+--------------------------------+----------+\n");
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