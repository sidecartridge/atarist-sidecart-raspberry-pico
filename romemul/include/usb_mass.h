/**
 * File: usb_mass.h
 * Author: Diego Parrilla Santamaría
 * Date: June 2024
 * Copyright: 2024 - GOODDATA LABS SL
 * Description: Header for usb_mass.c which manages the USB Mass storage device of the SD card
 */

#ifndef USB_MASS_H
#define USB_MASS_H

#include "debug.h"
#include "constants.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tusb.h"
#include <hardware/watchdog.h>
#include "pico/cyw43_arch.h"

#include "sd_card.h"
#include "f_util.h"
#include "ff.h"
#include "diskio.h" /* Declarations of disk functions */

// For resetting the USB controller
#include "hardware/resets.h"

#include "include/config.h"

#define USBDRIVE_READ_ONLY false

#define TUD_OPT_HIGH_SPEED true

// Init USB Mass storage device
void usb_mass_init(void);
void usb_mass_start(void);

#endif // USB_MASS_H
