/**
 * File: gemdrvemul.h
 * Author: Diego Parrilla Santamaría
 * Date: November 2023
 * Copyright: 2023 - GOODDATA LABS SL
 * Description: Header file for the GEMDRIVE C program.
 */

#ifndef GEMDRVEMUL_H
#define GEMDRVEMUL_H

#include "debug.h"
#include "constants.h"
#include "firmware_gemdrvemul.h"

#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>

#include "pico/stdlib.h"
#include "hardware/pio.h"
#include "hardware/dma.h"
#include <hardware/watchdog.h>
#include "hardware/structs/bus_ctrl.h"
#include "pico/cyw43_arch.h"

#include "sd_card.h"
#include "f_util.h"

#include "../../build/romemul.pio.h"

#include "tprotocol.h"
#include "commands.h"
#include "config.h"
#include "filesys.h"

#define GEMDRVEMUL_RANDOM_TOKEN (0x0)                                       // Offset from 0x0000
#define GEMDRVEMUL_RANDOM_TOKEN_SEED (GEMDRVEMUL_RANDOM_TOKEN + 4)          // random_token + 4 bytes
#define GEMDRVEMUL_PING_SUCCESS (GEMDRVEMUL_RANDOM_TOKEN_SEED + 4)          // random_token_seed + 4 bytes
#define GEMDRVEMUL_OLD_GEMDOS_TRAP (GEMDRVEMUL_PING_SUCCESS + 2)            // ping_success + 2 bytes
#define GEMDRVEMUL_REENTRY_TRAP (GEMDRVEMUL_OLD_GEMDOS_TRAP + 2)            // old_gemdos_trap + 2 bytes
#define GEMDRVEMUL_DTA_F_FOUND (GEMDRVEMUL_REENTRY_TRAP + 4)                // reentry_trap + 4 bytes
#define GEMDRVEMUL_FORCE_BYPASS (GEMDRVEMUL_DTA_F_FOUND + 2)                // dta found file + 2 bytes
#define GEMDRVEMUL_DTA_TRANSFER (GEMDRVEMUL_FORCE_BYPASS + 2)               // force bypass flag + 2 bytes
#define GEMDRVEMUL_SET_DPATH_STATUS (GEMDRVEMUL_DTA_TRANSFER + sizeof(DTA)) // dta transfer + size of DTA

// Atari ST GEMDOS error codes
#define GEMDOS_EOK 0x00
#define GEMDOS_ERROR -1   // Generic error
#define GEMDOS_EFILNF -33 // File not found
#define GEMDOS_EPTHNF -34 // Path not found
#define GEMDOS_ENMFIL -47 // No more files
#define GEMDOS_EDRIVE -49 // Invalid drive specification

#define DTA_HASH_TABLE_SIZE 512
#define FA_READONLY (AM_RDO) // Read only
#define FA_HIDDEN (AM_HID)   // Hidden
#define FA_SYSTEM (AM_SYS)   // System
#define FA_LABEL (AM_VOL)    // Volume label
#define FA_DIREC (AM_DIR)    // Directory
#define FA_ARCH (AM_ARC)     // Archive

typedef struct
{
    uint8_t d_reserved[21];
    uint8_t d_attrib;
    uint16_t d_time;
    uint16_t d_date;
    uint32_t d_length;
    char d_fname[14];
} DTA;

typedef struct DTANode
{
    uint32_t key;
    DTA data;
    struct DTANode *next;
} DTANode;

typedef void (*IRQInterceptionCallback)();

extern int read_addr_rom_dma_channel;
extern int lookup_data_rom_dma_channel;

// Interrupt handler callback for DMA completion
void __not_in_flash_func(gemdrvemul_dma_irq_handler_lookup_callback)(void);

// Copy the Atari ST floopy emulator firmware to RAM
int copy_floppy_firmware_to_RAM();

// Function Prototypes
int init_gemdrvemul(bool safe_config_reboot);

#endif // GEMDRVEMUL_H
