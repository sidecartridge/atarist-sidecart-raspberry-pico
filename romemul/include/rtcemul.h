/**
 * File: rtcemul.h
 * Author: Diego Parrilla Santamaría
 * Date: July 2023
 * Copyright: 2023 - GOODDATA LABS SL
 * Description: Header file for the RTC emulator C program.
 */

#ifndef RTCEMUL_H
#define RTCEMUL_H

#include "debug.h"
#include "constants.h"
#include "firmware_rtcemul.h"

#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>

#include "pico/stdlib.h"
#include "hardware/pio.h"
#include "hardware/dma.h"
#include <hardware/watchdog.h>
#include "hardware/structs/bus_ctrl.h"
#include "pico/cyw43_arch.h"
#include "hardware/rtc.h"

#include "time.h"

#include "lwip/dns.h"
#include "lwip/pbuf.h"
#include "lwip/udp.h"

#include "sd_card.h"
#include "f_util.h"
#include "ff.h"

#include "../../build/romemul.pio.h"

#include "tprotocol.h"
#include "commands.h"
#include "config.h"
#include "network.h"
#include "filesys.h"

#define RTCEMUL_RANDOM_TOKEN 0x0                             // Offset from 0x0000 of the shared memory buffer
#define RTCEMUL_RANDOM_TOKEN_SEED (RTCEMUL_RANDOM_TOKEN + 4) // random_token + 4 bytes
#define RTCEMUL_NTP_SUCCESS (RTCEMUL_RANDOM_TOKEN_SEED + 4)  // random_token_seed + 4 bytes
#define RTCEMUL_DATETIME_BCD   (RTCEMUL_NTP_SUCCESS + 4)     // ntp_success + 4 bytes
#define RTCEMUL_DATETIME_MSDOS (RTCEMUL_DATETIME_BCD + 8)    // datetime_bcd + 8 bytes
#define RTCEMUL_OLD_XBIOS_TRAP (RTCEMUL_DATETIME_MSDOS + 8)  // datetime_msdos + 8 bytes
#define RTCEMUL_REENTRY_TRAP (RTCEMUL_OLD_XBIOS_TRAP + 4)    // old_bios trap + 4 bytes
#define RTCEMUL_Y2K_PATCH       (RTCEMUL_REENTRY_TRAP + 4)   // reentry_trap + 4 byte
#define RTCEMUL_SHARED_VARIABLES (RTCEMUL_Y2K_PATCH + 8)     // y2k_patch + 4 bytes

#define NTP_DEFAULT_PORT 123 // NTP UDP port
#define NTP_DELTA 2208988800 // seconds between 1 Jan 1900 and 1 Jan 1970
#define NTP_MSG_LEN 48       // ignore Authenticator (optional)

typedef enum
{
    RTC_SIDECART,
    RTC_DALLAS,
    RTC_AREAL,
    RTC_FMCII,
    RTC_UNKNOWN
} RTC_TYPE;

typedef struct NTP_TIME_T
{
    ip_addr_t ntp_ipaddr;
    struct udp_pcb *ntp_pcb;
    bool ntp_server_found;
    bool ntp_error;
} NTP_TIME;

// DAllas RTC. Info here: https://pdf1.alldatasheet.es/datasheet-pdf/view/58439/DALLAS/DS1216.html
typedef struct
{
    uint64_t last_magic_found;
    uint16_t retries;
    uint64_t magic_sequence_hex;
    uint8_t clock_sequence[64];
    uint8_t read_address_bit;
    uint8_t write_address_bit_zero;
    uint8_t write_address_bit_one;
    uint8_t magic_sequence[66];
    uint16_t size_magic_sequence;
    uint16_t size_clock_sequence;
    uint32_t rom_address;
} DallasClock;

typedef void (*IRQInterceptionCallback)();

extern int read_addr_rom_dma_channel;
extern int lookup_data_rom_dma_channel;

// Interrupt handler callback for DMA completion
void __not_in_flash_func(rtcemul_dma_irq_handler_lookup_callback)(void);

// Function Prototypes
int init_rtcemul(bool safe_config_reboot);
void host_found_callback(const char *name, const ip_addr_t *ipaddr, void *arg);
void set_internal_rtc();
void ntp_init();
datetime_t *get_rtc_time();
NTP_TIME *get_net_time();
long get_utc_offset_seconds();
void set_utc_offset_seconds(long offset);
uint8_t to_bcd(uint8_t val);
uint8_t add_bcd(uint8_t bcd1, uint8_t bcd2);
uint8_t sub_bcd(uint8_t bcd1, uint8_t bcd2);
void set_ikb_datetime_msg(uint32_t mem_shared_addr, 
                        uint16_t rtcemul_datetime_bcd_idx, 
                        uint16_t rtcemul_y2k_patch_idx, 
                        uint16_t rtcemul_datetime_msdos_idx, 
                        uint16_t gemdos_version,
                        bool y2k_patch);

#endif // RTCEMUL_H
