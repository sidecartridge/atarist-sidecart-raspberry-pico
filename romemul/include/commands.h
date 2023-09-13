#ifndef COMMANDS_H_
#define COMMANDS_H_

#define DOWNLOAD_ROM 0          // Download a ROM from the URL
#define LOAD_ROM 1              // Load a ROM from the SD card
#define LIST_ROMS 2             // List the ROMs in the SD card
#define GET_CONFIG 3            // Get the configuration of the device
#define PUT_CONFIG_STRING 4     // Put a configuration string parameter in the device
#define PUT_CONFIG_INTEGER 5    // Put a configuration integer parameter in the device
#define PUT_CONFIG_BOOL 6       // Put a configuration boolean parameter in the device
#define SAVE_CONFIG 7           // Persist the configuration in the FLASH of the device
#define RESET_DEVICE 8          // Reset the device to the default configuration
#define LAUNCH_SCAN_NETWORKS 9  // Launch the scan the networks. No results should return here
#define GET_SCANNED_NETWORKS 10 // Read the result of the scanned networks
#define CONNECT_NETWORK 11      // Connect to a network. Needs the SSID, password and auth method
#define GET_IP_DATA 12          // Get the IP, mask and gateway of the device
#define DISCONNECT_NETWORK 13   // Disconnect from the network
#define GET_ROMS_JSON_FILE 14   // Download the JSON file of ROMs from the URL

#endif // COMMANDS_H_