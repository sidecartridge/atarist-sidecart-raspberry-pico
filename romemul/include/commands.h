#ifndef COMMANDS_H_
#define COMMANDS_H_

// The commands code is the combinatino of two bytes:
// - The most significant byte is the application code. All the commands of an app should have the same code
// - The least significant byte is the command code. Each command of an app should have a different code
#define APP_CONFIGURATOR 0x00 // The configurator app
#define APP_ROMEMUL 0x01      // The ROM emulator app. Should not have any command
#define APP_FLOPPYEMUL 0x02   // The floppy emulator app

// APP_CONFIGURATOR commands
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
#define LOAD_FLOPPY_RO 15       // Load a floppy image from the SD card in read-only mode
#define LIST_FLOPPIES 16        // List the floppy images in the SD card
#define LOAD_FLOPPY_RW 17       // Load a floppy image from the SD card in read-write mode
#define QUERY_FLOPPY_DB 18      // Query the floppy database. Need to pass the letter or number to query
#define DOWNLOAD_FLOPPY 19      // Download a floppy image from the URL

// APP_ROMEMUL commands
// No commands

// APP_FLOPPYEMUL commands
#define FLOPPYEMUL_SAVE_VECTORS (APP_FLOPPYEMUL << 8 | 0)  // Set the vectors of the floppy emulator
#define FLOPPYEMUL_SET_BPB (APP_FLOPPYEMUL << 8 | 1)       // Set the BPB of the floppy emulator
#define FLOPPYEMUL_READ_SECTORS (APP_FLOPPYEMUL << 8 | 2)  // Read sectors from the floppy emulator
#define FLOPPYEMUL_WRITE_SECTORS (APP_FLOPPYEMUL << 8 | 3) // Write sectors to the floppy emulator
#define FLOPPYEMUL_PING (APP_FLOPPYEMUL << 8 | 4)          // Ping the floppy emulator

#endif // COMMANDS_H_