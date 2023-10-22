/**
 * File: dongleemul.c
 * Author: Diego Parrilla Santamaría
 * Date: October 2023
 * Copyright: 2023 - GOODDATA LABS SL
 * Description: Emulate Dongle
 */

#include "include/dongleemul.h"

static PIO default_pio = pio0;

static uint32_t counter = 0;

static int init_monitor_rom4(PIO pio)
{
    // Configure the monitor ROM4 state machine
    // Add the assembled program to the PIO into the memory where there are enough space
    uint offsetMonitorROM4 = pio_add_program(pio, &monitor_rom4_program);

    // Claim a free state machine from the PIO read program
    uint smMonitorROM4 = pio_claim_unused_sm(pio, true);

    // Start the state machine, executing the PIO read program
    monitor_rom4_program_init(pio, smMonitorROM4, offsetMonitorROM4, SAMPLE_DIV_FREQ);

    // Enable the state machine
    pio_sm_set_enabled(pio, smMonitorROM4, true);

    DPRINTF("ROM4 signal monitor initialized.\n");
    return smMonitorROM4;
}

static int init_monitor_rom3(PIO pio)
{
    // Configure the monitor ROM3 state machine
    // Add the assembled program to the PIO into the memory where there are enough space
    uint offsetMonitorROM3 = pio_add_program(pio, &monitor_rom3_program);

    // Claim a free state machine from the PIO read program
    uint smMonitorROM3 = pio_claim_unused_sm(pio, true);

    // Start the state machine, executing the PIO read program
    // monitor rom3 and rom4 share the same init function
    monitor_rom4_program_init(pio, smMonitorROM3, offsetMonitorROM3, SAMPLE_DIV_FREQ);

    // Enable the state machine
    pio_sm_set_enabled(pio, smMonitorROM3, true);

    DPRINTF("ROM3 signal monitor initialized.\n");
    return smMonitorROM3;
}

static uint init_dongle_emulator(PIO pio)
{

    // Configure the read PIO state machine
    // Add the assembled program to the PIO into the memory where there are enough space
    uint offsetReadROM = pio_add_program(pio, &romemul_read_program);

    // Claim a free state machine from the PIO read program
    uint smReadDongle = pio_claim_unused_sm(pio, true);

    // Start the state machine, executing the PIO read program
    romemul_read_program_init(pio, smReadDongle, offsetReadROM, READ_ADDR_GPIO_BASE, READ_ADDR_PIN_COUNT, READ_SIGNAL_GPIO_BASE, SAMPLE_DIV_FREQ);

    // Need to clear _input shift counter_, as well as FIFO, because there may be
    // partial ISR contents left over from a previous run. sm_restart does this.
    pio_sm_clear_fifos(pio, smReadDongle);
    pio_sm_restart(pio, smReadDongle);
    pio_sm_set_enabled(pio, smReadDongle, true);

    DPRINTF("Dongle PIO state machine initialized.\n");
    return smReadDongle;
}

int init_dongleemul(int safe_config_reboot)
{

    inline uint16_t dongle_function(uint32_t addr)
    {
        // IMPLEMENT HERRE THE FUNCTION TO RETURN THE VALUE FOR THE ADDRESS GIVEN
        return (uint16_t)(addr + counter++);
    }

    set_sys_clock_khz(RP2040_CLOCK_FREQ_KHZ + 75000, true);

    // Grant high bus priority to the DMA, so it can shove the processors out
    // of the way. This should only be needed if you are pushing things up to
    // >16bits/clk here, i.e. if you need to saturate the bus completely.
    bus_ctrl_hw->priority = BUSCTRL_BUS_PRIORITY_DMA_W_BITS | BUSCTRL_BUS_PRIORITY_DMA_R_BITS;

    int smMonitorROM4 = init_monitor_rom4(default_pio);
    if (smMonitorROM4 < 0)
    {
        DPRINTF("Error initializing ROM4 monitor. Error code: %d\n", smMonitorROM4);
        return -1;
    }

    int smMonitorROM3 = init_monitor_rom3(default_pio);
    if (smMonitorROM3 < 0)
    {
        DPRINTF("Error initializing ROM3 monitor. Error code: %d\n", smMonitorROM3);
        return -1;
    }

    uint smReadDongle = init_dongle_emulator(default_pio);
    if (smReadDongle < 0)
    {
        DPRINTF("Error initializing CUBASE emulator. Error code: %d\n", smReadDongle);
        return -1;
    }

    // Setting the signals after configuring the PIO makes the ROM emulator to not put
    // inconsistent data in the address or data bus at any time, avoiding glitches.

    // Configure the output pins for the READ and WRITE signals.
    pio_gpio_init(default_pio, READ_SIGNAL_GPIO_BASE);
    gpio_set_dir(READ_SIGNAL_GPIO_BASE, GPIO_OUT);
    gpio_set_pulls(READ_SIGNAL_GPIO_BASE, true, false); // Pull up (true, false)
    gpio_put(READ_SIGNAL_GPIO_BASE, 1);

    pio_gpio_init(default_pio, WRITE_SIGNAL_GPIO_BASE);
    gpio_set_dir(WRITE_SIGNAL_GPIO_BASE, GPIO_OUT);
    gpio_set_pulls(WRITE_SIGNAL_GPIO_BASE, true, false); // Pull up (true, false)
    gpio_put(WRITE_SIGNAL_GPIO_BASE, 1);

    // Configure the input pins for ROM4 and ROM3
    pio_gpio_init(default_pio, ROM4_GPIO);
    gpio_set_dir(ROM4_GPIO, GPIO_IN);
    gpio_set_pulls(ROM4_GPIO, true, false); // Pull up (true, false)
    gpio_pull_up(ROM4_GPIO);

    pio_gpio_init(default_pio, ROM3_GPIO);
    gpio_set_dir(ROM3_GPIO, GPIO_IN);
    gpio_set_pulls(ROM3_GPIO, true, false); // Pull up (true, false)
    gpio_pull_up(ROM3_GPIO);

    // Configure the output pins for the output data bus
    for (int i = 0; i < WRITE_DATA_PIN_COUNT; i++)
    {
        pio_gpio_init(default_pio, WRITE_DATA_GPIO_BASE + i);
        gpio_set_dir(WRITE_DATA_GPIO_BASE + i, GPIO_OUT);
        gpio_set_pulls(WRITE_DATA_GPIO_BASE + i, false, true); // Pull down (false, true)
        gpio_put(WRITE_DATA_GPIO_BASE + i, 0);
    }

    bool write_config_only_once = true;

    DPRINTF("Dongle Emulator started.\n");

    // Push to the FIFO the Most Significant word of the addresses to read from the ROM
    // in the lower 16 bits of the 32 bits of the FIFO register.
    // Only need 15 bits from the rp2040 memory address, so shift right 17 bits to get the 15 bits
    // In the PIO program, the address is shifted left 1 bit to make room for the ROM4 signal
    // and the 16 bits of the address from the GPIO input.
    // So the address is created as follows:
    // bits 31-17: In this scenario zeored because we don't need it
    // bit 16: ROM4 signal. Since is an inverted signal, we set it to 0 for ROM4 and 1 if not ROM4 (ROM3)
    // bits 15-0: 16 bits of the address from the GPIO input
    // Please do not modify these values, because they are carefully selected to avoid conflicts
    // and be performant.
    pio_sm_put_blocking(default_pio, smReadDongle, 0);

    // Let's keep the state of the simulated dongle
    uint16_t latch_sim = 0;
#define BIT(value, position) (((value) >> (position)) & 0x01)
    while (true)
    {
        // while (pio_sm_is_rx_fifo_empty(default_pio, smReadDongle))
        // {
        //     // If SELECT button is pressed, launch the configurator
        //     if (gpio_get(5) != 0)
        //     {
        //         select_button_action(safe_config_reboot, write_config_only_once);
        //         // Write config only once to avoid hitting the flash too much
        //         write_config_only_once = false;
        //     }
        // }
        // uint32_t read_addr = pio_sm_get_blocking(default_pio, smReadDongle);
        // pio_sm_put(default_pio, smReadDongle, dongle_function(read_addr & 0x1FFFF) << 16);

        while ((default_pio->fstat & (1u << (PIO_FSTAT_RXEMPTY_LSB + smReadDongle))) != 0)
        {
            // If SELECT button is pressed, launch the configurator
            if (gpio_get(5) != 0)
            {
                select_button_action(safe_config_reboot, write_config_only_once);
                // Write config only once to avoid hitting the flash too much
                write_config_only_once = false;
            }
        };
        uint32_t read_addr = default_pio->rxf[smReadDongle];
        latch_sim = BIT(latch_sim, 0) ^ (!BIT(read_addr, 0) & !BIT(read_addr, 14)) | (BIT(read_addr, 0) & BIT(read_addr, 8));
        default_pio->txf[smReadDongle] = ((read_addr + read_addr) & 0x1FFFF) << 16;
    }
}
