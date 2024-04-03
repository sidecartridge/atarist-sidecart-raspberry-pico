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

#include "time.h"

#include "pico/stdlib.h"
#include "hardware/pio.h"
#include "hardware/dma.h"
#include <hardware/watchdog.h>
#include "hardware/structs/bus_ctrl.h"
#include "pico/cyw43_arch.h"
#include "hardware/rtc.h"

#include "sd_card.h"
#include "f_util.h"

#include "../../build/romemul.pio.h"

#include "tprotocol.h"
#include "commands.h"
#include "config.h"
#include "filesys.h"
#include "rtcemul.h"

#define SWAP_LONGWORD(data) ((((uint32_t)data << 16) & 0xFFFF0000) | (((uint32_t)data >> 16) & 0xFFFF))

#define DEFAULT_FOPEN_READ_BUFFER_SIZE 8192
#define DEFAULT_FWRITE_BUFFER_SIZE 2048
#define FIRST_FILE_DESCRIPTOR 16384
#define PRG_STRUCT_SIZE 28 // Size of the GEMDOS structure in the executable header file (PRG)
#define SHARED_VARIABLES_MAXSIZE 32
#define SHARED_VARIABLES_SIZE 6
#define DTA_SIZE_ON_ST 44

// Index for the shared variables
#define SHARED_VARIABLE_PEXEC_RESTORE 0
#define SHARED_VARIABLE_SVERSION 1
#define SHARED_VARIABLE_FIRST_FILE_DESCRIPTOR 2
#define SHARED_VARIABLE_DRIVE_LETTER 3
#define SHARED_VARIABLE_DRIVE_NUMBER 4
#define SHARED_VARIABLE_BUFFER_TYPE 5

#define GEMDRVEMUL_RANDOM_TOKEN (0x0)                                   // Offset from 0x0000
#define GEMDRVEMUL_RANDOM_TOKEN_SEED (GEMDRVEMUL_RANDOM_TOKEN + 4)      // random_token + 4 bytes
#define GEMDRVEMUL_TIMEOUT_SEC (GEMDRVEMUL_RANDOM_TOKEN_SEED + 4)       // random_token_seed + 4 bytes
#define GEMDRVEMUL_PING_STATUS (GEMDRVEMUL_TIMEOUT_SEC + 4)             // timeout_sec + 4 bytes
#define GEMDRVEMUL_RTC_STATUS (GEMDRVEMUL_PING_STATUS + 4)              // ping status + 4 bytes
#define GEMDRVEMUL_NETWORK_STATUS (GEMDRVEMUL_RTC_STATUS + 4)           // rtc status + 4 bytes
#define GEMDRVEMUL_NETWORK_ENABLED (GEMDRVEMUL_NETWORK_STATUS + 4)      // network status + 4 bytes
#define GEMDRVEMUL_REENTRY_TRAP (GEMDRVEMUL_NETWORK_ENABLED + 8)        // network enabled + 4 bytes + 4 GAP
#define GEMDRVEMUL_DEFAULT_PATH (GEMDRVEMUL_REENTRY_TRAP + 4)           // reentry_trap file + 4 bytes
#define GEMDRVEMUL_DTA_F_FOUND (GEMDRVEMUL_DEFAULT_PATH + 128)          // default path + 128 bytes
#define GEMDRVEMUL_DTA_TRANSFER (GEMDRVEMUL_DTA_F_FOUND + 4)            // dta found + 4
#define GEMDRVEMUL_DTA_EXIST (GEMDRVEMUL_DTA_TRANSFER + DTA_SIZE_ON_ST) // dta transfer + DTA_SIZE_ON_ST bytes
#define GEMDRVEMUL_DTA_RELEASE (GEMDRVEMUL_DTA_EXIST + 4)               // dta exist + 4 bytes
#define GEMDRVEMUL_SET_DPATH_STATUS (GEMDRVEMUL_DTA_RELEASE + 4)        // dta release + 4 bytes
#define GEMDRVEMUL_FOPEN_HANDLE (GEMDRVEMUL_SET_DPATH_STATUS + 4)       // set dpath status + 4 bytes

#define GEMDRVEMUL_READ_BYTES (GEMDRVEMUL_FOPEN_HANDLE + 4)                            // fopen handle + 4 bytes.
#define GEMDRVEMUL_READ_BUFF (GEMDRVEMUL_READ_BYTES + 4)                               // read bytes + 4 bytes
#define GEMDRVEMUL_WRITE_BYTES (GEMDRVEMUL_READ_BUFF + DEFAULT_FOPEN_READ_BUFFER_SIZE) // GEMDRVEMUL_READ_BUFFER + DEFAULT_FOPEN_READ_BUFFER_SIZE bytes
#define GEMDRVEMUL_WRITE_CHK (GEMDRVEMUL_WRITE_BYTES + 4)                              // GEMDRVEMUL_WRITE_BYTES + 4 bytes
#define GEMDRVEMUL_WRITE_CONFIRM_STATUS (GEMDRVEMUL_WRITE_CHK + 4)                     // write check + 4 bytes

#define GEMDRVEMUL_FCLOSE_STATUS (GEMDRVEMUL_WRITE_CONFIRM_STATUS + 4) // read buff + 4 bytes
#define GEMDRVEMUL_DCREATE_STATUS (GEMDRVEMUL_FCLOSE_STATUS + 4)       // fclose status + 2 bytes + 2 bytes padding
#define GEMDRVEMUL_DDELETE_STATUS (GEMDRVEMUL_DCREATE_STATUS + 4)      // dcreate status + 2 bytes + 2 bytes padding
#define GEMDRVEMUL_EXEC_HEADER (GEMDRVEMUL_DDELETE_STATUS + 4)         // ddelete status + 2 bytes + 2 bytes padding. Must be aligned to 4 bytes/32 bits
#define GEMDRVEMUL_FCREATE_HANDLE (GEMDRVEMUL_EXEC_HEADER + 32)        // exec header + 32 bytes
#define GEMDRVEMUL_FDELETE_STATUS (GEMDRVEMUL_FCREATE_HANDLE + 4)      // fcreate handle + 4 bytes
#define GEMDRVEMUL_FSEEK_STATUS (GEMDRVEMUL_FDELETE_STATUS + 4)        // fdelete status + 4 bytes
#define GEMDRVEMUL_FATTRIB_STATUS (GEMDRVEMUL_FSEEK_STATUS + 4)        // fseek status + 4
#define GEMDRVEMUL_FRENAME_STATUS (GEMDRVEMUL_FATTRIB_STATUS + 4)      // fattrib status + 4 bytes
#define GEMDRVEMUL_FDATETIME_DATE (GEMDRVEMUL_FRENAME_STATUS + 4)      // frename status + 4 bytes
#define GEMDRVEMUL_FDATETIME_TIME (GEMDRVEMUL_FDATETIME_DATE + 4)      // fdatetime date + 4
#define GEMDRVEMUL_FDATETIME_STATUS (GEMDRVEMUL_FDATETIME_TIME + 4)    // fdatetime time + 4 bytes
#define GEMDRVEMUL_DFREE_STATUS (GEMDRVEMUL_FDATETIME_STATUS + 4)      // fdatetime status + 4 bytes
#define GEMDRVEMUL_DFREE_STRUCT (GEMDRVEMUL_DFREE_STATUS + 4)          // dfree status + 4 bytes

#define GEMDRVEMUL_PEXEC_MODE (GEMDRVEMUL_DFREE_STRUCT + 32)     // dfree struct + 32 bytes
#define GEMDRVEMUL_PEXEC_STACK_ADDR (GEMDRVEMUL_PEXEC_MODE + 4)  // pexec mode + 4 bytes
#define GEMDRVEMUL_PEXEC_FNAME (GEMDRVEMUL_PEXEC_STACK_ADDR + 4) // pexec stack addr + 4 bytes
#define GEMDRVEMUL_PEXEC_CMDLINE (GEMDRVEMUL_PEXEC_FNAME + 4)    // pexec fname + 4 bytes
#define GEMDRVEMUL_PEXEC_ENVSTR (GEMDRVEMUL_PEXEC_CMDLINE + 4)   // pexec cmd line + 4 bytes

#define GEMDRVEMUL_SHARED_VARIABLES (GEMDRVEMUL_PEXEC_ENVSTR + 4) // pexec envstr + 4 bytes

#define GEMDRVEMUL_EXEC_PD (GEMDRVEMUL_SHARED_VARIABLES + 256) // shared variables + 256 bytes

// Atari ST FATTRIB flag
#define FATTRIB_INQUIRE 0x00
#define FATTRIB_SET 0x01

// Atari ST FDATETIME flag
#define FDATETIME_INQUIRE 0x00
#define FDATETIME_SET 0x01

// Atari ST GEMDOS error codes
#define GEMDOS_EOK 0       // OK
#define GEMDOS_ERROR -1    // Generic error
#define GEMDOS_EDRVNR -2   // Drive not ready
#define GEMDOS_EUNCMD -3   // Unknown command
#define GEMDOS_E_CRC -4    // CRC error
#define GEMDOS_EBADRQ -5   // Bad request
#define GEMDOS_E_SEEK -6   // Seek error
#define GEMDOS_EMEDIA -7   // Unknown media
#define GEMDOS_ESECNF -8   // Sector not found
#define GEMDOS_EPAPER -9   // Out of paper
#define GEMDOS_EWRITF -10  // Write fault
#define GEMDOS_EREADF -11  // Read fault
#define GEMDOS_EWRPRO -13  // Device is write protected
#define GEMDOS_E_CHNG -14  // Media change detected
#define GEMDOS_EUNDEV -15  // Unknown device
#define GEMDOS_EINVFN -32  // Invalid function
#define GEMDOS_EFILNF -33  // File not found
#define GEMDOS_EPTHNF -34  // Path not found
#define GEMDOS_ENHNDL -35  // No more handles
#define GEMDOS_EACCDN -36  // Access denied
#define GEMDOS_EIHNDL -37  // Invalid handle
#define GEMDOS_ENSMEM -39  // Insufficient memory
#define GEMDOS_EIMBA -40   // Invalid memory block address
#define GEMDOS_EDRIVE -46  // Invalid drive specification
#define GEMDOS_ENSAME -48  // Cross device rename
#define GEMDOS_ENMFIL -49  // No more files
#define GEMDOS_ELOCKED -58 // Record is already locked
#define GEMDOS_ENSLOCK -59 // Invalid lock removal request
#define GEMDOS_ERANGE -64  // Range error
#define GEMDOS_EINTRN -65  // Internal error
#define GEMDOS_EPLFMT -66  // Invalid program load format
#define GEMDOS_EGSBF -67   // Memory block growth failure
#define GEMDOS_ELOOP -80   // Too many symbolic links
#define GEMDOS_EMOUNT -200 // Mount point crossed (indicator)

#define DTA_HASH_TABLE_SIZE 512

#define PDCLSIZE 0x80 /*  size of command line in bytes  */
#define MAXDEVS 16    /* max number of block devices */

typedef struct
{
    /* No. of Free Clusters */
    uint32_t b_free;
    /* Clusters per Drive */
    uint32_t b_total;
    /* Bytes per Sector */
    uint32_t b_secsize;
    /* Sectors per Cluster */
    uint32_t b_clsize;
} TOS_DISKINFO;

typedef struct
{
    char d_name[12];         /* file name: filename.typ     00-11   */
    uint32_t d_offset_drive; /* dir position                12-15   */
    uint16_t d_curbyt;       /* byte pointer within current cluster 16-17 */
    uint16_t d_curcl;        /* current cluster number for file	   18-19 */
    uint8_t d_attr;          /* attributes of file          20      */
    uint8_t d_attrib;        /* attributes of f file 21 */
    uint16_t d_time;         /* time from file date 22-23 */
    uint16_t d_date;         /* date from file date 24-25 */
    uint32_t d_length;       /* file length in bytes 26-29 */
    char d_fname[14];        /* file name: filename.typ 30-43 */
} DTA;

typedef struct DTANode
{
    uint32_t key;
    DTA data;
    DIR *dj;
    FILINFO *fno;
    TCHAR *pat; /* Pointer to the name matching pattern. Hack for dir_findfirst().  */
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
