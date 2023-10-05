/**
 * File: main.c
 * Author: Diego Parrilla Santamaría
 * Date: July 2023
 * Copyright: 2023 - GOODDATA LABS SL
 * Description: C file that launches the ROM emulator or configurator
 */

#include "include/romloader.h"
#include "include/romemul.h"
#include "include/floppyemul.h"

// List of roms to include in the program
// Keep in mind that actually they don't load in RAM, but in FLASH
// and then the code has to move them manually to the .rom3 or .rom4 sections
// #include "stetest.h"

/**
 * @brief   Blinks an LED to represent a given character in Morse code.
 *
 * @param   ch  The character to blink in Morse code.
 *
 * @details This function searches for the provided character in the
 *          `morseAlphabet` structure array to get its Morse code representation.
 *          If found, it then blinks an LED in the pattern of dots and dashes
 *          corresponding to the Morse code of the character. The LED blinks are
 *          separated by time intervals defined by constants such as DOT_DURATION_MS,
 *          DASH_DURATION_MS, SYMBOL_GAP_MS, and CHARACTER_GAP_MS.
 *
 * @return  void
 */
void blink_morse(char ch)
{
    const char *morseCode = NULL;

    // Search for character's Morse code
    for (int i = 0; morseAlphabet[i].character != '\0'; i++)
    {
        if (morseAlphabet[i].character == ch)
        {
            morseCode = morseAlphabet[i].morse;
            break;
        }
    }

    // If character not found in Morse alphabet, return
    if (!morseCode)
        return;

    // Blink pattern
    for (int i = 0; morseCode[i] != '\0'; i++)
    {
        cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 1);
        if (morseCode[i] == '.')
        {
            sleep_ms(DOT_DURATION_MS); // Short blink for dot
        }
        else
        {
            sleep_ms(DASH_DURATION_MS); // Long blink for dash
        }
        cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 0);
        sleep_ms(SYMBOL_GAP_MS); // Gap between symbols
    }

    sleep_ms(CHARACTER_GAP_MS); // Gap between characters
}

int main()
{
    // Set the clock frequency. 20% overclocking
    set_sys_clock_khz(RP2040_CLOCK_FREQ_KHZ, true);

    // Configure the input pins for ROM4 and ROM3
    gpio_init(SELECT_GPIO);
    gpio_set_dir(SELECT_GPIO, GPIO_IN);
    gpio_set_pulls(SELECT_GPIO, false, true); // Pull down (false, true)
    gpio_pull_down(SELECT_GPIO);

    // Initialize chosen serial port
    stdio_init_all();
    setvbuf(stdout, NULL, _IONBF, 1); // specify that the stream should be unbuffered

    // Only startup information to display
    printf("Sidecart ROM emulator. %s (%s). %s mode.\n\n", RELEASE_VERSION, RELEASE_DATE, _DEBUG ? "DEBUG" : "RELEASE");

    // Init the CYW43 WiFi module
    if (cyw43_arch_init())
    {
        DPRINTF("Wi-Fi init failed\n");
        return -1;
    }

    // Load the config from FLASH
    load_all_entries();

    ConfigEntry *default_config_entry = find_entry("BOOT_FEATURE");
    DPRINTF("BOOT_FEATURE: %s\n", default_config_entry->value);

    // No SELECT button pressed or CONFIGURATOR entry found in config. Normal boot
    if ((gpio_get(5) == 0) && (strcmp(default_config_entry->value, "CONFIGURATOR") != 0))
    {
        DPRINTF("No SELECT button pressed.\n");
        if (strcmp(default_config_entry->value, "ROM_EMULATOR") == 0)
        {
            DPRINTF("No SELECT button pressed. ROM_EMULATOR entry found in config. Launching.\n");

            // Check if Delay ROM emulation (ripper style boot) is true
            ConfigEntry *rom_delay_config_entry = find_entry("DELAY_ROM_EMULATION");
            DPRINTF("DELAY_ROM_EMULATION: %s\n", rom_delay_config_entry->value);
            if ((strcmp(rom_delay_config_entry->value, "true") == 0) || (strcmp(rom_delay_config_entry->value, "TRUE") == 0) || (strcmp(rom_delay_config_entry->value, "T") == 0))
            {
                DPRINTF("Delaying ROM emulation.\n");
                // The "D" character stands for "D"
                blink_morse('D');

                // While until the user presses the SELECT button again to launch the ROM emulator
                while (gpio_get(5) == 0)
                {
                    tight_loop_contents();
                    sleep_ms(1000); // Give me a break... to display the message
                }

                DPRINTF("SELECT button pressed.\n");
                // Now wait for the user to release the SELECT button
                while (gpio_get(5) != 0)
                {
                    tight_loop_contents();
                }

                DPRINTF("SELECT button released. Launching ROM emulator.\n");
            }

            // Canonical way to initialize the ROM emulator:
            // No IRQ handler callbacks, copy the FLASH ROMs to RAM, and start the state machine
            init_romemul(NULL, NULL, true);

            // The "E" character stands for "Emulator"
            blink_morse('E');

            // Loop forever and block until the state machine put data into the FIFO
            while (true)
            {
                tight_loop_contents();
                sleep_ms(1000); // Give me a break... to display the message
                if (gpio_get(5) != 0)
                {
                    DPRINTF("SELECT button pressed. Launch configurator.\n");
                    watchdog_reboot(0, SRAM_END, 10);
                    while (1)
                        ;
                    return 0;
                }
            }
        }
        if (strcmp(default_config_entry->value, "FLOPPY_EMULATOR") == 0)
        {
            DPRINTF("FLOPPY_EMULATOR entry found in config. Launching.\n");

            // Copy the ST floppy firmware emulator to RAM
            copy_floppy_firmware_to_RAM();

            // Reserve memory for the protocol parser
            init_protocol_parser();
            // Hybrid way to initialize the ROM emulator:
            // IRQ handler callback to read the commands in ROM3, and NOT copy the FLASH ROMs to RAM
            // and start the state machine
            init_romemul(NULL, floppyemul_dma_irq_handler_lookup_callback, false);
            DPRINTF("Ready to accept commands.\n");

            init_floppyemul();

            // Loop forever and block until the state machine put data into the FIFO
            while (true)
            {
                tight_loop_contents();
                if (gpio_get(5) != 0)
                {
                    DPRINTF("SELECT button pressed. Launch configurator.\n");
                    watchdog_reboot(0, SRAM_END, 10);
                    while (1)
                        ;
                    return 0;
                }
            }
        }

        DPRINTF("You should never see this line...\n");
        return 0;
    }
    else
    {
        DPRINTF("SELECT button pressed. Launch configurator.\n");

        // Keep in development mode
        if (strcmp(default_config_entry->value, "CONFIGURATOR") != 0)
        {
            put_string("BOOT_FEATURE", "CONFIGURATOR");
            write_all_entries();
        }

        network_init();

        // Print the config
        print_config_table();

        // Should not write if not necessary
        // Delete FLASH ROMs
        //        delete_FLASH();

        // Copy the firmware to RAM
        copy_firmware_to_RAM();

        // Reserve memory for the protocol parser
        init_protocol_parser();

        // Hybrid way to initialize the ROM emulator:
        // IRQ handler callback to read the commands in ROM3, and NOT copy the FLASH ROMs to RAM
        // and start the state machine
        init_romemul(NULL, dma_irq_handler_lookup_callback, false);

        DPRINTF("Ready to accept commands.\n");

        // The "F" character stands for "Firmware"
        blink_morse('F');

        init_firmware();

        // Now the user needs to reset or poweroff the board to load the ROMs
        DPRINTF("Rebooting the board.\n");
        sleep_ms(1000); // Give me a break... to display the message

        watchdog_reboot(0, SRAM_END, 10);
        while (1)
            ;
        return 0;
    }
}
