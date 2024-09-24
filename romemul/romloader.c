/**
 * File: romloader.c
 * Author: Diego Parrilla Santamaría
 * Date: August 2023
 * Copyright: 2023 - GOODDATA LABS SL
 * Description: Load ROM files from SD card
 */

#include "include/romloader.h"

// Synchronous command variables
static uint16_t *payloadPtr = NULL;
static uint32_t random_token;

// Clean start command
static bool clean_start = false;

// Latest release
static bool latest_release = false;

// Microsd variables
static bool microsd_initialized = false;
static bool microsd_mounted = false;
static bool microsd_status = false;

// Filtered files list variables
static int filtered_num_local_files = 0;
static char **filtered_local_list = NULL;

// ROMs in sd card variables
static int list_roms = false;
static int rom_file_selected = -1;

// Floppy images in sd card variables
static bool list_floppies = false;
static int floppy_file_selected = -1;
static bool floppy_read_write = true;
static int floppy_drive = -1;

// Query floppy database variables
static bool query_floppy_db = false;
static char query_floppy_letter = 'a';
static FloppyImageInfo *floppy_images_files = NULL;
static int filtered_num_floppy_images_files = 0;
static int floppy_image_selected = -1;
static int floppy_image_selected_status = 0;

// Network variables
static RomInfo *network_files;
static int filtered_num_network_files = 0;

WifiNetworkAuthInfo *wifi_auth = NULL; // IF NULL, do not connect to any network
static char *wifi_password_file_content = NULL;
static bool persist_config = false;
static bool reset_default = false;
static bool scan_network = false;
static bool disconnect_network = false;
static bool restart_network = false;
static bool get_rom_catalog = false;

// ROMs in network variables
static int rom_network_selected = -1;
static char *rom_rescue_mode_file_content = NULL;

// Floppy header informationf for new images
static FloppyImageHeader floppy_header = {0};

// RTC boot variables
static bool rtc_boot = false;

// Harddisk boot variables
static bool gemdrive_boot = false;

// Config call
static bool get_config_call = false;

// Custom case-insensitive comparison function
static int compare_strings(const void *a, const void *b)
{
    const char *str1 = *(const char **)a;
    const char *str2 = *(const char **)b;

    while (*str1 && *str2 && tolower((unsigned char)*str1) == tolower((unsigned char)*str2))
    {
        str1++;
        str2++;
    }

    return tolower((unsigned char)*str1) - tolower((unsigned char)*str2);
}

// Check if the sd card is initalized and mounted and update the information of the folders
static void update_sd_status(FATFS *fs, SdCardData *sd_data_ptr)
{
    SdCardData *sd_data_local = malloc(sizeof(SdCardData));
    if (sd_data_local == NULL)
    {
        DPRINTF("Failed to allocate memory for sd_data_local\n");
        return;
    }

    ConfigEntry *is_fcount_enabled_conf = find_entry(PARAM_FILE_COUNT_ENABLED);
    bool is_fcount_enabled = (is_fcount_enabled_conf->value[0] == 't' || is_fcount_enabled_conf->value[0] == 'T') ? true : false;
    get_sdcard_data(fs, sd_data_local, sd_data_ptr, is_fcount_enabled);

    // Copy the content of sd_data_local to sd_data_ptr
    memcpy(sd_data_ptr, sd_data_local, sizeof(SdCardData));
    free(sd_data_local);
}

static void __not_in_flash_func(handle_protocol_command)(const TransmissionProtocol *protocol)
{
    ConfigEntry *entry = NULL;
    uint16_t value_payload = 0;
    uint8_t *memory_area = (uint8_t *)(ROM3_START_ADDRESS);
    // Handle the protocol
    switch (protocol->command_id)
    {
    case DOWNLOAD_ROM:
        // Download the ROM index passed as argument in the payload
        DPRINTF("Command DOWNLOAD_ROM (%i) received: %d\n", protocol->command_id, protocol->payload_size);
        random_token = ((*((uint32_t *)protocol->payload) & 0xFFFF0000) >> 16) | ((*((uint32_t *)protocol->payload) & 0x0000FFFF) << 16);
        value_payload = protocol->payload[4] | (protocol->payload[5] << 8);
        DPRINTF("Value: %d\n", value_payload);
        rom_network_selected = value_payload;
        break;
    case LOAD_ROM:
        // Load ROM passed as argument in the payload
        DPRINTF("Command LOAD_ROM (%i) received: %d\n", protocol->command_id, protocol->payload_size);
        random_token = ((*((uint32_t *)protocol->payload) & 0xFFFF0000) >> 16) | ((*((uint32_t *)protocol->payload) & 0x0000FFFF) << 16);
        value_payload = protocol->payload[4] | (protocol->payload[5] << 8);
        DPRINTF("Value: %d\n", value_payload);
        if (microsd_mounted)
        {
            rom_file_selected = value_payload;
        }
        else
        {
            DPRINTF("SD card not mounted. Cannot load ROM.\n");
            memset(memory_area, 0, CONFIGURATOR_SHARED_MEMORY_SIZE_BYTES);
        }
        break;
    case LIST_ROMS:
        // Get the list of roms in the SD card
        DPRINTF("Command LIST_ROMS (%i) received: %d\n", protocol->command_id, protocol->payload_size);
        random_token = ((*((uint32_t *)protocol->payload) & 0xFFFF0000) >> 16) | ((*((uint32_t *)protocol->payload) & 0x0000FFFF) << 16);
        if (!microsd_mounted)
        {
            DPRINTF("SD card not mounted. Cannot list ROMs.\n");
            memset(memory_area, 0, CONFIGURATOR_SHARED_MEMORY_SIZE_BYTES);
        }
        else
        {
            list_roms = true; // now the active loop should stop and list the ROMs
        }
        break;
    case GET_CONFIG:
        // Get the list of parameters in the device
        DPRINTF("Command GET_CONFIG (%i) received: %d\n", protocol->command_id, protocol->payload_size);
        random_token = ((*((uint32_t *)protocol->payload) & 0xFFFF0000) >> 16) | ((*((uint32_t *)protocol->payload) & 0x0000FFFF) << 16);
        get_config_call = true; // now the active loop should stop and get the config
        break;
    case PUT_CONFIG_STRING:
    {
        // Put a configuration string parameter in the device
        DPRINTF("Command PUT_CONFIG_STRING (%i) received: %d\n", protocol->command_id, protocol->payload_size);
        random_token = ((*((uint32_t *)protocol->payload) & 0xFFFF0000) >> 16) | ((*((uint32_t *)protocol->payload) & 0x0000FFFF) << 16);
        entry = malloc(sizeof(ConfigEntry));
        memcpy(entry, protocol->payload + RANDOM_SEED_SIZE, sizeof(ConfigEntry));
        swap_data((__uint16_t *)entry);
        char keyStr[MAX_KEY_LENGTH + 1] = {0};
        strncpy(keyStr, entry->key, MAX_KEY_LENGTH);
        DPRINTF("Key:%s - Value: %s\n", keyStr, entry->value);
        put_string(keyStr, entry->value);
        if (!strcmp(keyStr, PARAM_WIFI_COUNTRY))
        {
            restart_network = true;
        }
        *((volatile uint32_t *)(memory_area)) = random_token;
        break;
    }
    case PUT_CONFIG_INTEGER:
    {
        // Put a configuration integer parameter in the device
        DPRINTF("Command PUT_CONFIG_INTEGER (%i) received: %d\n", protocol->command_id, protocol->payload_size);
        random_token = ((*((uint32_t *)protocol->payload) & 0xFFFF0000) >> 16) | ((*((uint32_t *)protocol->payload) & 0x0000FFFF) << 16);
        entry = malloc(sizeof(ConfigEntry));
        memcpy(entry, protocol->payload + RANDOM_SEED_SIZE, sizeof(ConfigEntry));
        swap_data((__uint16_t *)entry);
        char keyStr[MAX_KEY_LENGTH + 1] = {0};
        strncpy(keyStr, entry->key, MAX_KEY_LENGTH);
        DPRINTF("Key:%s - Value: %s\n", keyStr, entry->value);
        put_integer(keyStr, atoi(entry->value));
        *((volatile uint32_t *)(memory_area)) = random_token;
        break;
    }
    case PUT_CONFIG_BOOL:
    {
        // Put a configuration boolean parameter in the device
        DPRINTF("Command PUT_CONFIG_BOOL (6) received: %d\n", protocol->payload_size);
        random_token = ((*((uint32_t *)protocol->payload) & 0xFFFF0000) >> 16) | ((*((uint32_t *)protocol->payload) & 0x0000FFFF) << 16);
        entry = malloc(sizeof(ConfigEntry));
        memcpy(entry, protocol->payload + RANDOM_SEED_SIZE, sizeof(ConfigEntry));
        swap_data((__uint16_t *)entry);
        char keyStr[MAX_KEY_LENGTH + 1] = {0};
        strncpy(keyStr, entry->key, MAX_KEY_LENGTH);
        DPRINTF("Key:%s - Value: %s\n", keyStr, entry->value);
        put_bool(keyStr, (strcmp(entry->value, "true") == 0) ? true : false);
        *((volatile uint32_t *)(memory_area)) = random_token;
        break;
    }
    case SAVE_CONFIG:
        // Save the current configuration in the FLASH of the device
        DPRINTF("Command SAVE_CONFIG (%i) received: %d\n", protocol->command_id, protocol->payload_size);
        random_token = ((*((uint32_t *)protocol->payload) & 0xFFFF0000) >> 16) | ((*((uint32_t *)protocol->payload) & 0x0000FFFF) << 16);
        persist_config = true; // now the active loop should stop and save the config
        break;
    case RESET_DEVICE:
        // Reset the device
        DPRINTF("Command RESET_DEVICE (%i) received: %d\n", protocol->command_id, protocol->payload_size);
        random_token = ((*((uint32_t *)protocol->payload) & 0xFFFF0000) >> 16) | ((*((uint32_t *)protocol->payload) & 0x0000FFFF) << 16);
        reset_default = true; // now the active loop should stop and reset the config
        break;
    case REBOOT:
        // Reboot the device
        DPRINTF("Command REBOOT (%i) received: %d\n", protocol->command_id, protocol->payload_size);
        reboot();
        break;
    case LAUNCH_SCAN_NETWORKS:
        // Scan the networks and return the results
        DPRINTF("Command LAUNCH_SCAN_NETWORKS (%i) received: %d\n", protocol->command_id, protocol->payload_size);
        random_token = ((*((uint32_t *)protocol->payload) & 0xFFFF0000) >> 16) | ((*((uint32_t *)protocol->payload) & 0x0000FFFF) << 16);
        scan_network = true; // now the active loop should stop and scan the networks
        *((volatile uint32_t *)(memory_area)) = random_token;
        break;
    case GET_SCANNED_NETWORKS:
        // Get the results of the scanned networks
        DPRINTF("Command GET_SCANNED_NETWORKS (%i) received: %d\n", protocol->command_id, protocol->payload_size);
        random_token = ((*((uint32_t *)protocol->payload) & 0xFFFF0000) >> 16) | ((*((uint32_t *)protocol->payload) & 0x0000FFFF) << 16);
        memcpy(memory_area + RANDOM_SEED_SIZE, &wifiScanData, sizeof(wifiScanData));
        network_swap_data((__uint16_t *)(memory_area + RANDOM_SEED_SIZE), wifiScanData.count);
        *((volatile uint32_t *)(memory_area)) = random_token;
        break;
    case CONNECT_NETWORK:
        // Put a configuration string parameter in the device
        DPRINTF("Command CONNECT_NETWORK (%i) received: %d\n", protocol->command_id, protocol->payload_size);
        wifi_auth = malloc(sizeof(WifiNetworkAuthInfo));
        memcpy(wifi_auth, protocol->payload, sizeof(WifiNetworkAuthInfo));
        network_swap_auth_data((__uint16_t *)wifi_auth);
        DPRINTF("SSID:%s - Pass: %s - Auth: %d\n", wifi_auth->ssid, wifi_auth->password, wifi_auth->auth_mode);
        break;
    case GET_IP_DATA:
        // Get IPv4 and IPv6 and SSID info
        DPRINTF("Command GET_IP_DATA (%i) received: %d\n", protocol->command_id, protocol->payload_size);
        random_token = ((*((uint32_t *)protocol->payload) & 0xFFFF0000) >> 16) | ((*((uint32_t *)protocol->payload) & 0x0000FFFF) << 16);
        ConnectionData *connection_data = malloc(sizeof(ConnectionData));
        get_connection_data(connection_data);
        memcpy(memory_area + RANDOM_SEED_SIZE, connection_data, sizeof(ConnectionData));
        network_swap_connection_data((__uint16_t *)(memory_area + RANDOM_SEED_SIZE));
        free(connection_data);
        *((volatile uint32_t *)(memory_area)) = random_token;
        break;
    case DISCONNECT_NETWORK:
        // Disconnect from the network
        DPRINTF("Command DISCONNECT_NETWORK (%i) received: %d\n", protocol->command_id, protocol->payload_size);
        disconnect_network = true; // now in the active loop should stop and disconnect from the network
        break;
    case GET_ROMS_JSON_FILE:
        // Download the JSON file of the ROMs from the URL
        DPRINTF("Command GET_ROMS_JSON_FILE (%i) received: %d\n", protocol->command_id, protocol->payload_size);
        random_token = ((*((uint32_t *)protocol->payload) & 0xFFFF0000) >> 16) | ((*((uint32_t *)protocol->payload) & 0x0000FFFF) << 16);
        get_rom_catalog = true; // now in the active loop should stop and download the JSON file
        break;
    case LOAD_FLOPPY_RO:
        // Load the floppy image in ro mode passed as argument in the payload
        DPRINTF("Command LOAD_FLOPPY_RO (%i) received: %d\n", protocol->command_id, protocol->payload_size);
        random_token = ((*((uint32_t *)protocol->payload) & 0xFFFF0000) >> 16) | ((*((uint32_t *)protocol->payload) & 0x0000FFFF) << 16);
        if (microsd_mounted)
        {
            floppy_file_selected = protocol->payload[4] | (protocol->payload[5] << 8);
            floppy_read_write = false;
            floppy_drive = protocol->payload[6] | (protocol->payload[7] << 8);
        }
        else
        {
            DPRINTF("SD card not mounted. Cannot load ROM.\n");
            memset(memory_area, 0, CONFIGURATOR_SHARED_MEMORY_SIZE_BYTES);
        }
        break;
    case LOAD_FLOPPY_RW:
        // Load the floppy image in rw mode passed as argument in the payload
        DPRINTF("Command LOAD_FLOPPY_RW (%i) received: %d\n", protocol->command_id, protocol->payload_size);
        random_token = ((*((uint32_t *)protocol->payload) & 0xFFFF0000) >> 16) | ((*((uint32_t *)protocol->payload) & 0x0000FFFF) << 16);
        if (microsd_mounted)
        {
            floppy_file_selected = protocol->payload[4] | (protocol->payload[5] << 8);
            floppy_read_write = true;
            floppy_drive = protocol->payload[6] | (protocol->payload[7] << 8);
        }
        else
        {
            DPRINTF("SD card not mounted. Cannot load ROM.\n");
            memset(memory_area, 0, CONFIGURATOR_SHARED_MEMORY_SIZE_BYTES);
        }
        break;
    case LIST_FLOPPIES:
        // Get the list of floppy images in the SD card
        DPRINTF("Command LIST_FLOPPIES (%i) received: %d\n", protocol->command_id, protocol->payload_size);
        random_token = ((*((uint32_t *)protocol->payload) & 0xFFFF0000) >> 16) | ((*((uint32_t *)protocol->payload) & 0x0000FFFF) << 16);
        if (!microsd_mounted)
        {
            DPRINTF("SD card not mounted. Cannot list Floppies.\n");
            memset(memory_area, 0, CONFIGURATOR_SHARED_MEMORY_SIZE_BYTES);
        }
        else
        {
            list_floppies = true; // now the active loop should stop and list the floppy images
        }
        break;
    case QUERY_FLOPPY_DB:
        // Get the list of floppy images for a given letter from the Atari ST Databse
        DPRINTF("Command QUERY_FLOPPY_DB (%i) received: %d\n", protocol->command_id, protocol->payload_size);
        random_token = ((*((uint32_t *)protocol->payload) & 0xFFFF0000) >> 16) | ((*((uint32_t *)protocol->payload) & 0x0000FFFF) << 16);
        payloadPtr = (uint16_t *)protocol->payload + 2;
        query_floppy_letter = (char)(*(payloadPtr) & 0xFF);

        // Convert to lowercase if it's an uppercase letter
        if (query_floppy_letter >= 'A' && query_floppy_letter <= 'Z')
        {
            query_floppy_letter += 'a' - 'A';
        }

        DPRINTF("Random token: %x\n", random_token);
        DPRINTF("Letter: %c\n", query_floppy_letter);
        query_floppy_db = true;
        break;
    case DOWNLOAD_FLOPPY:
        // Download the floppy image passed as argument in the payload
        DPRINTF("Command DOWNLOAD_FLOPPY (%i) received: %d\n", protocol->command_id, protocol->payload_size);
        random_token = ((*((uint32_t *)protocol->payload) & 0xFFFF0000) >> 16) | ((*((uint32_t *)protocol->payload) & 0x0000FFFF) << 16);
        value_payload = protocol->payload[4] | (protocol->payload[5] << 8);
        DPRINTF("Value: %d\n", value_payload);
        floppy_image_selected_status = 0; // Reset the status
        if (microsd_mounted)
        {
            floppy_image_selected = value_payload;
        }
        else
        {
            DPRINTF("SD card not mounted. Cannot save the image to download.\n");
            memset(memory_area, 0, CONFIGURATOR_SHARED_MEMORY_SIZE_BYTES);
            floppy_image_selected_status = 1; // Error: SD card not mounted
            floppy_image_selected = 0;
        }
        break;
    case GET_SD_DATA:
        // Get the SD card data
        DPRINTF("Command GET_SD_DATA (%i) received: %d\n", protocol->command_id, protocol->payload_size);
        random_token = ((*((uint32_t *)protocol->payload) & 0xFFFF0000) >> 16) | ((*((uint32_t *)protocol->payload) & 0x0000FFFF) << 16);
        microsd_status = true;
        break;
    case GET_LATEST_RELEASE:
        // Get the latest release from the url given
        DPRINTF("Command GET_LATEST_RELEASE (%i) received: %d\n", protocol->command_id, protocol->payload_size);
        random_token = ((*((uint32_t *)protocol->payload) & 0xFFFF0000) >> 16) | ((*((uint32_t *)protocol->payload) & 0x0000FFFF) << 16);
        latest_release = true;
        break;
    case CREATE_FLOPPY:
        // Create an empty floppy image based in a template
        DPRINTF("Command CREATE_FLOPPY (%i) received: %d\n", protocol->command_id, protocol->payload_size);
        random_token = ((*((uint32_t *)protocol->payload) & 0xFFFF0000) >> 16) | ((*((uint32_t *)protocol->payload) & 0x0000FFFF) << 16);
        floppy_header.template = protocol->payload[4] | (protocol->payload[5] << 8);
        DPRINTF("Template: %d\n", floppy_header.template);
        floppy_header.num_tracks = protocol->payload[6] | (protocol->payload[7] << 8);
        DPRINTF("Num tracks: %d\n", floppy_header.num_tracks);
        floppy_header.num_sectors = protocol->payload[8] | (protocol->payload[9] << 8);
        DPRINTF("Num sectors: %d\n", floppy_header.num_sectors);
        floppy_header.num_sides = protocol->payload[10] | (protocol->payload[11] << 8);
        DPRINTF("Num sides: %d\n", floppy_header.num_sides);
        floppy_header.overwrite = protocol->payload[12] | (protocol->payload[13] << 8);
        DPRINTF("Overwrite: %d\n", floppy_header.overwrite);
        CHANGE_ENDIANESS_BLOCK16(&protocol->payload[sizeof(floppy_header.volume_name)], (sizeof(floppy_header.volume_name) + sizeof(floppy_header.floppy_name)));
        // Now read the volume name until a zero is found
        int i = 0;
        for (i = 0; i < 14; i++)
        {
            floppy_header.volume_name[i] = protocol->payload[14 + i];
            if (floppy_header.volume_name[i] == 0)
            {
                break;
            }
        }
        DPRINTF("Volume name: %s\n", floppy_header.volume_name);
        // Now read the floppy name in the SidecarT until a zero is found
        for (i = 0; i < 256; i++)
        {
            floppy_header.floppy_name[i] = protocol->payload[28 + i];
            if (floppy_header.floppy_name[i] == 0)
            {
                break;
            }
        }
        DPRINTF("Floppy name: %s\n", floppy_header.floppy_name);
        break;
    case BOOT_RTC:
        // Boot RTC emulator
        DPRINTF("Command BOOT_RTC (%i) received: %d\n", protocol->command_id, protocol->payload_size);
        random_token = ((*((uint32_t *)protocol->payload) & 0xFFFF0000) >> 16) | ((*((uint32_t *)protocol->payload) & 0x0000FFFF) << 16);
        rtc_boot = true; // now in the active loop should stop and boot the RTC emulator
        break;
    case BOOT_GEMDRIVE:
        // Boot GEMDRIVE emulator
        DPRINTF("Command BOOT_GEMDRIVE (%i) received: %d\n", protocol->command_id, protocol->payload_size);
        random_token = ((*((uint32_t *)protocol->payload) & 0xFFFF0000) >> 16) | ((*((uint32_t *)protocol->payload) & 0x0000FFFF) << 16);
        gemdrive_boot = true; // now in the active loop should stop and boot the RTC emulator
        break;
    case CLEAN_START:
        // Start the configurator when the app starts
        DPRINTF("Command CLEAN_START (%i) received: %d\n", protocol->command_id, protocol->payload_size);
        clean_start = true;
        break;

    // ... handle other commands
    default:
        DPRINTF("Unknown command: %d\n", protocol->command_id);
    }
}

// Interrupt handler callback for DMA completion
void __not_in_flash_func(dma_irq_handler_lookup_callback)(void)
{
    // Read the address to process
    uint32_t addr = (uint32_t)dma_hw->ch[lookup_data_rom_dma_channel].al3_read_addr_trig;

    // Avoid priting anything inside an IRQ handled function
    // DPRINTF("DMA LOOKUP: $%x\n", addr);
    if (addr >= ROM3_START_ADDRESS)
    {
        parse_protocol((uint16_t)(addr & 0xFFFF), handle_protocol_command);
    }

    // Clear the interrupt request for the channel
    dma_hw->ints1 = 1u << lookup_data_rom_dma_channel;
}

int delete_FLASH(void)
{
    // Erase the content before loading the new file. It seems that
    // overwriting it's not enough
    DPRINTF("Erasing FLASH...\n");
    flash_range_erase(FLASH_ROM_LOAD_OFFSET, (ROM_SIZE_BYTES * 2) - 1); // Two banks of 64K
    DPRINTF("FLASH erased.\n");
    return 0;
}

int init_firmware()
{
    // Hybrid way to initialize the ROM emulator:
    // IRQ handler callback to read the commands in ROM3, and NOT copy the FLASH ROMs to RAM
    // and start the state machine
    init_romemul(NULL, dma_irq_handler_lookup_callback, false);

    // Copy the firmware to RAM
    COPY_FIRMWARE_TO_RAM((uint16_t *)firmwareROM, firmwareROM_length);

    // Reserve memory for the protocol parser
    init_protocol_parser();

    DPRINTF("Ready to accept commands.\n");

    DPRINTF("\033[2J\033[H"); // Clear Screen
    DPRINTF("\n> ");
    DPRINTF("Initializing Configurator...\n"); // Always this print message to the console
    stdio_flush();

    // Print the config
    print_config_table();

    FRESULT fr;
    FATFS fs;
    int num_files = 0;
    char **file_list = NULL;

    network_init();

    // Initialize SD card
    microsd_initialized = sd_init_driver();
    if (!microsd_initialized)
    {
        DPRINTF("ERROR: Could not initialize SD card\r\n");
    }

    if (microsd_initialized)
    {
        // Mount drive
        microsd_mounted = is_sdcard_mounted(&fs);
    }

    // Copy the content of the file list to the end of the ROM4 memory minus 4Kbytes
    // Translated to pure ROM4 address of the ST: 0xFB0000 - 0x1000 = 0xFAF000
    // The firmware code should be able to read the list of files from 0xFAF000
    // To select the desired ROM from the list, the ST code should send the command
    // LOAD_ROM with the number of the ROM to load PLUS 1. For example, to load the
    // first ROM in the list, the ST code should send the command LOAD_ROM with the
    // value 1 (0 + 1) because the first ROM of the index  is 0.
    // x=PEEK(&HFBABCD) 'Magic header number of commands
    // x=PEEK(&HFB0001) 'Command LOAD_ROM
    // x=PEEK(&HFB0002) 'Size of the payload (always even numbers)
    // x=PEEK(&HFB0001) 'Payload (two bytes per word)

    uint8_t *memory_area = (uint8_t *)(ROM3_START_ADDRESS);

    // Here comes the tricky part. We have to put in the higher section of the ROM4 memory the content
    // of the file list available in the SD card.
    // The structure is a list of chars separated with a 0x00 byte. The end of the list is marked with
    // two 0x00 bytes.

    // Configure polling times
    u_int64_t wifi_scan_poll_counter_mcs = 0; // Force the first scan to be done in the loop
    u_int16_t wifi_scan_poll_counter = get_wifi_scan_poll_secs();
    u_int32_t network_status_polling_ms = get_network_status_polling_ms();

    SdCardData sd_data = {0}; // Lazy initalization
    if (microsd_mounted)
    {
        FRESULT err = read_and_trim_file(WIFI_PASS_FILE_NAME, &wifi_password_file_content, MAX_WIFI_PASSWORD_LENGTH);
        if (err == FR_OK)
        {
            DPRINTF("Wifi password file found. Content: %s\n", wifi_password_file_content);
        }
        else
        {
            DPRINTF("Wifi password file not found.\n");
        }
    }

    if (microsd_mounted)
    {
        FRESULT err = read_and_trim_file(ROM_RESCUE_MODE_FILE_NAME, &rom_rescue_mode_file_content, MAX_RESCUE_ROM_NAME_LENGTH);
        if (err == FR_OK)
        {
            DPRINTF("ROM rescue mode file found. Content: %s\n", rom_rescue_mode_file_content);
        }
        else
        {
            DPRINTF("ROM rescue mode file not found.\n");
        }
    }

    // Start the network.
    network_connect(false, NETWORK_CONNECTION_ASYNC, &wifi_password_file_content);

    bool show_blink_code = true;
    while (!clean_start)
    {
        // Wait until the clean start command is received
        tight_loop_contents();
        if (show_blink_code)
        {
            show_blink_code = false;
            // The "C" character stands for "Configurator"
            blink_morse('C');
        }
        sleep_ms(100);
#if PICO_CYW43_ARCH_POLL
        cyw43_arch_lwip_begin();
        network_poll();
        cyw43_arch_wait_for_work_until(make_timeout_time_ms(1));
        cyw43_arch_lwip_end();
#elif PICO_CYW43_ARCH_THREADSAFE_BACKGROUND
        cyw43_arch_lwip_begin();
        cyw43_arch_wait_for_work_until(make_timeout_time_ms(10));
        cyw43_arch_lwip_end();
#endif
    }

    u_int32_t network_poll_counter = 0;
    u_int32_t storage_poll_counter = 0;
    while ((rom_file_selected < 0) &&
           (rom_network_selected < 0) &&
           (!reset_default) && (!rtc_boot) && (!gemdrive_boot) &&
           (rom_rescue_mode_file_content == NULL))
    {
        tight_loop_contents();

#if PICO_CYW43_ARCH_POLL
        network_poll();
#endif
        cyw43_arch_lwip_begin();
        cyw43_arch_lwip_check();
        cyw43_arch_lwip_end();
        //        cyw43_arch_wait_for_work_until(make_timeout_time_ms(1));

        if ((time_us_64() - wifi_scan_poll_counter_mcs) > (wifi_scan_poll_counter * 1000000))
        {
            if (get_network_connection_status() == DISCONNECTED)
            {
                DPRINTF("Wifi scan polling...\n");
                network_scan();
                wifi_scan_poll_counter = get_wifi_scan_poll_secs();
                wifi_scan_poll_counter_mcs = time_us_64();
            }
        }
        if (wifi_auth != NULL)
        {
            DPRINTF("Connecting to network...\n");
            put_string(PARAM_WIFI_SSID, wifi_auth->ssid);
            put_string(PARAM_WIFI_PASSWORD, wifi_auth->password);
            put_integer(PARAM_WIFI_AUTH, wifi_auth->auth_mode);
            write_all_entries();

            network_connect(true, NETWORK_CONNECTION_ASYNC, &wifi_password_file_content);
            free(wifi_auth);
            wifi_auth = NULL;
        }
        if (restart_network)
        {
            restart_network = false;
            // Force  network disconnection
            network_disconnect();
            // Need to deinit and init again the full network stack to be able to scan again
            cyw43_arch_deinit();
            cyw43_arch_init();
            network_init();

            network_connect(false, NETWORK_CONNECTION_ASYNC, &wifi_password_file_content);
        }
        if (disconnect_network)
        {
            disconnect_network = false;
            // Force  network disconnection
            network_disconnect();
            // Need to deinit and init again the full network stack to be able to scan again
            cyw43_arch_deinit();
            cyw43_arch_init();
            network_init();

            network_scan();

            // Clean the credentials configuration
            put_string(PARAM_WIFI_SSID, "");
            put_string(PARAM_WIFI_PASSWORD, "");
            put_integer(PARAM_WIFI_AUTH, 0);
            write_all_entries();
        }
        if (network_poll_counter == 0)
        {
            if (strlen(find_entry(PARAM_WIFI_SSID)->value) > 0)
            {
                // Only display when changes status to avoid flooding the console
                ConnectionStatus previous_status = get_previous_connection_status();
                ConnectionStatus current_status = get_network_connection_status();
                DPRINTF("Network status: %d\n", current_status);
                if (current_status != previous_status)
                {
                    DPRINTF("Network status: %d\n", current_status);
                    DPRINTF("Network previous status: %d\n", previous_status);
                    ConnectionData *connection_data = malloc(sizeof(ConnectionData));
                    get_connection_data(connection_data);
                    show_connection_data(connection_data);
                    free(connection_data);
                    if (current_status == BADAUTH_ERROR)
                    {
                        DPRINTF("Bad authentication. Should enter again the credentials...\n");
                        network_disconnect();

                        // Need to deinit and init again the full network stack to be able to scan again
                        cyw43_arch_deinit();
                        cyw43_arch_init();
                        network_init();

                        network_scan();

                        // Clean the credentials configuration
                        put_string(PARAM_WIFI_SSID, "");
                        put_string(PARAM_WIFI_PASSWORD, "");
                        put_integer(PARAM_WIFI_AUTH, 0);
                        write_all_entries();

                        // Start the network.
                        network_connect(false, NETWORK_CONNECTION_ASYNC, &wifi_password_file_content);
                    }
                }
                else if ((current_status >= TIMEOUT_ERROR) && (current_status <= INSUFFICIENT_RESOURCES_ERROR))
                {
                    DPRINTF("Connection failed. Resetting network...\n");
                    network_disconnect();

                    // Need to deinit and init again the full network stack to be able to scan again
                    cyw43_arch_deinit();
                    cyw43_arch_init();
                    network_init();

                    network_scan();
                    // Start the network.
                    network_connect(true, NETWORK_CONNECTION_ASYNC, &wifi_password_file_content);
                }
            }
        }
        if (storage_poll_counter == 0)
        {
            update_sd_status(&fs, &sd_data);
        }
        if (get_config_call)
        {
            get_config_call = false;
            memcpy(memory_area + RANDOM_SEED_SIZE, &configData, sizeof(configData));

            // Swap the keys and values section bytes in the words
            // The endians conversions should be done always in the rp2040 side to relief
            // the ST side from this task
            uint16_t *dest_ptr = (uint16_t *)(memory_area + sizeof(__uint32_t) + RANDOM_SEED_SIZE); // Bypass magic number and random size
            DPRINTF("ConfigData count: %d\n", configData.count);
            for (int i = 0; i < configData.count; i++)
            {
                swap_data(dest_ptr);
                dest_ptr += sizeof(ConfigEntry) / 2;
            }
            *((volatile uint32_t *)(memory_area)) = random_token;
        }
        if (persist_config)
        {
            persist_config = false;
            DPRINTF("Saving configuration to FLASH\n");
            write_all_entries();
            *((volatile uint32_t *)(memory_area)) = random_token;
        }

        if (microsd_status)
        {
            microsd_status = false;
            update_sd_status(&fs, &sd_data);

            memcpy(memory_area + RANDOM_SEED_SIZE, &sd_data, sizeof(SdCardData));
            SdCardData *sd_data_mem = (SdCardData *)(memory_area + RANDOM_SEED_SIZE);
            sd_data_mem->roms_folder_count = (sd_data.roms_folder_count >> 16) | (sd_data.roms_folder_count << 16);
            sd_data_mem->floppies_folder_count = (sd_data.floppies_folder_count >> 16) | (sd_data.floppies_folder_count << 16);
            sd_data_mem->harddisks_folder_count = (sd_data.harddisks_folder_count >> 16) | (sd_data.harddisks_folder_count << 16);
            sd_data_mem->sd_free_space = (sd_data.sd_free_space >> 16) | (sd_data.sd_free_space << 16);
            sd_data_mem->sd_size = (sd_data.sd_size >> 16) | (sd_data.sd_size << 16);

            CHANGE_ENDIANESS_BLOCK16(memory_area + RANDOM_SEED_SIZE, MAX_FOLDER_LENGTH * 3);

            *((volatile uint32_t *)(memory_area)) = random_token;
        }

        if (latest_release)
        {
            latest_release = false;
            memset(memory_area + RANDOM_SEED_SIZE, 0, CONFIGURATOR_SHARED_MEMORY_SIZE_BYTES - RANDOM_SEED_SIZE);
            char *latest_version = get_latest_release();
            if (latest_version != NULL)
            {
                DPRINTF("Current version: %s\n", RELEASE_VERSION);
                DPRINTF("Latest version: %s\n", latest_version);
                if (strcmp(RELEASE_VERSION, latest_version) != 0)
                {
                    DPRINTF("New version available: %s\n", latest_version);
                    strcpy((char *)(memory_area + RANDOM_SEED_SIZE), latest_version);
                    // Convert to motorla endian
                    CHANGE_ENDIANESS_BLOCK16(memory_area + RANDOM_SEED_SIZE, strlen(latest_version));
                }
                else
                {
                    DPRINTF("No new version available.\n");
                }
            }
            if (latest_version != NULL)
            {
                free(latest_version);
            }
            *((volatile uint32_t *)(memory_area)) = random_token;
        }

        // Download the json file
        if (get_rom_catalog)
        {
            get_rom_catalog = false;

            // Free dynamically allocated memory first just in case
            if (filtered_num_network_files > 0)
            {
                RomInfo *current = network_files;
                // Free dynamically allocated memory
                while (current != NULL)
                {
                    RomInfo *next = current->next;
                    freeRomItem(current);
                    free(current);
                    current = next;
                }
            }

            // Clean memory space
            memset(memory_area + RANDOM_SEED_SIZE, 0, CONFIGURATOR_SHARED_MEMORY_SIZE_BYTES - RANDOM_SEED_SIZE);

            // Get the URL from the configuration
            // char *url = find_entry(PARAM_ROMS_YAML_URL)->value;
            char *url = find_entry(PARAM_ROMS_CSV_URL)->value;

            // The the JSON file info
            err_t err = get_rom_catalog_file(&network_files, &filtered_num_network_files, url);
            if (err == ERR_OK)
            {
                // Iterate over the RomInfo items and populate the names array
                char *dest_ptr = (char *)(memory_area + RANDOM_SEED_SIZE);
                RomInfo *current = network_files;
                for (int i = 0; i < filtered_num_network_files; i++)
                {
                    DPRINTF("Name: %s, tags: %s, size: %d\n", current->name, current->tags, current->size_kb);
                    // Ensure the name is padded with spaces up to position 60
                    char padded_name[51]; // 50 characters for padding + 1 for null terminator
                    snprintf(padded_name, sizeof(padded_name), "%-50s", current->name);

                    char padded_tags[26]; // 25 characters for padding + 1 for null terminator
                    snprintf(padded_tags, sizeof(padded_tags), "%-25s", current->tags);

                    char padded_size[6]; // 5 characters for padding + 1 for null terminator
                    snprintf(padded_size, sizeof(padded_size), "%5d", current->size_kb);

                    // Display padded content
                    sprintf(dest_ptr, "%s%s%s\0", padded_name, padded_tags, padded_size);
                    dest_ptr += strlen(dest_ptr) + 1;

                    current = current->next;
                }
                // If dest_ptr is odd, add a 0x00 byte to align the next string
                if ((uintptr_t)dest_ptr & 1)
                {
                    *dest_ptr++ = 0x00;
                } // Add an additional 0x00 word to mark the end of the list
                *dest_ptr++ = 0x00;
                *dest_ptr++ = 0x00;

                // Swap the words to motorola endian format: BIG ENDIAN
                CHANGE_ENDIANESS_BLOCK16(memory_area + RANDOM_SEED_SIZE, CONFIGURATOR_SHARED_MEMORY_SIZE_BYTES - RANDOM_SEED_SIZE);
            }
            else
            {
                DPRINTF("Error getting the ROM catalog: %d\n", err);
            }

            *((volatile uint32_t *)(memory_area)) = random_token;
        }

        // List the ROM images in the SD card
        if (list_roms)
        {
            list_roms = false;
            // Show the root directory content (ls command)
            char *dir = find_entry(PARAM_ROMS_FOLDER)->value;
            if (strlen(dir) == 0)
            {
                dir = "";
            }
            DPRINTF("ROM images folder: %s\n", dir);
            file_list = show_dir_files(dir, &num_files);

            // Remove hidden files from the list
            const char *allowed_extensions[] = {"img", "bin", "stc", "rom"};

            filtered_local_list = filter(file_list, num_files, &filtered_num_local_files, allowed_extensions, 4);
            // Sort remaining valid filenames lexicographically
            qsort(filtered_local_list, filtered_num_local_files, sizeof(char *), compare_strings);
            // Store the list in the ROM memory space
            store_file_list(filtered_local_list, filtered_num_local_files, (memory_area + RANDOM_SEED_SIZE));

            *((volatile uint32_t *)(memory_area)) = random_token;
        }

        // List the floppy images in the SD card
        if (list_floppies)
        {
            list_floppies = false;
            // Show the root directory content (ls command)
            char *dir = find_entry(PARAM_FLOPPIES_FOLDER)->value;
            if (strlen(dir) == 0)
            {
                dir = "";
            }
            DPRINTF("Floppy images folder: %s\n", dir);
            // Get the list of floppy image files in the directory
            file_list = show_dir_files(dir, &num_files);

            // Remove hidden files from the list
            const char *allowed_extensions[] = {"st", "msa", "rw"};
            filtered_local_list = filter(file_list, num_files, &filtered_num_local_files, allowed_extensions, 3);
            // Sort remaining valid filenames lexicographically
            qsort(filtered_local_list, filtered_num_local_files, sizeof(char *), compare_strings);
            // Store the list in the ROM memory space
            store_file_list(filtered_local_list, filtered_num_local_files, (memory_area + RANDOM_SEED_SIZE));

            *((volatile uint32_t *)(memory_area)) = random_token;
        }

        // Query the Atari ST Database for the list of floppy images for a given letter
        if (query_floppy_db)
        {
            query_floppy_db = false;

            // Free dynamically allocated memory first
            while (floppy_images_files != NULL)
            {
                FloppyImageInfo *current = floppy_images_files;
                floppy_images_files = floppy_images_files->next; // Move to the next item before freeing the current one

                // Free each dynamically allocated string in the structure
                free(current->name);
                free(current->status);
                free(current->description);
                free(current->tags);
                free(current->extra);
                free(current->url);

                // Free the current structure
                free(current);
            }
            floppy_images_files = NULL;

            dma_channel_set_irq1_enabled(lookup_data_rom_dma_channel, false);

            // Get the URL from the configuration
            char *base_url = find_entry(PARAM_FLOPPY_DB_URL)->value;

            // Ensure that the buffer is large enough for the original URL, the `/db/`, the letter, `.csv`, and the null terminator.
            char url[256]; // Adjust the size as needed based on the maximum length of base_url.

            sprintf(url, "%s/db/%c.csv", base_url, query_floppy_letter);

            err_t res = get_floppy_db_files(&floppy_images_files, &filtered_num_floppy_images_files, url);

            dma_channel_set_irq1_enabled(lookup_data_rom_dma_channel, true);

            // Demonstrate the results
            // for (int i = 0; i < filtered_num_floppy_images_files; i++)
            // {
            //     DPRINTF("Name: %s, Status: %s, Description: %s, Tags: %s, Extra: %s, URL: %s\n",
            //             floppy_images_files[i].name, floppy_images_files[i].status, floppy_images_files[i].description,
            //             floppy_images_files[i].tags, floppy_images_files[i].extra, floppy_images_files[i].url);
            // }

            memset(memory_area + RANDOM_SEED_SIZE, 0, CONFIGURATOR_SHARED_MEMORY_SIZE_BYTES - RANDOM_SEED_SIZE); // Clean the memory area except the random token
            if (res == ERR_OK)
            {
                if (filtered_num_floppy_images_files > 0)
                {
                    // Iterate over the RomInfo items and populate the names array
                    char *dest_ptr = (char *)(memory_area + 4); // Bypass random token
                    // Get the first element, if any
                    FloppyImageInfo *floppy_images_file = floppy_images_files;
                    for (int i = 0; i < filtered_num_floppy_images_files; i++)
                    {
                        // Copy the string from network_files[i].name to dest_ptr
                        // Ensure the name is padded with spaces up to position 60
                        char padded_name[61]; // 60 characters for padding + 1 for null terminator
                        snprintf(padded_name, sizeof(padded_name), "%-52s", floppy_images_file->name);

                        char padded_extra[16]; // 15 characters for padding + 1 for null terminator
                        snprintf(padded_extra, sizeof(padded_extra), "%-15s", floppy_images_file->extra);

                        char padded_filename[13]; // 12 characters for padding + 1 for null terminator
                        // Get the filename from the URL
                        char fname[256] = {0};
                        extract_filename(floppy_images_file->url, fname);
                        snprintf(padded_filename, sizeof(padded_filename), "%-13s", fname);

                        // Display padded content
                        sprintf(dest_ptr, "%s%s %s\0", padded_name, padded_extra, padded_filename);
                        dest_ptr += strlen(dest_ptr) + 1;
                        floppy_images_file = floppy_images_file->next;
                    }

                    // Swap the words to motorola endian format: BIG ENDIAN
                    CHANGE_ENDIANESS_BLOCK16(memory_area + RANDOM_SEED_SIZE, CONFIGURATOR_SHARED_MEMORY_SIZE_BYTES - RANDOM_SEED_SIZE);
                }
                else
                {
                    DPRINTF("No floppy images found for letter %c\n", query_floppy_letter);
                }
            }
            else
            {
                DPRINTF("Error getting floppy images from the Atari ST Database: %d\n", res);
            }

            DPRINTF("Random token: %x\n", random_token);

            *((volatile uint32_t *)(memory_area)) = random_token;
        }

        if (floppy_header.template > 0)
        {
            // Append to the floppy_header floppy_name the extension .st.rw
            char *dest_ptr = floppy_header.floppy_name;
            while (*dest_ptr != 0)
            {
                dest_ptr++;
            }
            strcpy(dest_ptr, ".st.rw");
            DPRINTF("Floppy file to create: %s\n", floppy_header.floppy_name);
            char *dir = find_entry(PARAM_FLOPPIES_FOLDER)->value;
            DPRINTF("Floppy folder: %s\n", dir);
            FRESULT err = create_blank_ST_image(dir,
                                                floppy_header.floppy_name,
                                                floppy_header.num_tracks,
                                                floppy_header.num_sectors,
                                                floppy_header.num_sides,
                                                floppy_header.volume_name,
                                                floppy_header.overwrite);
            if (err != FR_OK)
            {
                DPRINTF("Create blank ST image error: %d\n", err);
            }
            else
            {
                DPRINTF("Created blank ST image OK\n");
            }
            floppy_header.template = 0;
            *((volatile uint32_t *)(memory_area)) = random_token;
        }

        if (floppy_image_selected > 0)
        {
            char *extract_filename(char *path)
            {
                char *last_slash = strrchr(path, '/'); // Find the last occurrence of '/'

                // If '/' was found, return the string after it
                if (last_slash && *(last_slash + 1))
                {
                    return last_slash + 1;
                }
                return path; // Return the original path if '/' wasn't found
            }
            DPRINTF("Floppy image selected to download: %d\n", floppy_image_selected);
            FloppyImageInfo remote = *floppy_images_files;
            for (int i = 0; i < floppy_image_selected - 1; i++)
            {
                remote = *(FloppyImageInfo *)remote.next;
            }
            char *remote_name = remote.name;
            char *remote_uri = remote.url;

            char full_url[512];
            // Get the URL from the configuration
            char *base_url = find_entry("FLOPPY_DB_URL")->value;

            char *dest_filename = extract_filename(remote.url);
            char *dir = find_entry(PARAM_FLOPPIES_FOLDER)->value;

            if (strncmp(remote_uri, "http", 4) == 0)
            { // Check if remote_uri starts with "http"
                strcpy(full_url, remote_uri);
            }
            else
            {
                // Use sprintf to format and concatenate strings
                sprintf(full_url, "%s/%s", base_url, remote_uri);
            }

            DPRINTF("Full URL: %s\n", full_url);
            DPRINTF("Remote name: %s\n", remote_name);
            DPRINTF("Name in folder: %s\n", dest_filename);
            DPRINTF("Directory: %s\n", dir);

            if (directory_exists(dir))
            {
                // Directory exists
                DPRINTF("Directory exists: %s\n", dir);

                err_t err = download_floppy(&full_url[0], dir, dest_filename, true);

                if (err != ERR_OK)
                {
                    floppy_image_selected_status = 3; // Error: Failed downloading file
                    DPRINTF("Download floppy error: %d\n", err);
                }
                else
                {
                    // When downloading a floppy image, the floppy image B is cleared
                    // to avoid conflicts
                    put_string(PARAM_FLOPPY_IMAGE_A, dest_filename);
                    put_string(PARAM_FLOPPY_IMAGE_B, "");
                    // put_string(PARAM_BOOT_FEATURE, "FLOPPY_EMULATOR");
                    // write_all_entries();
                }
            }
            else
            {
                floppy_image_selected_status = 2; // Error: Directory does not exist
                DPRINTF("Directory does not exist: %s\n", dir);
            }

            floppy_image_selected = -1;
            *((volatile uint16_t *)(memory_area + 4)) = floppy_image_selected_status;

            DPRINTF("Random token: %x\n", random_token);
            *((volatile uint32_t *)(memory_area)) = random_token;
        }

        if (floppy_file_selected > 0)
        {
            DPRINTF("Floppy file selected: %d in disk %c (%d)\n", floppy_file_selected, floppy_drive == 0 ? 'A' : 'B', floppy_drive);

            if (floppy_drive < 0)
            {
                DPRINTF("Floppy drive not selected\n");
            }
            else
            {

                char *old_floppy = NULL;
                char *filename = NULL;
                char *dir = find_entry(PARAM_FLOPPIES_FOLDER)->value;
                filename = filtered_local_list[floppy_file_selected - 1];

                size_t filename_length = strlen(filename);
                bool is_msa = filename_length > 4 &&
                              (strcasecmp(&filename[filename_length - 4], ".MSA") == 0);

                DPRINTF("Floppy drive: %c\n", floppy_drive == 0 ? 'A' : 'B');
                DPRINTF("Floppy folder: %s\n", dir);
                DPRINTF("Floppy file: %s\n", filename);
                DPRINTF("Floppy file length: %d\n", filename_length);
                DPRINTF("Floppy file is MSA: %s\n", is_msa ? "true" : "false");

                if (is_msa)
                {
                    // Create a filename and change the extension to .ST
                    char *stFilename = malloc(filename_length + 1);
                    strcpy(stFilename, filename);
                    strcpy(&stFilename[filename_length - 4], ".ST");
                    DPRINTF("MSA to ST: %s -> %s\n", filename, stFilename);
                    dma_channel_set_irq1_enabled(lookup_data_rom_dma_channel, false);
                    FRESULT err = MSA_to_ST(dir, filename, stFilename, true);
                    dma_channel_set_irq1_enabled(lookup_data_rom_dma_channel, true);
                    if (err != FR_OK)
                    {
                        DPRINTF("MSA to ST error: %d\n", err);
                    }
                    else
                    {
                        old_floppy = stFilename;
                    }
                }
                else
                {
                    old_floppy = filename;
                }

                if (old_floppy != NULL)
                {
                    DPRINTF("Load file: %s\n", old_floppy);
                    char *new_floppy = NULL;
                    // Check if old_floppy ends with ".rw"
                    bool use_existing_rw = (strlen(old_floppy) > 3 && strcmp(&old_floppy[strlen(old_floppy) - 3], ".rw") == 0);
                    if (floppy_read_write && !use_existing_rw)
                    {
                        new_floppy = malloc(strlen(old_floppy) + strlen(".rw") + 1); // Allocate space for the old string, the new suffix, and the null terminator
                        sprintf(new_floppy, "%s.rw", old_floppy);                    // Create the new string with the .rw suffix
                        dma_channel_set_irq1_enabled(lookup_data_rom_dma_channel, false);
                        FRESULT result = copy_file(dir, old_floppy, new_floppy, false); // Do not overwrite if exists
                        dma_channel_set_irq1_enabled(lookup_data_rom_dma_channel, true);
                    }
                    else
                    {
                        new_floppy = strdup(old_floppy);
                    }
                    DPRINTF("Floppy Read/Write: %s\n", floppy_read_write ? "true" : "false");

                    if (floppy_drive == 0)
                    {
                        put_string(PARAM_FLOPPY_IMAGE_A, new_floppy);
                    }
                    else
                    {
                        put_string(PARAM_FLOPPY_IMAGE_B, new_floppy);
                    }
                    put_string(PARAM_BOOT_FEATURE, "FLOPPY_EMULATOR");
                    write_all_entries();

                    free(new_floppy);
                    fflush(stdout);
                }
            }
            floppy_file_selected = -1;
            *((volatile uint32_t *)(memory_area)) = random_token;
        }

        // Increase the counter and reset it if it reaches the limit
        network_poll_counter >= network_status_polling_ms ? network_poll_counter = 0 : network_poll_counter++;
        storage_poll_counter >= STORAGE_POLL_INTERVAL ? storage_poll_counter = 0 : storage_poll_counter++;

        // Store the seed of the random number generator in the ROM memory space
        *((volatile uint32_t *)(memory_area - RANDOM_SEED_SIZE)) = rand() % 0xFFFFFFFF;
    }

    if (rom_file_selected > 0)
    {
        DPRINTF("ROM file selected: %d\n", rom_file_selected);

        // Erase the content before loading the new file. It seems that
        // overwriting it's not enough
        uint32_t ints = save_and_disable_interrupts();
        flash_range_erase(FLASH_ROM_LOAD_OFFSET, ROM_SIZE_BYTES * 2); // Two banks of 64K
        restore_interrupts(ints);
        int res = load_rom_from_fs(find_entry(PARAM_ROMS_FOLDER)->value, filtered_local_list[rom_file_selected - 1], FLASH_ROM_LOAD_OFFSET);

        if (res != FR_OK)
            DPRINTF("f_open error: %s (%d)\n", FRESULT_str(res), res);

        release_memory_files(file_list, num_files);
        release_memory_files(filtered_local_list, filtered_num_local_files);

        put_string(PARAM_BOOT_FEATURE, "ROM_EMULATOR");
        write_all_entries();
        *((volatile uint32_t *)(memory_area)) = random_token;
    }

    if (rom_rescue_mode_file_content != NULL)
    {
        DPRINTF("ROM rescue mode file content: %s\n", rom_rescue_mode_file_content);

        // Erase the content before loading the new file. It seems that
        // overwriting it's not enough
        uint32_t ints = save_and_disable_interrupts();
        flash_range_erase(FLASH_ROM_LOAD_OFFSET, ROM_SIZE_BYTES * 2); // Two banks of 64K
        restore_interrupts(ints);
        int res = load_rom_from_fs(find_entry(PARAM_ROMS_FOLDER)->value, rom_rescue_mode_file_content, FLASH_ROM_LOAD_OFFSET);

        if (res != FR_OK)
            DPRINTF("f_open error: %s (%d)\n", FRESULT_str(res), res);

        put_string(PARAM_BOOT_FEATURE, "ROM_EMULATOR");
        write_all_entries();
    }

    if (rom_network_selected > 0)
    {
        DPRINTF("ROM network selected: %d\n", rom_network_selected);
        RomInfo *current = network_files;
        for (size_t i = 0; i < rom_network_selected - 1; i++)
        {
            current = current->next;
        }
        const char *url = find_entry(PARAM_ROMS_CSV_URL)->value;
        // Split the url in parts
        UrlParts url_parts;
        int url_parts_err = split_url(url, &url_parts);

        char *full_url = NULL;
        // Check if the current->url starts with "http"
        if (strncmp(current->url, "http", 4) == 0)
        {
            full_url = strdup(current->url);
        }
        else
        {
            // Use sprintf to format and concatenate strings
            full_url = malloc(strlen(url_parts.protocol) + strlen(url_parts.domain) + strlen(current->url) + 4); // Allocate space for the protocol, the host, the url, and the null terminator
            sprintf(full_url, "%s://%s/%s", url_parts.protocol, url_parts.domain, current->url);
        }
        err_t res = download_rom(full_url, FLASH_ROM_LOAD_OFFSET);
        DPRINTF("Download ROM result: %d\n", res);

        // No need to release the memory used. We are going to reset the board

        if (res == ERR_OK)
        {
            put_string(PARAM_BOOT_FEATURE, "ROM_EMULATOR");
            write_all_entries();

            *((volatile uint32_t *)(memory_area)) = random_token;
        }
        else
        {
            DPRINTF("Error downloading ROM: %d\n", res);
            // Continue and graciously reset the board
        }
    }

    if (rtc_boot)
    {
        DPRINTF("Boot the RTC emulator.\n");
        put_string(PARAM_BOOT_FEATURE, "RTC_EMULATOR");
        write_all_entries();
        *((volatile uint32_t *)(memory_area)) = random_token;
    }
    if (gemdrive_boot)
    {
        DPRINTF("Boot the HARDDISK emulator.\n");
        put_string(PARAM_BOOT_FEATURE, "GEMDRIVE_EMULATOR");
        write_all_entries();
        *((volatile uint32_t *)(memory_area)) = random_token;
    }
    if (reset_default)
    {
        DPRINTF("Resetting configuration to default and rebooting SidecarT.\n");
        reset_config_default();
        *((volatile uint32_t *)(memory_area)) = random_token;
    }
    // Release memory from the protocol
    // terminate_protocol_parser();
}
