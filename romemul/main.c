/**
 * File: main.c
 * Author: Diego Parrilla Santamaría
 * Date: July 2023
 * Copyright: 2023 - GOODDATA LABS SL
 * Description: C file that launches the ROM emulator or configurator
 */

#include "include/romloader.h"
#include "include/romemul.h"

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
    set_sys_clock_khz(RP2040_CLOCK_FREQ_KHZ, true);

    // Configure the input pins for ROM4 and ROM3
    gpio_init(SELECT_GPIO);
    gpio_set_dir(SELECT_GPIO, GPIO_IN);
    gpio_set_pulls(SELECT_GPIO, false, true); // Pull down (false, true)
    gpio_pull_down(SELECT_GPIO);

    // Init the CYW43 WiFi module
    if (cyw43_arch_init())
    {
        printf("Wi-Fi init failed\n");
        return -1;
    }

    // Initialize chosen serial port
    stdio_init_all();

    if (gpio_get(5) == 0)
    {
        printf("No SELECT button pressed. Continue loading ROM Emulator.\n");

        // Canonical way to initialize the ROM emulator:
        // No IRQ handler callbacks, copy the FLASH ROMs to RAM, and start the state machine
        init_romemul(NULL, NULL, true);

        // The "E" character stands for "Emulator"
        blink_morse('E');

        // Loop forever and block until the state machine put data into the FIFO
        while (true)
        {
            tight_loop_contents();
        }

        printf("You should never see this line...\n");
        return 0;
    }
    else
    {
        printf("SELECT button pressed. Launch configurator.\n");

        // Delete FLASH ROMs
        delete_FLASH();

        // Copy the firmware to RAM
        copy_firmware_to_RAM();

        // Hybrid way to initialize the ROM emulator:
        // IRQ handler callback to read the commands in ROM3, and NOT copy the FLASH ROMs to RAM
        // and start the state machine
        init_romemul(NULL, dma_irq_handler_lookup_callback, false);

        printf("Ready to accept commands.\n");

        // The "L" character stands for "Loader"
        blink_morse('L');

        init_firmware();

        // Now the user needs to reset or poweroff the board to load the ROMs
        printf("Rebooting the board.\n");
        sleep_ms(1000); // Give me a break... to display the message

        watchdog_enable(1, 1);
        while (1)
            ;
        return 0;
    }
}
