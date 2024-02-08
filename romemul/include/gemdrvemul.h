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

#define SWAP_LONGWORD(data) ((((uint32_t)data << 16) & 0xFFFF0000) | (((uint32_t)data >> 16) & 0xFFFF))

#define DEFAULT_FOPEN_READ_BUFFER_SIZE 1024
#define FIRST_FILE_DESCRIPTOR 6
#define PRG_STRUCT_SIZE 28 // Size of the GEMDOS structure in the executable header file (PRG)

#define GEMDRVEMUL_RANDOM_TOKEN (0x0)                                       // Offset from 0x0000
#define GEMDRVEMUL_RANDOM_TOKEN_SEED (GEMDRVEMUL_RANDOM_TOKEN + 4)          // random_token + 4 bytes
#define GEMDRVEMUL_PING_SUCCESS (GEMDRVEMUL_RANDOM_TOKEN_SEED + 4)          // random_token_seed + 4 bytes
#define GEMDRVEMUL_OLD_GEMDOS_TRAP (GEMDRVEMUL_PING_SUCCESS + 2)            // ping_success + 2 bytes
#define GEMDRVEMUL_REENTRY_TRAP (GEMDRVEMUL_OLD_GEMDOS_TRAP + 2)            // old_gemdos_trap + 2 bytes
#define GEMDRVEMUL_DEFAULT_PATH (GEMDRVEMUL_REENTRY_TRAP + 4)               // reentry_trap file + 4 bytes
#define GEMDRVEMUL_DTA_F_FOUND (GEMDRVEMUL_DEFAULT_PATH + 128)              // default path + 128 bytes
#define GEMDRVEMUL_FORCE_BYPASS (GEMDRVEMUL_DTA_F_FOUND + 2)                // dta found file + 2 bytes
#define GEMDRVEMUL_DTA_TRANSFER (GEMDRVEMUL_FORCE_BYPASS + 2)               // force bypass flag + 2 bytes
#define GEMDRVEMUL_SET_DPATH_STATUS (GEMDRVEMUL_DTA_TRANSFER + sizeof(DTA)) // dta transfer + size of DTA
#define GEMDRVEMUL_FOPEN_HANDLE (GEMDRVEMUL_SET_DPATH_STATUS + 2)           // set dpath status + 2 bytes

#define GEMDRVEMUL_READ_BYTES (GEMDRVEMUL_FOPEN_HANDLE + 2) // fopen handle + 2 bytes. Must be aligned to 4 bytes/32 bits
#define GEMDRVEMUL_READ_BUFF (GEMDRVEMUL_READ_BYTES + 4)    // read bytes + 4 bytes

#define GEMDRVEMUL_FCLOSE_STATUS (GEMDRVEMUL_READ_BUFF + DEFAULT_FOPEN_READ_BUFFER_SIZE) // read buff + 4 bytes
#define GEMDRVEMUL_DCREATE_STATUS (GEMDRVEMUL_FCLOSE_STATUS + 2)                         // fclose status + 2 bytes
#define GEMDRVEMUL_DDELETE_STATUS (GEMDRVEMUL_DCREATE_STATUS + 2)                        // dcreate status + 2 bytes
#define GEMDRVEMUL_EXEC_HEADER (GEMDRVEMUL_DDELETE_STATUS + 4)                           // ddelete status + 2 bytes + 2 bytes padding. Must be aligned to 4 bytes/32 bits
#define GEMDRVEMUL_EXEC_PD (GEMDRVEMUL_EXEC_HEADER + PRG_STRUCT_SIZE)                    // exec header + 28 bytes (PRG structure). Must be aligned to 4 bytes/32 bits
#define GEMDRVEMUL_FCREATE_HANDLE (GEMDRVEMUL_EXEC_PD + 256)                             // exec PD + 256 bytes
#define GEMDRVEMUL_FDELETE_STATUS (GEMDRVEMUL_FCREATE_HANDLE + 4)                        // fcreate handle + 4 bytes

// Atari ST GEMDOS error codes
#define GEMDOS_EOK 0x00
#define GEMDOS_ERROR -1   // Generic error
#define GEMDOS_EFILNF -33 // File not found
#define GEMDOS_EPTHNF -34 // Path not found
#define GEMDOS_ENHNDL -35 // Handle pool exhausted
#define GEMDOS_EACCDN -36 // Access denied
#define GEMDOS_EIHNDL -37 // Invalid handle
#define GEMDOS_ENMFIL -47 // No more files
#define GEMDOS_EDRIVE -49 // Invalid drive specification
#define GEMDOS_EINTRN -65 // GEMDOS Internal error

#define DTA_HASH_TABLE_SIZE 512
#define FA_READONLY (AM_RDO) // Read only
#define FA_HIDDEN (AM_HID)   // Hidden
#define FA_SYSTEM (AM_SYS)   // System
#define FA_LABEL (AM_VOL)    // Volume label
#define FA_DIREC (AM_DIR)    // Directory
#define FA_ARCH (AM_ARC)     // Archive

#define PDCLSIZE 0x80 /*  size of command line in bytes  */
#define MAXDEVS 16    /* max number of block devices */

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

typedef struct FileDescriptors
{
    char fpath[128];
    int fd;
    FIL fobject;
    struct FileDescriptors *next;
    uint32_t offset;
} FileDescriptors;

typedef struct _pd PD;
struct _pd
{
    /* 0x00 */
    char *p_lowtpa;  /* pointer to start of TPA */
    char *p_hitpa;   /* pointer to end of TPA+1 */
    char *p_tbase;   /* pointer to base of text segment */
    uint32_t p_tlen; /* length of text segment */

    /* 0x10 */
    char *p_dbase;   /* pointer to base of data segment */
    uint32_t p_dlen; /* length of data segment */
    char *p_bbase;   /* pointer to base of bss segment */
    uint32_t p_blen; /* length of bss segment */

    /* 0x20 */
    DTA *p_xdta;
    PD *p_parent;      /* parent PD */
    uint32_t p_hflags; /* see below */
    char *p_env;       /* pointer to environment string */

    /* 0x30 */
    uint32_t p_1fill[2]; /* (junk) */
    uint16_t p_curdrv;   /* current drive */
    uint16_t p_uftsize;  /* number of OFD pointers at p_uft */
    void **p_uft;        /* ptr to my uft (allocated after env.) */

    /* 0x40 */
    uint p_curdir[MAXDEVS]; /* startcl of cur dir on each drive */

    /* 0x60 */
    ulong p_3fill[2]; /* (junk) */
    ulong p_dreg[1];  /* dreg[0] */
    ulong p_areg[5];  /* areg[3..7] */

    /* 0x80 */
    char p_cmdlin[PDCLSIZE]; /* command line image */
};

typedef struct ExecHeader
{
    uint16_t magic;
    uint16_t text_h;
    uint16_t text_l;
    uint16_t data_h;
    uint16_t data_l;
    uint16_t bss_h;
    uint16_t bss_l;
    uint16_t syms_h;
    uint16_t syms_l;
    uint16_t reserved1_h;
    uint16_t reserved1_l;
    uint16_t prgflags_h;
    uint16_t prgflags_l;
    uint16_t absflag;
} ExecHeader;

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
