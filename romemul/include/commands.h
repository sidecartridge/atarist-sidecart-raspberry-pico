#ifndef COMMANDS_H_
#define COMMANDS_H_

#define LOAD_ROM 1           // Load a ROM from the SD card
#define LIST_ROMS 2          // List the ROMs in the SD card
#define GET_CONFIG 3         // Get the configuration of the device
#define PUT_CONFIG_STRING 4  // Put a configuration string parameter in the device
#define PUT_CONFIG_INTEGER 5 // Put a configuration integer parameter in the device
#define PUT_CONFIG_BOOL 6    // Put a configuration boolean parameter in the device
#define SAVE_CONFIG 7        // Persist the configuration in the FLASH of the device
#define RESET_DEVICE 8       // Reset the device to the default configuration

#endif // COMMANDS_H_