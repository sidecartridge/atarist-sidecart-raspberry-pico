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

#include "lwip/apps/httpd.h"

#include "sd_card.h"
#include "f_util.h"

#include "../../build/romemul.pio.h"

#include "tprotocol.h"
#include "commands.h"
#include "config.h"
#include "memfunc.h"
#include "filesys.h"
#include "httpd.h"

#define FLOPPYEMUL_RANDOM_TOKEN (0x0)                              // Offset from 0x0000
#define FLOPPYEMUL_RANDOM_TOKEN_SEED (FLOPPYEMUL_RANDOM_TOKEN + 4) // random_token + 4 bytes
#define FLOPPYEMUL_BUFFER_TYPE (FLOPPYEMUL_RANDOM_TOKEN_SEED + 4)  // random_token_seed + 4 byte
#define FLOPPYEMUL_BPB_DATA_A (FLOPPYEMUL_BUFFER_TYPE + 4)         // buffer_type + 4 bytes
#define FLOPPYEMUL_SECPCYL_A (FLOPPYEMUL_BPB_DATA_A + 22)          // BPB_data + 22 bytes
#define FLOPPYEMUL_SECPTRACK_A (FLOPPYEMUL_SECPCYL_A + 2)          // secpcyl + 2 bytes
#define FLOPPYEMUL_DISK_NUMBER_A (FLOPPYEMUL_SECPTRACK_A + 8)      // BTB + 2 bytes

#define FLOPPYEMUL_BPB_DATA_B (FLOPPYEMUL_DISK_NUMBER_A + 2)  // FLOPPYEMUL_DISK_NUMBER_A + 2 bytes
#define FLOPPYEMUL_SECPCYL_B (FLOPPYEMUL_BPB_DATA_B + 22)     // BPB_data + 22 bytes
#define FLOPPYEMUL_SECPTRACK_B (FLOPPYEMUL_SECPCYL_B + 2)     // secpcyl + 2 bytes
#define FLOPPYEMUL_DISK_NUMBER_B (FLOPPYEMUL_SECPTRACK_B + 8) // BTB + 2 bytes

#define FLOPPYEMUL_OLD_XBIOS_TRAP (FLOPPYEMUL_DISK_NUMBER_B + 6)  // disk_number + 4 bytes + 2 bytes align
#define FLOPPYEMUL_OLD_HDV_BPB (FLOPPYEMUL_OLD_XBIOS_TRAP + 4)    // old_XBIOS_trap + 4 bytes
#define FLOPPYEMUL_OLD_HDV_RW (FLOPPYEMUL_OLD_HDV_BPB + 4)        // old_hdv_bpb + 4 bytes
#define FLOPPYEMUL_OLD_HDV_MEDIACH (FLOPPYEMUL_OLD_HDV_RW + 4)    // old_hdv_rw + 4 bytes
#define FLOPPYEMUL_HARDWARE_TYPE (FLOPPYEMUL_OLD_HDV_MEDIACH + 4) // old_hdv_mediach + 4 bytes

// Done to align to 4 bytes
#define FLOPPYEMUL_READ_CHECKSUM (FLOPPYEMUL_HARDWARE_TYPE + 4) // network_timeout_sec + 4 bytes

// Copy the IP address and hostname
#define FLOPPYEMUL_IP_ADDRESS (FLOPPYEMUL_READ_CHECKSUM + 4) // read_checksum + 4 bytes
#define FLOPPYEMUL_HOSTNAME (FLOPPYEMUL_IP_ADDRESS + 128)    // ip_address + 128 bytes

// Define shared varibles
#define FLOPPYEMUL_SHARED_VARIABLES (FLOPPYEMUL_RANDOM_TOKEN + 512) // random token + 512 bytes to the shared variables area

// Memory address for the buffer swap
#define FLOPPYEMUL_IMAGE (FLOPPYEMUL_RANDOM_TOKEN + 0x1000) // random_token + 0x1000 bytes

// Media type changed flags
#define MED_NOCHANGE 0
#define MED_UNKNOWN 1
#define MED_CHANGED 2

// BPB fields
#define BPB_RECSIZE 0
#define BPB_CLSIZ 1
#define BPB_CLSIZB 2
#define BPB_RDLEN 3
#define BPB_FSIZ 4
#define BPB_FATREC 5
#define BPB_DATREC 6
#define BPB_NUMCL 7
#define BPB_BFLAGS 8
#define BPB_TRACKCNT 9
#define SIDE_COUNT 10
#define SEC_CYL 11
#define SEC_TRACK 12
#define DISK_NUMBER 16
#define DISK_NUMBER_A 0
#define DISK_NUMBER_B 1

// Flags for the command actions
#define FILE_READY_A_FLAG (1 << 0)
#define SECTOR_READ_FLAG (1 << 1)
#define SECTOR_WRITE_FLAG (1 << 2)
#define SET_BPB_FLAG (1 << 3)
#define SAVE_VECTORS_FLAG (1 << 4)
#define SAVE_HARDWARE_FLAG (1 << 5)
#define PING_RECEIVED_FLAG (1 << 6)
#define FILE_READY_B_FLAG (1 << 7)
#define MOUNT_DRIVE_A_FLAG (1 << 8)
#define MOUNT_DRIVE_B_FLAG (1 << 9)
#define UMOUNT_DRIVE_A_FLAG (1 << 10)
#define UMOUNT_DRIVE_B_FLAG (1 << 11)
#define SHOW_VECTOR_CALL_FLAG (1 << 12)

// Now the index for the shared variables of the program
#define FLOPPYEMUL_SVAR_DO_TRANSFER (SHARED_VARIABLE_SHARED_FUNCTIONS_SIZE + 0)
#define FLOPPYEMUL_SVAR_EXIT_TRANSFER (SHARED_VARIABLE_SHARED_FUNCTIONS_SIZE + 1)
#define FLOPPYEMUL_SVAR_XBIOS_TRAP_ENABLED (SHARED_VARIABLE_SHARED_FUNCTIONS_SIZE + 2)
#define FLOPPYEMUL_SVAR_BOOT_ENABLED (SHARED_VARIABLE_SHARED_FUNCTIONS_SIZE + 3)
#define FLOPPYEMUL_SVAR_PING_STATUS (SHARED_VARIABLE_SHARED_FUNCTIONS_SIZE + 4)
#define FLOPPYEMUL_SVAR_PING_TIMEOUT (SHARED_VARIABLE_SHARED_FUNCTIONS_SIZE + 5)
#define FLOPPYEMUL_SVAR_MEDIA_CHANGED_A (SHARED_VARIABLE_SHARED_FUNCTIONS_SIZE + 6)
#define FLOPPYEMUL_SVAR_MEDIA_CHANGED_B (SHARED_VARIABLE_SHARED_FUNCTIONS_SIZE + 7)
#define FLOPPYEMUL_SVAR_EMULATION_MODE (SHARED_VARIABLE_SHARED_FUNCTIONS_SIZE + 8)

typedef struct
{
    uint16_t recsize;     /* 0: Sector size in bytes                */
    uint16_t clsiz;       /* 1: Cluster size in sectors             */
    uint16_t clsizb;      /* 2: Cluster size in bytes               */
    uint16_t rdlen;       /* 3: Root Directory length in sectors    */
    uint16_t fsiz;        /* 4: FAT size in sectors                 */
    uint16_t fatrec;      /* 5: Sector number of second FAT         */
    uint16_t datrec;      /* 6: Sector number of first data cluster */
    uint16_t numcl;       /* 7: Number of data clusters on the disk */
    uint16_t bflags;      /* 8: Magic flags                         */
    uint16_t trackcnt;    /* 9: Track count                         */
    uint16_t sidecnt;     /* 10: Side count                         */
    uint16_t secpcyl;     /* 11: Sectors per cylinder               */
    uint16_t secptrack;   /* 12: Sectors per track                  */
    uint16_t reserved[3]; /* 13-15: Reserved                        */
    uint16_t disk_number; /* 16: Disk number                        */
} BPBData;

typedef struct
{
    uint32_t hdv_bpb_payload;
    uint32_t hdv_rw_payload;
    uint32_t hdv_mediach_payload;
    uint32_t XBIOS_trap_payload;
    bool hdv_bpb_payload_set;
    bool hdv_rw_payload_set;
    bool hdv_mediach_payload_set;
    bool XBIOS_trap_payload_set;
} DiskVectors;

typedef struct
{
    uint32_t machine;
    uint32_t start_function;
    uint32_t end_function;
} HardwareType;

typedef struct
{
    char **list;
    int size;
} FloppyCatalog;

typedef void (*IRQInterceptionCallback)();

extern int read_addr_rom_dma_channel;
extern int lookup_data_rom_dma_channel;

// Interrupt handler callback for DMA completion
void __not_in_flash_func(floppyemul_dma_irq_handler_lookup_callback)(void);

// Function Prototypes
void init_floppyemul(bool safe_config_reboot);

#endif // FLOPPYEMUL_H
