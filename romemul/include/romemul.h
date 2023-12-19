/**
 * File: romemul.h
 * Author: Diego Parrilla Santamaría
 * Date: July 2023
 * Copyright: 2023 - GOODDATA LABS SL
 * Description: Header file for the ROM emulator C program.
 */

#ifndef ROMEMUL_H
#define ROMEMUL_H

#include "debug.h"
#include "constants.h"

#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>

#include "pico/stdlib.h"
#include "hardware/pio.h"
#include "hardware/dma.h"
#include "hardware/vreg.h"
#include "hardware/structs/bus_ctrl.h"
#include "pico/cyw43_arch.h"

#include "../../build/romemul.pio.h"

typedef void (*IRQInterceptionCallback)();

extern int read_addr_rom_dma_channel;
extern int lookup_data_rom_dma_channel;

// Function Prototypes
int init_romemul(IRQInterceptionCallback requestCallback, IRQInterceptionCallback responseCallback, bool copyFlashToRAM);

#endif // ROMEMUL_H
