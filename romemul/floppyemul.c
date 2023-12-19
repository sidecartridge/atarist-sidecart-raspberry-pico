/**
 * File: floppyemul.c
 * Author: Diego Parrilla Santamaría
 * Date: August 2023
 * Copyright: 2023 - GOODDATA LABS SL
 * Description: Load floppy images files from SD card
 */

#include "include/floppyemul.h"

#define BPB_RECSIZE 0
#define BPB_CLSIZ 1
#define BPB_CLSIZB 2
#define BPB_RDLEN 3
#define BPB_FSIZ 4
#define BPB_FATREC 5
#define BPB_DATREC 6
#define BPB_NUMCL 7
#define BPB_BFLAGS 8
#define BPB_TRACKCNT 9
#define SIDE_COUNT 10
#define SEC_CYL 11
#define SEC_TRACK 12
#define DISK_NUMBER 16

#define FF_USE_FASTSEEK 1
static uint16_t BpbData[] = {
    512,  /*         0: recsize: Sector size in bytes                */
    2,    /*         1: clsiz:   Cluster size in sectors             */
    1024, /*         2: clsizb:  Cluster size in bytes               */
    8,    /*         3: rdlen:   Root Directory length in sectors    */
    6,    /*         4: fsiz:    FAT size in sectors                 */
    7,    /*         5: fatrec:  Sector number of second FAT         */
    21,   /*         6: datrec:  Sector number of first data cluster */
    1015, /*         7: numcl:   Number of data clusters on the disk */
    0,    /*         8: bflags:  Magic flags                         */
    0,    /*         9: trackcnt: Track count                        */
    0,    /*        10: sidecnt:  Side count                         */
    0,    /*        11: secpcyl: Sectors per cylinder                */
    0,    /*        12: secptrack: Sectors per track                 */
    0,
    0,
    0,
    0 /*            16: disk_number                                  */
};

static uint16_t *payloadPtr = NULL;
static char *fullpath_a = NULL;
static uint32_t random_token;
static bool ping_received = false;
static bool floppy_read_write = true;
static bool file_ready_a = false;
static bool sector_read = false;
static bool sector_write = false;
static bool set_bpb = false;
static bool save_vectors = false;
static uint16_t logical_sector = 0;
static uint16_t sector_size = 512;
static uint32_t hdv_bpb_payload = 0;
static uint32_t hdv_rw_payload = 0;
static uint32_t hdv_mediach_payload = 0;
static uint32_t XBIOS_trap_payload = 0;
static bool hdv_bpb_payload_set = false;
static bool hdv_rw_payload_set = false;
static bool hdv_mediach_payload_set = false;
static bool XBIOS_trap_payload_set = false;

static uint32_t hardware_type = 0;
static uint32_t hardware_type_start_function = 0;
static uint32_t hardware_type_end_function = 0;
static bool hardware_type_set = false;

static int __not_in_flash_func(create_BPB(FRESULT *fr, FIL *fsrc))
{
    BYTE buffer[512];    /* File copy buffer */
    unsigned int br = 0; /* File read/write count */

    DPRINTF("Creating BPB from first sector of floppy image\n");

    /* Set read/write pointer to logical sector position */
    *fr = f_lseek(fsrc, 0);
    if (*fr)
    {
        DPRINTF("ERROR: Could not seek to the start of the first sector to create BPB\n");
        f_close(fsrc);
        return (int)*fr; // Check for error in reading
    }

    *fr = f_read(fsrc, buffer, sizeof buffer, &br); /* Read a chunk of data from the source file */
    if (*fr)
    {
        DPRINTF("ERROR: Could not read the first boot sector to create the BPBP\n");
        f_close(fsrc);
        return (int)*fr; // Check for error in reading
    }
    BpbData[BPB_RECSIZE] = ((uint16_t)buffer[11]) | ((uint16_t)buffer[12] << 8);                                            // Sector size in bytes
    BpbData[BPB_CLSIZ] = (uint16_t)buffer[13];                                                                              // Cluster size
    BpbData[BPB_CLSIZB] = BpbData[BPB_CLSIZ] * BpbData[BPB_RECSIZE];                                                        // Cluster size in bytes
    BpbData[BPB_RDLEN] = ((uint16_t)buffer[17] >> 4) | ((uint16_t)buffer[18] << 8);                                         // Root directory length in sectors
    BpbData[BPB_FSIZ] = (uint16_t)buffer[22];                                                                               // FAT size in sectors
    BpbData[BPB_FATREC] = BpbData[BPB_FSIZ] + 1;                                                                            // Sector number of second FAT
    BpbData[BPB_DATREC] = BpbData[BPB_RDLEN] + BpbData[BPB_FATREC] + BpbData[BPB_FSIZ];                                     // Sector number of first data cluster
    BpbData[BPB_NUMCL] = ((((uint16_t)buffer[20] << 8) | (uint16_t)buffer[19]) - BpbData[BPB_DATREC]) / BpbData[BPB_CLSIZ]; // Number of data clusters on the disk
    // Leave space for sector to cluster rounding
    BpbData[SIDE_COUNT] = (uint16_t)buffer[26]; // Side count
    BpbData[SEC_CYL] = (uint16_t)(buffer[24] * BpbData[SIDE_COUNT]);
    BpbData[SEC_TRACK] = (uint16_t)buffer[24];
    BpbData[SEC_TRACK + 1] = 0;
    BpbData[SEC_TRACK + 2] = 0;
    BpbData[SEC_TRACK + 3] = 0;
    BpbData[DISK_NUMBER] = 0;

    // DPRINTF("BpbData[BPB_RECSIZE] = %u\n", BpbData[BPB_RECSIZE]);
    // DPRINTF("BpbData[BPB_CLSIZ] = %u\n", BpbData[BPB_CLSIZ]);
    // DPRINTF("BpbData[BPB_CLSIZB] = %u\n", BpbData[BPB_CLSIZB]);
    // DPRINTF("BpbData[BPB_RDLEN] = %u\n", BpbData[BPB_RDLEN]);
    // DPRINTF("BpbData[BPB_FSIZ] = %u\n", BpbData[BPB_FSIZ]);
    // DPRINTF("BpbData[BPB_FATREC] = %u\n", BpbData[BPB_FATREC]);
    // DPRINTF("BpbData[BPB_DATREC] = %u\n", BpbData[BPB_DATREC]);
    // DPRINTF("BpbData[BPB_NUMCL] = %u\n", BpbData[BPB_NUMCL]);
    // DPRINTF("BpbData[SIDE_COUNT] = %u\n", BpbData[SIDE_COUNT]);
    // DPRINTF("BpbData[SEC_CYL] = %u\n", BpbData[SEC_CYL]);
    // DPRINTF("BpbData[SEC_TRACK] = %u\n", BpbData[SEC_TRACK]);
    // DPRINTF("BpbData[DISK_NUMBER] = %u\n", BpbData[DISK_NUMBER]);
    return 0;
}

static void __not_in_flash_func(handle_protocol_command)(const TransmissionProtocol *protocol)
{
    ConfigEntry *entry = NULL;
    uint16_t value_payload = 0;
    // Handle the protocol
    switch (protocol->command_id)
    {
    case FLOPPYEMUL_SAVE_VECTORS:
        // Save the vectors needed for the floppy emulation
        DPRINTF("Command SAVE_VECTORS (%i) received: %d\n", protocol->command_id, protocol->payload_size);
        payloadPtr = (uint16_t *)protocol->payload + 2;
        hdv_bpb_payload = ((uint32_t)payloadPtr[0] << 16) | payloadPtr[1];
        payloadPtr += 2;
        hdv_rw_payload = ((uint32_t)payloadPtr[0] << 16) | payloadPtr[1];
        payloadPtr += 2;
        hdv_mediach_payload = ((uint32_t)payloadPtr[0] << 16) | payloadPtr[1];
        payloadPtr += 2;
        XBIOS_trap_payload = ((uint32_t)payloadPtr[0] << 16) | payloadPtr[1];
        random_token = ((*((uint32_t *)protocol->payload) & 0xFFFF0000) >> 16) | ((*((uint32_t *)protocol->payload) & 0x0000FFFF) << 16);
        save_vectors = true;

        break;
    case FLOPPYEMUL_READ_SECTORS:
        // Read sectors from the floppy emulator
        DPRINTF("Command READ_SECTORS (%i) received: %d\n", protocol->command_id, protocol->payload_size);
        random_token = ((*((uint32_t *)protocol->payload) & 0xFFFF0000) >> 16) | ((*((uint32_t *)protocol->payload) & 0x0000FFFF) << 16);
        payloadPtr = (uint16_t *)protocol->payload + 2;
        sector_size = *(uint16_t *)payloadPtr++;
        logical_sector = *(uint16_t *)payloadPtr;
        sector_read = true;
        break;
    case FLOPPYEMUL_WRITE_SECTORS:
        // Write sectors from the floppy emulator
        DPRINTF("Command WRITE_SECTORS (%i) received: %d\n", protocol->command_id, protocol->payload_size);
        random_token = ((*((uint32_t *)protocol->payload) & 0xFFFF0000) >> 16) | ((*((uint32_t *)protocol->payload) & 0x0000FFFF) << 16);
        payloadPtr = (uint16_t *)protocol->payload + 2;
        sector_size = *(uint16_t *)payloadPtr++;
        logical_sector = *(uint16_t *)payloadPtr++;
        sector_write = true;
        break;
    case FLOPPYEMUL_SET_BPB:
        // Set the BPB of the floppy
        DPRINTF("Command SET_BPB (%i) received: %d\n", protocol->command_id, protocol->payload_size);
        random_token = ((*((uint32_t *)protocol->payload) & 0xFFFF0000) >> 16) | ((*((uint32_t *)protocol->payload) & 0x0000FFFF) << 16);
        set_bpb = true;
        break;
    case FLOPPYEMUL_PING:
        DPRINTF("Command PING (%i) received: %d\n", protocol->command_id, protocol->payload_size);
        random_token = ((*((uint32_t *)protocol->payload) & 0xFFFF0000) >> 16) | ((*((uint32_t *)protocol->payload) & 0x0000FFFF) << 16);
        ping_received = true;
        break; // ... handle other commands
    case FLOPPYEMUL_SAVE_HARDWARE:
        DPRINTF("Command SAVE_HARDWARE (%i) received: %d\n", protocol->command_id, protocol->payload_size);
        random_token = ((*((uint32_t *)protocol->payload) & 0xFFFF0000) >> 16) | ((*((uint32_t *)protocol->payload) & 0x0000FFFF) << 16);
        payloadPtr = (uint16_t *)protocol->payload + 2;
        hardware_type = ((uint32_t)payloadPtr[1] << 16) | payloadPtr[0];
        payloadPtr += 2;
        hardware_type_start_function = ((uint32_t)payloadPtr[1] << 16) | payloadPtr[0];
        payloadPtr += 2;
        hardware_type_end_function = ((uint32_t)payloadPtr[1] << 16) | payloadPtr[0];
        hardware_type_set = true;
        break;
    default:
        DPRINTF("Unknown command: %d\n", protocol->command_id);
    }
}

// Interrupt handler callback for DMA completion
void __not_in_flash_func(floppyemul_dma_irq_handler_lookup_callback)(void)
{
    // Read the address to process
    uint32_t addr = (uint32_t)dma_hw->ch[lookup_data_rom_dma_channel].al3_read_addr_trig;

    // Avoid priting anything inside an IRQ handled function
    // DPRINTF("DMA LOOKUP: $%x\n", addr);
    if (addr >= ROM3_START_ADDRESS)
    {
        parse_protocol((uint16_t)(addr & 0xFFFF), handle_protocol_command);
    }
    // Clear the interrupt request for the channel
    dma_hw->ints1 = 1u << lookup_data_rom_dma_channel;
}

int init_floppyemul(bool safe_config_reboot)
{

    FRESULT fr; /* FatFs function common result code */
    FATFS fs;
    int SZ_TBL = 1024;       /* Number of table entries */
    DWORD clmt[SZ_TBL];      /* Linked list of cluster status */
    FIL fsrc_a;              /* File objects */
    unsigned int br_a = 0;   /* File read/write count */
    unsigned int size_a = 0; // File size

    bool write_config_only_once = true;

    DPRINTF("Waiting for commands...\n");
    uint32_t memory_shared_address = ROM3_START_ADDRESS; // Start of the shared memory buffer
    uint32_t memory_code_address = ROM4_START_ADDRESS;   // Start of the code memory

    bool error = false;
    bool show_blink = true;
    srand(time(0));
    while (!error)
    {
        *((volatile uint32_t *)(memory_shared_address + FLOPPYEMUL_RANDOM_TOKEN_SEED)) = rand() % 0xFFFFFFFF;
        tight_loop_contents();

        /* Delay the blink to the main loop */
        if (show_blink)
        {
            blink_morse('F');
            // Deinit the CYW43 WiFi module. DO NOT INTERRUPT, BUDDY!
            cyw43_arch_deinit();
            show_blink = false;
        }

        if (ping_received)
        {
            ping_received = false;
            if (!file_ready_a)
            {
                // Initialize SD card
                if (!sd_init_driver())
                {
                    DPRINTF("ERROR: Could not initialize SD card\r\n");
                    error = true;
                }

                // Mount drive
                fr = f_mount(&fs, "0:", 1);
                bool microsd_mounted = (fr == FR_OK);
                if (!microsd_mounted)
                {
                    DPRINTF("ERROR: Could not mount filesystem (%d)\r\n", fr);
                    error = true;
                }

                char *dir = find_entry("FLOPPIES_FOLDER")->value;
                char *filename_a = find_entry("FLOPPY_IMAGE_A")->value;
                fullpath_a = malloc(strlen(dir) + strlen(filename_a) + 2);
                strcpy(fullpath_a, dir);
                strcat(fullpath_a, "/");
                strcat(fullpath_a, filename_a);
                DPRINTF("Emulating floppy image in drive A: %s\n", fullpath_a);

                floppy_read_write = (strlen(fullpath_a) >= 3 && strcmp(fullpath_a + strlen(fullpath_a) - 3, ".rw") == 0);
                DPRINTF("Floppy image is %s\n", floppy_read_write ? "read/write" : "read only");

                /* Open source file on the drive 0 */
                fr = f_open(&fsrc_a, fullpath_a, floppy_read_write ? FA_READ | FA_WRITE : FA_READ);
                if (fr)
                {
                    DPRINTF("ERROR: Could not open file %s (%d)\r\n", fullpath_a, fr);
                    error = true;
                }

                fsrc_a.cltbl = clmt;                   /* Enable fast seek mode (cltbl != NULL) */
                clmt[0] = SZ_TBL;                      /* Set table size */
                fr = f_lseek(&fsrc_a, CREATE_LINKMAP); /* Create CLMT */
                if (fr)
                {
                    DPRINTF("ERROR: Could not create CLMT for file %s (%d). Closing file.\r\n", fullpath_a, fr);
                    f_close(&fsrc_a);
                    error = true;
                }

                // Get file size
                size_a = f_size(&fsrc_a);
                fr = f_lseek(&fsrc_a, size_a);
                if (fr)
                {
                    DPRINTF("ERROR: Could not seek file %s (%d). Closing file.\r\n", fullpath_a, fr);
                    f_close(&fsrc_a);
                    error = true;
                }
                fr = f_lseek(&fsrc_a, 0);
                if (fr)
                {
                    DPRINTF("ERROR: Could not seek file %s (%d). Closing file.\r\n", fullpath_a, fr);
                    f_close(&fsrc_a);
                    error = true;
                }
                DPRINTF("File size of %s: %i bytes\n", fullpath_a, size_a);

                // Reset the vectors if they are not set
                hdv_bpb_payload_set = (XBIOS_trap_payload & 0xFF == 0xFA);
                hdv_rw_payload_set = (hdv_bpb_payload & 0xFF == 0xFA);
                hdv_mediach_payload_set = (hdv_rw_payload & 0xFF == 0xFA);
                XBIOS_trap_payload_set = (hdv_mediach_payload & 0xFF == 0xFA);
                file_ready_a = true;
            }
            *((volatile uint32_t *)(memory_shared_address + FLOPPYEMUL_RANDOM_TOKEN)) = random_token;
        }
        if (file_ready_a && set_bpb)
        {
            set_bpb = false;
            // Create BPB
            int bpb_found = create_BPB(&fr, &fsrc_a);
            if (bpb_found)
            {
                DPRINTF("ERROR: Could not create BPB for image file  %s (%d)\r\n", fullpath_a, fr);
                error = true;
            }
            for (int i = 0; i < sizeof(BpbData) / sizeof(uint16_t); i++)
            {
                *((volatile uint16_t *)(memory_shared_address + FLOPPYEMUL_BPB_DATA + i * 2)) = BpbData[i];
            }
            *((volatile uint32_t *)(memory_shared_address + FLOPPYEMUL_RANDOM_TOKEN)) = random_token;
        }

        if (file_ready_a && save_vectors)
        {
            save_vectors = false;
            // Save the vectors needed for the floppy emulation
            DPRINTF("Saving vectors\n");
            // DPRINTF("random token: %x\n", random_token);
            if (!XBIOS_trap_payload_set)
            {
                *((volatile uint16_t *)(memory_shared_address + FLOPPYEMUL_OLD_XBIOS_TRAP)) = XBIOS_trap_payload & 0xFFFF;
                *((volatile uint16_t *)(memory_shared_address + FLOPPYEMUL_OLD_XBIOS_TRAP + 2)) = XBIOS_trap_payload >> 16;
                XBIOS_trap_payload_set = true;
            }
            else
            {
                DPRINTF("XBIOS_trap_payload previously set.\n");
            }
            DPRINTF("XBIOS_trap_payload: %x\n", XBIOS_trap_payload);

            if (!hdv_bpb_payload_set)
            {
                *((volatile uint16_t *)(memory_shared_address + FLOPPYEMUL_OLD_HDV_BPB)) = hdv_bpb_payload & 0xFFFF;
                *((volatile uint16_t *)(memory_shared_address + FLOPPYEMUL_OLD_HDV_BPB + 2)) = hdv_bpb_payload >> 16;
                hdv_bpb_payload_set = true;
            }
            else
            {
                DPRINTF("hdv_bpb_payload previously set.\n");
            }
            DPRINTF("hdv_bpb_payload: %x\n", hdv_bpb_payload);

            if (!hdv_rw_payload_set)
            {
                *((volatile uint16_t *)(memory_shared_address + FLOPPYEMUL_OLD_HDV_RW)) = hdv_rw_payload & 0xFFFF;
                *((volatile uint16_t *)(memory_shared_address + FLOPPYEMUL_OLD_HDV_RW + 2)) = hdv_rw_payload >> 16;
                hdv_rw_payload_set = true;
            }
            else
            {
                DPRINTF("hdv_rw_payload previously set.\n");
            }
            DPRINTF("hdv_rw_payload: %x\n", hdv_rw_payload);

            if (!hdv_mediach_payload_set)
            {
                *((volatile uint16_t *)(memory_shared_address + FLOPPYEMUL_OLD_HDV_MEDIACH)) = hdv_mediach_payload & 0xFFFF;
                *((volatile uint16_t *)(memory_shared_address + FLOPPYEMUL_OLD_HDV_MEDIACH + 2)) = hdv_mediach_payload >> 16;
                hdv_mediach_payload_set = true;
            }
            else
            {
                DPRINTF("hdv_mediach_payload previously set.\n");
            }
            DPRINTF("hdv_mediach_payload: %x\n", hdv_mediach_payload);
            *((volatile uint32_t *)(memory_shared_address + FLOPPYEMUL_RANDOM_TOKEN)) = random_token;
        }
        if (file_ready_a && hardware_type_set)
        {
            hardware_type_set = false;
            DPRINTF("Setting hardware type: %x\n", hardware_type);
            DPRINTF("Setting hardware type start function: %x\n", hardware_type_start_function & 0xFFFF);
            DPRINTF("Setting hardware type end function: %x\n", hardware_type_end_function & 0xFFFF);

            *((volatile uint16_t *)(memory_shared_address + FLOPPYEMUL_HARDWARE_TYPE + 2)) = hardware_type & 0xFFFF;
            *((volatile uint16_t *)(memory_shared_address + FLOPPYEMUL_HARDWARE_TYPE)) = hardware_type >> 16;
            // Self-modifying code to change the speed of the cpu and cache or not. Not strictly needed, but can avoid bus errors
            // Check if the hardware type is 0x00010010 (Atari MegaSTe)
            if (hardware_type != 0x00010010)
            {
                // 16 bytes
                for (int i = 0; i < 8; i++)
                {
                    *((volatile uint16_t *)(memory_code_address + (hardware_type_start_function & 0xFFFF) + i * 2)) = 0x4E71; // NOP
                }
                // 4 bytes
                for (int i = 0; i < 2; i++)
                {
                    *((volatile uint16_t *)(memory_code_address + (hardware_type_end_function & 0xFFFF) + i * 2)) = 0x4E71; // NOP
                }
            }

            *((volatile uint32_t *)(memory_shared_address + FLOPPYEMUL_RANDOM_TOKEN)) = random_token;
        }

        if (file_ready_a && sector_read)
        {
            sector_read = false;
            DPRINTF("LSECTOR: %i / SSIZE: %i\n", logical_sector, sector_size);

            /* Set read/write pointer to logical sector position */
            fr = f_lseek(&fsrc_a, logical_sector * sector_size);
            if (fr)
            {
                printf("ERROR: Could not seek file %s (%d). Closing file.\r\n", fullpath_a, fr);
                f_close(&fsrc_a);
                error = true;
            }
            fr = f_read(&fsrc_a, (void *)(memory_shared_address + FLOPPYEMUL_IMAGE), sector_size, &br_a); /* Read a chunk of data from the source file */
            if (fr)
            {
                printf("ERROR: Could not read file %s (%d). Closing file.\r\n", fullpath_a, fr);
                f_close(&fsrc_a);
                error = true;
            }

            swap_words((uint16_t *)(memory_shared_address + FLOPPYEMUL_IMAGE), sector_size);
            *((volatile uint32_t *)(memory_shared_address + FLOPPYEMUL_RANDOM_TOKEN)) = random_token;
        }
        if (file_ready_a && sector_write)
        {
            sector_write = false;

            // Only write if the floppy image is read/write. It's important because the FatFS seems to ignore the FA_READ flag
            if (floppy_read_write)
            {
                DPRINTF("LSECTOR: %i / SSIZE: %i\n", logical_sector, sector_size);
                // Transform buffer's words from little endian to big endian inline
                uint16_t *target_start = payloadPtr;
                volatile uint16_t *target = (volatile uint16_t *)target_start;
                for (int i = 0; i < (sector_size / 2); i += 1)
                {
                    uint16_t value = *(uint16_t *)(target + i);
                    value = (value << 8) | (value >> 8);
                    *(target + i) = value;
                }
                dma_channel_set_irq1_enabled(lookup_data_rom_dma_channel, false);
                /* Set read/write pointer to logical sector position */
                fr = f_lseek(&fsrc_a, logical_sector * sector_size);
                if (fr)
                {
                    DPRINTF("ERROR: Could not seek file %s (%d). Closing file.\r\n", fullpath_a, fr);
                    f_close(&fsrc_a);
                    error = true;
                }
                fr = f_write(&fsrc_a, target_start, sector_size, &br_a); /* Write a chunk of data from the source file */
                if (fr)
                {
                    DPRINTF("ERROR: Could not read file %s (%d). Closing file.\r\n", fullpath_a, fr);
                    f_close(&fsrc_a);
                    error = true;
                }
                dma_channel_set_irq1_enabled(lookup_data_rom_dma_channel, true);
            }
            else
            {
                DPRINTF("ERROR: Trying to write to a read-only floppy image.\r\n");
            }
            *((volatile uint32_t *)(memory_shared_address + FLOPPYEMUL_RANDOM_TOKEN)) = random_token;
        }
        // If SELECT button is pressed, launch the configurator
        if (gpio_get(5) != 0)
        {
            select_button_action(safe_config_reboot, write_config_only_once);
            // Write config only once to avoid hitting the flash too much
            write_config_only_once = false;
        }
    }
    // Init the CYW43 WiFi module. Needed to show the error message in the LED
    cyw43_arch_init();
    blink_error();
}
