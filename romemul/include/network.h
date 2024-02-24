/**
 * File: network.h
 * Author: Diego Parrilla Santamaría
 * Date: August 2023
 * Copyright: 2023 - GOODDATA LABS SL
 * Description: Header for network.c which starts the network stack
 */

#ifndef NETWORK_H
#define NETWORK_H

#include "debug.h"
#include "constants.h"
#include "firmware.h"
#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON/cJSON.h"

#include "pico/stdlib.h"
#include "hardware/clocks.h"
#include "hardware/vreg.h"

#include "pico/cyw43_arch.h"
#include "lwip/apps/http_client.h"
#include "lwip/dns.h"

#include "sd_card.h"
#include "f_util.h"

#define MAX_NETWORKS 100
#define MAX_SSID_LENGTH 36
#define MAX_BSSID_LENGTH 20
#define IPV4_ADDRESS_LENGTH 16
#define IPV6_ADDRESS_LENGTH 40
#define WIFI_SCAN_POLL_COUNTER 15 // 15 seconds
#define WIFI_SCAN_POLL_COUNTER_STR STRINGIFY(WIFI_SCAN_POLL_COUNTER)
#define WIFI_SCAN_POLL_COUNTER_MIN 5 // seconds
#define NETWORK_POLL_INTERVAL 5      // seconds
#define NETWORK_POLL_INTERVAL_STR STRINGIFY(NETWORK_POLL_INTERVAL)
#define NETWORK_POLL_INTERVAL_MIN 3 // seconds
#define NETWORK_CONNECTION_ASYNC true
#define NETWORK_CONNECTION_TIMEOUT 5000 // 5000 milliseconds
#define FIRMWARE_RELEASE_VERSION_URL "https://api.github.com/repos/diegoparrilla/atarist-sidecart-raspberry-pico/releases/latest"

typedef enum
{
    DISCONNECTED,
    CONNECTING,
    CONNECTED_WIFI,
    CONNECTED_WIFI_NO_IP,
    CONNECTED_WIFI_IP,
    TIMEOUT_ERROR,
    GENERIC_ERROR,
    NO_DATA_ERROR,
    NOT_PERMITTED_ERROR,
    INVALID_ARG_ERROR,
    IO_ERROR,
    BADAUTH_ERROR,
    CONNECT_FAILED_ERROR,
    INSUFFICIENT_RESOURCES_ERROR,
    NOT_SUPPORTED
} ConnectionStatus;

typedef struct
{
    char ssid[MAX_SSID_LENGTH];   // SSID can have up to 32 characters + null terminator
    char bssid[MAX_BSSID_LENGTH]; // BSSID in the format xx:xx:xx:xx:xx:xx + null terminator
    uint16_t auth_mode;           // MSB is not used, the data is in the LSB
} WifiNetworkInfo;

typedef struct
{
    char ssid[MAX_SSID_LENGTH];     // SSID to connect
    char password[MAX_SSID_LENGTH]; // Password
    uint16_t auth_mode;             // auth mode
} WifiNetworkAuthInfo;

typedef struct
{
    uint32_t magic; // Some magic value for identification/validation
    WifiNetworkInfo networks[MAX_NETWORKS];
    __uint16_t count; // The number of networks found/stored
} WifiScanData;

// Adjust alignment to 4 bytes always to pass the whole structure to the Atari ST
typedef struct connection_data
{
    char ssid[MAX_SSID_LENGTH];                     // SSID to connect
    char ipv4_address[IPV4_ADDRESS_LENGTH];         // IP address
    char ipv6_address[IPV6_ADDRESS_LENGTH];         // IPv6 address
    char mac_address[MAX_BSSID_LENGTH];             // MAC address
    char gw_ipv4_address[IPV4_ADDRESS_LENGTH];      // Gateway IP address
    char gw_ipv6_address[IPV6_ADDRESS_LENGTH];      // Gateway IPv6 address
    char netmask_ipv4_address[IPV4_ADDRESS_LENGTH]; // Netmask IP address
    char netmask_ipv6_address[IPV6_ADDRESS_LENGTH]; // Netmask IPv6 address
    char dns_ipv4_address[IPV4_ADDRESS_LENGTH];     // DNS IP address
    char dns_ipv6_address[IPV6_ADDRESS_LENGTH];     // DNS IPv6 address
    char wifi_country[4];                           // WiFi country iso-3166-1 alpha-2 code. 2 characters. Example: ES. 2 extra characters for padding
    uint16_t wifi_auth_mode;                        // WiFi auth mode (WPA, WPA2, WPA3, etc)
    uint16_t wifi_scan_interval;                    // WiFi scan interval in seconds
    uint16_t network_status_poll_interval;          // Network status poll interval in seconds
    uint16_t network_status;                        // Network connection status
    uint16_t file_downloading_timeout;              // File downloading timeout in seconds
    uint16_t padding;                               // Padding to align to 4 bytes
} ConnectionData;

typedef struct
{
    char *url;
    char *name;
    char *description;
    // Ignoring tags as per your request
    char *tags;
    int size_kb;
} RomInfo;

typedef struct
{
    char *name;
    char *status;
    char *description;
    char *tags;
    char *extra;
    char *url;
    void *next;
} FloppyImageInfo;

typedef struct
{
    char *protocol;
    char *domain;
    char *uri;
} UrlParts;

extern WifiScanData wifiScanData;

ConnectionStatus get_network_connection_status();
ConnectionStatus get_previous_connection_status();
void network_swap_auth_data(uint16_t *dest_ptr_word);
void network_swap_data(uint16_t *dest_ptr_word, uint16_t total_items);
void network_swap_connection_data(uint16_t *dest_ptr_word);
void network_swap_json_data(uint16_t *dest_ptr_word);

void network_init();
void network_scan();
void network_connect(bool force, bool async, char **pass);
void network_disconnect();
void network_poll();
u_int32_t get_ip_address();
u_int32_t get_netmask();
u_int32_t get_gateway();
u_int8_t *get_mac_address();
char *print_ipv4(uint32_t ip);
char *print_mac(uint8_t *mac_address);
void get_connection_data(ConnectionData *connection_data);
void show_connection_data(ConnectionData *connection_data);
uint16_t get_wifi_scan_poll_secs();
uint32_t get_network_status_polling_ms();
void get_json_files(RomInfo **items, int *itemCount, const char *url);
char *get_latest_release(void);
int download_rom(const char *url, uint32_t rom_load_offset);
int download_floppy(const char *url, const char *folder, const char *dest_filename, bool overwrite_flag);
err_t get_floppy_db_files(FloppyImageInfo **items, int *itemCount, const char *url);

void freeRomItem(RomInfo *item);

#endif // NETWORK_H
