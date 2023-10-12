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

uint16_t *payloadPtr = NULL;

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

static int create_BPB(FRESULT *fr, FIL *fsrc)
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
        if (file_ready_a)
        {
            random_token = ((*((uint32_t *)protocol->payload) & 0xFFFF0000) >> 16) | ((*((uint32_t *)protocol->payload) & 0x0000FFFF) << 16);
        }
        ping_received = file_ready_a;
        break; // ... handle other commands
    default:
        DPRINTF("Unknown command: %d\n", protocol->command_id);
    }
}

// Interrupt handler callback for DMA completion
void __not_in_flash_func(floppyemul_dma_irq_handler_lookup_callback)(void)
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

int copy_floppy_firmware_to_RAM()
{
    // Need to initialize the ROM4 section with the firmware data
    extern uint16_t __rom_in_ram_start__;
    uint16_t *rom4_dest = &__rom_in_ram_start__;
    uint16_t *rom4_src = (uint16_t *)floppyemulROM;
    for (int i = 0; i < floppyemulROM_length; i++)
    {
        uint16_t value = *rom4_src++;
        *rom4_dest++ = value;
    }
    DPRINTF("Floppy emulation firmware copied to RAM.\n");
    return 0;
}

int init_floppyemul()
{

    FRESULT fr; /* FatFs function common result code */
    FATFS fs;
    bool microsd_mounted = false;

    srand(time(0));
    printf("Initializing floppy emulation...\n"); // Print always

    // Initialize SD card
    if (!sd_init_driver())
    {
        DPRINTF("ERROR: Could not initialize SD card\r\n");
        return -1;
    }

    FIL fsrc_a;              /* File objects */
    BYTE buffer_a[512];      /* File copy buffer */
    unsigned int br_a = 0;   /* File read/write count */
    unsigned int size_a = 0; // File size

    DPRINTF("Waiting for commands...\n");

    while (true)
    {
        *((volatile uint32_t *)(ROM4_START_ADDRESS + FLOPPYEMUL_RANDOM_TOKEN_SEED)) = rand() % 0xFFFFFFFF;
        tight_loop_contents();

        if (!file_ready_a)
        {
            // Mount drive
            fr = f_mount(&fs, "0:", 1);
            microsd_mounted = (fr == FR_OK);
            if (!microsd_mounted)
            {
                DPRINTF("ERROR: Could not mount filesystem (%d)\r\n", fr);
                return -1;
            }
            char *dir = find_entry("FLOPPIES_FOLDER")->value;
            char *filename_a = find_entry("FLOPPY_IMAGE_A")->value;
            char *fullpath_a = malloc(strlen(dir) + strlen(filename_a) + 2);
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
                return -1;
            }
            // Get file size
            size_a = f_size(&fsrc_a);
            DPRINTF("File size of %s: %i bytes\n", fullpath_a, size_a);

            file_ready_a = true;
        }

        if (file_ready_a && ping_received)
        {
            ping_received = false;
            *((volatile uint32_t *)(ROM4_START_ADDRESS + FLOPPYEMUL_RANDOM_TOKEN)) = random_token;
        }
        if (file_ready_a && set_bpb)
        {
            set_bpb = false;
            // Create BPB
            int bpb_found = create_BPB(&fr, &fsrc_a);
            if (bpb_found)
            {
                DPRINTF("ERROR: Could not create BPB for image file  %s (%d)\r\n", fullpath_a, fr);
                return -1;
            }
            for (int i = 0; i < sizeof(BpbData) / sizeof(uint16_t); i++)
            {
                *((volatile uint16_t *)(ROM4_START_ADDRESS + FLOPPYEMUL_BPB_DATA + i * 2)) = BpbData[i];
            }
            *((volatile uint32_t *)(ROM4_START_ADDRESS + FLOPPYEMUL_RANDOM_TOKEN)) = random_token;
        }

        if (file_ready_a && save_vectors)
        {
            save_vectors = false;
            // Save the vectors needed for the floppy emulation
            DPRINTF("Saving vectors\n");
            // DPRINTF("XBIOS_trap_payload: %x\n", XBIOS_trap_payload);
            // DPRINTF("hdv_bpb_payload: %x\n", hdv_bpb_payload);
            // DPRINTF("hdv_rw_payload: %x\n", hdv_rw_payload);
            // DPRINTF("hdv_mediach_payload: %x\n", hdv_mediach_payload);
            // DPRINTF("random token: %x\n", random_token);
            *((volatile uint16_t *)(ROM4_START_ADDRESS + FLOPPYEMUL_OLD_XBIOS_TRAP)) = XBIOS_trap_payload & 0xFFFF;
            *((volatile uint16_t *)(ROM4_START_ADDRESS + FLOPPYEMUL_OLD_XBIOS_TRAP + 2)) = XBIOS_trap_payload >> 16;

            *((volatile uint16_t *)(ROM4_START_ADDRESS + FLOPPYEMUL_OLD_HDV_BPB)) = hdv_bpb_payload & 0xFFFF;
            *((volatile uint16_t *)(ROM4_START_ADDRESS + FLOPPYEMUL_OLD_HDV_BPB + 2)) = hdv_bpb_payload >> 16;

            *((volatile uint16_t *)(ROM4_START_ADDRESS + FLOPPYEMUL_OLD_HDV_RW)) = hdv_rw_payload & 0xFFFF;
            *((volatile uint16_t *)(ROM4_START_ADDRESS + FLOPPYEMUL_OLD_HDV_RW + 2)) = hdv_rw_payload >> 16;

            *((volatile uint16_t *)(ROM4_START_ADDRESS + FLOPPYEMUL_OLD_HDV_MEDIACH)) = hdv_mediach_payload & 0xFFFF;
            *((volatile uint16_t *)(ROM4_START_ADDRESS + FLOPPYEMUL_OLD_HDV_MEDIACH + 2)) = hdv_mediach_payload >> 16;

            *((volatile uint32_t *)(ROM4_START_ADDRESS + FLOPPYEMUL_RANDOM_TOKEN)) = random_token;
        }
        if (file_ready_a && sector_read)
        {
            sector_read = false;
            dma_channel_set_irq1_enabled(lookup_data_rom_dma_channel, false);
            DPRINTF("LSECTOR: %i / SSIZE: %i\n", logical_sector, sector_size);
            /* Set read/write pointer to logical sector position */
            fr = f_lseek(&fsrc_a, logical_sector * sector_size);
            if (fr)
            {
                DPRINTF("ERROR: Could not seek file %s (%d). Closing file.\r\n", fullpath_a, fr);
                f_close(&fsrc_a);
                //                                return (int)fr; // Check for error in reading
            }
            fr = f_read(&fsrc_a, buffer_a, sizeof buffer_a, &br_a); /* Read a chunk of data from the source file */
            if (fr)
            {
                DPRINTF("ERROR: Could not read file %s (%d). Closing file.\r\n", fullpath_a, fr);
                f_close(&fsrc_a);
                //                                return (int)fr; // Check for error in reading
            }
            dma_channel_set_irq1_enabled(lookup_data_rom_dma_channel, true);
            // Transform buffer's words from little endian to big endian inline
            volatile uint16_t *target = (volatile uint16_t *)(ROM4_START_ADDRESS + FLOPPYEMUL_IMAGE);
            for (int i = 0; i < br_a; i += 2)
            {
                uint16_t value = *(uint16_t *)(buffer_a + i);
                value = (value << 8) | (value >> 8);
                *(target++) = value;
            }
            *((volatile uint32_t *)(ROM4_START_ADDRESS + FLOPPYEMUL_RANDOM_TOKEN)) = random_token;
        }
        if (file_ready_a && sector_write)
        {
            sector_write = false;

            // Only write if the floppy image is read/write. It's important because the FatFS seems to ignore the FA_READ flag
            if (floppy_read_write)
            {
                dma_channel_set_irq1_enabled(lookup_data_rom_dma_channel, false);
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
                /* Set read/write pointer to logical sector position */
                fr = f_lseek(&fsrc_a, logical_sector * sector_size);
                if (fr)
                {
                    DPRINTF("ERROR: Could not seek file %s (%d). Closing file.\r\n", fullpath_a, fr);
                    f_close(&fsrc_a);
                    //                                return (int)fr; // Check for error in reading
                }
                fr = f_write(&fsrc_a, target_start, sector_size, &br_a); /* Write a chunk of data from the source file */
                if (fr)
                {
                    DPRINTF("ERROR: Could not read file %s (%d). Closing file.\r\n", fullpath_a, fr);
                    f_close(&fsrc_a);
                    //                                return (int)fr; // Check for error in reading
                }
                dma_channel_set_irq1_enabled(lookup_data_rom_dma_channel, true);
            }
            else
            {
                DPRINTF("ERROR: Trying to write to a read-only floppy image.\r\n");
            }
            *((volatile uint32_t *)(ROM4_START_ADDRESS + FLOPPYEMUL_RANDOM_TOKEN)) = random_token;
        }

        // If SELECT button is pressed, launch the configurator
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
