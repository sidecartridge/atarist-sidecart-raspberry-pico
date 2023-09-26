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

uint16_t BpbData[] = {
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

static uint32_t random_token;
static bool ping_received = false;
static bool file_ready_a = false;
static bool sector_read = false;
static bool set_bpb = false;
static uint16_t logical_sector = 0;
static uint16_t sector_size = 512;

static int create_BPB(FRESULT *fr, FIL *fsrc)
{
    BYTE buffer[512];      /* File copy buffer */
    unsigned int br = 0;   /* File read/write count */
    unsigned int size = 0; // File size

    printf("Creating BPB from first sector of floppy image\n");

    /* Set read/write pointer to logical sector position */
    *fr = f_lseek(fsrc, 0);
    if (*fr)
    {
        printf("ERROR: Could not seek to the start of the first sector to create BPB\n");
        f_close(fsrc);
        return (int)*fr; // Check for error in reading
    }

    *fr = f_read(fsrc, buffer, sizeof buffer, &br); /* Read a chunk of data from the source file */
    if (*fr)
    {
        printf("ERROR: Could not read the first boot sector to create the BPBP\n");
        f_close(fsrc);
        return (int)*fr; // Check for error in reading
    }
    // Transform buffer's words from little endian to big endian inline
    // for (int i = 0; i < br; i += 2)
    // {
    //     uint16_t value = *(uint16_t *)(buffer + i);
    //     value = (value << 8) | (value >> 8);
    //     *(uint16_t *)(buffer + i) = value;
    // }
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
    BpbData[SEC_CYL] = ((uint16_t)buffer[24] << 8) * BpbData[SIDE_COUNT];
    BpbData[SEC_TRACK] = (uint16_t)buffer[24];
    BpbData[SEC_TRACK + 1] = 0;
    BpbData[SEC_TRACK + 2] = 0;
    BpbData[SEC_TRACK + 3] = 0;
    BpbData[DISK_NUMBER] = 0;

    printf("BpbData[BPB_RECSIZE] = %u\n", BpbData[BPB_RECSIZE]);
    printf("BpbData[BPB_CLSIZ] = %u\n", BpbData[BPB_CLSIZ]);
    printf("BpbData[BPB_CLSIZB] = %u\n", BpbData[BPB_CLSIZB]);
    printf("BpbData[BPB_RDLEN] = %u\n", BpbData[BPB_RDLEN]);
    printf("BpbData[BPB_FSIZ] = %u\n", BpbData[BPB_FSIZ]);
    printf("BpbData[BPB_FATREC] = %u\n", BpbData[BPB_FATREC]);
    printf("BpbData[BPB_DATREC] = %u\n", BpbData[BPB_DATREC]);
    printf("BpbData[BPB_NUMCL] = %u\n", BpbData[BPB_NUMCL]);
    printf("BpbData[SIDE_COUNT] = %u\n", BpbData[SIDE_COUNT]);
    printf("BpbData[SEC_CYL] = %u\n", BpbData[SEC_CYL]);
    printf("BpbData[SEC_TRACK] = %u\n", BpbData[SEC_TRACK]);
    printf("BpbData[DISK_NUMBER] = %u\n", BpbData[DISK_NUMBER]);
    return 0;
}

static void __not_in_flash_func(handle_protocol_command)(const TransmissionProtocol *protocol)
{
    uint16_t *payloadPtr = NULL;
    ConfigEntry *entry = NULL;
    uint16_t value_payload = 0;
    // Handle the protocol
    switch (protocol->command_id)
    {
    case FLOPPYEMUL_SAVE_VECTORS:
        // Save the vectors needed for the floppy emulation
        printf("Command SAVE_VECTORS (%i) received: %d\n", protocol->command_id, protocol->payload_size);
        payloadPtr = (uint16_t *)protocol->payload + 2;

        uint32_t hdv_bpb_payload =
            ((uint32_t)payloadPtr[0] << 16) | payloadPtr[1];
        payloadPtr += 2;

        uint32_t hdv_rw_payload =
            ((uint32_t)payloadPtr[0] << 16) | payloadPtr[1];
        payloadPtr += 2;

        uint32_t hdv_mediach_payload =
            ((uint32_t)payloadPtr[0] << 16) | payloadPtr[1];
        payloadPtr += 2;

        uint32_t XBIOS_trap_payload =
            ((uint32_t)payloadPtr[0] << 16) | payloadPtr[1];

        printf("XBIOS_trap_payload: %x\n", XBIOS_trap_payload);
        *((volatile uint16_t *)(ROM4_START_ADDRESS + FLOPPYEMUL_OLD_XBIOS_TRAP)) = XBIOS_trap_payload & 0xFFFF;
        *((volatile uint16_t *)(ROM4_START_ADDRESS + FLOPPYEMUL_OLD_XBIOS_TRAP + 2)) = XBIOS_trap_payload >> 16;

        printf("hdv_bpb_payload: %x\n", hdv_bpb_payload);
        *((volatile uint16_t *)(ROM4_START_ADDRESS + FLOPPYEMUL_OLD_HDV_BPB)) = hdv_bpb_payload & 0xFFFF;
        *((volatile uint16_t *)(ROM4_START_ADDRESS + FLOPPYEMUL_OLD_HDV_BPB + 2)) = hdv_bpb_payload >> 16;

        printf("hdv_rw_payload: %x\n", hdv_rw_payload);
        *((volatile uint16_t *)(ROM4_START_ADDRESS + FLOPPYEMUL_OLD_HDV_RW)) = hdv_rw_payload & 0xFFFF;
        *((volatile uint16_t *)(ROM4_START_ADDRESS + FLOPPYEMUL_OLD_HDV_RW + 2)) = hdv_rw_payload >> 16;

        printf("hdv_mediach_payload: %x\n", hdv_mediach_payload);
        *((volatile uint16_t *)(ROM4_START_ADDRESS + FLOPPYEMUL_OLD_HDV_MEDIACH)) = hdv_mediach_payload & 0xFFFF;
        *((volatile uint16_t *)(ROM4_START_ADDRESS + FLOPPYEMUL_OLD_HDV_MEDIACH + 2)) = hdv_mediach_payload >> 16;

        random_token = ((*((uint32_t *)protocol->payload) & 0xFFFF0000) >> 16) | ((*((uint32_t *)protocol->payload) & 0x0000FFFF) << 16);
        printf("random token: %x\n", random_token);
        *((volatile uint32_t *)(ROM4_START_ADDRESS + FLOPPYEMUL_RANDOM_TOKEN)) = random_token;

        break;
    case FLOPPYEMUL_READ_SECTORS:
        // Read sectors from the floppy emulator
        printf("Command READ_SECTORS (%i) received: %d\n", protocol->command_id, protocol->payload_size);
        random_token = ((*((uint32_t *)protocol->payload) & 0xFFFF0000) >> 16) | ((*((uint32_t *)protocol->payload) & 0x0000FFFF) << 16);
        sector_read = true;
        payloadPtr = (uint16_t *)protocol->payload + 2;

        sector_size = *(uint16_t *)payloadPtr++;
        logical_sector = *(uint16_t *)payloadPtr;

        printf("Logical sector: %i\n", logical_sector);
        printf("Sector size: %i\n", sector_size);

        break;
    case FLOPPYEMUL_WRITE_SECTORS:
        // Write sectors to the floppy emulator
        printf("Command WRITE_SECTORS (%i) received: %d\n", protocol->command_id, protocol->payload_size);
        break;
    case FLOPPYEMUL_SET_BPB:
        // Set the BPB of the floppy
        printf("Command SET_BPB (%i) received: %d\n", protocol->command_id, protocol->payload_size);
        random_token = ((*((uint32_t *)protocol->payload) & 0xFFFF0000) >> 16) | ((*((uint32_t *)protocol->payload) & 0x0000FFFF) << 16);
        set_bpb = true;
        break;
    case FLOPPYEMUL_PING:
        printf("Command PING (%i) received: %d\n", protocol->command_id, protocol->payload_size);
        if (file_ready_a)
        {
            random_token = ((*((uint32_t *)protocol->payload) & 0xFFFF0000) >> 16) | ((*((uint32_t *)protocol->payload) & 0x0000FFFF) << 16);
            *((volatile uint32_t *)(ROM4_START_ADDRESS + FLOPPYEMUL_RANDOM_TOKEN)) = random_token;
        }
        ping_received = file_ready_a;
        break; // ... handle other commands
    default:
        printf("Unknown command: %d\n", protocol->command_id);
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
    // printf("DMA LOOKUP: $%x\n", addr);
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
    printf("Floppy emulation firmware copied to RAM.\n");
    return 0;
}

int init_floppyemul()
{

    printf("\033[2J\033[H"); // Clear Screen
    printf("\n> ");
    stdio_flush();

    FRESULT fr; /* FatFs function common result code */
    FATFS fs;
    bool microsd_mounted = false;
    bool microsd_initialized;

    // Initialize SD card
    microsd_initialized = sd_init_driver();
    if (!microsd_initialized)
    {
        printf("ERROR: Could not initialize SD card\r\n");
    }

    if (microsd_initialized)
    {
        // Mount drive
        fr = f_mount(&fs, "0:", 1);
        microsd_mounted = (fr == FR_OK);
        if (!microsd_mounted)
        {
            printf("ERROR: Could not mount filesystem (%d)\r\n", fr);
        }
    }
    microsd_mounted = microsd_mounted & microsd_initialized;

    if (microsd_mounted)
    {
        char *dir = find_entry("FLOPPIES_FOLDER")->value;
        char *filename_a = find_entry("FLOPPY_IMAGE_A")->value;
        char *fullpath_a = malloc(strlen(dir) + strlen(filename_a) + 2);
        strcpy(fullpath_a, dir);
        strcat(fullpath_a, "/");
        strcat(fullpath_a, filename_a);
        printf("Emulating floppy image in drive A: %s\n", fullpath_a);

        FIL fsrc_a;              /* File objects */
        BYTE buffer_a[512];      /* File copy buffer */
        unsigned int br_a = 0;   /* File read/write count */
        unsigned int size_a = 0; // File size

        /* Open source file on the drive 0 */
        fr = f_open(&fsrc_a, fullpath_a, FA_READ);
        if (fr)
        {
            printf("ERROR: Could not open file %s (%d)\r\n", fullpath_a, fr);
            return (int)fr;
        }
        // Get file size
        size_a = f_size(&fsrc_a);
        printf("File size of %s: %i bytes\n", fullpath_a, size_a);
        file_ready_a = true;

        while (true)
        {
            tight_loop_contents();

            if (file_ready_a && ping_received)
            {
                ping_received = false;
                *((volatile uint32_t *)(ROM4_START_ADDRESS + FLOPPYEMUL_RANDOM_TOKEN)) = random_token;
            }
            else
            {
                random_token = 0;
            }

            if (sector_read)
            {
                sector_read = false;
                /* Set read/write pointer to logical sector position */
                fr = f_lseek(&fsrc_a, logical_sector * sector_size);
                if (fr)
                {
                    printf("ERROR: Could not seek file %s (%d). Closing file.\r\n", fullpath_a, fr);
                    f_close(&fsrc_a);
                    return (int)fr; // Check for error in reading
                }

                fr = f_read(&fsrc_a, buffer_a, sizeof buffer_a, &br_a); /* Read a chunk of data from the source file */
                if (fr)
                {
                    printf("ERROR: Could not read file %s (%d). Closing file.\r\n", fullpath_a, fr);
                    f_close(&fsrc_a);
                    return (int)fr; // Check for error in reading
                }
                // Transform buffer's words from little endian to big endian inline
                // for (int i = 0; i < br_a; i += 2)
                // {
                //     uint16_t value = *(uint16_t *)(buffer_a + i);
                //     value = (value << 8) | (value >> 8);
                //     *(uint16_t *)(buffer_a + i) = value;
                // }
                *((volatile uint32_t *)(ROM4_START_ADDRESS + FLOPPYEMUL_RANDOM_TOKEN)) = random_token;
            }
            if (set_bpb)
            {
                set_bpb = false;
                printf("Setting BPB\n");
                // Create BPB
                int bpb_found = create_BPB(&fr, &fsrc_a);
                if (bpb_found)
                {
                    printf("ERROR: Could not create BPB for image file  %s (%d)\r\n", fullpath_a, fr);
                    return (int)fr;
                }

                for (int i = 0; i < sizeof(BpbData) / sizeof(uint16_t); i++)
                {
                    *((volatile uint16_t *)(ROM4_START_ADDRESS + FLOPPYEMUL_BPB_DATA + i * 2)) = BpbData[i];
                }
                *((volatile uint32_t *)(ROM4_START_ADDRESS + FLOPPYEMUL_RANDOM_TOKEN)) = random_token;
            }
        }
    }
}