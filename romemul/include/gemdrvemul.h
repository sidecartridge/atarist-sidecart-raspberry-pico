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

#define GEMDRVEMUL_RANDOM_TOKEN (0x0)                              // Offset from 0x0000
#define GEMDRVEMUL_RANDOM_TOKEN_SEED (GEMDRVEMUL_RANDOM_TOKEN + 4) // random_token + 4 bytes
#define GEMDRVEMUL_PING_SUCCESS (GEMDRVEMUL_RANDOM_TOKEN_SEED + 4) // random_token_seed + 4 bytes
#define GEMDRVEMUL_OLD_XBIOS_TRAP (GEMDRVEMUL_PING_SUCCESS + 2)    // ping_success + 2 bytes

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
