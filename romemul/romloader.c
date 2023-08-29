/**
 * File: romloader.c
 * Author: Diego Parrilla Santamaría
 * Date: August 2023
 * Copyright: 2023 - GOODDATA LABS SL
 * Description: Load ROM files from SD card
 */

#include "include/romloader.h"

static int rom_selected = -1;

static void release_memory_files(char **files, int num_files)
{
    for (int i = 0; i < num_files; i++)
    {
        free(files[i]); // Free each string
    }
    free(files); // Free the list itself
}

static char **
ls(const char *dir, int *num_files)
{
    char cwdbuf[FF_LFN_BUF] = {0};
    FRESULT fr;
    char const *p_dir;
    char **filenames = NULL;
    *num_files = 0; // Initialize the count of files to 0

    if (dir[0])
    {
        p_dir = dir;
    }
    else
    {
        fr = f_getcwd(cwdbuf, sizeof cwdbuf);
        if (FR_OK != fr)
        {
            printf("f_getcwd error: %s (%d)\n", FRESULT_str(fr), fr);
            return NULL;
        }
        p_dir = cwdbuf;
    }

    DIR dj;
    FILINFO fno;
    memset(&dj, 0, sizeof dj);
    memset(&fno, 0, sizeof fno);

    fr = f_findfirst(&dj, &fno, p_dir, "*");
    if (FR_OK != fr)
    {
        printf("f_findfirst error: %s (%d)\n", FRESULT_str(fr), fr);
        return NULL;
    }

    while (fr == FR_OK && fno.fname[0] && fno.fname[0])
    {
        // Allocate space for a new pointer in the filenames array
        filenames = realloc(filenames, sizeof(char *) * (*num_files + 1));
        if (!filenames)
        {
            printf("Memory allocation failed\n");
            return NULL;
        }
        filenames[*num_files] = strdup(fno.fname); // Store the filename
        (*num_files)++;

        fr = f_findnext(&dj, &fno); // Search for next item
    }

    f_closedir(&dj);

    return filenames;
}

static int load(char *filename, uint32_t rom_load_offset)
{
    FIL fsrc;                                /* File objects */
    BYTE buffer[4096];                       /* File copy buffer */
    FRESULT fr;                              /* FatFs function common result code */
    unsigned int br = 0;                     /* File read/write count */
    unsigned int size = 0;                   // File size
    uint32_t dest_address = rom_load_offset; // Initialize pointer to the ROM address

    printf("Loading file '%s'  ", filename);

    /* Open source file on the drive 0 */
    fr = f_open(&fsrc, filename, FA_READ);
    if (fr)
        return (int)fr;

    // Get file size
    size = f_size(&fsrc);
    printf("File size: %i bytes\n", size);

    // Erase the content before loading the new file. It seems that
    // overwriting it's not enough
    flash_range_erase(dest_address, ROM_SIZE_BYTES * 2); // Two banks of 64K

    // If the size of the image is not 65536 or 131072 bytes, check if the file
    // is 4 bytes larger and the first 4 bytes are 0x0000. If so, skip them
    if ((size == ROM_SIZE_BYTES + 4) || (size == ROM_SIZE_BYTES * 2 + 4))
    {
        // Read the first 4 bytes
        fr = f_read(&fsrc, buffer, 4, &br);
        if (fr)
        {
            f_close(&fsrc);
            return (int)fr; // Check for error in reading
        }

        // Check if the first 4 bytes are 0x0000
        if (buffer[0] == 0x00 && buffer[1] == 0x00 && buffer[2] == 0x00 && buffer[3] == 0x00)
        {
            printf("Skipping first 4 bytes. Looks like a STEEM cartridge image.\n");
        }
        else
        {
            // Rollback the file pointer to the previous 4 bytes read
            f_lseek(&fsrc, -4);
        }
    }
    /* Copy source to destination */
    size = 0;
    for (;;)
    {
        fr = f_read(&fsrc, buffer, sizeof buffer, &br); /* Read a chunk of data from the source file */
        if (fr)
        {
            f_close(&fsrc);
            return (int)fr; // Check for error in reading
        }
        if (br == 0)
            break; // EOF

        // Transform buffer's words from little endian to big endian inline
        for (int i = 0; i < br; i += 2)
        {
            uint16_t value = *(uint16_t *)(buffer + i);
            value = (value << 8) | (value >> 8);
            *(uint16_t *)(buffer + i) = value;
        }

        // Transfer buffer to FLASH
        // WARNING! TRANSFER THE INFORMATION IN THE BUFFER AS LITTLE ENDIAN!!!!
        flash_range_program(dest_address, buffer, br);

        dest_address += br; // Increment the pointer to the ROM address
        size += br;

        printf(".");
    }

    // Close open file
    f_close(&fsrc);

    printf(" %i bytes loaded\n", size);
    printf("File loaded at offset 0x%x\n", rom_load_offset);
    printf("Dest ROM address end is 0x%x\n", dest_address - 1);
    return (int)fr;
}

static char **filter(char **file_list, int file_count, int *num_files)
{
    int validCount = 0;

    // Count valid filenames
    for (int i = 0; i < file_count; i++)
    {
        if (file_list[i][0] != '.')
        {
            validCount++;
        }
    }

    // Allocate memory for the new array
    char **filtered_list = (char **)malloc(validCount * sizeof(char *));
    if (filtered_list == NULL)
    {
        perror("Failed to allocate memory");
        exit(1);
    }

    int index = 0;
    for (int i = 0; i < file_count; i++)
    {
        if (file_list[i][0] != '.')
        {
            filtered_list[index++] = strdup(file_list[i]);
            if (filtered_list[index - 1] == NULL)
            {
                perror("Failed to duplicate string");
                exit(1);
            }
        }
    }
    *num_files = validCount;
    return filtered_list;
}

// Comparison function for qsort.
static int compare_strings(const void *a, const void *b)
{
    return strcmp(*(const char **)a, *(const char **)b);
}

static int get_number_within_range(int num_files)
{
    char input[3];
    int number;

    while (1)
    {
        // Prompt the user
        printf("Enter the ROM to load (1 to %d): ", num_files);

        // Get the input as a string (this is to handle empty input)
        if (fgets(input, sizeof(input), stdin) == NULL || strlen(input) <= 1)
        {
            // If input is empty or just a newline character, return
            return -1; // Use -1 or another value to indicate that no valid number was received
        }

        // Convert the string input to an integer
        if (sscanf(input, "%d", &number) == 1)
        {
            if (number >= 1 && number <= num_files)
            {
                // If the number is within the desired range, return the number
                return number;
            }
        }

        // If out of range or not a valid number, print an error message
        printf("Invalid input! Please enter a number between 1 and %d.\n", num_files);
    }
}

static void store_file_list(char **file_list, int num_files, uint8_t *memory_location)
{
    uint8_t *dest_ptr = memory_location;

    int total_size = 0;
    // Iterate through each file in the file_list
    for (int i = 0; i < num_files; i++)
    {
        char *current_file = file_list[i];
        total_size += strlen(current_file) + 1; // +1 for null terminator

        // Copy each character of the current file name into the memory location
        while (*current_file)
        {
            *dest_ptr++ = *current_file++;
        }

        // Place a zero after each file name
        *dest_ptr++ = 0x00;
    }

    // Ensure even address for the following data
    if ((uintptr_t)dest_ptr & 1)
    {
        *dest_ptr++ = 0x00;
    }
    // Add an additional 0x00 byte to mark the end of the list
    *dest_ptr++ = 0x00;
    *dest_ptr++ = 0x00;
    *dest_ptr++ = 0xFF;
    *dest_ptr++ = 0xFF;

    // Transform buffer's words from little endian to big endian inline
    uint16_t *dest_ptr_word = (uint16_t *)memory_location;
    for (int i = 0; i < total_size / 2; i++)
    {
        uint16_t value = *(uint16_t *)(dest_ptr_word);
        *(uint16_t *)(dest_ptr_word)++ = (value << 8) | (value >> 8);
    }
}

static void __not_in_flash_func(handle_protocol_command)(const TransmissionProtocol *protocol)
{
    // Handle the protocol
    switch (protocol->command_id)
    {
    case 1:
        // Load ROM passed as argument in the payload
        printf("Command LOAD_ROM (1) received: %d\n", protocol->payload_size);
        uint16_t value_payload = protocol->payload[0] | (protocol->payload[1] << 8);
        printf("Value: %d\n", value_payload);
        //        int res = load(file_list[value_payload - 1], FLASH_ROM_LOAD_OFFSET);
        rom_selected = value_payload;
        break;
    case 2:
        // Get the list of roms in the SD card
        printf("Command LIST_ROMS (2) received: %d\n", protocol->payload_size);
        break;
    // ... handle other commands
    default:
        printf("Unknown command: %d\n", protocol->command_id);
    }
}

// Interrupt handler callback for DMA completion
void __not_in_flash_func(dma_irq_handler_lookup_callback)(void)
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

int copy_firmware_to_RAM()
{
    // Need to initialize the ROM4 section with the firmware data
    extern uint16_t __rom_in_ram_start__;
    uint16_t *rom4_dest = &__rom_in_ram_start__;
    uint16_t *rom4_src = (uint16_t *)firmwareROM;
    for (int i = 0; i < firmwareROM_length; i++)
    {
        uint16_t value = *rom4_src++;
        *rom4_dest++ = value;
    }
    printf("Firmware copied to RAM.\n");
    return 0;
}

int delete_FLASH(void)
{
    // Erase the content before loading the new file. It seems that
    // overwriting it's not enough
    flash_range_erase(FLASH_ROM_LOAD_OFFSET, ROM_SIZE_BYTES * 2); // Two banks of 64K
    printf("FLASH erased.\n");
    return 0;
}

int init_firmware()
{

    FRESULT fr;
    FATFS fs;
    int num_files = 0;
    char **file_list = NULL;
    char **filtered_list = NULL;

    printf("\033[2J\033[H"); // Clear Screen
    printf("\n> ");
    stdio_flush();

    // Initialize SD card
    if (!sd_init_driver())
    {
        printf("ERROR: Could not initialize SD card\r\n");
        return -1;
    }

    // Mount drive
    fr = f_mount(&fs, "0:", 1);
    if (fr != FR_OK)
    {
        printf("ERROR: Could not mount filesystem (%d)\r\n", fr);
        return -1;
    }

    // Show the root directory content (ls command)
    file_list = ls("", &num_files);

    // Remove hidden files from the list
    int filtered_num_files = 0;
    filtered_list = filter(file_list, num_files, &filtered_num_files);
    // Sort remaining valid filenames lexicographically
    qsort(filtered_list, filtered_num_files, sizeof(char *), compare_strings);

    // Copy the content of the file list to the end of the ROM4 memory minus 4Kbytes
    // Translated to pure ROM4 address of the ST: 0xFB0000 - 0x1000 = 0xFAF000
    // The firmware code should be able to read the list of files from 0xFAF000
    // To select the desired ROM from the list, the ST code should send the command
    // LOAD_ROM with the number of the ROM to load PLUS 1. For example, to load the
    // first ROM in the list, the ST code should send the command LOAD_ROM with the
    // value 1 (0 + 1) because the first ROM of the index  is 0.
    // x=PEEK(&HFBABCD) 'Magic header number of commands
    // x=PEEK(&HFB0001) 'Command LOAD_ROM
    // x=PEEK(&HFB0002) 'Size of the payload (always even numbers)
    // x=PEEK(&HFB0001) 'Payload (two bytes per word)

    uint8_t *memory_area = (uint8_t *)(ROM3_START_ADDRESS - 4096); // 4Kbytes = 4096 bytes
    store_file_list(filtered_list, filtered_num_files, memory_area);

    // Here comes the tricky part. We have to put in the higher section of the ROM4 memory the content
    // of the file list available in the SD card.
    // The structure is a list of chars separated with a 0x00 byte. The end of the list is marked with
    // two 0x00 bytes.

    while (rom_selected < 0)
    {
        tight_loop_contents();
        sleep_ms(1000);
    }

    printf("ROM selected: %d\n", rom_selected);
    int res = load(filtered_list[rom_selected - 1], FLASH_ROM_LOAD_OFFSET);

    if (res != FR_OK)
        printf("f_open error: %s (%d)\n", FRESULT_str(res), res);

    release_memory_files(file_list, num_files);
    release_memory_files(filtered_list, filtered_num_files);
}
