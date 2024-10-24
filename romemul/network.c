#include "include/network.h"

static ConnectionStatus connection_status = DISCONNECTED;
static ConnectionStatus previous_connection_status = NOT_SUPPORTED;
WifiScanData wifiScanData;
static char wifi_hostname[32];
static ip_addr_t current_ip;
static uint8_t cyw43_mac[6];
static bool cyw43_initialized = false;

int time_passed(absolute_time_t *t, uint32_t ms)
{
    if (t == NULL)
        return -1; // Error: invalid pointer

    absolute_time_t t_now = get_absolute_time(); // Get the current time

    // If *t is not initialized, or if the desired time has passed
    if (to_us_since_boot(*t) == 0 ||
        absolute_time_diff_us(*t, t_now) >= (ms * 1000))
    {

        *t = t_now; // Reset *t to the current time for the next check
        return 1;   // Time has passed
    }

    return 0; // Time has not yet passed
}

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
    // We need to change the endianness of the ssid and password
    char *base = (char *)(dest_ptr_word);
    WifiNetworkAuthInfo *authInfo = (WifiNetworkAuthInfo *)base;

    // Create a temporary buffer to hold the SSID data
    char tmp[MAX_SSID_LENGTH] = {0};
    memcpy(tmp, authInfo->ssid, MAX_SSID_LENGTH);    // Copy the SSID data to the temporary buffer
    CHANGE_ENDIANESS_BLOCK16(&tmp, MAX_SSID_LENGTH); // Swap the SSID data
    // Write the result back to the SSID field safely
    memcpy(authInfo->ssid, tmp, MAX_SSID_LENGTH);

    // Create a temporary buffer to hold the password data
    char tmp_password[MAX_PASSWORD_LENGTH] = {0};
    memcpy(tmp_password, authInfo->password, MAX_PASSWORD_LENGTH); // Copy the password data to the temporary buffer
    CHANGE_ENDIANESS_BLOCK16(&tmp_password, MAX_PASSWORD_LENGTH);  // Swap the password data
    // Write the result back to the password field safely
    memcpy(authInfo->password, tmp_password, MAX_PASSWORD_LENGTH);

    // No need to swap the auth_mode uint16_t
}

void network_swap_data(uint16_t *dest_ptr_word, uint16_t total_items)
{
    // Skip the MAGIC number (assumed to be a 32-bit value)
    char *ssid_base = (char *)(dest_ptr_word) + sizeof(uint32_t);
    WifiNetworkInfo *netInfo = (WifiNetworkInfo *)ssid_base;

    for (uint16_t i = 0; i < total_items; i++)
    {
        // Create a temporary buffer to hold the SSID data
        char tmp[MAX_SSID_LENGTH] = {0};
        memcpy(tmp, netInfo[i].ssid, MAX_SSID_LENGTH);   // Copy the SSID data to the temporary buffer
        CHANGE_ENDIANESS_BLOCK16(&tmp, MAX_SSID_LENGTH); // Swap the SSID data
        // Write the result back to the SSID field safely
        memcpy(netInfo[i].ssid, tmp, MAX_SSID_LENGTH);

        // Create a temporary buffer to hold the BSSID data
        char tmp_bssid[MAX_BSSID_LENGTH] = {0};
        memcpy(tmp_bssid, netInfo[i].bssid, MAX_BSSID_LENGTH);  // Copy the BSSID data to the temporary buffer
        CHANGE_ENDIANESS_BLOCK16(&tmp_bssid, MAX_BSSID_LENGTH); // Swap the BSSID data
        // Write the result back to the BSSID field safely
        memcpy(netInfo[i].bssid, tmp_bssid, MAX_BSSID_LENGTH);
    }
}

void network_swap_connection_data(uint16_t *dest_ptr_word)
{
    // No need to swap the uint16_t
    CHANGE_ENDIANESS_BLOCK16(dest_ptr_word, sizeof(ConnectionData) - sizeof(uint16_t) * 6);
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

const char *pico_serial_str()
{
    static char buf[PICO_UNIQUE_BOARD_ID_SIZE_BYTES * 2 + 1];
    pico_unique_board_id_t board_id;

    memset(&board_id, 0, sizeof(board_id));
    pico_get_unique_board_id(&board_id);
    for (int i = 0; i < PICO_UNIQUE_BOARD_ID_SIZE_BYTES; i++)
        snprintf(&buf[i * 2], 3, "%02x", board_id.id[i]);

    return buf;
}

static int16_t get_rssi(void)
{
    static absolute_time_t ABSOLUTE_TIME_INITIALIZED_VAR(rssi_poll_counter, 0);
    static int32_t rssi_tmp = 0;
    static int32_t rssi_polling_interval = 0;

    if (rssi_polling_interval == 0)
    {
        rssi_polling_interval = get_network_status_polling_ms();
    }

    if (time_passed(&rssi_poll_counter, rssi_polling_interval))
    {
        cyw43_ioctl(&cyw43_state, 254, sizeof rssi_tmp, (uint8_t *)&rssi_tmp, CYW43_ITF_STA);
    }
    return (int16_t)rssi_tmp;
}

void wifi_link_callback(struct netif *netif)
{
    DPRINTF("WiFi Link: %s\n", (netif_is_link_up(netif) ? "UP" : "DOWN"));
}

void network_status_callback(struct netif *netif)
{
    if (netif_is_up(netif))
    {
        DPRINTF("WiFi Status: UP (%s)\n", ipaddr_ntoa(netif_ip_addr4(netif)));
        ip_addr_set(&current_ip, netif_ip_addr4(netif));
    }
    else
    {
        DPRINTF("WiFi Status: DOWN\n");
    }
}

// We MUST call this function and avoid the cy43_arch_deinit() function to avoid a crash
void network_terminate()
{
    // This flag is important, because calling a cyw43 function before the initialization will cause a crash
    cyw43_initialized = false;
    cyw43_arch_deinit();
}

int network_wifi_init()
{
    // This flag is important, because calling a cyw43 function before the initialization will cause a crash
    cyw43_initialized = true;
    DPRINTF("CYW43 Logging level: %d\n", CYW43_VERBOSE_DEBUG);
    uint32_t country = CYW43_COUNTRY_WORLDWIDE;
    ConfigEntry *country_entry = find_entry(PARAM_WIFI_COUNTRY);
    if (country_entry != NULL)
    {
        char *valid;
        country = get_country_code(country_entry->value, &valid);
        put_string(PARAM_WIFI_COUNTRY, valid);
    }

    int res;
    DPRINTF("Initialization WiFi...\n");

    if ((res = cyw43_arch_init_with_country(country)))
    {
        DPRINTF("Failed to initialize WiFi: %d\n", res);
        return -1;
    }
    DPRINTF("Country: %s\n", country_entry->value);

    DPRINTF("Enabling STA mode...\n");
    cyw43_arch_enable_sta_mode();

    // Setting the power management
    uint32_t pm_value = 0xa11140; // 0: Disable PM
    ConfigEntry *pm_entry = find_entry(PARAM_WIFI_POWER);
    if (pm_entry != NULL)
    {
        pm_value = strtoul(pm_entry->value, NULL, 16);
    }
    if (pm_value < 5)
    {
        switch (pm_value)
        {
        case 0:
            pm_value = 0xa11140; // DISABLED_PM
            break;
        case 1:
            pm_value = CYW43_PERFORMANCE_PM; // PERFORMANCE_PM
            break;
        case 2:
            pm_value = CYW43_AGGRESSIVE_PM; // AGGRESSIVE_PM
            break;
        case 3:
            pm_value = CYW43_DEFAULT_PM; // DEFAULT_PM
            break;
        default:
            pm_value = CYW43_NO_POWERSAVE_MODE; // NO_POWERSAVE_MODE
            break;
        }
    }
    DPRINTF("Setting power management to: %08x\n", pm_value);
    cyw43_wifi_pm(&cyw43_state, pm_value);
}

int network_init(bool force, bool async, char **pass)
{
    if (!cyw43_initialized)
    {
        // Setup the underlying WiFi stack
        network_wifi_init();
    }

    int res;

    // Set hostname
    char *hostname = find_entry(PARAM_HOSTNAME)->value;

    struct netif *n = &cyw43_state.netif[CYW43_ITF_STA];

    cyw43_arch_lwip_begin();

    if ((hostname != NULL) && (strlen(hostname) > 0))
    {
        strncpy(wifi_hostname, hostname, sizeof(wifi_hostname));
    }
    else
    {
        snprintf(wifi_hostname, sizeof(wifi_hostname), "SidecarT-%s", pico_serial_str());
    }
    DPRINTF("Hostname: %s\n", wifi_hostname);
    netif_set_hostname(n, wifi_hostname);

    // Set callbacks
    netif_set_link_callback(n, wifi_link_callback);
    netif_set_status_callback(n, network_status_callback);

    // DHCP or static IP
    if ((find_entry(PARAM_WIFI_DHCP) != NULL) && (find_entry(PARAM_WIFI_DHCP)->value[0] == 't' || find_entry(PARAM_WIFI_DHCP)->value[0] == 'T'))
    {
        DPRINTF("DHCP enabled\n");
    }
    else
    {
        DPRINTF("Static IP enabled\n");
        dhcp_stop(n);
        ip_addr_t ipaddr, netmask, gw;
        ipaddr.addr = ipaddr_addr(find_entry(PARAM_WIFI_IP)->value);
        netmask.addr = ipaddr_addr(find_entry(PARAM_WIFI_NETMASK)->value);
        gw.addr = ipaddr_addr(find_entry(PARAM_WIFI_GATEWAY)->value);
        netif_set_addr(n, &ipaddr, &netmask, &gw);
        DPRINTF("IP: %s\n", ipaddr_ntoa(&ipaddr));
        DPRINTF("Netmask: %s\n", ipaddr_ntoa(&netmask));
        DPRINTF("Gateway: %s\n", ipaddr_ntoa(&gw));
    }
    netif_set_up(n);

    cyw43_arch_lwip_end();

    // Get the MAC address
    if ((res = cyw43_wifi_get_mac(&cyw43_state, CYW43_ITF_STA, cyw43_mac)))
    {
        DPRINTF("Failed to get MAC address: %d\n", res);
        cyw43_arch_deinit();
        return -2;
    }

    ConfigEntry *ssid = find_entry(PARAM_WIFI_SSID);
    if (strlen(ssid->value) == 0)
    {
        DPRINTF("No SSID found in config. Can't connect\n");
        return -3;
    }
    ConfigEntry *auth_mode = find_entry(PARAM_WIFI_AUTH);
    if (strlen(auth_mode->value) == 0)
    {
        DPRINTF("No auth mode found in config. Can't connect\n");
        return -4;
    }
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
    int error_code = 0;
    if (!async)
    {
        uint32_t network_timeout = NETWORK_CONNECTION_TIMEOUT;
        if (find_entry(PARAM_WIFI_CONNECT_TIMEOUT) != NULL)
        {
            network_timeout = atoi(find_entry(PARAM_WIFI_CONNECT_TIMEOUT)->value) * 1000;
        }
        uint16_t retries = 3;
        do
        {
            DPRINTF("Connecting to SSID=%s, password=%s, auth=%08x. SYNC. Retry: %d\n", ssid->value, password_value, auth_value, retries);
            error_code = cyw43_arch_wifi_connect_timeout_ms(ssid->value, password_value, auth_value, network_timeout);
        } while (error_code != 0 && retries--);
    }
    else
    {
        DPRINTF("Connecting to SSID=%s, password=%s, auth=%08x. ASYNC\n", ssid->value, password_value, auth_value);
        error_code = cyw43_arch_wifi_connect_async(ssid->value, password_value, auth_value);
    }
    free(password_value);
    if (error_code != 0)
    {
        DPRINTF("Failed to connect to WiFi: %d\n", error_code);
        return -5;
    }
    DPRINTF("Connected. Check the connection status...\n");
    return 0;
}

void network_scan()
{
    if (!cyw43_initialized)
    {
        network_wifi_init();
    }
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

            // Store signal strength
            network.rssi = result->rssi;

            // Check if BSSID already exists
            if (!bssid_exists(&network))
            {
                if (strlen(network.ssid) > 0)
                {
                    wifiScanData.networks[wifiScanData.count] = network;
                    wifiScanData.count++;
                    DPRINTF("FOUND NETWORK %s (%s) with auth %d and RSSI %d\n", network.ssid, network.bssid, network.auth_mode, network.rssi);
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

void dhcp_set_ntp_servers(u8_t num_ntp_servers, const ip4_addr_t *ntp_server_addrs)
{
    if (num_ntp_servers > LWIP_DHCP_MAX_NTP_SERVERS)
    {
        num_ntp_servers = LWIP_DHCP_MAX_NTP_SERVERS;
    }
    for (u8_t i = 0; i < num_ntp_servers; i++)
    {
        DPRINTF("Reading NTP server %d: %s\n", i, ip4addr_ntoa(&ntp_server_addrs[i]));
    }
}

void network_wifi_disconnect()
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

inline void wait_cyw43_with_polling(uint32_t milliseconds)
{
    uint64_t start_time = time_us_64();
    cyw43_arch_poll();
    cyw43_arch_wait_for_work_until(make_timeout_time_ms(milliseconds * 0.1));
    while (time_us_64() - start_time < 1000 * milliseconds * 0.9)
    {
        sleep_ms(10);
    }
}

ConnectionStatus get_network_connection_status()
{
    ConnectionStatus old_previous_connection_status = previous_connection_status;
    previous_connection_status = connection_status;
    int link_status = cyw43_tcpip_link_status(&cyw43_state, CYW43_ITF_STA);
    switch (link_status)
    {
    case CYW43_LINK_DOWN:
        connection_status = DISCONNECTED;
        break;
    case CYW43_LINK_JOIN:
        connection_status = CONNECTED_WIFI;
        break;
    case CYW43_LINK_NOIP:
        connection_status = CONNECTED_WIFI_NO_IP;
        break;
    case CYW43_LINK_UP:
        connection_status = CONNECTED_WIFI_IP;
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
        case CYW43_LINK_NOIP:
            DPRINTF("Link no IP\n");
            break;
        case CYW43_LINK_UP:
            DPRINTF("Link up\n");
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

void network_safe_poll()
{
    if (cyw43_initialized)
    {
        cyw43_arch_poll();
    }
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
    DPRINTF("IP: %s\n", ipaddr_ntoa(&current_ip));
    return cyw43_state.netif[0].ip_addr.addr;
}

u_int8_t *get_mac_address()
{
    DPRINTF("MAC: %02X:%02X:%02X:%02X:%02X:%02X\n", cyw43_mac[0], cyw43_mac[1], cyw43_mac[2], cyw43_mac[3], cyw43_mac[4], cyw43_mac[5]);
    return cyw43_state.mac;
}

u_int32_t get_netmask()
{
    DPRINTF("Netmask: %s\n", ipaddr_ntoa(&cyw43_state.netif[0].netmask));
    return cyw43_state.netif[0].netmask.addr;
}

u_int32_t get_gateway()
{
    DPRINTF("Gateway: %s\n", ipaddr_ntoa(&cyw43_state.netif[0].gw));
    return cyw43_state.netif[0].gw.addr;
}

u_int32_t get_dns()
{
    const ip_addr_t *dns_ip = dns_getserver(0);
    DPRINTF("DNS: %s\n", ipaddr_ntoa(dns_ip));
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
    ConfigEntry *wifi_rssi_visible = find_entry(PARAM_WIFI_RSSI);
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
    connection_data->rssi = 0;

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
    {
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
        if ((wifi_rssi_visible != NULL) && (wifi_rssi_visible->value[0] == 't' || wifi_rssi_visible->value[0] == 'T'))
        {
            connection_data->rssi = get_rssi();
        }
        else {
            connection_data->rssi = 0;
        }
        break;
    }
    case CONNECTED_WIFI:
    case CONNECTED_WIFI_NO_IP:
    {
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
        if ((wifi_rssi_visible != NULL) && (wifi_rssi_visible->value[0] == 't' || wifi_rssi_visible->value[0] == 'T'))
        {
            connection_data->rssi = get_rssi();
        }
        else {
            connection_data->rssi = 0;
        }
        break;
    }
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
    DPRINTF("SSID: %s (%ddb) - Status: %d - IPv4: %s - IPv6: %s - GW:%s - Mask:%s - MAC:%s DNS:%s\n",
            connection_data->ssid,
            connection_data->rssi,
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

static void remove_quotes(char *token)
{
    size_t len = strlen(token);
    if (len > 1 && token[0] == '"' && token[len - 1] == '"')
    {
        // Shift the string left by one position to remove the opening quote
        memmove(token, token + 1, len - 2);
        // Null-terminate the string after removing the closing quote
        token[len - 2] = '\0';
    }
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

int split_url(const char *url, UrlParts *parts)
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
        cyw43_arch_poll();
#endif
        cyw43_arch_lwip_begin();
        cyw43_arch_lwip_check();
        cyw43_arch_lwip_end();
        //        cyw43_arch_wait_for_work_until(make_timeout_time_ms(10));
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

static char *next_token(char **line_ptr)
{
    char *line = *line_ptr;
    if (!line || *line == '\0')
        return NULL;

    char *token;
    if (*line == '"')
    {
        // Token starts with a quote, find the closing quote
        token = ++line;
        while (*line && (*line != '"' || *(line + 1) == '"'))
        {
            if (*line == '"' && *(line + 1) == '"')
            {
                // Handle escaped quote
                memmove(line, line + 1, strlen(line));
            }
            line++;
        }
        if (*line == '"')
        {
            *line = '\0';
            line++;
            if (*line == ',')
                line++;
        }
    }
    else
    {
        // Token does not start with a quote, find the next comma
        token = line;
        while (*line && *line != ',')
        {
            line++;
        }
        if (*line)
        {
            *line = '\0';
            line++;
        }
    }

    // Trim leading spaces in the next token
    while (isspace(*line))
        line++;

    *line_ptr = line;
    return token;
}

err_t get_rom_catalog_file(RomInfo **items, int *itemCount, const char *url)
{
    char buff[32768] = {0};
    uint32_t buff_pos = 0;
    httpc_state_t *connection;
    bool complete = false;
    err_t callback_error = ERR_OK; // If any error found in the callback and cannot be returned, store it here
    UrlParts parts;
    u32_t content_len = 0;

    err_t headers(httpc_state_t * connection, void *arg,
                  struct pbuf *hdr, u16_t hdr_len, u32_t content_len)
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
            DPRINTF("JSON something went wrong. HTTP error: %d\n", srv_res);
            callback_error = srv_res;
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

    DPRINTF("Downloading CSV file from %s\n", url);
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
        return -1;
    }
    while (!complete)
    {
#if PICO_CYW43_ARCH_POLL
        network_safe_poll();
#endif
    }

    free_url_parts(&parts);

    if (callback_error != ERR_OK)
    {
        return callback_error;
    }

    // First, count the number of entries
    int count = 0;
    const char *p = buff;
    for (int i = 0; i < content_len; i++)
    {
        if (*p == '\n')
            count++;
        p++;
    }
    *itemCount = count - 1; // The first line is the header

    DPRINTF("Found %d entries\n", *itemCount);
    if (*itemCount == 0)
    {
        // If no entries found, short circuit and return
        return -1;
    }

    RomInfo *current = NULL;
    // Allocate memory for the first RomInfo structure
    *items = malloc(sizeof(RomInfo));
    if (*items == NULL)
    {
        DPRINTF("Failed to allocate memory for RomInfo\n");
        return -1;
    }

    current = *items; // Point to the first RomInfo structure
    char *buff_parse = buff;
    char *line = NULL;
    // Skip the first line
    line = strtok(buff_parse, "\n");
    if (line == NULL)
    {
        DPRINTF("No lines found. Empty file.\n");
        return -1;
    }
    // Increment the buff structure position after the line
    buff_parse += strlen(line) + 1;
    DPRINTF("Header: %s\n", line);
    line = strtok(buff_parse, "\n");
    for (size_t items_count = 0; items_count < *itemCount; items_count++)
    {
        if (line == NULL)
            break; // Ensure line is valid
        buff_parse += strlen(line) + 1;
        DPRINTF("Line: %s\n", line);
        for (size_t token_count = 0; token_count < 5; token_count++)
        {
            // Parse the line splitting by comma
            char *token = next_token(&line);
            if (!token)
                break;
            // DPRINTF("Token(%d): %s\n", token_count, token);
            switch (token_count)
            {
            case 0:
                current->url = strdup(token);
                break;
            case 1:
                current->name = strdup(token);
                break;
                // Ignore the description, we don't use it for now
                //            case 2:
                //
                //                current->description = strdup(token);
                // break;
            case 3:
                current->tags = strdup(token);
                break;
            case 4:
                current->size_kb = atoi(token);
                break;
            default:
                break;
            }
            // Increment the buff structure position after the token
        }
        // Allocate memory for the next RomInfo structure
        if (items_count < *itemCount - 1)
        {
            RomInfo *next = malloc(sizeof(RomInfo));
            if (!next)
            {
                DPRINTF("Memory allocation failed\n");
                return -1;
            }
            *next = (RomInfo){0};
            current->next = next;
            current = next;
        }
        else
        {
            current->next = NULL;
        }
        line = strtok(buff_parse, "\n");
    }
    DPRINTF("Parsing complete\n");

    DPRINTF("Returning %d items\n", *itemCount);
    return callback_error;
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
    fflush(stdout);
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
#if PICO_CYW43_ARCH_POLL
        network_safe_poll();
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
        network_safe_poll();
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
#if PICO_CYW43_ARCH_POLL
        network_safe_poll();
#endif
    }

    // Close open file
    f_close(&dest_file);

    free_url_parts(&parts);
    free(buff);
    return callback_error;
}
