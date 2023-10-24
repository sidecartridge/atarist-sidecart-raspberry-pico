/**
 * File: romloader.h
 * Author: Diego Parrilla Santamaría
 * Date: August 2023
 * Copyright: 2023 - GOODDATA LABS SL
 * Description: Header for romloader.c which loads ROM files from SD card
 */

#ifndef ROMLOADER_H
#define ROMLOADER_H

#include "debug.h"
#include "constants.h"
#include "firmware.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pico/stdlib.h"
#include "hardware/pio.h"
#include "hardware/dma.h"
#include <hardware/flash.h>
#include <hardware/watchdog.h>
#include "hardware/structs/bus_ctrl.h"

// #include "pico/cyw43_arch.h"

#include "sd_card.h"
#include "f_util.h"

#include "config.h"
#include "tprotocol.h"
#include "commands.h"
#include "romemul.h"
#include "network.h"

// Size of the random seed to use in the sync commands
#define RANDOM_SEED_SIZE 4 // 4 bytes

typedef enum
{
    SD_CARD_MOUNTED = 0,       // SD card is OK
    SD_CARD_NOT_MOUNTED,       // SD not mounted
    ROMS_FOLDER_OK = 100,      // ROMs folder is OK
    ROMS_FOLDER_NOTFOUND,      // ROMs folder error
    FLOPPIES_FOLDER_OK = 200,  // Floppies folder is OK
    FLOPPIES_FOLDER_NOTFOUND,  // Floppies folder error
    HARDDISKS_FOLDER_OK = 300, // Hard disks folder is OK
    HARDDISKS_FOLDER_NOTFOUND, // Hard disks folder error
} StorageStatus;

#define MAX_FOLDER_LENGTH 128 // Max length of the folder names
typedef struct sd_data
{
    char roms_folder[MAX_FOLDER_LENGTH];      // ROMs folder name
    char floppies_folder[MAX_FOLDER_LENGTH];  // Floppies folder name
    char harddisks_folder[MAX_FOLDER_LENGTH]; // Hard disks folder name
    uint32_t sd_size;                         // SD card size
    uint32_t sd_free_space;                   // SD card free space
    uint32_t roms_folder_count;               // ROMs folder number of files
    uint32_t floppies_folder_count;           // Floppies folder number of files
    uint32_t harddisks_folder_count;          // Hard disks folder number of files
    uint16_t status;                          // Status of the SD card
    uint16_t roms_folder_status;              // ROMs folder status
    uint16_t floppies_folder_status;          // Floppies folder status
    uint16_t harddisks_folder_status;         // Hard disks folder status
} SdCardData;

// Delete flash
int delete_FLASH(void);

// Copy embedded firmware to RAM
int copy_firmware_to_RAM();

// Interrupt handler callback for DMA completion
void __not_in_flash_func(dma_irq_handler_lookup_callback)(void);

// Declare the function to initialize the firmware
int init_firmware();

#endif // ROMLOADER_H
