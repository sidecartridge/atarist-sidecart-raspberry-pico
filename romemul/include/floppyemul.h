/**
 * File: floppyemul.h
 * Author: Diego Parrilla Santamaría
 * Date: July 2023
 * Copyright: 2023 - GOODDATA LABS SL
 * Description: Header file for the Floppy emulator C program.
 */

#ifndef FLOPPYEMUL_H
#define FLOPPYEMUL_H

#include "debug.h"
#include "constants.h"
#include "firmware_floppyemul.h"

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

#define FLOPPYEMUL_RANDOM_TOKEN (0x0)                              // Offset from 0x0000
#define FLOPPYEMUL_RANDOM_TOKEN_SEED (FLOPPYEMUL_RANDOM_TOKEN + 4) // random_token + 4 bytes
#define FLOPPYEMUL_BPB_DATA (FLOPPYEMUL_RANDOM_TOKEN_SEED + 4)     // FLOPPYEMUL_RANDOM_TOKEN_SEED + 4 bytes
#define FLOPPYEMUL_SECPCYL (FLOPPYEMUL_BPB_DATA + 22)              // BPB_data + 22 bytes
#define FLOPPYEMUL_SECPTRACK (FLOPPYEMUL_SECPCYL + 2)              // secpcyl + 2 bytes
#define FLOPPYEMUL_DISK_NUMBER (FLOPPYEMUL_SECPTRACK + 8)          // BTB + 2 bytes
#define FLOPPYEMUL_OLD_XBIOS_TRAP (FLOPPYEMUL_DISK_NUMBER + 2)     // disk_number + 2 bytes
#define FLOPPYEMUL_OLD_HDV_BPB (FLOPPYEMUL_OLD_XBIOS_TRAP + 4)     // old_XBIOS_trap + 4 bytes
#define FLOPPYEMUL_OLD_HDV_RW (FLOPPYEMUL_OLD_HDV_BPB + 4)         // old_hdv_bpb + 4 bytes
#define FLOPPYEMUL_OLD_HDV_MEDIACH (FLOPPYEMUL_OLD_HDV_RW + 4)     // old_hdv_rw + 4 bytes
#define FLOPPYEMUL_IMAGE (FLOPPYEMUL_RANDOM_TOKEN + 0x1000)        // random_token + 0x1000 bytes

typedef void (*IRQInterceptionCallback)();

extern int read_addr_rom_dma_channel;
extern int lookup_data_rom_dma_channel;

// Interrupt handler callback for DMA completion
void __not_in_flash_func(floppyemul_dma_irq_handler_lookup_callback)(void);

// Copy the Atari ST floopy emulator firmware to RAM
int copy_floppy_firmware_to_RAM();

// Function Prototypes
int init_floppyemul(bool safe_config_reboot);

#endif // FLOPPYEMUL_H
