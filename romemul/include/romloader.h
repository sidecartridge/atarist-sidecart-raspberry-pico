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

// Delete flash
int delete_FLASH(void);

// Copy embedded firmware to RAM
int copy_firmware_to_RAM();

// Interrupt handler callback for DMA completion
void __not_in_flash_func(dma_irq_handler_lookup_callback)(void);

// Declare the function to initialize the firmware
int init_firmware();

#endif // ROMLOADER_H
