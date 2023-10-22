/**
 * File: dongleemul.h
 * Author: Diego Parrilla Santamaría
 * Date: July 2023
 * Copyright: 2023 - GOODDATA LABS SL
 * Description: Header file for Dongle dongle emulator experiment.
 */

#ifndef DONGLEEMUL_H
#define DONGLEEMUL_H

// JED file with the functions in the 5C060
// The datasheet https://pdf1.alldatasheet.com/datasheet-pdf/view/101883/INTEL/5C060.html
// The Atari Forum thread https://www.atari-forum.com/viewtopic.php?t=20130&start=350
//

// File generated from rainbow unicorn farts

// From the source file SRD_OK.JED

// No rights reserved

// Device 5c060
//  pin02          = 2
//  pin03          = 3
//  pin04          = 4
//  pin05          = 5
//  pin06          = 6
//  pin07          = 7
//  pin08          = 8
//  pin09          = 9
//  pin10          = 10
//  pin11          = 11
//  pin14          = 14
//  pin15          = 15
//  pin16          = 16
//  pin17          = 17
//  pin18          = 18
//  pin19          = 19
//  pin20          = 20
//  pin21          = 21
//  pin22          = 22
//  pin23          = 23

// turbo

// start

//  pin03          ^:=  /pin03*/pin14
//                  +   pin03* pin14;

//  pin04          ^:=  /pin04* pin14
//                  +   pin03*/pin04*/pin14
//                  +   pin03* pin04* pin14;

//  pin05          ^:=   pin03*/pin05* pin14
//                  +   pin04* pin05* pin14
//                  +   pin03* pin04*/pin05*/pin14;

//  pin06          ^:=   pin03*/pin06*/pin14
//                  +   pin04*/pin05* pin06
//                  +   pin03* pin04* pin05*/pin06*/pin14;

//  pin07          ^:=  /pin03* pin05*/pin07
//                  +  /pin04*/pin06* pin07* pin14
//                  +   pin03* pin04* pin05* pin06*/pin07*/pin14;

//  pin08          ^:=  /pin03*/pin05* pin07*/pin08
//                  +  /pin04* pin06* pin08* pin14
//                  +   pin03* pin04* pin05* pin06* pin07*/pin08*/pin14;

//  pin09          ^:=  /pin07* pin08*/pin09
//                  +   pin04*/pin05*/pin06* pin09
//                  +   pin03* pin04* pin05* pin06* pin07* pin08*/pin09*/pin14;

//  pin10          ^:=  /pin04* pin07*/pin08*/pin10
//                  +   pin05* pin06*/pin09* pin10
//                  +   pin03* pin04* pin05* pin06* pin07* pin08* pin09*/pin10*/pin14;

//  pin15          ^:=  /pin07* pin08*/pin15
//                  +  /pin06* pin09*/pin10* pin15
//                  +   pin03* pin04* pin05* pin06* pin07* pin08* pin09* pin10*/pin14*/pin15;

//  pin16          ^:=  /pin09*/pin15* pin16
//                  +  /pin08* pin10*/pin16
//                  +   pin03* pin04* pin05* pin06* pin07* pin08* pin09* pin10*/pin14* pin15*/pin16;

//  pin17          ^:=  /pin08* pin17
//                  +  /pin10*/pin16*/pin17
//                  +   pin03* pin04* pin05* pin06* pin07* pin08* pin09* pin10*/pin14* pin15* pin16*/pin17;

//  pin18          ^:=  /pin15* pin16* pin18
//                  +   pin08*/pin10* pin17*/pin18
//                  +   pin03* pin04* pin05* pin06* pin07* pin08* pin09* pin10*/pin14* pin15* pin16* pin17*/pin18;

//  pin19          ^:=   pin10*/pin15*/pin19
//                  +   pin16*/pin17* pin18* pin19
//                  +   pin03* pin04* pin05* pin06* pin07* pin08* pin09* pin10*/pin14* pin15* pin16* pin17* pin18*/pin19;

//  pin20          ^:=  /pin16*/pin19*/pin20
//                  +   pin17*/pin18* pin20
//                  +   pin03* pin04* pin05* pin06* pin07* pin08* pin09* pin10*/pin14* pin15* pin16* pin17* pin18* pin19*/pin20;

//  pin21          ^:=  /pin17* pin18*/pin21
//                  +  /pin16* pin19*/pin20* pin21
//                  +   pin03* pin04* pin05* pin06* pin07* pin08* pin09* pin10*/pin14* pin15* pin16* pin17* pin18* pin19* pin20*/pin21;

//  pin22.ena       =  /pin02;
//  pin22          ^:=  /pin04* pin22
//                  +   pin05* pin14*/pin22
//                  +   pin09*/pin14*/pin16*/pin18* pin22
//                  +  /pin06* pin09* pin14* pin17*/pin21* pin22
//                  +   pin03* pin04* pin05* pin06* pin07* pin08* pin09* pin10*/pin14* pin15* pin16* pin17* pin18* pin19* pin20* pin21*/pin22;

// end

#include "debug.h"
#include "constants.h"
#include "hardware/dma.h"

#include "../../build/romemul.pio.h"
#include "hardware/structs/bus_ctrl.h"

#include "config.h"

typedef void (*IRQInterceptionCallback)();

extern int read_addr_rom_dma_channel;
extern int lookup_data_rom_dma_channel;

// Function Prototypes
int init_dongleemul(int safe_config_reboot);

#endif // DONGLEEMUL_H
