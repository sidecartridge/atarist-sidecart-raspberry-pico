/**
 * File: rtcemul.c
 * Author: Diego Parrilla Santamaría
 * Date: November 2023
 * Copyright: 2023 - GOODDATA LABS SL
 * Description: Multi format RTC emulator
 */

#include "include/rtcemul.h"

// MEmory base
static uint32_t memory_shared_address;

// RTC type to emulate
static RTC_TYPE rtc_type = RTC_UNKNOWN;

// ROM emulation variables
static uint16_t *payloadPtr = NULL;
static uint32_t random_token;
static bool test_ntp_received = false;
static bool read_time_received = false;

// Save XBIOS vector variables
static uint32_t XBIOS_trap_payload;
static bool save_vectors = false;

// XBIOS reentry lock variables
static bool reentry_locked = false;
static bool reentry_unlocked = false;

// NTP and RTC variables
static datetime_t rtc_time = {0};
static NTP_TIME net_time;
static long utc_offset_seconds = 0;
static char *ntp_server_host = NULL;
static int ntp_server_port = NTP_DEFAULT_PORT;

// Dallas RTC variables
static DallasClock dallasClock = {0};

// Microsd variables
static bool microsd_initialized = false;
static bool microsd_mounted = false;

// Local wifi password in the local file
static char *wifi_password_file_content = NULL;

// Y2K patch
static bool y2k_patch_enabled = false;

datetime_t *get_rtc_time()
{
    return &rtc_time;
}

NTP_TIME *get_net_time()
{
    return &net_time;
}

long get_utc_offset_seconds()
{
    return utc_offset_seconds;
}

void set_utc_offset_seconds(long offset)
{
    utc_offset_seconds = offset;
}

static uint16_t rtc_get_raw_time()
{
    // Ensure the values fit into the designated bit sizes
    uint16_t hours = rtc_time.hour & 0x1F;  // Take only the lowest 5 bits
    uint16_t minutes = rtc_time.min & 0x3F; // Take only the lowest 6 bits
    uint16_t seconds = rtc_time.sec & 0x1F; // Take only the lowest 5 bits

    // Shift the values into their respective positions
    // Assuming the bit format is as follows: HHMMMMMMSSSSS (from most significant to least significant)
    uint16_t bit_time = (hours << 11) | (minutes << 5) | seconds;

    return bit_time;
}

static uint16_t rtc_get_raw_date()
{
    // Ensure the values fit into the designated bit sizes
    uint16_t day = rtc_time.day & 0x1F;           // Take only the lowest 5 bits
    uint16_t month = rtc_time.month & 0xF;        // Take only the lowest 4 bits
    uint16_t year = rtc_time.year & 0x7FFF >> 16; // Take only the lowest 7 bits

    // Shift the values into their respective positions
    // Assuming the bit format is as follows: DDDDDMMMMMYYYYYY (from most significant to least significant)
    uint16_t bit_date = (year << 9) | (month << 5) | day;

    return bit_date;
}

void host_found_callback(const char *name, const ip_addr_t *ipaddr, void *arg)
{
    if (name == NULL)
    {
        DPRINTF("NTP host name is NULL\n");
        return;
    }

    NTP_TIME *ntime = (NTP_TIME *)(arg);
    if (ntime == NULL)
    {
        DPRINTF("NTP_TIME argument is NULL\n");
        ntime->ntp_error = true;
        return;
    }

    if (ipaddr != NULL && !ntime->ntp_server_found)
    {
        ntime->ntp_server_found = true;
        ntime->ntp_ipaddr = *ipaddr;
        DPRINTF("NTP Host found: %s\n", name);
        DPRINTF("NTP Server IP: %s\n", ipaddr_ntoa(&ntime->ntp_ipaddr));
    }
    else if (ipaddr == NULL)
    {
        DPRINTF("IP address for NTP Host '%s' not found.\n", name);
        ntime->ntp_error = true;
    }
}

static void ntp_recv_callback(void *arg, struct udp_pcb *pcb, struct pbuf *p, const ip_addr_t *addr, u16_t port)
{
    // Logging the entry into the callback
    DPRINTF("ntp_recv_callback\n");

    // Validate the NTP response
    if (p == NULL || p->tot_len != NTP_MSG_LEN)
    {
        DPRINTF("Invalid NTP response size\n");
        if (p != NULL)
        {
            pbuf_free(p);
        }
        return;
    }

    // Ensure we are getting the response from the server we expect
    if (!ip_addr_cmp(&net_time.ntp_ipaddr, addr) || port != NTP_DEFAULT_PORT)
    {
        DPRINTF("Received response from unexpected server or port\n");
        pbuf_free(p);
        return;
    }

    // Extract relevant fields from the NTP message
    uint8_t mode = pbuf_get_at(p, 0) & 0x07; // mode should be 4 for server response
    uint8_t stratum = pbuf_get_at(p, 1);     // stratum should not be 0

    // Check if the message has the correct mode and stratum
    if (mode != 4 || stratum == 0)
    {
        DPRINTF("Invalid mode or stratum in NTP response\n");
        pbuf_free(p);
        return;
    }

    // Extract the Transmit Timestamp (field starting at byte 40)
    uint32_t transmit_timestamp_secs;
    pbuf_copy_partial(p, &transmit_timestamp_secs, sizeof(transmit_timestamp_secs), 40);
    transmit_timestamp_secs = lwip_ntohl(transmit_timestamp_secs) - NTP_DELTA + utc_offset_seconds;

    // Convert NTP time to a `struct tm`
    time_t utc_sec = transmit_timestamp_secs;
    struct tm *utc = gmtime(&utc_sec);
    if (utc == NULL)
    {
        DPRINTF("Error converting NTP time to struct tm\n");
        pbuf_free(p);
        return;
    }

    // Fill the rtc_time structure
    rtc_time.year = utc->tm_year + 1900;
    rtc_time.month = utc->tm_mon + 1;
    rtc_time.day = utc->tm_mday;
    rtc_time.hour = utc->tm_hour;
    rtc_time.min = utc->tm_min;
    rtc_time.sec = utc->tm_sec;
    rtc_time.dotw = utc->tm_wday; // Day of the week, Sunday is day 0

    // Set the RTC with the received time
    if (!rtc_set_datetime(&rtc_time))
    {
        DPRINTF("Cannot set internal RTC!\n");
    }
    else
    {
        DPRINTF("RP2040 RTC set to: %02d/%02d/%04d %02d:%02d:%02d UTC+0\n",
                rtc_time.day, rtc_time.month, rtc_time.year, rtc_time.hour, rtc_time.min, rtc_time.sec);
    }

    // Free the packet buffer
    pbuf_free(p);
}

void ntp_init()
{
    // Attempt to allocate a new UDP control block.
    net_time.ntp_pcb = udp_new_ip_type(IPADDR_TYPE_ANY);
    if (net_time.ntp_pcb == NULL)
    {
        DPRINTF("Failed to allocate a new UDP control block.\n");
        return;
    }

    // Set up the callback function that will be called when an NTP response is received.
    udp_recv(net_time.ntp_pcb, ntp_recv_callback, &net_time);

    // Initialization success, set flag.
    net_time.ntp_server_found = false;
    net_time.ntp_error = false;
    DPRINTF("NTP UDP control block initialized and callback set.\n");
}

void set_internal_rtc()
{
    // Begin LwIP operation
    cyw43_arch_lwip_begin();

    // Allocate a pbuf for the NTP request.
    struct pbuf *pb = pbuf_alloc(PBUF_TRANSPORT, NTP_MSG_LEN, PBUF_RAM);
    if (!pb)
    {
        DPRINTF("Failed to allocate pbuf for NTP request.\n");
        cyw43_arch_lwip_end();
        return; // Early exit if pbuf allocation fails
    }

    // Prepare the NTP request.
    uint8_t *req = (uint8_t *)pb->payload;
    memset(req, 0, NTP_MSG_LEN);
    req[0] = 0x1b; // NTP request header for a client request

    // Send the NTP request.
    err_t err = udp_sendto(net_time.ntp_pcb, pb, &net_time.ntp_ipaddr, ntp_server_port);
    if (err != ERR_OK)
    {
        DPRINTF("Failed to send NTP request: %s\n", lwip_strerr(err));
        pbuf_free(pb); // Clean up the pbuf
        cyw43_arch_lwip_end();
        return; // Early exit if sending fails
    }

    // Free the pbuf after sending.
    pbuf_free(pb);

    // End LwIP operation.
    cyw43_arch_lwip_end();

    DPRINTF("NTP request sent successfully.\n");
}

void set_ikb_datetime_msg(uint8_t *rtc_time_ptr, int16_t gemdos_version)
{
    DPRINTF("GEMDOS version: %x\n", gemdos_version);
    rtc_get_datetime(&rtc_time);

    DPRINTF("RP2040 RTC set to: %02d/%02d/%04d %02d:%02d:%02d UTC+0\n",
                    rtc_time.day, 
                    rtc_time.month, 
                    rtc_time.year, 
                    rtc_time.hour, 
                    rtc_time.min, 
                    rtc_time.sec);

    // Now set the MSDOS time format after the BCD format
    uint32_t msdos_datetime = 0;

    // Convert the RTC time to MSDOS datetime format
    uint16_t msdos_date = ((rtc_time.year - 1980) << 9) | (rtc_time.month << 5) | (rtc_time.day);
    uint16_t msdos_time = (rtc_time.hour << 11) | (rtc_time.min << 5) | (rtc_time.sec / 2);

    // Change order for the endianess
    rtc_time_ptr[1] = 0x1b;

    // If negative number, it is EmuTOS
    if ((gemdos_version >= 0) && (y2k_patch_enabled)) {
        DPRINTF("Applying Y2K fix in the date\n");
        rtc_time_ptr[0] = add_bcd(to_bcd((rtc_time.year % 100)), to_bcd((2000 - 1980) + (80 - 30))); // Fix Y2K issue
    } else {
        DPRINTF("Not applying Y2K fix in the date\n");
        rtc_time_ptr[0] = to_bcd(rtc_time.year % 100); // EmuTOS already handles the Y2K issue 
        // If the TOS is EmuTOS, then we disable the Y2K fix
        *((volatile uint32_t *)(memory_shared_address + RTCEMUL_Y2K_PATCH)) = 0;
    }
    rtc_time_ptr[3] = to_bcd(rtc_time.month);
    rtc_time_ptr[2] = to_bcd(rtc_time.day);
    rtc_time_ptr[5] = to_bcd(rtc_time.hour);
    rtc_time_ptr[4] = to_bcd(rtc_time.min);
    rtc_time_ptr[7] = to_bcd(rtc_time.sec);
    rtc_time_ptr[6] = 0x0;

    // Store MSDOS datetime into shared memory
    msdos_datetime = (msdos_date << 16) | msdos_time;
    WRITE_AND_SWAP_LONGWORD(memory_shared_address, RTCEMUL_DATETIME_MSDOS, msdos_datetime);
    DPRINTF("MSDOS datetime: 0x%08x\n", msdos_datetime);
}


static void set_shared_var(uint32_t p_shared_variable_index, uint32_t p_shared_variable_value, uint32_t memory_shared_address)
{
    DPRINTF("Setting shared variable %d to %x\n", p_shared_variable_index, p_shared_variable_value);
    *((volatile uint16_t *)(memory_shared_address + RTCEMUL_SHARED_VARIABLES + (p_shared_variable_index * 4) + 2)) = p_shared_variable_value & 0xFFFF;
    *((volatile uint16_t *)(memory_shared_address + RTCEMUL_SHARED_VARIABLES + (p_shared_variable_index * 4))) = p_shared_variable_value >> 16;
}

static void get_shared_var(uint32_t p_shared_variable_index, uint32_t *p_shared_variable_value, uint32_t memory_shared_address)
{
    *p_shared_variable_value = *((volatile uint16_t *)(memory_shared_address + RTCEMUL_SHARED_VARIABLES + (p_shared_variable_index * 4))) << 16;
    *p_shared_variable_value |= *((volatile uint16_t *)(memory_shared_address + RTCEMUL_SHARED_VARIABLES + (p_shared_variable_index * 4) + 2));
    DPRINTF("Getting shared variable %d with value %x\n", p_shared_variable_index, *p_shared_variable_value);
}


static void __not_in_flash_func(handle_protocol_command)(const TransmissionProtocol *protocol)
{
    ConfigEntry *entry = NULL;
    uint16_t value_payload = 0;
    // Handle the protocol
    switch (protocol->command_id)
    {
    case RTCEMUL_TEST_NTP:
    {
        DPRINTF("Command TEST_NTP (%i) received: %d\n", protocol->command_id, protocol->payload_size);
        random_token = ((*((uint32_t *)protocol->payload) & 0xFFFF0000) >> 16) | ((*((uint32_t *)protocol->payload) & 0x0000FFFF) << 16);
        test_ntp_received = true;
        break;
    }
    case RTCEMUL_READ_TIME:
    {
        DPRINTF("Command READ_TIME (%i) received: %d\n", protocol->command_id, protocol->payload_size);
        random_token = ((*((uint32_t *)protocol->payload) & 0xFFFF0000) >> 16) | ((*((uint32_t *)protocol->payload) & 0x0000FFFF) << 16);
        read_time_received = true;
        break; // ... handle other commands
    }
    case RTCEMUL_SAVE_VECTORS:
    {
        // Save the vectors needed for the RTC emulation
        DPRINTF("Command SAVE_VECTORS (%i) received: %d\n", protocol->command_id, protocol->payload_size);
        payloadPtr = (uint16_t *)protocol->payload + 2;
        XBIOS_trap_payload = ((uint32_t)payloadPtr[0] << 16) | payloadPtr[1];
        random_token = ((*((uint32_t *)protocol->payload) & 0xFFFF0000) >> 16) | ((*((uint32_t *)protocol->payload) & 0x0000FFFF) << 16);
        save_vectors = true;
        break;
    }
    case RTCEMUL_REENTRY_LOCK:
    {
        DPRINTF("Command REENTRY_LOCK (%i) received: %d\n", protocol->command_id, protocol->payload_size);
        random_token = ((*((uint32_t *)protocol->payload) & 0xFFFF0000) >> 16) | ((*((uint32_t *)protocol->payload) & 0x0000FFFF) << 16);
        reentry_locked = true;
        break;
    }
    case RTCEMUL_REENTRY_UNLOCK:
    {
        DPRINTF("Command REENTRY_UNLOCK (%i) received: %d\n", protocol->command_id, protocol->payload_size);
        random_token = ((*((uint32_t *)protocol->payload) & 0xFFFF0000) >> 16) | ((*((uint32_t *)protocol->payload) & 0x0000FFFF) << 16);
        reentry_unlocked = true;
        break;
    }
    case RTCEMUL_SET_SHARED_VAR:
        {
            DPRINTF("Command SET_SHARED_VAR (%i) received: %d\n", protocol->command_id, protocol->payload_size);
            payloadPtr = (uint16_t *)protocol->payload + 2;
            // Shared variables
            uint32_t shared_variable_index = ((uint32_t)payloadPtr[1] << 16) | payloadPtr[0]; // d3 register
            payloadPtr += 2;                                                                  // Skip two words
            uint32_t shared_variable_value = ((uint32_t)payloadPtr[1] << 16) | payloadPtr[0]; // d4 register
            set_shared_var(shared_variable_index, shared_variable_value, memory_shared_address);
            random_token = ((*((uint32_t *)protocol->payload) & 0xFFFF0000) >> 16) | ((*((uint32_t *)protocol->payload) & 0x0000FFFF) << 16);
            *((volatile uint32_t *)(memory_shared_address + RTCEMUL_RANDOM_TOKEN)) = random_token;
            break;
        }
    default:
        DPRINTF("Unknown command: %d\n", protocol->command_id);
    }
}

// Function to convert a binary number to BCD format
inline uint8_t to_bcd(uint8_t val)
{
    return ((val / 10) << 4) | (val % 10);
}

// Function to add two BCD values
inline uint8_t add_bcd(uint8_t bcd1, uint8_t bcd2)
{
    uint8_t low_nibble = (bcd1 & 0x0F) + (bcd2 & 0x0F);
    uint8_t high_nibble = (bcd1 & 0xF0) + (bcd2 & 0xF0);

    if (low_nibble > 9)
    {
        low_nibble += 6;
    }

    high_nibble += (low_nibble & 0xF0); // Add carry to high nibble
    low_nibble &= 0x0F;                 // Keep only the low nibble

    if ((high_nibble & 0x1F0) > 0x90)
    {
        high_nibble += 0x60;
    }

    return (high_nibble & 0xF0) | (low_nibble & 0x0F);
}

// Function to subtract two BCD values
inline uint8_t sub_bcd(uint8_t bcd1, uint8_t bcd2)
{
    uint8_t low_nibble = (bcd1 & 0x0F) - (bcd2 & 0x0F);
    uint8_t high_nibble = (bcd1 & 0xF0) - (bcd2 & 0xF0);

    if (low_nibble > 9) // Handle borrow in the low nibble
    {
        low_nibble -= 6;
        high_nibble -= 0x10; // Borrow from the high nibble
    }

    if (high_nibble > 0x90) // Handle borrow in the high nibble
    {
        high_nibble -= 0x60;
    }

    return (high_nibble & 0xF0) | (low_nibble & 0x0F);
}


// WARNING: CHANGE THIS OFFSET WITH CAUTION
// The offset_sync is the time the RP2040 takes to read and process the time and date from the internal RTC
// and put it in the clock_sequence_dallas_rtc array. This offset is calculated by trial and error.
// So if you change the code, you have to recalculate this offset.
static uint8_t offset_sync = 3;

// Function to populate the magic_sequence_dallas_rtc
static void populate_magic_sequence(uint8_t *sequence, uint64_t hex_value)
{
    // Loop through each bit of the 64-bit hex value. Leave the first two bits untouched
    for (int i = 2; i < 66; i++)
    {
        // Check if the bit is 0 or 1 by shifting hex_value right i positions
        // and checking the least significant bit
        if ((hex_value >> (i - 2)) & 1)
        {
            sequence[i] = dallasClock.write_address_bit_one; // If the bit is 1
        }
        else
        {
            sequence[i] = dallasClock.write_address_bit_zero; // If the bit is 0
        }
    }
}

// Function to populate the clock_sequence_dallas_rtc
static void populate_clock_sequence(uint8_t *sequence, uint8_t bcd_value)
{
    // Loop through each bit of the 8 bit BDC value
    for (int i = 0; i < 8; i++)
    {
        // Check if the bit is 0 or 1 by shifting bcd_value right i positions
        // and checking the least significant bit
        if ((bcd_value >> i) & 1)
        {
            sequence[i] = 0xFF; // If the bit is 1
        }
        else
        {
            sequence[i] = 0x0; // If the bit is 0
        }
    }
}

// Interrupt handler callback for DMA completion
void __not_in_flash_func(rtcemul_dma_irq_handler_lookup_callback)(void)
{
    // Clear the interrupt request for the channel
    dma_hw->ints1 = 1u << lookup_data_rom_dma_channel;

    // Read the address to process
    uint32_t addr = (uint32_t)dma_hw->ch[lookup_data_rom_dma_channel].al3_read_addr_trig;
    switch (rtc_type)
    {
    case RTC_SIDECART:
        if (addr >= ROM3_START_ADDRESS)
        {
            parse_protocol((uint16_t)(addr & 0xFFFF), handle_protocol_command);
        }
        break;
    case RTC_DALLAS:
        if (addr >= dallasClock.rom_address)
        {
            // Reset counter
            uint64_t new_header_found = time_us_64();
            if (new_header_found - dallasClock.last_magic_found > PROTOCOL_READ_RESTART_MICROSECONDS)
            {
                dallasClock.last_magic_found = new_header_found;
                dallasClock.retries = 0;
            }

            uint8_t addr_lsb = addr; // Only the LSB is needed
            // Check the magic sequence of the Dallas RTC
            if ((dallasClock.magic_sequence[dallasClock.retries] == addr_lsb) && (dallasClock.retries < dallasClock.size_magic_sequence))
            {
                dallasClock.retries++;
                if (dallasClock.retries == dallasClock.size_magic_sequence)
                {
                    // We have the magic sequence, now we have to put the time and date of the internal RTC mimicking the Dallas RTC clock
                    // 32 bit the date and 32 bit the time

                    bool ready = rtc_get_datetime(&rtc_time);
                    if (ready)
                    {
                        uint8_t cent_seconds = 0;
                        uint8_t seconds = to_bcd(rtc_time.sec);
                        uint8_t minutes = to_bcd(rtc_time.min);
                        uint8_t hours = to_bcd(rtc_time.hour);
                        uint8_t dotw = to_bcd(rtc_time.dotw);
                        uint8_t day = to_bcd(rtc_time.day);
                        uint8_t month = to_bcd(rtc_time.month);
                        uint8_t year = to_bcd(rtc_time.year % 100);

                        // Set the values in the clock_sequence_dallas_rtc
                        // populate_clock_sequence(&dallasClock.clock_sequence[0], cent_seconds);
                        populate_clock_sequence(&dallasClock.clock_sequence[8 - offset_sync], seconds);
                        populate_clock_sequence(&dallasClock.clock_sequence[16 - offset_sync], minutes);
                        populate_clock_sequence(&dallasClock.clock_sequence[24 - offset_sync], hours);
                        populate_clock_sequence(&dallasClock.clock_sequence[32 - offset_sync], dotw);
                        populate_clock_sequence(&dallasClock.clock_sequence[40 - offset_sync], day);
                        populate_clock_sequence(&dallasClock.clock_sequence[48 - offset_sync], month);
                        populate_clock_sequence(&dallasClock.clock_sequence[56 - offset_sync], year);

                        dallasClock.retries = 0;
                    }
                    else
                    {
                        DPRINTF("Cannot get internal RTC!\n");
                    }
                }
            }
            else
            {
                // Now we have to put the time and date of the internal RTC mimicking the Dallas RTC clock
                if (dallasClock.retries <= (dallasClock.size_magic_sequence + dallasClock.size_clock_sequence))
                {
                    uint32_t clock_read_address = dallasClock.rom_address + dallasClock.read_address_bit;
                    (*((volatile uint8_t *)(clock_read_address))) = dallasClock.clock_sequence[dallasClock.retries - dallasClock.size_magic_sequence];
                    // 32 bit the date and 32 bit the time
                    dallasClock.retries++;
                }
            }
        }
        break;
    default:
        break;
    }
}

int init_rtcemul(bool safe_config_reboot)
{
    memory_shared_address = ROM3_START_ADDRESS;
    *((volatile uint16_t *)(memory_shared_address + RTCEMUL_REENTRY_TRAP)) = 0x0;
    uint8_t *rtc_time_ptr = (uint8_t *)(memory_shared_address + RTCEMUL_DATETIME_BCD);
    set_shared_var(SHARED_VARIABLE_HARDWARE_TYPE, 0, memory_shared_address);
    set_shared_var(SHARED_VARIABLE_SVERSION, 0, memory_shared_address);
    set_shared_var(SHARED_VARIABLE_BUFFER_TYPE, 0, memory_shared_address); // 0: Diskbuffer, 1: Stack. But useless in the RTC

    bool write_config_only_once = true;

    FRESULT fr;
    FATFS fs;

    ConfigEntry *y2k_patch = find_entry(PARAM_RTC_Y2K_PATCH);
    if (y2k_patch != NULL)
    {
        char *str = y2k_patch->value;
        y2k_patch_enabled = ((y2k_patch!=NULL) && (strlen(y2k_patch->value)) && ((str[0] == 'T') || (str[0] == 't'))) ? true : false;
    }
    else {
        y2k_patch_enabled = true;
        DPRINTF("Y2K patch enabled by default\n");
    }
    DPRINTF("Y2K patch enabled: %s\n", y2k_patch_enabled ? "true" : "false");
    *((volatile uint32_t *)(memory_shared_address + RTCEMUL_Y2K_PATCH)) = y2k_patch_enabled ? 0xFFFFFFFF : 0;


    srand(time(0));
    char *rtc_type_str = find_entry(PARAM_RTC_TYPE)->value;
    if (strcmp(rtc_type_str, "DALLAS") == 0)
    {
        DPRINTF("RTC type: DALLAS\n");

        rtc_type = RTC_DALLAS;
        // Initialize the Dallas RTC clock structure
        dallasClock.last_magic_found = 0;
        dallasClock.retries = 0;
        dallasClock.magic_sequence_hex = 0x5ca33ac55ca33ac5;
        dallasClock.read_address_bit = 0x9;
        dallasClock.write_address_bit_zero = 0x1;
        dallasClock.write_address_bit_one = 0x3;
        dallasClock.size_magic_sequence = sizeof(dallasClock.magic_sequence);
        dallasClock.size_clock_sequence = sizeof(dallasClock.clock_sequence);
        dallasClock.rom_address = ROM3_START_ADDRESS;

        // Populate the magic_sequence_dallas_rtc array
        populate_magic_sequence(dallasClock.magic_sequence, dallasClock.magic_sequence_hex);
    }
    else if (strcmp(rtc_type_str, "SIDECART") == 0)
    {
        DPRINTF("RTC type: SIDECART\n");
        rtc_type = RTC_SIDECART;
    }
    else
    {
        DPRINTF("RTC type: UNKNOWN\n");
        rtc_type = RTC_UNKNOWN;
    }

    DPRINTF("\n");

    // Initialize SD card
    microsd_initialized = sd_init_driver();
    if (!microsd_initialized)
    {
        DPRINTF("ERROR: Could not initialize SD card\r\n");
    }

    if (microsd_initialized)
    {
        // Mount drive
        fr = f_mount(&fs, "0:", 1);
        microsd_mounted = (fr == FR_OK);
        if (!microsd_mounted)
        {
            DPRINTF("ERROR: Could not mount filesystem (%d)\r\n", fr);
        }
    }

    if (microsd_mounted)
    {
        FRESULT err = read_and_trim_file(WIFI_PASS_FILE_NAME, &wifi_password_file_content, MAX_WIFI_PASSWORD_LENGTH);
        if (err == FR_OK)
        {
            DPRINTF("Wifi password file found. Content: %s\n", wifi_password_file_content);
        }
        else
        {
            DPRINTF("Wifi password file not found.\n");
        }
    }

    uint32_t rtc_timeout_sec = 45;
    // Only try to get the datetime from the network if the wifi is configured
    if (strlen(find_entry(PARAM_WIFI_SSID)->value) > 0)
    {
        cyw43_arch_deinit();

        network_init(true, NETWORK_CONNECTION_ASYNC, &wifi_password_file_content);
        absolute_time_t ABSOLUTE_TIME_INITIALIZED_VAR(reconnect_t, 0);
        absolute_time_t ABSOLUTE_TIME_INITIALIZED_VAR(second_t, 0);
        uint32_t time_to_connect_again = 1000; // 1 second
        bool network_ready = false;
        bool wifi_init = true;
        uint32_t wifi_timeout_sec = rtc_timeout_sec;

        // Wait until timeout
        while ((!network_ready) && (wifi_timeout_sec > 0) && (strlen(find_entry(PARAM_WIFI_SSID)->value) > 0))
        {
            *((volatile uint32_t *)(memory_shared_address + RTCEMUL_RANDOM_TOKEN_SEED)) = rand() % 0xFFFFFFFF;
#if PICO_CYW43_ARCH_POLL
            if (wifi_init)
            {
                cyw43_arch_poll();
            }
#endif
            // Only display when changes status to avoid flooding the console
            ConnectionStatus previous_status = get_previous_connection_status();
            ConnectionStatus current_status = get_network_connection_status();
            if (current_status != previous_status)
            {
#if defined(_DEBUG) && (_DEBUG != 0)
                ConnectionData connection_data = {0};
                get_connection_data(&connection_data);
                DPRINTF("Status: %d - Prev: %d - SSID: %s - IPv4: %s - GW:%s - Mask:%s - MAC:%s\n",
                        current_status,
                        previous_status,
                        connection_data.ssid,
                        connection_data.ipv4_address,
                        print_ipv4(get_gateway()),
                        print_ipv4(get_netmask()),
                        print_mac(get_mac_address()));
#endif
                if ((current_status == GENERIC_ERROR) || (current_status == CONNECT_FAILED_ERROR) || (current_status == BADAUTH_ERROR))
                {
                    if (wifi_init)
                    {
                        cyw43_arch_deinit();
                        reconnect_t = make_timeout_time_ms(0);
                        time_to_connect_again = time_to_connect_again * 1.2;
                        wifi_init = false;
                        DPRINTF("Connection failed. Retrying in %d ms...\n", time_to_connect_again);
                    }
                }
            }
            network_ready = (current_status == CONNECTED_WIFI_IP);
            if (time_passed(&second_t, 1000) == 1)
            {
                DPRINTF("Timeout in seconds: %d\n", wifi_timeout_sec);
                wifi_timeout_sec--;
                second_t = make_timeout_time_ms(0);
            }

            if (test_ntp_received)
            {
                test_ntp_received = false;
                if (rtc_time.year != 0)
                {
                    *((volatile uint16_t *)(memory_shared_address + RTCEMUL_NTP_SUCCESS)) = 0xFFFF;
                }
                else
                {
                    *((volatile uint16_t *)(memory_shared_address + RTCEMUL_NTP_SUCCESS)) = 0x0;
                }
                DPRINTF("NTP test received. Answering with: %d\n", *((volatile uint16_t *)(memory_shared_address + RTCEMUL_NTP_SUCCESS)));
                *((volatile uint32_t *)(memory_shared_address + RTCEMUL_RANDOM_TOKEN)) = random_token;
            }

            // If SELECT button is pressed, launch the configurator
            if (gpio_get(SELECT_GPIO) != 0)
            {
                select_button_action(safe_config_reboot, write_config_only_once);
                // Write config only once to avoid hitting the flash too much
                write_config_only_once = false;
            }

            if ((!wifi_init) && (time_passed(&reconnect_t, time_to_connect_again) == 1))
            {
                network_init(true, NETWORK_CONNECTION_ASYNC, &wifi_password_file_content);
                reconnect_t = make_timeout_time_ms(0);
                wifi_init = true;
            }
            *((volatile uint16_t *)(memory_shared_address + RTCEMUL_NTP_SUCCESS)) = 0x0;
            *((volatile uint32_t *)(memory_shared_address + RTCEMUL_RANDOM_TOKEN)) = random_token;
        }
        if (wifi_timeout_sec <= 0)
        {
            // Just be sure to deinit the network stack
            cyw43_arch_deinit();
            DPRINTF("No wifi configured. Skipping network initialization.\n");
        }
        else
        {
            // We have network connection!
            // Start the internal RTC
            rtc_init();

            ntp_server_host = find_entry(PARAM_RTC_NTP_SERVER_HOST)->value;
            ntp_server_port = atoi(find_entry(PARAM_RTC_NTP_SERVER_PORT)->value);

            DPRINTF("NTP server host: %s\n", ntp_server_host);
            DPRINTF("NTP server port: %d\n", ntp_server_port);

            char *utc_offset_entry = find_entry(PARAM_RTC_UTC_OFFSET)->value;
            if (strlen(utc_offset_entry) > 0)
            {
                // The offset can be in decimal format
                set_utc_offset_seconds((long)(atoi(utc_offset_entry) * 60 * 60));
            }
            DPRINTF("UTC offset: %ld\n", get_utc_offset_seconds());

            // Start the NTP client
            ntp_init();
            get_net_time()->ntp_server_found = false;

            bool dns_query_done = false;

            // Wait until the RTC is set by the NTP server
            while ((rtc_timeout_sec > 0) && (get_rtc_time()->year == 0))
            {

#if PICO_CYW43_ARCH_POLL
                network_safe_poll();
#endif
                if ((get_net_time()->ntp_server_found) && dns_query_done)
                {
                    DPRINTF("NTP server found. Connecting to NTP server...\n");
                    get_net_time()->ntp_server_found = false;
                    set_internal_rtc();
                }
                // Get the IP address from the DNS server if the wifi is connected and no IP address is found yet
                if (!(dns_query_done))
                {
                    // Let's connect to ntp server
                    DPRINTF("Querying the DNS...\n");
                    err_t dns_ret = dns_gethostbyname(ntp_server_host, &get_net_time()->ntp_ipaddr, host_found_callback, get_net_time());
#if PICO_CYW43_ARCH_POLL
                    network_safe_poll();
#endif
                    if (dns_ret == ERR_ARG)
                    {
                        DPRINTF("Invalid DNS argument\n");
                    }
                    DPRINTF("DNS query done\n");
                    dns_query_done = true;
                }
                if (get_net_time()->ntp_error)
                {
                    DPRINTF("Error getting the NTP server IP address\n");
                    dns_query_done = false;
                    get_net_time()->ntp_error = false;
                    get_net_time()->ntp_server_found = false;
                }
                // If SELECT button is pressed, launch the configurator
                if (gpio_get(SELECT_GPIO) != 0)
                {
                    select_button_action(safe_config_reboot, write_config_only_once);
                    // Write config only once to avoid hitting the flash too much
                    write_config_only_once = false;
                }
            }
            if (get_rtc_time()->year != 0)
            {
                DPRINTF("RTC set by NTP server\n");
                // Set the RTC time for the Atari ST to read
                uint32_t gemdos_version = 0;
                get_shared_var(SHARED_VARIABLE_SVERSION, &gemdos_version, memory_shared_address);
                DPRINTF("Shared variable SVERSION: %x\n", gemdos_version);
                set_ikb_datetime_msg(rtc_time_ptr, (int16_t)gemdos_version);
            }
            else
            {
                DPRINTF("Timeout reached. RTC not set.\n");
                cyw43_arch_deinit();
                DPRINTF("No wifi configured. Skipping network initialization.\n");
            }
        }
    }
    else
    {
        // Just be sure to deinit the network stack
        cyw43_arch_deinit();
        DPRINTF("No wifi configured. Skipping network initialization.\n");
    }

    bool rtc_error = false;
    DPRINTF("Waiting for commands...\n");
    while (!rtc_error)
    {
        *((volatile uint32_t *)(memory_shared_address + RTCEMUL_RANDOM_TOKEN_SEED)) = rand() % 0xFFFFFFFF;
        tight_loop_contents();
        if (save_vectors)
        {
            save_vectors = false;
            // Save the vectors needed for the RTC emulation
            DPRINTF("Saving vectors\n");
            *((volatile uint16_t *)(memory_shared_address + RTCEMUL_OLD_XBIOS_TRAP)) = XBIOS_trap_payload & 0xFFFF;
            *((volatile uint16_t *)(memory_shared_address + RTCEMUL_OLD_XBIOS_TRAP + 2)) = XBIOS_trap_payload >> 16;
            // DPRINTF("random token: %x\n", random_token);
            *((volatile uint32_t *)(memory_shared_address + RTCEMUL_RANDOM_TOKEN)) = random_token;
        }

        if (test_ntp_received)
        {
            test_ntp_received = false;
            if (rtc_time.year != 0)
            {
                *((volatile uint16_t *)(memory_shared_address + RTCEMUL_NTP_SUCCESS)) = 0xFFFF;
            }
            else
            {
                *((volatile uint16_t *)(memory_shared_address + RTCEMUL_NTP_SUCCESS)) = 0x0;
            }
            DPRINTF("NTP test received. Answering with: %d\n", *((volatile uint16_t *)(memory_shared_address + RTCEMUL_NTP_SUCCESS)));
            *((volatile uint32_t *)(memory_shared_address + RTCEMUL_RANDOM_TOKEN)) = random_token;
        }

        if (read_time_received)
        {
            read_time_received = false;

            uint32_t gemdos_version = 0;
            get_shared_var(SHARED_VARIABLE_SVERSION, &gemdos_version, memory_shared_address);
            DPRINTF("Shared variable SVERSION: %x\n", gemdos_version);
            gemdos_version = gemdos_version & 0x0000FFFF;
            set_ikb_datetime_msg(rtc_time_ptr, (int16_t)gemdos_version);

            *((volatile uint32_t *)(memory_shared_address + RTCEMUL_RANDOM_TOKEN)) = random_token;
        }

        if (reentry_locked)
        {
            reentry_locked = false;
            *((volatile uint16_t *)(memory_shared_address + RTCEMUL_REENTRY_TRAP)) = 0xFFFF;
            DPRINTF("Reentry locked\n");
            *((volatile uint32_t *)(memory_shared_address + RTCEMUL_RANDOM_TOKEN)) = random_token;
        }

        if (reentry_unlocked)
        {
            reentry_unlocked = false;
            *((volatile uint16_t *)(memory_shared_address + RTCEMUL_REENTRY_TRAP)) = 0x0;
            DPRINTF("Reentry unlocked\n");
            *((volatile uint32_t *)(memory_shared_address + RTCEMUL_RANDOM_TOKEN)) = random_token;
        }

        // If SELECT button is pressed, launch the configurator
        if (gpio_get(SELECT_GPIO) != 0)
        {
            select_button_action(safe_config_reboot, write_config_only_once);
            // Write config only once to avoid hitting the flash too much
            write_config_only_once = false;
        }
    }

    blink_error();
}
