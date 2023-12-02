/**
 * File: gemdrvemul.c
 * Author: Diego Parrilla Santamaría
 * Date: November 2023
 * Copyright: 2023 - GOODDATA LABS SL
 * Description: Emulate GEMDOS hard disk driver
 */

#include "include/gemdrvemul.h"

static uint16_t *payloadPtr = NULL;
static char *fullpath_a = NULL;
static uint32_t random_token;
static bool ping_received = false;
static bool hd_folder_ready = false;

// Save XBIOS vector variables
static uint32_t gemdos_trap_address_old;
static uint32_t gemdos_trap_address_xbra;
static uint16_t trap_call = 0xFFFF;
static bool save_vectors = false;

static void __not_in_flash_func(handle_protocol_command)(const TransmissionProtocol *protocol)
{
    ConfigEntry *entry = NULL;
    uint16_t value_payload = 0;
    // Handle the protocol
    switch (protocol->command_id)
    {
    case GEMDRVEMUL_SAVE_VECTORS:
        // Save the vectors needed for the floppy emulation
        DPRINTF("Command SAVE_VECTORS (%i) received: %d\n", protocol->command_id, protocol->payload_size);
        payloadPtr = (uint16_t *)protocol->payload + 2;
        gemdos_trap_address_old = ((uint32_t)payloadPtr[0] << 16) | payloadPtr[1];
        payloadPtr += 2;
        gemdos_trap_address_xbra = ((uint32_t)payloadPtr[1] << 16) | payloadPtr[0];
        random_token = ((*((uint32_t *)protocol->payload) & 0xFFFF0000) >> 16) | ((*((uint32_t *)protocol->payload) & 0x0000FFFF) << 16);
        save_vectors = true;
        break;
    case GEMDRVEMUL_PING:
        DPRINTF("Command PING (%i) received: %d\n", protocol->command_id, protocol->payload_size);
        random_token = ((*((uint32_t *)protocol->payload) & 0xFFFF0000) >> 16) | ((*((uint32_t *)protocol->payload) & 0x0000FFFF) << 16);
        ping_received = true;
        break; // ... handle other commands
    case GEMDRVEMUL_SHOW_VECTOR_CALL:
        DPRINTF("Command SHOW_VECTOR_CALL (%i) received: %d\n", protocol->command_id, protocol->payload_size);
        payloadPtr = (uint16_t *)protocol->payload + 2;
        trap_call = (uint16_t)payloadPtr[0];
        random_token = ((*((uint32_t *)protocol->payload) & 0xFFFF0000) >> 16) | ((*((uint32_t *)protocol->payload) & 0x0000FFFF) << 16);
        break;
    default:
        DPRINTF("Unknown command: %d\n", protocol->command_id);
    }
}

// Interrupt handler callback for DMA completion
void __not_in_flash_func(gemdrvemul_dma_irq_handler_lookup_callback)(void)
{
    // Clear the interrupt request for the channel
    dma_hw->ints1 = 1u << lookup_data_rom_dma_channel;

    // Read the address to process
    uint32_t addr = (uint32_t)dma_hw->ch[lookup_data_rom_dma_channel].al3_read_addr_trig;

    // Avoid priting anything inside an IRQ handled function
    // DPRINTF("DMA LOOKUP: $%x\n", addr);
    if (addr >= ROM3_START_ADDRESS)
    {
        parse_protocol((uint16_t)(addr & 0xFFFF), handle_protocol_command);
    }
}

int copy_gemdrv_firmware_to_RAM()
{
    // Need to initialize the ROM4 section with the firmware data
    extern uint16_t __rom_in_ram_start__;
    uint16_t *rom4_dest = &__rom_in_ram_start__;
    uint16_t *rom4_src = (uint16_t *)gemdrvemulROM;
    for (int i = 0; i < gemdrvemulROM_length; i++)
    {
        uint16_t value = *rom4_src++;
        *rom4_dest++ = value;
    }
    DPRINTF("GEMDRIVE firmware copied to RAM.\n");
    return 0;
}

int init_gemdrvemul(bool safe_config_reboot)
{

    FRESULT fr; /* FatFs function common result code */
    FATFS fs;

    srand(time(0));
    printf("Initializing GEMDRIVE...\n"); // Print alwayse

    bool write_config_only_once = true;

    DPRINTF("Waiting for commands...\n");
    uint32_t memory_shared_address = ROM3_START_ADDRESS; // Start of the shared memory buffer
    uint32_t memory_firmware_code = ROM4_START_ADDRESS;  // Start of the firmware code

    while (true)
    {
        *((volatile uint32_t *)(memory_shared_address + GEMDRVEMUL_RANDOM_TOKEN_SEED)) = rand() % 0xFFFFFFFF;
        tight_loop_contents();

        if (ping_received)
        {
            ping_received = false;
            if (!hd_folder_ready)
            {
                // Initialize SD card
                if (!sd_init_driver())
                {
                    DPRINTF("ERROR: Could not initialize SD card\r\n");
                    *((volatile uint16_t *)(memory_shared_address + GEMDRVEMUL_PING_SUCCESS)) = 0x0;
                }
                else
                {
                    // Mount drive
                    fr = f_mount(&fs, "0:", 1);
                    bool microsd_mounted = (fr == FR_OK);
                    if (!microsd_mounted)
                    {
                        DPRINTF("ERROR: Could not mount filesystem (%d)\r\n", fr);
                        *((volatile uint16_t *)(memory_shared_address + GEMDRVEMUL_PING_SUCCESS)) = 0x0;
                    }
                    else
                    {
                        char *hd_folder = find_entry("GEMDRIVE_FOLDERS")->value;
                        DPRINTF("Emulating GEMDRIVE in folder: %s\n", hd_folder);
                        hd_folder_ready = true;
                        *((volatile uint16_t *)(memory_shared_address + GEMDRVEMUL_PING_SUCCESS)) = 0xFFFF;
                    }
                }
            }
            DPRINTF("PING received. Answering with: %d\n", *((volatile uint16_t *)(memory_shared_address + GEMDRVEMUL_PING_SUCCESS)));
            *((volatile uint32_t *)(memory_shared_address + GEMDRVEMUL_RANDOM_TOKEN)) = random_token;
        }
        if (save_vectors)
        {
            save_vectors = false;
            // Save the vectors needed for the floppy emulation
            DPRINTF("Saving vectors\n");
            DPRINTF("gemdos_trap_addres_xbra: %x\n", gemdos_trap_address_xbra);
            DPRINTF("gemdos_trap_address_old: %x\n", gemdos_trap_address_old);
            // DPRINTF("random token: %x\n", random_token);
            // Self modifying code to create the old and venerable XBRA structure
            *((volatile uint16_t *)(memory_firmware_code + gemdos_trap_address_xbra - ATARI_ROM4_START_ADDRESS)) = gemdos_trap_address_old & 0xFFFF;
            *((volatile uint16_t *)(memory_firmware_code + gemdos_trap_address_xbra - ATARI_ROM4_START_ADDRESS + 2)) = gemdos_trap_address_old >> 16;
            *((volatile uint32_t *)(memory_shared_address + GEMDRVEMUL_RANDOM_TOKEN)) = random_token;
        }
        if (trap_call != 0xFFFF)
        {
            DPRINTF("TRAP CALL: %x\n", trap_call);
            trap_call = 0xFFFF;
            *((volatile uint32_t *)(memory_shared_address + GEMDRVEMUL_RANDOM_TOKEN)) = random_token;
        }
        // If SELECT button is pressed, launch the configurator
        if (gpio_get(5) != 0)
        {
            select_button_action(safe_config_reboot, write_config_only_once);
            // Write config only once to avoid hitting the flash too much
            write_config_only_once = false;
        }
    }
}
