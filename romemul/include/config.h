/**
 * File: config.h
 * Author: Diego Parrilla Santamaría
 * Date: August 2023
 * Copyright: 2023 - GOODDATA LABS SL
 * Description: Header file for the configuration manager
 */

#ifndef CONFIG_H
#define CONFIG_H

#include "debug.h"
#include "constants.h"

#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>

#include <hardware/flash.h>
#include <hardware/sync.h>
#include <hardware/watchdog.h>
#include <hardware/dma.h>
#include "pico/cyw43_arch.h"

#include "include/network.h"
#include "hardware/resets.h"

// sync values here as well : atarist-sidecart-firmware/configurator/src/include/config.h
// Warning. There will be an issue when reaching the maximum number of entries for 4Kbytes of flash memory
// The maximum number of entries is 46
// Change the memory size of the config structure to 8Kbytes when reaching the maximum number of entries
#define MAX_ENTRIES 39
#define MAX_KEY_LENGTH 20
#define MAX_STRING_VALUE_LENGTH 64

#define PARAM_BOOT_FEATURE "BOOT_FEATURE"
#define PARAM_CONFIGURATOR_DARK "CONFIGURATOR_DARK"
#define PARAM_DELAY_ROM_EMULATION "DELAY_ROM_EMULATION"
#define PARAM_DOWNLOAD_TIMEOUT_SEC "DOWNLOAD_TIMEOUT_SEC"
#define PARAM_FILE_COUNT_ENABLED "FILE_COUNT_ENABLED"
#define PARAM_FLOPPY_BOOT_ENABLED "FLOPPY_BOOT_ENABLED"
#define PARAM_FLOPPY_BUFFER_TYPE "FLOPPY_BUFFER_TYPE"
#define PARAM_FLOPPIES_FOLDER "FLOPPIES_FOLDER"
#define PARAM_FLOPPY_DB_URL "FLOPPY_DB_URL"
#define PARAM_FLOPPY_IMAGE_A "FLOPPY_IMAGE_A"
#define PARAM_FLOPPY_IMAGE_B "FLOPPY_IMAGE_B"
#define PARAM_FLOPPY_NET_ENABLED "FLOPPY_NET_ENABLED"
#define PARAM_FLOPPY_NET_TOUT_SEC "FLOPPY_NET_TOUT_SEC"
#define PARAM_FLOPPY_XBIOS_ENABLED "FLOPPY_XBIOS_ENABLED"
#define PARAM_GEMDRIVE_BUFF_TYPE "GEMDRIVE_BUFF_TYPE"
#define PARAM_GEMDRIVE_DRIVE "GEMDRIVE_DRIVE"
#define PARAM_GEMDRIVE_FOLDERS "GEMDRIVE_FOLDERS"
#define PARAM_GEMDRIVE_RTC "GEMDRIVE_RTC"
#define PARAM_GEMDRIVE_TIMEOUT_SEC "GEMDRIVE_TIMEOUT_SEC"
#define PARAM_GEMDRIVE_FAKEFLOPPY "GEMDRIVE_FAKEFLOPPY"
#define PARAM_HOSTNAME "HOSTNAME"
#define PARAM_LASTEST_RELEASE_URL "LASTEST_RELEASE_URL"
#define PARAM_MENU_REFRESH_SEC "MENU_REFRESH_SEC"
#define PARAM_NETWORK_STATUS_SEC "NETWORK_STATUS_SEC"
#define PARAM_ROMS_CSV_URL "ROMS_CSV_URL"
#define PARAM_ROMS_FOLDER "ROMS_FOLDER"
#define PARAM_ROMS_YAML_URL "ROMS_YAML_URL"
#define PARAM_RTC_NTP_SERVER_HOST "RTC_NTP_SERVER_HOST"
#define PARAM_RTC_NTP_SERVER_PORT "RTC_NTP_SERVER_PORT"
#define PARAM_RTC_TYPE "RTC_TYPE"
#define PARAM_RTC_UTC_OFFSET "RTC_UTC_OFFSET"
#define PARAM_SAFE_CONFIG_REBOOT "SAFE_CONFIG_REBOOT"
#define PARAM_SD_MASS_STORAGE "SD_MASS_STORAGE"
#define PARAM_SD_BAUD_RATE_KB "SD_BAUD_RATE_KB"
#define PARAM_WIFI_AUTH "WIFI_AUTH"
#define PARAM_WIFI_COUNTRY "WIFI_COUNTRY"
#define PARAM_WIFI_PASSWORD "WIFI_PASSWORD"
#define PARAM_WIFI_SCAN_SECONDS "WIFI_SCAN_SECONDS"
#define PARAM_WIFI_SSID "WIFI_SSID"

#define TYPE_INT ((uint16_t)0)
#define TYPE_STRING ((uint16_t)1)
#define TYPE_BOOL ((uint16_t)2)

#define STRINGIFY_HELPER(x) #x
#define STRINGIFY(x) STRINGIFY_HELPER(x)

typedef uint16_t DataType;

typedef struct
{
    char key[MAX_KEY_LENGTH];
    DataType dataType;
    char value[MAX_STRING_VALUE_LENGTH];
} ConfigEntry;

typedef struct
{
    uint32_t magic;
    ConfigEntry entries[MAX_ENTRIES];
    size_t count;
} ConfigData;

extern ConfigData configData;

// Load functions. Should be used only at startup
void load_all_entries();
// Save all entries as a batch. Use in configuration mode only.
int write_all_entries();
// Reset config to default
int reset_config_default();
// Clear all entries
void clear_config(void);
// Get the size of the structure
size_t get_config_size();
// Print tabular data
void print_config_table();
// Swap the text fields of the structure to be readable by the host
void swap_data(uint16_t *dest_ptr_word);

ConfigEntry *find_entry(const char *key);

int put_bool(const char key[MAX_KEY_LENGTH], bool value);
int put_string(const char key[MAX_KEY_LENGTH], const char *value);
int put_integer(const char key[MAX_KEY_LENGTH], int value);

void select_button_action(bool safe_config_reboot, bool write_config_only_once);
void blink_morse(char ch);

void blink_error();

char *bin_2_str(int number);

void reboot();

// int remove_entry(const char *key);

#endif // CONFIG_H
