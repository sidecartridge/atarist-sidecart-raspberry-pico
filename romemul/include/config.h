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
#include "pico/cyw43_arch.h"

#define MAX_ENTRIES 19
#define MAX_KEY_LENGTH 20
#define MAX_STRING_VALUE_LENGTH 64

#define TYPE_INT ((uint16_t)0)
#define TYPE_STRING ((uint16_t)1)
#define TYPE_BOOL ((uint16_t)2)

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

void swap_words(void *dest_ptr_word, uint16_t size_in_bytes);
void null_words(void *dest_ptr_word, uint16_t size_in_bytes);

int copy_firmware_to_RAM(uint16_t *emulROM, int emulROM_length);
int erase_firmware_from_RAM();

void blink_error();

// int remove_entry(const char *key);

#endif // CONFIG_H
