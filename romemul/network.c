#include "include/network.h"

static ConnectionStatus connection_status = DISCONNECTED;
static ConnectionStatus previous_connection_status = NOT_SUPPORTED;
WifiScanData wifiScanData;

u_int32_t get_auth_pico_code(u_int16_t connect_code)
{
    switch (connect_code)
    {
    case 0:
        return CYW43_AUTH_OPEN;
    case 1:
    case 2:
        return CYW43_AUTH_WPA_TKIP_PSK;
    case 3:
    case 4:
    case 5:
        return CYW43_AUTH_WPA2_AES_PSK;
    case 6:
    case 7:
    case 8:
        return CYW43_AUTH_WPA2_MIXED_PSK;
    default:
        return CYW43_AUTH_OPEN;
    }
}

ConnectionStatus get_connection_status()
{
    return connection_status;
}

ConnectionStatus get_previous_connection_status()
{
    return previous_connection_status;
}

void network_swap_auth_data(uint16_t *dest_ptr_word)
{
    // +2 is for the auth_mode type
    swap_words(dest_ptr_word, MAX_SSID_LENGTH + MAX_SSID_LENGTH + 2);
}
void network_swap_data(uint16_t *dest_ptr_word, uint16_t total_items)
{
    // +4 is for the MAGIC and +2 each entry COUNT
    swap_words(dest_ptr_word, total_items * (MAX_SSID_LENGTH + MAX_BSSID_LENGTH + 2) + 4);
}

void network_swap_connection_data(uint16_t *dest_ptr_word)
{
    // No need to swap the uint16_t
    swap_words(dest_ptr_word, sizeof(ConnectionData) - sizeof(uint16_t) * 6);
}

void network_swap_json_data(uint16_t *dest_ptr_word)
{
    // No need to swap the connection status
    swap_words(dest_ptr_word, 4096);
}

uint32_t get_country_code(char *c, char **valid_country_str)
{
    *valid_country_str = "XX";
    // empty configuration select worldwide
    if (strlen(c) == 0)
    {
        return CYW43_COUNTRY_WORLDWIDE;
    }

    if (strlen(c) != 2)
    {
        return CYW43_COUNTRY_WORLDWIDE;
    }

    // current supported country code https://www.raspberrypi.com/documentation/pico-sdk/networking.html#CYW43_COUNTRY_
    // ISO-3166-alpha-2
    // XX select worldwide
    char *valid_country_code[] = {
        "XX", "AU", "AR", "AT", "BE", "BR", "CA", "CL",
        "CN", "CO", "CZ", "DK", "EE", "FI", "FR", "DE",
        "GR", "HK", "HU", "IS", "IN", "IL", "IT", "JP",
        "KE", "LV", "LI", "LT", "LU", "MY", "MT", "MX",
        "NL", "NZ", "NG", "NO", "PE", "PH", "PL", "PT",
        "SG", "SK", "SI", "ZA", "KR", "ES", "SE", "CH",
        "TW", "TH", "TR", "GB", "US"};

    char country[3] = {toupper(c[0]), toupper(c[1]), 0};
    for (int i = 0; i < (sizeof(valid_country_code) / sizeof(valid_country_code[0])); i++)
    {
        if (!strcmp(country, valid_country_code[i]))
        {
            *valid_country_str = valid_country_code[i];
            return CYW43_COUNTRY(country[0], country[1], 0);
        }
    }
    return CYW43_COUNTRY_WORLDWIDE;
}

void network_init()
{
    uint32_t country = CYW43_COUNTRY_WORLDWIDE;
    ConfigEntry *country_entry = find_entry(PARAM_WIFI_COUNTRY);
    if (country_entry != NULL)
    {
        char *valid;
        country = get_country_code(country_entry->value, &valid);
        put_string(PARAM_WIFI_COUNTRY, valid);
    }
    cyw43_wifi_set_up(&cyw43_state,
                      CYW43_ITF_STA,
                      true,
                      country);

    // Enable the STA mode
    cyw43_arch_enable_sta_mode();
    DPRINTF("STA network mode enabled\n");

    // Set hostname
    char *hostname = find_entry("HOSTNAME")->value;
    netif_set_hostname(netif_default, hostname + '\0');
    DPRINTF("Hostname: %s\n", hostname);

    // Initialize the scan data
    wifiScanData.magic = NETWORK_MAGIC;
    memset(wifiScanData.networks, 0, sizeof(wifiScanData.networks));
    wifiScanData.count = 0;
    DPRINTF("Scan data initialized\n");
}

void network_scan()
{
    int scan_result(void *env, const cyw43_ev_scan_result_t *result)
    {
        int bssid_exists(WifiNetworkInfo * network)
        {
            for (size_t i = 0; i < wifiScanData.count; i++)
            {
                if (strcmp(wifiScanData.networks[i].bssid, network->bssid) == 0)
                {
                    return 1; // BSSID found
                }
            }
            return 0; // BSSID not found
        }
        if (result && wifiScanData.count < MAX_NETWORKS)
        {
            WifiNetworkInfo network;

            // Copy SSID
            snprintf(network.ssid, sizeof(network.ssid), "%s", result->ssid);

            // Format BSSID
            snprintf(network.bssid, sizeof(network.bssid), "%02x:%02x:%02x:%02x:%02x:%02x",
                     result->bssid[0], result->bssid[1], result->bssid[2], result->bssid[3], result->bssid[4], result->bssid[5]);

            // Store authentication mode
            network.auth_mode = result->auth_mode;

            // Check if BSSID already exists
            if (!bssid_exists(&network))
            {
                if (strlen(network.ssid) > 0)
                {
                    wifiScanData.networks[wifiScanData.count] = network;
                    wifiScanData.count++;
                    DPRINTF("Found network %s\n", network.ssid);
                }
            }
        }
        return 0;
    }
    if (!cyw43_wifi_scan_active(&cyw43_state))
    {
        DPRINTF("Scanning networks...\n");
        cyw43_wifi_scan_options_t scan_options = {0};
        int err = cyw43_wifi_scan(&cyw43_state, &scan_options, NULL, scan_result);
        if (err == 0)
        {
            DPRINTF("Performing wifi scan\n");
        }
        else
        {
            DPRINTF("Failed to start scan: %d\n", err);
        }
    }
    else
    {
        DPRINTF("Scan already in progress\n");
    }
}

void network_disconnect()
{
    // The library seems to have a bug when disconnecting. It doesn't work. So I'm using the ioctl directly
    int custom_cyw43_wifi_leave(cyw43_t * self, int itf)
    {
        // Disassociate with SSID
        cyw43_wifi_set_up(self,
                          itf,
                          false,
                          CYW43_COUNTRY_WORLDWIDE);
        return cyw43_ioctl(self, 0x76, 0, NULL, itf);
    }

    int error = custom_cyw43_wifi_leave(&cyw43_state, CYW43_ITF_STA);
    if (error == 0)
    {
        DPRINTF("Disconnected\n");
    }
    else
    {
        DPRINTF("Failed to disconnect: %d\n", error);
    }
    connection_status = DISCONNECTED;
}

void network_connect(bool force, bool async, char **pass)
{
    if (!force)
    {
        if ((connection_status == CONNECTED_WIFI_IP))
        {
            DPRINTF("Already connected\n");
            return;
        }
    }

    ConfigEntry *ssid = find_entry(PARAM_WIFI_SSID);
    if (strlen(ssid->value) == 0)
    {
        DPRINTF("No SSID found in config. Can't connect\n");
        connection_status = DISCONNECTED;
        return;
    }
    ConfigEntry *auth_mode = find_entry(PARAM_WIFI_AUTH);
    connection_status = CONNECTING;
    char *password_value = NULL;
    if (*pass == NULL)
    {
        ConfigEntry *password = find_entry(PARAM_WIFI_PASSWORD);
        if (strlen(password->value) > 0)
        {
            password_value = strdup(password->value);
        }
        else
        {
            DPRINTF("No password found in config. Trying to connect without password\n");
        }
    }
    else
    {
        password_value = strdup(*pass);
    }
    DPRINTF("The password is: %s\n", password_value);
    uint32_t auth_value = get_auth_pico_code(atoi(auth_mode->value));
    DPRINTF("Connecting to SSID=%s, password=%s, auth=%08x\n", ssid->value, password_value, auth_value);
    int error_code = 0;
    if (!async)
    {
        error_code = cyw43_arch_wifi_connect_timeout_ms(ssid->value, password_value, auth_value, NETWORK_CONNECTION_TIMEOUT);
    }
    else
    {
        error_code = cyw43_arch_wifi_connect_async(ssid->value, password_value, auth_value);
    }

    if ((error_code == 0) && (async))
    {
        connection_status = CONNECTING;
        DPRINTF("Connecting to SSID=%s\n", ssid->value);
    }
    else
    {
        switch (error_code)
        {
        case PICO_ERROR_TIMEOUT:
            DPRINTF("Failed to connect to SSID=%s. Timeout\n", ssid->value);
            connection_status = TIMEOUT_ERROR;
            break;
        case PICO_ERROR_GENERIC:
            DPRINTF("Failed to connect to SSID=%s. Generic error\n", ssid->value);
            connection_status = GENERIC_ERROR;
            break;
        case PICO_ERROR_NO_DATA:
            DPRINTF("Failed to connect to SSID=%s. No data\n", ssid->value);
            connection_status = NO_DATA_ERROR;
            break;
        case PICO_ERROR_NOT_PERMITTED:
            DPRINTF("Failed to connect to SSID=%s. Not permitted\n", ssid->value);
            connection_status = NOT_PERMITTED_ERROR;
            break;
        case PICO_ERROR_INVALID_ARG:
            DPRINTF("Failed to connect to SSID=%s. Invalid argument\n", ssid->value);
            connection_status = INVALID_ARG_ERROR;
            break;
        case PICO_ERROR_IO:
            DPRINTF("Failed to connect to SSID=%s. IO error\n", ssid->value);
            connection_status = IO_ERROR;
            break;
        case PICO_ERROR_BADAUTH:
            DPRINTF("Failed to connect to SSID=%s. Bad auth\n", ssid->value);
            connection_status = BADAUTH_ERROR;
            break;
        case PICO_ERROR_CONNECT_FAILED:
            DPRINTF("Failed to connect to SSID=%s. Connect failed\n", ssid->value);
            connection_status = CONNECT_FAILED_ERROR;
            break;
        case PICO_ERROR_INSUFFICIENT_RESOURCES:
            DPRINTF("Failed to connect to SSID=%s. Insufficient resources\n", ssid->value);
            connection_status = INSUFFICIENT_RESOURCES_ERROR;
            break;
        default:
            connection_status = CONNECTED_WIFI;
            DPRINTF("Connected to SSID=%s\n", ssid->value);
        }
    }
    if (password_value != NULL)
    {
        free(password_value);
    }
}

ConnectionStatus get_network_connection_status()
{
    ConnectionStatus old_previous_connection_status = previous_connection_status;
    previous_connection_status = connection_status;
    int link_status = cyw43_wifi_link_status(&cyw43_state, CYW43_ITF_STA);
    switch (link_status)
    {
    case CYW43_LINK_DOWN:
        connection_status = DISCONNECTED;
        break;
    case CYW43_LINK_JOIN:
        connection_status = CONNECTED_WIFI;
        if (get_ip_address() != 0x0)
        {
            connection_status = CONNECTED_WIFI_IP;
        }
        break;
    case CYW43_LINK_FAIL:
        connection_status = GENERIC_ERROR;
        break;
    case CYW43_LINK_NONET:
        connection_status = CONNECT_FAILED_ERROR;
        break;
    case CYW43_LINK_BADAUTH:
        connection_status = BADAUTH_ERROR;
        break;
    default:
        connection_status = GENERIC_ERROR;
    }

    if (connection_status != old_previous_connection_status)
    {
        switch (link_status)
        {
        case CYW43_LINK_DOWN:
            DPRINTF("Link down\n");
            break;
        case CYW43_LINK_JOIN:
            DPRINTF("Link join. Connected!\n");
            break;
        case CYW43_LINK_FAIL:
            DPRINTF("Link fail\n");
            break;
        case CYW43_LINK_NONET:
            DPRINTF("Link no net\n");
            break;
        case CYW43_LINK_BADAUTH:
            DPRINTF("Link bad auth\n");
            break;
        default:
            DPRINTF("Link unknown\n");
        }
    }
    return connection_status;
}

void network_poll()
{
    cyw43_arch_poll();
}

uint32_t get_network_status_polling_ms()
{
    uint32_t network_status_polling_ms = NETWORK_POLL_INTERVAL * 1000;
    ConfigEntry *default_network_status_polling_sec = find_entry(PARAM_NETWORK_STATUS_SEC);
    if (default_network_status_polling_sec != NULL)
    {
        network_status_polling_ms = atoi(default_network_status_polling_sec->value) * 1000;
        // If the value is too small, set the minimum value
        if (network_status_polling_ms < NETWORK_POLL_INTERVAL_MIN * 1000)
        {
            network_status_polling_ms = NETWORK_POLL_INTERVAL_MIN * 1000;
            DPRINTF("NETWORK_STATUS_SEC value too small. Changing to minimum value: %d\n", network_status_polling_ms);
        }
    }
    else
    {
        DPRINTF("%s not found in the config file. Using default value: %d\n", PARAM_NETWORK_STATUS_SEC, network_status_polling_ms);
    }
    return network_status_polling_ms;
}

uint16_t get_wifi_scan_poll_secs()
{
    uint16_t value = WIFI_SCAN_POLL_COUNTER;
    ConfigEntry *default_config_entry = find_entry(PARAM_WIFI_SCAN_SECONDS);
    if (default_config_entry != NULL)
    {
        value = atoi(default_config_entry->value);
    }
    else
    {
        DPRINTF("WIFI_SCAN_SECONDS not found in the config file. Disabling polling.\n");
    }
    if (value < WIFI_SCAN_POLL_COUNTER_MIN)
    {
        value = WIFI_SCAN_POLL_COUNTER_MIN;
        DPRINTF("WIFI_SCAN_SECONDS value too small. Changing to minimum value: %d\n", value);
    }
    return value;
}

u_int32_t get_ip_address()
{
    return cyw43_state.netif[0].ip_addr.addr;
}

u_int8_t *get_mac_address()
{
    return cyw43_state.mac;
}

u_int32_t get_netmask()
{
    return cyw43_state.netif[0].netmask.addr;
}

u_int32_t get_gateway()
{
    return cyw43_state.netif[0].gw.addr;
}

u_int32_t get_dns()
{
    const ip_addr_t *dns_ip = dns_getserver(0);
    return dns_ip->addr;
}

char *print_ipv4(u_int32_t ip)
{
    char *ip_str = malloc(16);
    snprintf(ip_str, 16, "%d.%d.%d.%d", ip & 0xFF, (ip >> 8) & 0xFF, (ip >> 16) & 0xFF, (ip >> 24) & 0xFF);
    return ip_str;
}

char *print_mac(uint8_t *mac_address)
{
    static char mac_str[18];
    snprintf(mac_str, sizeof(mac_str), "%02X:%02X:%02X:%02X:%02X:%02X",
             mac_address[0],
             mac_address[1],
             mac_address[2],
             mac_address[3],
             mac_address[4],
             mac_address[5]);
    return mac_str;
}

void get_connection_data(ConnectionData *connection_data)
{
    ConfigEntry *ssid = find_entry(PARAM_WIFI_SSID);
    ConfigEntry *wifi_auth = find_entry(PARAM_WIFI_AUTH);
    ConfigEntry *wifi_scan_interval = find_entry(PARAM_WIFI_SCAN_SECONDS);
    ConfigEntry *network_status_scan_interval = find_entry(PARAM_NETWORK_STATUS_SEC);
    ConfigEntry *file_downloading_timeout = find_entry(PARAM_DOWNLOAD_TIMEOUT_SEC);
    ConfigEntry *wifi_country = find_entry(PARAM_WIFI_COUNTRY);
    connection_data->network_status = (u_int16_t)connection_status;
    snprintf(connection_data->ipv4_address, sizeof(connection_data->ipv4_address), "%s", "Not connected" + '\0');
    snprintf(connection_data->ipv6_address, sizeof(connection_data->ipv6_address), "%s", "Not connected" + '\0');
    snprintf(connection_data->mac_address, sizeof(connection_data->mac_address), "%s", "Not connected" + '\0');
    snprintf(connection_data->gw_ipv4_address, sizeof(connection_data->gw_ipv4_address), "%s", "Not connected" + '\0');
    snprintf(connection_data->netmask_ipv4_address, sizeof(connection_data->netmask_ipv4_address), "Not connected" + '\0');
    snprintf(connection_data->dns_ipv4_address, sizeof(connection_data->dns_ipv4_address), "%s", "Not connected" + '\0');
    connection_data->wifi_auth_mode = (uint16_t)atoi(wifi_auth->value);
    connection_data->wifi_scan_interval = get_wifi_scan_poll_secs();
    connection_data->network_status_poll_interval = (uint16_t)(get_network_status_polling_ms() / 1000);
    connection_data->file_downloading_timeout = (uint16_t)atoi(file_downloading_timeout->value);

    // If the country is empty, set it to XX. Otherwise, copy the first two characters
    if (wifi_country->value[0] == '\0')
    { // Check if the country value is empty
        snprintf(connection_data->wifi_country, 4, "XX\0\0");
    }
    else
    {
        snprintf(connection_data->wifi_country, 4, "%.2s\0\0", wifi_country->value);
    }

    switch (connection_status)
    {
    case CONNECTED_WIFI_IP:
        snprintf(connection_data->ssid, sizeof(connection_data->ssid), "%s", ssid->value);
        snprintf(connection_data->ipv4_address, sizeof(connection_data->ipv4_address), "%s", print_ipv4(get_ip_address()));
        snprintf(connection_data->ipv6_address, sizeof(connection_data->ipv6_address), "%s", "Not implemented" + '\0');
        snprintf(connection_data->mac_address, sizeof(connection_data->mac_address), "%s", print_mac(get_mac_address()));
        snprintf(connection_data->gw_ipv4_address, sizeof(connection_data->gw_ipv4_address), "%s", print_ipv4(get_gateway()));
        snprintf(connection_data->gw_ipv6_address, sizeof(connection_data->gw_ipv6_address), "%s", "Not implemented" + '\0');
        snprintf(connection_data->netmask_ipv4_address, sizeof(connection_data->netmask_ipv4_address), "%s", print_ipv4(get_netmask()));
        snprintf(connection_data->netmask_ipv6_address, sizeof(connection_data->netmask_ipv6_address), "%s", "Not implemented" + '\0');
        snprintf(connection_data->dns_ipv4_address, sizeof(connection_data->dns_ipv4_address), "%s", print_ipv4(get_dns()));
        snprintf(connection_data->dns_ipv6_address, sizeof(connection_data->dns_ipv6_address), "%s", "Not implemented" + '\0');
        break;
    case CONNECTED_WIFI:
        snprintf(connection_data->ssid, sizeof(connection_data->ssid), "%s", ssid->value);
        snprintf(connection_data->ipv4_address, sizeof(connection_data->ipv4_address), "%s", "Waiting address" + '\0');
        snprintf(connection_data->ipv6_address, sizeof(connection_data->ipv6_address), "%s", "Waiting address" + '\0');
        snprintf(connection_data->mac_address, sizeof(connection_data->mac_address), "%s", "Waiting address" + '\0');
        snprintf(connection_data->gw_ipv4_address, sizeof(connection_data->gw_ipv4_address), "%s", "Waiting address" + '\0');
        snprintf(connection_data->gw_ipv6_address, sizeof(connection_data->gw_ipv6_address), "%s", "Waiting address" + '\0');
        snprintf(connection_data->netmask_ipv4_address, sizeof(connection_data->netmask_ipv4_address), "%s", "Waiting address" + '\0');
        snprintf(connection_data->netmask_ipv6_address, sizeof(connection_data->netmask_ipv6_address), "%s", "Waiting address" + '\0');
        snprintf(connection_data->dns_ipv4_address, sizeof(connection_data->dns_ipv4_address), "%s", "Waiting address" + '\0');
        snprintf(connection_data->dns_ipv6_address, sizeof(connection_data->dns_ipv6_address), "%s", "Waiting address" + '\0');
        break;
    case CONNECTING:
        snprintf(connection_data->ssid, MAX_SSID_LENGTH, "%s", "Initializing" + '\0');
        snprintf(connection_data->ipv4_address, sizeof(connection_data->ipv4_address), "%s", "Initializing" + '\0');
        snprintf(connection_data->ipv6_address, sizeof(connection_data->ipv6_address), "%s", "Initializing" + '\0');
        snprintf(connection_data->mac_address, sizeof(connection_data->mac_address), "%s", "Initializing" + '\0');
        snprintf(connection_data->gw_ipv4_address, sizeof(connection_data->gw_ipv4_address), "%s", "Initializing" + '\0');
        snprintf(connection_data->gw_ipv6_address, sizeof(connection_data->gw_ipv6_address), "%s", "Initializing" + '\0');
        snprintf(connection_data->netmask_ipv4_address, sizeof(connection_data->netmask_ipv4_address), "%s", "Initializing" + '\0');
        snprintf(connection_data->netmask_ipv6_address, sizeof(connection_data->netmask_ipv6_address), "%s", "Initializing" + '\0');
        snprintf(connection_data->dns_ipv4_address, sizeof(connection_data->dns_ipv4_address), "%s", "Initializing" + '\0');
        snprintf(connection_data->dns_ipv6_address, sizeof(connection_data->dns_ipv6_address), "%s", "Initializing" + '\0');
        break;
    case DISCONNECTED:
        snprintf(connection_data->ssid, MAX_SSID_LENGTH, "%s", "Not connected" + '\0');
        break;
    case CONNECT_FAILED_ERROR:
        snprintf(connection_data->ssid, MAX_SSID_LENGTH, "%s", "CONNECT FAILED ERROR!" + '\0');
        break;
    case BADAUTH_ERROR:
        snprintf(connection_data->ssid, MAX_SSID_LENGTH, "%s", "BAD AUTH ERROR!" + '\0');
        break;
    case NOT_SUPPORTED:
        snprintf(connection_data->ssid, MAX_SSID_LENGTH, "%s", "NETWORKING NOT SUPPORTED!" + '\0');
        break;
    default:
        snprintf(connection_data->ssid, MAX_SSID_LENGTH, "%s", "ERROR!" + '\0');
    }
}

void show_connection_data(ConnectionData *connection_data)
{
    DPRINTF("SSID: %s - Status: %d - IPv4: %s - IPv6: %s - GW:%s - Mask:%s - MAC:%s DNS:%s\n",
            connection_data->ssid,
            connection_data->network_status,
            connection_data->ipv4_address,
            connection_data->ipv6_address,
            connection_data->gw_ipv4_address,
            connection_data->netmask_ipv4_address,
            connection_data->mac_address,
            connection_data->dns_ipv4_address);
    DPRINTF("WiFi country: %s - Auth mode: %d - Scan interval: %d - Network status poll interval: %d - File downloading timeout: %d\n",
            connection_data->wifi_country,
            connection_data->wifi_auth_mode,
            connection_data->wifi_scan_interval,
            connection_data->network_status_poll_interval,
            connection_data->file_downloading_timeout);
}

RomInfo parseRomItem(cJSON *json_item)
{
    RomInfo item = {0};

    cJSON *url = cJSON_GetObjectItemCaseSensitive(json_item, "url");
    cJSON *name = cJSON_GetObjectItemCaseSensitive(json_item, "name");
    cJSON *description = cJSON_GetObjectItemCaseSensitive(json_item, "description");
    cJSON *tags = cJSON_GetObjectItemCaseSensitive(json_item, "tags");
    cJSON *size_kb = cJSON_GetObjectItemCaseSensitive(json_item, "size_kb");

    if (cJSON_IsString(url) && url->valuestring)
    {
        item.url = strdup(url->valuestring);
    }
    if (cJSON_IsString(name) && name->valuestring)
    {
        item.name = strdup(name->valuestring);
    }
    if (cJSON_IsString(description) && description->valuestring)
    {
        item.description = strdup(description->valuestring);
    }
    // The tags is an array, read the array content and concatenate it in a single string
    if (cJSON_IsArray(tags))
    {
        int tags_count = cJSON_GetArraySize(tags);
        char *tags_str = malloc(256);
        for (int i = 0; i < tags_count; i++)
        {
            cJSON *tag = cJSON_GetArrayItem(tags, i);
            if (cJSON_IsString(tag) && tag->valuestring)
            {
                if (i == 0)
                {
                    strcpy(tags_str, tag->valuestring);
                }
                else
                {
                    strcat(tags_str, ", ");
                    strcat(tags_str, tag->valuestring);
                }
            }
        }
        item.tags = tags_str;
    }
    if (cJSON_IsNumber(size_kb))
    {
        item.size_kb = size_kb->valueint;
    }

    return item;
}

void freeRomItem(RomInfo *item)
{
    if (item->url)
        free(item->url);
    if (item->name)
        free(item->name);
    if (item->description)
        free(item->description);
}

// Comparator function for sorting RomInfo based on 'name' and then 'url'
int compareRomInfo(const void *a, const void *b)
{
    RomInfo *itemA = (RomInfo *)a;
    RomInfo *itemB = (RomInfo *)b;

    int nameComparison = strcmp(itemA->name, itemB->name);
    if (nameComparison != 0)
    {
        return nameComparison;
    }

    return strcmp(itemA->url, itemB->url);
}

static int split_url(const char *url, UrlParts *parts)
{
    if (!url || !parts)
        return -1;

    // Initialize parts with NULL
    parts->protocol = NULL;
    parts->domain = NULL;
    parts->uri = NULL;

    char *p, *q;

    // Get protocol
    p = strstr(url, "://");
    if (!p)
        return -1; // Invalid URL format

    parts->protocol = malloc(p - url + 1);
    if (!parts->protocol)
        return -1; // Allocation failed
    strncpy(parts->protocol, url, p - url);
    parts->protocol[p - url] = '\0';

    // Get domain
    p += 3; // Skip over "://"
    q = strchr(p, '/');

    if (q)
    {
        parts->domain = malloc(q - p + 1);
        if (!parts->domain)
            return -1; // Allocation failed
        strncpy(parts->domain, p, q - p);
        parts->domain[q - p] = '\0';

        // Get URI
        parts->uri = strdup(q);
        if (!parts->uri)
            return -1; // Allocation failed
    }
    else
    {
        parts->domain = strdup(p);
        if (!parts->domain)
            return -1; // Allocation failed
    }

    return 0;
}

static void free_url_parts(UrlParts *parts)
{
    if (parts->protocol)
        free(parts->protocol);
    if (parts->domain)
        free(parts->domain);
    if (parts->uri)
        free(parts->uri);
}

bool check_STEEM_extension(UrlParts parts)
{
    bool steem_extension = false;

    int len = strlen(parts.uri);
    if (len >= 4)
    {
        // Point to the last four characters of the uri
        char *extension = parts.uri + len - 4;

        // Check for .stc or .STC extension
        if (strcasecmp(extension, ".stc") == 0)
        {
            steem_extension = true;
        }
    }

    return steem_extension;
}

char *download_latest_release(const char *url)
{
    char *buff = malloc(4096);
    uint32_t buff_pos = 0;
    httpc_state_t *connection;
    bool complete = false;
    UrlParts parts;

    err_t headers(httpc_state_t * connection, void *arg,
                  struct pbuf *hdr, u16_t hdr_len, u32_t content_len)
    {
        pbuf_copy_partial(hdr, buff, hdr->tot_len, 0);
        return ERR_OK;
    }

    void result(void *arg, httpc_result_t httpc_result,
                u32_t rx_content_len, u32_t srv_res, err_t err)

    {
        complete = true;
        if (srv_res != 200)
        {
            DPRINTF("version.txt something went wrong. HTTP error: %d\n", srv_res);
        }
        else
        {
            DPRINTF("version.txt Transfer complete. %d transfered.\n", rx_content_len);
        }
    }

    err_t body(void *arg, struct altcp_pcb *conn,
               struct pbuf *p, err_t err)
    {
        pbuf_copy_partial(p, (buff + buff_pos), p->tot_len, 0);
        buff_pos += p->tot_len;
        tcp_recved(conn, p->tot_len);
        if (p != NULL)
        {
            pbuf_free(p);
        }

        return ERR_OK;
    }

    DPRINTF("Getting latest release version.txt %s\n", url);
    if (split_url(url, &parts) != 0)
    {
        DPRINTF("Failed to split URL\n");
        return NULL;
    }

    DPRINTF("Protocol %s\n", parts.protocol);
    DPRINTF("Domain %s\n", parts.domain);
    DPRINTF("URI %s\n", parts.uri);

    httpc_connection_t settings;
    settings.result_fn = result;
    settings.headers_done_fn = headers;
    settings.use_proxy = false;

    complete = false;
    cyw43_arch_lwip_begin();
    err_t err = httpc_get_file_dns(
        parts.domain,
        LWIP_IANA_PORT_HTTP,
        parts.uri,
        &settings,
        body,
        NULL,
        NULL);
    cyw43_arch_lwip_end();

    if (err != ERR_OK)
    {
        DPRINTF("HTTP GET failed: %d\n", err);
        free_url_parts(&parts);
        return NULL;
    }
    while (!complete)
    {
#if PICO_CYW43_ARCH_POLL
        cyw43_arch_lwip_begin();
        network_poll();
        cyw43_arch_wait_for_work_until(make_timeout_time_ms(100));
        cyw43_arch_lwip_end();
#else
        sleep_ms(100);
#endif
    }

    char *newline_pos = strchr(buff, '\n');
    char *latest_release_version = strndup(buff, newline_pos ? newline_pos - buff : strlen(buff));

    free_url_parts(&parts);
    free(buff);

    return latest_release_version;
}

char *get_latest_release(void)
{
    ConfigEntry *entry = find_entry(PARAM_LASTEST_RELEASE_URL);

    if (entry == NULL)
    {
        DPRINTF("%s not found in config\n", PARAM_LASTEST_RELEASE_URL);
        return NULL;
    }
    if (strlen(entry->value) == 0)
    {
        DPRINTF("%s is empty\n", PARAM_LASTEST_RELEASE_URL);
        return NULL;
    }

    char *latest_release = download_latest_release(entry->value);
    char *formatted_release = malloc(strlen(latest_release) + 2);

    if (formatted_release)
    {
        sprintf(formatted_release, "%s", latest_release);
        free(latest_release);
    }
    return formatted_release ? formatted_release : latest_release;
}

void get_json_files(RomInfo **items, int *itemCount, const char *url)
{
    char *buff = malloc(32768);
    uint32_t buff_pos = 0;
    httpc_state_t *connection;
    bool complete = false;
    UrlParts parts;

    err_t headers(httpc_state_t * connection, void *arg,
                  struct pbuf *hdr, u16_t hdr_len, u32_t content_len)
    {
        return ERR_OK;
    }

    void result(void *arg, httpc_result_t httpc_result,
                u32_t rx_content_len, u32_t srv_res, err_t err)

    {
        complete = true;
        if (srv_res != 200)
        {
            DPRINTF("JSON something went wrong. HTTP error: %d\n", srv_res);
        }
        else
        {
            DPRINTF("JSON Transfer complete. %d transfered.\n", rx_content_len);
        }
    }

    err_t body(void *arg, struct altcp_pcb *conn,
               struct pbuf *p, err_t err)
    {
        // DPRINTF("Body received. ");
        // DPRINTF("Buffer size:%d\n", p->tot_len);
        // fflush(stdout);
        pbuf_copy_partial(p, (buff + buff_pos), p->tot_len, 0);
        buff_pos += p->tot_len;
        tcp_recved(conn, p->tot_len);
        if (p != NULL)
        {
            pbuf_free(p);
        }

        return ERR_OK;
    }

    DPRINTF("Downloading JSON file from %s\n", url);
    if (split_url(url, &parts) != 0)
    {
        DPRINTF("Failed to split URL\n");
        return;
    }

    DPRINTF("Protocol %s\n", parts.protocol);
    DPRINTF("Domain %s\n", parts.domain);
    DPRINTF("URI %s\n", parts.uri);

    httpc_connection_t settings;
    settings.result_fn = result;
    settings.headers_done_fn = headers;
    settings.use_proxy = false;

    complete = false;
    cyw43_arch_lwip_begin();
    err_t err = httpc_get_file_dns(
        parts.domain,
        LWIP_IANA_PORT_HTTP,
        parts.uri,
        &settings,
        body,
        NULL,
        NULL);
    cyw43_arch_lwip_end();

    if (err != ERR_OK)
    {
        DPRINTF("HTTP GET failed: %d\n", err);
        free_url_parts(&parts);
        return;
    }
    while (!complete)
    {
#if PICO_CYW43_ARCH_POLL
        cyw43_arch_lwip_begin();
        cyw43_arch_poll();
        cyw43_arch_wait_for_work_until(make_timeout_time_ms(10));
        cyw43_arch_lwip_end();
#elif PICO_CYW43_ARCH_THREADSAFE_BACKGROUND
        cyw43_arch_lwip_begin();
        cyw43_arch_wait_for_work_until(make_timeout_time_ms(10));
        cyw43_arch_lwip_end();
#else
        sleep_ms(1000);
#endif
    }

    free_url_parts(&parts);

    cJSON *json = cJSON_Parse(buff);
    if (json != NULL)
    {

        *itemCount = cJSON_GetArraySize(json);
        *items = (RomInfo *)malloc(sizeof(RomInfo) * *itemCount);

        for (int i = 0; i < *itemCount; i++)
        {
            cJSON *json_item = cJSON_GetArrayItem(json, i);
            (*items)[i] = parseRomItem(json_item);
        }

        // Sort the RomInfo items array
        qsort(*items, *itemCount, sizeof(RomInfo), compareRomInfo);

        cJSON_Delete(json);
        free(buff);

        // Print parsed data as a test
        // for (int i = 0; i < *itemCount; i++)
        // {
        //     DPRINTF("URL: %s\n", (*items)[i].url);
        //     DPRINTF("Name: %s\n", (*items)[i].name);
        //     DPRINTF("Description: %s\n", (*items)[i].description);
        //     DPRINTF("Size (KB): %d\n", (*items)[i].size_kb);
        // }

        // Free dynamically allocated memory
        // for (int i = 0; i < *itemCount; i++)
        // {
        //     freeRomItem(&items[i]);
        // }
        // free(items);
    }
}

int download_rom(const char *url, uint32_t rom_load_offset)
{
    const int FLASH_BUFFER_SIZE = 4096;
    uint8_t *flash_buff = malloc(FLASH_BUFFER_SIZE);
    uint32_t flash_buff_pos = 0;
    bool first_chunk = true;
    bool is_steem = false;
    httpc_state_t *connection;
    bool complete = false;
    err_t callback_error = ERR_OK; // If any error found in the callback and cannot be returned, store it here
    UrlParts parts;
    uint32_t dest_address = rom_load_offset; // Initialize pointer to the ROM address

    err_t headers(httpc_state_t * connection, void *arg,
                  struct pbuf *hdr, u16_t hdr_len, u32_t content_len)
    {
        return ERR_OK;
    }

    void result(void *arg, httpc_result_t httpc_result,
                u32_t rx_content_len, u32_t srv_res, err_t err)

    {
        complete = true;
        if (srv_res != 200)
        {
            DPRINTF("ROM image download something went wrong. HTTP error: %d\n", srv_res);
            callback_error = srv_res;
        }
        else
        {
            DPRINTF("ROM image transfer complete. %d transfered.\n", rx_content_len);
            DPRINTF("Pending bytes to write: %d\n", flash_buff_pos);
        }
    }

    err_t body(void *arg, struct altcp_pcb *conn,
               struct pbuf *p, err_t err)
    {
        // DPRINTF("Body received. ");
        // DPRINTF("Buffer size:%d. ", p->tot_len);
        // DPRINTF("Copying to address: %p\n", dest_address);
        // fflush(stdout);

        // Transform buffer's words from little endian to big endian inline
        uint8_t *buffer = (uint8_t *)p->payload;
        uint16_t total_bytes_copy = p->tot_len;
        int steem_offset = 0;
        if (is_steem && first_chunk)
        {
            // Check if the first 4 bytes are 0x0000
            if (buffer[0] == 0x00 && buffer[1] == 0x00 && buffer[2] == 0x00 && buffer[3] == 0x00)
            {
                DPRINTF("Skipping first 4 bytes. Looks like a STEEM cartridge image.\n");
                steem_offset = 4;
            }
            first_chunk = false;
        }
        uint16_t flash_buffer_current_size = FLASH_BUFFER_SIZE - flash_buff_pos - steem_offset;
        if (total_bytes_copy < flash_buffer_current_size)
        {
            // DPRINTF("Copying %d bytes to address: %p...", (total_bytes_copy - steem_offset), (flash_buff + flash_buff_pos));
            // THere is room in the flash_buff to copy the whole buffer
            pbuf_copy_partial(p, (flash_buff + flash_buff_pos), (total_bytes_copy - steem_offset), steem_offset);
            // increment the flash buffer position
            flash_buff_pos += (total_bytes_copy - steem_offset);
            // DPRINTF("Done.\n");
        }
        else
        {
            // DPRINTF("Copying %d bytes to address: %p...", flash_buffer_current_size, (flash_buff + flash_buff_pos));
            // There is no room, so we have to fill it first, invert, write to flash and continue
            pbuf_copy_partial(p, (flash_buff + flash_buff_pos), flash_buffer_current_size, 0);
            // DPRINTF("Done.\n");

            // Change to big endian
            for (int j = 0; j < FLASH_BUFFER_SIZE; j += 2)
            {
                uint16_t value = *(uint16_t *)(flash_buff + j);
                *(uint16_t *)(flash_buff + j) = (value << 8) | (value >> 8);
            }

            // Write chunk to flash
            DPRINTF("Writing %d bytes to address: %p...", flash_buff_pos + flash_buffer_current_size, dest_address);
            uint32_t ints = save_and_disable_interrupts();
            flash_range_program(dest_address, flash_buff, FLASH_BUFFER_SIZE);
            restore_interrupts(ints);
            dest_address += FLASH_BUFFER_SIZE;
            flash_buff_pos = 0;
            DPRINTF("Done.\n");

            // Reset the flash buffer position
            flash_buff_pos = 0;

            // we have total_bytes - flash_buffer_current_size left to copy!
            uint16_t bytes_left = total_bytes_copy - flash_buffer_current_size;
            if (bytes_left > 0)
            {
                // DPRINTF("Copying pending %d bytes to address: %p...", bytes_left, (flash_buff + flash_buff_pos));
                pbuf_copy_partial(p, (flash_buff + flash_buff_pos), bytes_left, flash_buffer_current_size);
                flash_buff_pos += bytes_left;
                // DPRINTF("Done.\n");
            }
        }

        // for (int i = 0; i < total_bytes_copy; i += 2)
        // {
        //     uint16_t value = *(uint16_t *)(buffer + i);
        //     value = (value << 8) | (value >> 8);
        //     *(uint16_t *)(flash_buff + flash_buff_pos) = value;
        //     flash_buff_pos += 2;
        //     if (flash_buff_pos == FLASH_BUFFER_SIZE)
        //     {
        //         DPRINTF("Writing %d bytes to address: %p...", flash_buff_pos, dest_address);
        //         // Disable the flash writing for now until I fix this issue
        //         uint32_t ints = save_and_disable_interrupts();
        //         flash_range_program(dest_address, flash_buff, FLASH_BUFFER_SIZE);
        //         restore_interrupts(ints);
        //         dest_address += FLASH_BUFFER_SIZE;
        //         flash_buff_pos = 0;
        //         DPRINTF("Done.\n");
        //     }
        // }

        tcp_recved(conn, p->tot_len);

        if (p != NULL)
        {
            pbuf_free(p);
        }
        return ERR_OK;
    }
    DPRINTF("Downloading ROM image from %s\n", url);
    if (split_url(url, &parts) != 0)
    {
        DPRINTF("Failed to split URL\n");
        return -1;
    }

    DPRINTF("Protocol %s\n", parts.protocol);
    DPRINTF("Domain %s\n", parts.domain);
    DPRINTF("URI %s\n", parts.uri);

    is_steem = check_STEEM_extension(parts);

    // Erase the content before loading the new file. It seems that
    // overwriting it's not enough
    DPRINTF("Erasing FLASH ROM image area at address: %p...\n", rom_load_offset);
    uint32_t ints = save_and_disable_interrupts();
    flash_range_erase(rom_load_offset, ROM_SIZE_BYTES * 2); // Two banks of 64K
    restore_interrupts(ints);
    DPRINTF("Erased.\n");

    httpc_connection_t settings;
    memset(&settings, 0, sizeof(settings));

    settings.result_fn = result;
    settings.headers_done_fn = headers;
    settings.use_proxy = false;

    complete = false;
    cyw43_arch_lwip_begin();
    DPRINTF("Downloading ROM image from %s\n", url);
    err_t err = httpc_get_file_dns(
        parts.domain,
        LWIP_IANA_PORT_HTTP,
        parts.uri,
        &settings,
        body,
        NULL,
        NULL);
    cyw43_arch_lwip_end();

    if (err != ERR_OK)
    {
        DPRINTF("HTTP GET failed: %d\n", err);
        free_url_parts(&parts);
        return -1;
    }
    else
    {
        DPRINTF("HTTP GET sent\n");
    }
    while (!complete)
    {
        tight_loop_contents();
#if PICO_CYW43_ARCH_POLL
        cyw43_arch_lwip_begin();
        cyw43_arch_poll();
        cyw43_arch_wait_for_work_until(make_timeout_time_ms(10));
        cyw43_arch_lwip_end();
#elif PICO_CYW43_ARCH_THREADSAFE_BACKGROUND
        cyw43_arch_lwip_begin();
        cyw43_arch_wait_for_work_until(make_timeout_time_ms(10));
        cyw43_arch_lwip_end();
#else
        sleep_ms(1000);
#endif
    }

    free_url_parts(&parts);
    free(flash_buff);
    return callback_error;
}

err_t get_floppy_db_files(FloppyImageInfo **items, int *itemCount, const char *url)
{
    size_t BUFFER_SIZE = 32768;
    char *buff = malloc(BUFFER_SIZE);
    uint32_t buff_pos = 0;
    httpc_state_t *connection;
    bool complete = false;
    err_t callback_error = ERR_OK; // If any error found in the callback and cannot be returned, store it here
    UrlParts parts;
    u32_t content_len = 0;

    err_t headers(httpc_state_t * connection, void *arg,
                  struct pbuf *hdr, u16_t hdr_len, u32_t rx_content_len)
    {
        return ERR_OK;
    }

    void result(void *arg, httpc_result_t httpc_result,
                u32_t rx_content_len, u32_t srv_res, err_t err)

    {
        content_len = rx_content_len;
        complete = true;
        if (srv_res != 200)
        {
            DPRINTF("Floppy images db something went wrong. HTTP error: %d\n", srv_res);
            callback_error = srv_res;
        }
        else
        {
            DPRINTF("Floppy images db Transfer complete. %d transfered.\n", rx_content_len);
        }
    }

    err_t body(void *arg, struct altcp_pcb *conn,
               struct pbuf *p, err_t err)
    {
        // DPRINTF("Body received. ");
        // DPRINTF("Buffer size:%d\n", p->tot_len);
        // fflush(stdout);
        pbuf_copy_partial(p, (buff + buff_pos), p->tot_len, 0);
        buff_pos += p->tot_len;
        tcp_recved(conn, p->tot_len);
        if (p != NULL)
        {
            pbuf_free(p);
        }

        return ERR_OK;
    }

    DPRINTF("Downloading Floppy images db file from %s\n", url);
    if (split_url(url, &parts) != 0)
    {
        DPRINTF("Failed to split URL\n");
        return -1;
    }

    DPRINTF("Protocol %s\n", parts.protocol);
    DPRINTF("Domain %s\n", parts.domain);
    DPRINTF("URI %s\n", parts.uri);

    httpc_connection_t settings;
    settings.result_fn = result;
    settings.headers_done_fn = headers;
    settings.use_proxy = false;

    complete = false;
    cyw43_arch_lwip_begin();
    err_t err = httpc_get_file_dns(
        parts.domain,
        LWIP_IANA_PORT_HTTP,
        parts.uri,
        &settings,
        body,
        NULL,
        NULL);
    cyw43_arch_lwip_end();

    if (err != ERR_OK)
    {
        DPRINTF("HTTP GET failed: %d\n", err);
        free_url_parts(&parts);
        free(buff);
        return -1;
    }
    while (!complete)
    {
#if PICO_CYW43_ARCH_POLL
        cyw43_arch_lwip_begin();
        cyw43_arch_poll();
        cyw43_arch_wait_for_work_until(make_timeout_time_ms(10));
        cyw43_arch_lwip_end();
#elif PICO_CYW43_ARCH_THREADSAFE_BACKGROUND
        cyw43_arch_lwip_begin();
        cyw43_arch_wait_for_work_until(make_timeout_time_ms(10));
        cyw43_arch_lwip_end();
#else
        sleep_ms(1000);
#endif
    }

    free_url_parts(&parts);

    if (callback_error != ERR_OK)
    {
        if (buff != NULL)
            free(buff);
        return callback_error;
    }

    bool inside_quotes = false;
    char *start = NULL;
    int items_count = 0;

    // First, count the number of entries
    int count = 0;
    const char *p = buff;
    for (int i = 0; i < content_len; i++)
    {
        if (*p == ';')
            count++;
        p++;
    }
    *itemCount = (count / 5);

    DPRINTF("Found %d entries\n", *itemCount);

    if (*itemCount == 0)
    {
        // If no entries found, short circuit and return
        free(buff);
        return -1;
    }

    FloppyImageInfo *current = NULL;

    // Allocate memory for the first FloppyImageInfo structure
    *items = malloc(sizeof(FloppyImageInfo));
    if (!*items)
    {
        DPRINTF("Memory allocation failed\n");
        free(buff);
        return -1;
    }
    char *buff_iter = buff;
    current = *items; // Set current to the first allocated FloppyImageInfo structure

    while (*buff_iter)
    {
        if (*buff_iter == '"')
        {
            inside_quotes = !inside_quotes;

            if (inside_quotes)
            {
                start = buff_iter + 1; // Start of a new value
            }
            else
            {
                char *value = strndup(start, buff_iter - start);
                switch (items_count % 6)
                {
                case 0:
                    current->name = value;
                    break;
                case 1:
                    current->status = value;
                    break;
                case 2:
                    current->description = value;
                    break;
                case 3:
                    current->tags = value;
                    break;
                case 4:
                    current->extra = value;
                    break;
                case 5:
                    current->url = value;
                    break;
                }

                items_count++;
                if (items_count % 6 == 0 && *(buff_iter + 1) != '\0')
                {
                    // Allocate memory for the next FloppyImageInfo structure
                    FloppyImageInfo *next = malloc(sizeof(FloppyImageInfo));
                    if (!next)
                    {
                        DPRINTF("Memory allocation failed\n");
                        return -1;
                    }
                    // Initialize the newly allocated structure
                    *next = (FloppyImageInfo){0};

                    current->next = next; // Link the current structure to the next one
                    current = next;       // Move the current pointer to the next structure
                }
            }
        }
        buff_iter++;
    }
    if (buff != NULL)
    {
        free(buff);
    }
    return callback_error;
}

int download_floppy(const char *url, const char *folder, const char *dest_filename, bool overwrite_flag)
{
    const int BUFFER_SIZE = 16384;
    uint8_t *buff = malloc(BUFFER_SIZE);
    uint32_t buff_pos = 0;
    httpc_state_t *connection;
    bool complete = false;
    err_t callback_error = ERR_OK; // If any error found in the callback and cannot be returned, store it here
    UrlParts parts;

    FRESULT fr;    // FatFS function common result code
    FIL dest_file; // File object
    UINT bw;       // File read/write count
    FILINFO fno;

    err_t headers(httpc_state_t * connection, void *arg,
                  struct pbuf *hdr, u16_t hdr_len, u32_t content_len)
    {
        return ERR_OK;
    }

    void result(void *arg, httpc_result_t httpc_result,
                u32_t rx_content_len, u32_t srv_res, err_t err)

    {
        complete = true;
        if (srv_res != 200)
        {
            DPRINTF("Floppy image download something went wrong. HTTP error: %d\n", srv_res);
            callback_error = srv_res;
        }
        else
        {
            DPRINTF("Floppy image transfer complete. %d transfered.\n", rx_content_len);
            DPRINTF("Pending bytes to write: %d\n", buff_pos);
        }
    }

    err_t body(void *arg, struct altcp_pcb *conn,
               struct pbuf *p, err_t err)
    {

        pbuf_copy_partial(p, buff, p->tot_len, 0);

        fr = f_write(&dest_file, buff, p->tot_len, &bw); // Write it to the destination file
        if (fr != FR_OK)
        {
            DPRINTF("f_write error: %s (%d)\n", FRESULT_str(fr), fr);
        }
        // Write chunk to flash
        DPRINTF("Writing %d bytes to file: %s...\n", p->tot_len, dest_filename);

        tcp_recved(conn, p->tot_len);

        if (p != NULL)
        {
            pbuf_free(p);
        }
        return ERR_OK;
    }

    // Create full paths for source and destination files
    char dest_path[256];
    sprintf(dest_path, "%s/%s", folder, dest_filename);

    // Check if the destination file exists
    fr = f_stat(dest_path, &fno);
    if (fr == FR_OK && !overwrite_flag)
    {
        DPRINTF("Destination file exists and overwrite_flag is false, canceling operation\n");
        return FR_FILE_EXISTS; // Destination file exists and overwrite_flag is false, cancel the operation
    }

    // Create and open the destination file
    fr = f_open(&dest_file, dest_path, FA_CREATE_ALWAYS | FA_WRITE);
    if (fr != FR_OK)
    {
        DPRINTF("f_open error: %s (%d)\n", FRESULT_str(fr), fr);
        f_close(&dest_file); // Close the source file if it was opened successfully
        return FR_CANNOT_OPEN_FILE_FOR_WRITE;
    }

    DPRINTF("Downloading Floppy image from %s\n", url);
    if (split_url(url, &parts) != 0)
    {
        DPRINTF("Failed to split URL\n");
        return -1;
    }

    DPRINTF("Protocol %s\n", parts.protocol);
    DPRINTF("Domain %s\n", parts.domain);
    DPRINTF("URI %s\n", parts.uri);

    httpc_connection_t settings;
    memset(&settings, 0, sizeof(settings));

    settings.result_fn = result;
    settings.headers_done_fn = headers;
    settings.use_proxy = false;

    complete = false;
    cyw43_arch_lwip_begin();
    err_t err = httpc_get_file_dns(
        parts.domain,
        LWIP_IANA_PORT_HTTP,
        parts.uri,
        &settings,
        body,
        NULL,
        NULL);
    cyw43_arch_lwip_end();

    if (err != ERR_OK)
    {
        DPRINTF("HTTP GET failed: %d\n", err);
        free_url_parts(&parts);
        return -1;
    }
    else
    {
        DPRINTF("HTTP GET sent\n");
    }
    while (!complete)
    {
        tight_loop_contents();
#if PICO_CYW43_ARCH_POLL
        cyw43_arch_lwip_begin();
        cyw43_arch_poll();
        cyw43_arch_wait_for_work_until(make_timeout_time_ms(10));
        cyw43_arch_lwip_end();
#elif PICO_CYW43_ARCH_THREADSAFE_BACKGROUND
        cyw43_arch_lwip_begin();
        cyw43_arch_wait_for_work_until(make_timeout_time_ms(10));
        cyw43_arch_lwip_end();
#else
        sleep_ms(1000);
#endif
    }

    // Close open file
    f_close(&dest_file);

    free_url_parts(&parts);
    free(buff);
    return callback_error;
}
