#ifndef COMMANDS_H_
#define COMMANDS_H_

// The commands code is the combinatino of two bytes:
// - The most significant byte is the application code. All the commands of an app should have the same code
// - The least significant byte is the command code. Each command of an app should have a different code
#define APP_CONFIGURATOR 0x00 // The configurator app
#define APP_ROMEMUL 0x01      // The ROM emulator app. Should not have any command
#define APP_FLOPPYEMUL 0x02   // The floppy emulator app
#define APP_RTCEMUL 0x03      // The RTC emulator app
#define APP_GEMDRVEMUL 0x04   // The GEMDRIVE app.

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
#define GET_SD_DATA 20          // Get the SD card status, size, free space and folders
#define GET_LATEST_RELEASE 21   // Get the latest release version of the firmware
#define CREATE_FLOPPY 22        // Create a floppy image based in a template
#define BOOT_RTC 23             // Boot the RTC emulator
#define CLEAN_START 24          // Start the configurator when the app starts
#define BOOT_GEMDRIVE 25        // Boot the GEMDRIVE emulator
#define REBOOT 26               // Reboot the device

// APP_ROMEMUL commands
// No commands

// APP_FLOPPYEMUL commands
#define FLOPPYEMUL_SAVE_VECTORS (APP_FLOPPYEMUL << 8 | 0)      // Save the vectors of the floppy emulator
#define FLOPPYEMUL_READ_SECTORS (APP_FLOPPYEMUL << 8 | 1)      // Read sectors from the floppy emulator
#define FLOPPYEMUL_WRITE_SECTORS (APP_FLOPPYEMUL << 8 | 2)     // Write sectors to the floppy emulator
#define FLOPPYEMUL_PING (APP_FLOPPYEMUL << 8 | 3)              // Ping the floppy emulator
#define FLOPPYEMUL_SAVE_HARDWARE (APP_FLOPPYEMUL << 8 | 4)     // Save the hardware of the floppy emulator
#define FLOPPYEMUL_SET_SHARED_VAR (APP_FLOPPYEMUL << 8 | 5)    // Set a shared variable
#define FLOPPYEMUL_RESET (APP_FLOPPYEMUL << 8 | 6)             // Reset the floppy emulator
#define FLOPPYEMUL_MOUNT_DRIVE_A (APP_FLOPPYEMUL << 8 | 7)     // Mount the drive A of the floppy emulator
#define FLOPPYEMUL_UNMOUNT_DRIVE_A (APP_FLOPPYEMUL << 8 | 8)   // Unmount the drive A of the floppy emulator
#define FLOPPYEMUL_MOUNT_DRIVE_B (APP_FLOPPYEMUL << 8 | 9)     // Mount the drive B of the floppy emulator
#define FLOPPYEMUL_UNMOUNT_DRIVE_B (APP_FLOPPYEMUL << 8 | 10)  // Unmount the drive B of the floppy emulator
#define FLOPPYEMUL_SHOW_VECTOR_CALL (APP_FLOPPYEMUL << 8 | 11) // Show the vector call of the floppy emulator

// APP_RTCEMUL commands
#define RTCEMUL_TEST_NTP (APP_RTCEMUL << 8 | 0)     // Test if the network is ready to use NTP
#define RTCEMUL_READ_TIME (APP_RTCEMUL << 8 | 1)    // Read the time from the internal RTC
#define RTCEMUL_SAVE_VECTORS (APP_RTCEMUL << 8 | 2) // Save the vectors of the RTC emulator
#define RTCEMUL_REENTRY_LOCK  (APP_RTCEMUL << 8 | 3) // Command code to lock the reentry to XBIOS in the Sidecart
#define RTCEMUL_REENTRY_UNLOCK  (APP_RTCEMUL << 8 | 4) // Command code to unlock the reentry to XBIOS in the Sidecart
#define RTCEMUL_SET_SHARED_VAR  (APP_RTCEMUL << 8 | 5) // Set a shared variable

// APP_GEMDRVEMUL commands
#define GEMDRVEMUL_PING (APP_GEMDRVEMUL << 8 | 0)              // Ping the GEMDRIVE emulator
#define GEMDRVEMUL_SAVE_VECTORS (APP_GEMDRVEMUL << 8 | 1)      // Save the vectors of the GEMDRIVE emulator
#define GEMDRVEMUL_SHOW_VECTOR_CALL (APP_GEMDRVEMUL << 8 | 2)  // Show the vector call of the GEMDRIVE emulator
#define GEMDRVEMUL_REENTRY_LOCK (APP_GEMDRVEMUL << 8 | 3)      // Lock the reentry of the GEMDRIVE emulator
#define GEMDRVEMUL_REENTRY_UNLOCK (APP_GEMDRVEMUL << 8 | 4)    // Unlock the reentry of the GEMDRIVE emulator
#define GEMDRVEMUL_CANCEL (APP_GEMDRVEMUL << 8 | 5)            // Cancel the current execution
#define GEMDRVEMUL_RTC_START (APP_GEMDRVEMUL << 8 | 6)         // Start RTC emulator
#define GEMDRVEMUL_RTC_STOP (APP_GEMDRVEMUL << 8 | 7)          // Stop RTC emulator
#define GEMDRVEMUL_NETWORK_START (APP_GEMDRVEMUL << 8 | 8)     // Start the network emulator
#define GEMDRVEMUL_NETWORK_STOP (APP_GEMDRVEMUL << 8 | 9)      // Stop the network emulator
#define GEMDRVEMUL_DGETDRV_CALL (APP_GEMDRVEMUL << 8 | 0x19)   // Show the Dgetdrv call
#define GEMDRVEMUL_FSETDTA_CALL (APP_GEMDRVEMUL << 8 | 0x1A)   // Show the Fsetdta call
#define GEMDRVEMUL_DFREE_CALL (APP_GEMDRVEMUL << 8 | 0x36)     // Show the Dfree call
#define GEMDRVEMUL_DCREATE_CALL (APP_GEMDRVEMUL << 8 | 0x39)   // Show the Dcreate call
#define GEMDRVEMUL_DDELETE_CALL (APP_GEMDRVEMUL << 8 | 0x3A)   // Show the Ddelete call
#define GEMDRVEMUL_DSETPATH_CALL (APP_GEMDRVEMUL << 8 | 0x3B)  // Show the Dgetpath call
#define GEMDRVEMUL_FCREATE_CALL (APP_GEMDRVEMUL << 8 | 0x3C)   // Show the Fcreate call
#define GEMDRVEMUL_FOPEN_CALL (APP_GEMDRVEMUL << 8 | 0x3D)     // Show the Fopen call
#define GEMDRVEMUL_FCLOSE_CALL (APP_GEMDRVEMUL << 8 | 0x3E)    // Show the Fclose call
#define GEMDRVEMUL_FDELETE_CALL (APP_GEMDRVEMUL << 8 | 0x41)   // Show the Fdelete call
#define GEMDRVEMUL_FSEEK_CALL (APP_GEMDRVEMUL << 8 | 0x42)     // Show the Fseek call
#define GEMDRVEMUL_FATTRIB_CALL (APP_GEMDRVEMUL << 8 | 0x43)   // Show the Fattrib call
#define GEMDRVEMUL_DGETPATH_CALL (APP_GEMDRVEMUL << 8 | 0x47)  // Show the Dgetpath call
#define GEMDRVEMUL_FSFIRST_CALL (APP_GEMDRVEMUL << 8 | 0x4E)   // Show the Fsfirst call
#define GEMDRVEMUL_FSNEXT_CALL (APP_GEMDRVEMUL << 8 | 0x4F)    // Show the Fsnext call
#define GEMDRVEMUL_FRENAME_CALL (APP_GEMDRVEMUL << 8 | 0x56)   // Show the Frename call
#define GEMDRVEMUL_FDATETIME_CALL (APP_GEMDRVEMUL << 8 | 0x57) // Show the Fdatetime call

#define GEMDRVEMUL_PEXEC_CALL (APP_GEMDRVEMUL << 8 | 0x4B)  // Show the Pexec call
#define GEMDRVEMUL_MALLOC_CALL (APP_GEMDRVEMUL << 8 | 0x48) // Show the Malloc call

#define GEMDRVEMUL_READ_BUFF_CALL (APP_GEMDRVEMUL << 8 | 0x81)   // Read from sdCard the read buffer call
#define GEMDRVEMUL_DEBUG (APP_GEMDRVEMUL << 8 | 0x82)            // Show the debug info
#define GEMDRVEMUL_SAVE_BASEPAGE (APP_GEMDRVEMUL << 8 | 0x83)    // Save a basepage
#define GEMDRVEMUL_SAVE_EXEC_HEADER (APP_GEMDRVEMUL << 8 | 0x84) // Save an exec header

#define GEMDRVEMUL_SET_SHARED_VAR (APP_GEMDRVEMUL << 8 | 0x87)   // Set a shared variable
#define GEMDRVEMUL_WRITE_BUFF_CALL (APP_GEMDRVEMUL << 8 | 0x88)  // Write to sdCard the write buffer call
#define GEMDRVEMUL_WRITE_BUFF_CHECK (APP_GEMDRVEMUL << 8 | 0x89) // Write to sdCard the write buffer check call
#define GEMDRVEMUL_DTA_EXIST_CALL (APP_GEMDRVEMUL << 8 | 0x8A)   // Check if the DTA exists in the rp2040 memory
#define GEMDRVEMUL_DTA_RELEASE_CALL (APP_GEMDRVEMUL << 8 | 0x8B) // Release the DTA from the rp2040 memory

typedef struct
{
    unsigned int value;
    const char *name;
} CommandName;

extern const CommandName commandStr[];
extern const int numCommands;
#endif // COMMANDS_H_