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
static char *hd_folder = NULL;

// Save GEMOS vector variables
static uint32_t gemdos_trap_address_old;
static uint32_t gemdos_trap_address_xbra;
static uint16_t trap_call = 0xFFFF;
static bool save_vectors = false;

// Reentry lock variable
static uint16_t reentry_lock = 0xFFFF; // 0xFFFF = ignore, 1 = lock, 0 = unlock

// Save Fsetdta variables
static uint32_t ndta = 0;
static bool fsetdta = false;

// Save Fsfirst variables
static char fspec_string[128] = {0};
static FILINFO *fspec_files_found = NULL;
static uint32_t fspec_files_found_count = 0;
static uint32_t fspec_files_found_index = 0;
static uint32_t fspec = 0;
static uint16_t attribs = 0;
static bool fsfirst = false;

// Save Fsnext variables
static bool fsnext = false;

// Save Dgetdrv variables
static uint16_t dgetdrive_value = 0xFFFF;

// Save Fopen variables
static bool fopen_call = false;
static uint16_t fopen_mode = 0xFFFF;

// Save Dgetpath variables
static bool dgetpath_call = false;
static uint16_t dpath_drive = 0xFFFF;

// Save Dsetpath variables
static bool dsetpath_call = false;
static char dpath_string[128] = {0};

//
static DTANode *hashTable[DTA_HASH_TABLE_SIZE];

// Hash function
static unsigned int hash(uint32_t key)
{
    return key % DTA_HASH_TABLE_SIZE;
}

// Insert function
static void insertDTA(uint32_t key, DTA data)
{
    unsigned int index = hash(key);
    DTANode *newNode = malloc(sizeof(DTANode));
    if (newNode == NULL)
    {
        // Handle allocation error
        return;
    }

    newNode->key = key;
    newNode->data = data;
    newNode->next = NULL;

    if (hashTable[index] == NULL)
    {
        hashTable[index] = newNode;
    }
    else
    {
        // Handle collision with separate chaining
        newNode->next = hashTable[index];
        hashTable[index] = newNode;
    }
}

// Lookup function
static DTA *lookup(uint32_t key)
{
    unsigned int index = hash(key);
    DTANode *current = hashTable[index];

    while (current != NULL)
    {
        if (current->key == key)
        {
            return &current->data;
        }
        current = current->next;
    }

    return NULL;
}

// Release function
static void release(uint32_t key)
{
    unsigned int index = hash(key);
    DTANode *current = hashTable[index];
    DTANode *prev = NULL;

    while (current != NULL)
    {
        if (current->key == key)
        {
            if (prev == NULL)
            {
                // The node to be deleted is the first node in the list
                hashTable[index] = current->next;
            }
            else
            {
                // The node to be deleted is not the first node
                prev->next = current->next;
            }

            free(current); // Free the memory of the node
            return;
        }

        prev = current;
        current = current->next;
    }
}

// Initialize the hash table
static void initializeDTAHashTable()
{
    for (int i = 0; i < DTA_HASH_TABLE_SIZE; ++i)
    {
        hashTable[i] = NULL;
    }
}

/* Search a directory for objects */
static void find(const char *fspec_str, uint8_t attribs)
{
    FRESULT fr;  /* Return value */
    DIR dj;      /* Directory object */
    FILINFO fno; /* File information */

    char drive[2] = {0};
    char path[256] = {0};
    char internal_path[256] = {0};
    char pattern[32] = {0};
    split_fullpath(fspec_str, drive, path, pattern);
    back_2_forwardslash(path);

    // Concatenate the path with the hd_folder
    snprintf(internal_path, sizeof(internal_path), "%s/%s", hd_folder, path);

    // Remove duplicated forward slashes
    for (char *p = internal_path; *p != '\0'; p++)
        if (*p == '/' && *(p + 1) == '/')
            memmove(p, p + 1, strlen(p));

    // Patterns do not work with FatFs as Atari ST expects, so we need to adjust them
    // Remove the asterisk if it is the last character behind a dot
    if ((pattern[strlen(pattern) - 1] == '*') && (pattern[strlen(pattern) - 2] == '.'))
    {
        pattern[strlen(pattern) - 2] = '\0';
    }

    DPRINTF("drive: %s\n", drive);
    DPRINTF("path: %s\n", internal_path);
    DPRINTF("filename pattern: %s\n", pattern);

    fspec_files_found_count = 0;
    fspec_files_found_index = 0;
    fr = f_findfirst(&dj, &fno, internal_path, pattern);
    while (fr == FR_OK && fno.fname[0])
    {
        // Filter out elements that do not match the attributes
        if ((fno.fattrib & attribs) && fno.fname[0] != '.') // Skip hidden files and folders of the FatFs system
        {
            char shorten_filename[14];
            shorten_fname(fno.fname, shorten_filename);
            strcpy(fno.fname, shorten_filename);

            DPRINTF("Found file: %s\n", fno.fname);
            // Reallocate memory to accommodate one more FILINFO
            FILINFO *temp = realloc(fspec_files_found, (fspec_files_found_count + 1) * sizeof(FILINFO));
            if (temp == NULL)
            {
                // Handle memory allocation failure
                DPRINTF("Memory allocation failed\n");
                fspec_files_found_count = 0;
                break; // Exit the loop if memory allocation fails
            }
            fspec_files_found = temp;

            // Add the new file information to the array
            fspec_files_found[fspec_files_found_count] = fno;
            fspec_files_found_count++;
        }
        else
        {
            DPRINTF("Skipping file: %s\n", fno.fname);
        }
        fr = f_findnext(&dj, &fno); // Search for next item
    }
    DPRINTF("Total files found: %u\n", fspec_files_found_count);

    f_closedir(&dj);
}

// Function to release the memory allocated for fspec_files_found
static void release_fspec()
{
    if (fspec_files_found != NULL)
    {
        free(fspec_files_found);  // Free the allocated memory
        fspec_files_found = NULL; // Set pointer to NULL to avoid dangling pointer

        // Reset counters
        fspec_files_found_count = 0;
        fspec_files_found_index = 0;
    }
}

// Function to print the contents of the fspec_files_found
static void print_fspec()
{
    for (uint32_t i = 0; i < fspec_files_found_count; i++)
    {
        DPRINTF("File %u:\n", i + 1);
        char shorten_filename[14];
        shorten_fname(fspec_files_found[i].fname, shorten_filename);
        DPRINTF("  Name: %s\n", shorten_filename);
        DPRINTF("  Size: %lu\n", (unsigned long)fspec_files_found[i].fsize);
        DPRINTF("  Date: %u\n", fspec_files_found[i].fdate);
        DPRINTF("  Time: %u\n", fspec_files_found[i].ftime);
        DPRINTF("  Attrib: %u\n", fspec_files_found[i].fattrib);
        DPRINTF("\n");
    }
}

static void populate_dta(uint32_t memory_address_dta, uint32_t dta_address)
{
    DPRINTF("ndta: %x\n", dta_address);
    // Search the folder for the files
    DTA *data = lookup(dta_address);
    // Obtain the fspec string and keep it in memory
    if (data != NULL)
    {
        *((volatile uint16_t *)(memory_address_dta + GEMDRVEMUL_DTA_F_FOUND)) = 0;
        *((volatile uint16_t *)(memory_address_dta + GEMDRVEMUL_FORCE_BYPASS)) = 0; // Force bypass
        if (fspec_files_found_index < fspec_files_found_count)
        {
            strcpy(data->d_fname, fspec_files_found[fspec_files_found_index].fname);
            data->d_attrib = fspec_files_found[fspec_files_found_index].fattrib;
            data->d_time = fspec_files_found[fspec_files_found_index].ftime;
            data->d_date = fspec_files_found[fspec_files_found_index].fdate;
            data->d_length = (uint32_t)fspec_files_found[fspec_files_found_index].fsize;
            // Ignore the reserved field

            DPRINTF("Found DTA: %x\n", ndta);
            // Transfer the DTA to the Atari ST
            DPRINTF("Size of DTA: %i\n", sizeof(DTA));
            // Copy the DTA to the shared memory
            *((volatile uint8_t *)(memory_address_dta + GEMDRVEMUL_DTA_TRANSFER + 21)) = data->d_attrib;
            DPRINTF("DTA attrib: %d\n", *((volatile uint8_t *)(memory_address_dta + GEMDRVEMUL_DTA_TRANSFER + 21)));
            swap_words((uint16_t *)(memory_address_dta + GEMDRVEMUL_DTA_TRANSFER + 20), 2);
            *((volatile uint16_t *)(memory_address_dta + GEMDRVEMUL_DTA_TRANSFER + 22)) = data->d_time;
            DPRINTF("DTA time: %d\n", *((volatile uint16_t *)(memory_address_dta + GEMDRVEMUL_DTA_TRANSFER + 22)));
            *((volatile uint16_t *)(memory_address_dta + GEMDRVEMUL_DTA_TRANSFER + 24)) = data->d_date;
            DPRINTF("DTA date: %d\n", *((volatile uint16_t *)(memory_address_dta + GEMDRVEMUL_DTA_TRANSFER + 24)));
            // Assuming memory_address_dta is a byte-addressable pointer (e.g., uint8_t*)
            uint32_t value = ((data->d_length << 16) & 0xFFFF0000) | ((data->d_length >> 16) & 0xFFFF);
            uint16_t *address = (uint16_t *)(memory_address_dta + GEMDRVEMUL_DTA_TRANSFER + 26);
            address[1] = (value >> 16) & 0xFFFF; // Most significant 16 bits
            address[0] = value & 0xFFFF;         // Least significant 16 bits
            DPRINTF("DTA length: %x\n", ((uint32_t)address[0] << 16) | address[1]);
            for (uint8_t i = 0; i < 14; i += 1)
            {
                *((volatile uint8_t *)(memory_address_dta + GEMDRVEMUL_DTA_TRANSFER + 30 + i)) = (uint8_t)data->d_fname[i];
            }
            DPRINTF("DTA filename: %s\n", (char *)(memory_address_dta + GEMDRVEMUL_DTA_TRANSFER + 30));
            swap_words((uint16_t *)(memory_address_dta + GEMDRVEMUL_DTA_TRANSFER + 30), 14);
            fspec_files_found_index++;
        }
        else
        {
            // If no more files found, return ENMFIL for Fsnext
            // If no files found, return EFILNF for Fsfirst
            int16_t error_code = (fspec_files_found_count == 0 ? GEMDOS_EFILNF : GEMDOS_ENMFIL);
            DPRINTF("Showing error code: %x\n", error_code);
            *((volatile uint16_t *)(memory_address_dta + GEMDRVEMUL_DTA_F_FOUND)) = error_code;
            // release the memory allocated for the hash table
            // release(dta_address);
            // Also release the memory allocated for fspec_files_found
            // release_fspec();
            DPRINTF("No more files found. End of search\n");
        }
    }
    else
    {
        // End of search, no more elements
        *((volatile uint16_t *)(memory_address_dta + GEMDRVEMUL_DTA_F_FOUND)) = 0xFFFF;
        *((volatile uint16_t *)(memory_address_dta + GEMDRVEMUL_FORCE_BYPASS)) = 0xFFFF; // Do not force bypass
    }
}

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
        //        DPRINTF("Command SHOW_VECTOR_CALL (%i) received: %d\n", protocol->command_id, protocol->payload_size);
        payloadPtr = (uint16_t *)protocol->payload + 2;
        trap_call = (uint16_t)payloadPtr[0];
        random_token = ((*((uint32_t *)protocol->payload) & 0xFFFF0000) >> 16) | ((*((uint32_t *)protocol->payload) & 0x0000FFFF) << 16);
        break;
    case GEMDRVEMUL_REENTRY_LOCK:
        DPRINTF("Command REENTRY_LOCK (%i) received: %d\n", protocol->command_id, protocol->payload_size);
        reentry_lock = 1;
        random_token = ((*((uint32_t *)protocol->payload) & 0xFFFF0000) >> 16) | ((*((uint32_t *)protocol->payload) & 0x0000FFFF) << 16);
        break;
    case GEMDRVEMUL_REENTRY_UNLOCK:
        DPRINTF("Command REENTRY_UNLOCK (%i) received: %d\n", protocol->command_id, protocol->payload_size);
        reentry_lock = 0;
        random_token = ((*((uint32_t *)protocol->payload) & 0xFFFF0000) >> 16) | ((*((uint32_t *)protocol->payload) & 0x0000FFFF) << 16);
        break;
    case GEMDRVEMUL_DGETDRV_CALL:
        DPRINTF("Command DGETDRV_CALL (%i) received: %d\n", protocol->command_id, protocol->payload_size);
        payloadPtr = (uint16_t *)protocol->payload + 2;
        dgetdrive_value = (uint16_t)payloadPtr[0];
        random_token = ((*((uint32_t *)protocol->payload) & 0xFFFF0000) >> 16) | ((*((uint32_t *)protocol->payload) & 0x0000FFFF) << 16);
        break;
    case GEMDRVEMUL_FSETDTA_CALL:
        DPRINTF("Command FSETDTA_CALL (%i) received: %d\n", protocol->command_id, protocol->payload_size);
        payloadPtr = (uint16_t *)protocol->payload + 2;
        ndta = ((uint32_t)payloadPtr[1] << 16) | payloadPtr[0];
        fsetdta = true;
        random_token = ((*((uint32_t *)protocol->payload) & 0xFFFF0000) >> 16) | ((*((uint32_t *)protocol->payload) & 0x0000FFFF) << 16);
        break;
    case GEMDRVEMUL_FSFIRST_CALL:
        DPRINTF("Command FSFIRST_CALL (%i) received: %d\n", protocol->command_id, protocol->payload_size);
        payloadPtr = (uint16_t *)protocol->payload + 2;
        ndta = ((uint32_t)payloadPtr[1] << 16) | payloadPtr[0];  // d3 register
        payloadPtr += 2;                                         // Skip two words
        attribs = payloadPtr[0];                                 // d4 register
        payloadPtr += 2;                                         // Skip two words
        fspec = ((uint32_t)payloadPtr[1] << 16) | payloadPtr[0]; // d5 register
        payloadPtr += 2;                                         // Skip two words
        fsfirst = true;
        random_token = ((*((uint32_t *)protocol->payload) & 0xFFFF0000) >> 16) | ((*((uint32_t *)protocol->payload) & 0x0000FFFF) << 16);
        break;
    case GEMDRVEMUL_FSNEXT_CALL:
        DPRINTF("Command FSNEXT_CALL (%i) received: %d\n", protocol->command_id, protocol->payload_size);
        payloadPtr = (uint16_t *)protocol->payload + 2;
        ndta = ((uint32_t)payloadPtr[1] << 16) | payloadPtr[0]; // d3 register
        fsnext = true;
        random_token = ((*((uint32_t *)protocol->payload) & 0xFFFF0000) >> 16) | ((*((uint32_t *)protocol->payload) & 0x0000FFFF) << 16);
        break;
    case GEMDRVEMUL_FOPEN_CALL:
        DPRINTF("Command FOPEN_CALL (%i) received: %d\n", protocol->command_id, protocol->payload_size);
        payloadPtr = (uint16_t *)protocol->payload + 2;
        fopen_mode = payloadPtr[0]; // d3 register
        payloadPtr += 6;            // Skip six words
        fopen_call = true;
        random_token = ((*((uint32_t *)protocol->payload) & 0xFFFF0000) >> 16) | ((*((uint32_t *)protocol->payload) & 0x0000FFFF) << 16);
        break;
    case GEMDRVEMUL_DGETPATH_CALL:
        DPRINTF("Command DGETPATH_CALL (%i) received: %d\n", protocol->command_id, protocol->payload_size);
        payloadPtr = (uint16_t *)protocol->payload + 2;
        dpath_drive = payloadPtr[0]; // d3 register
        payloadPtr += 6;             // Skip six words
        dgetpath_call = true;
        random_token = ((*((uint32_t *)protocol->payload) & 0xFFFF0000) >> 16) | ((*((uint32_t *)protocol->payload) & 0x0000FFFF) << 16);
    case GEMDRVEMUL_DSETPATH_CALL:
        DPRINTF("Command DSETPATH (%i) received: %d\n", protocol->command_id, protocol->payload_size);
        payloadPtr = (uint16_t *)protocol->payload + 2;
        payloadPtr += 6; // Skip six words
        dsetpath_call = true;
        random_token = ((*((uint32_t *)protocol->payload) & 0xFFFF0000) >> 16) | ((*((uint32_t *)protocol->payload) & 0x0000FFFF) << 16);
        break;
    default:
        DPRINTF("Unknown command: %d\n", protocol->command_id);
    }
}

// Interrupt handler callback for DMA completion
void __not_in_flash_func(gemdrvemul_dma_irq_handler_lookup_callback)(void)
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

    initializeDTAHashTable();

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
                        hd_folder = find_entry("GEMDRIVE_FOLDERS")->value;
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
            DPRINTF("GEMDOS CALL: %s (%x)\n", GEMDOS_CALLS[trap_call], trap_call);
            switch (trap_call)
            {
            case 0xe: // Dsetdrv
                DPRINTF("Dsetdrv\n");
                payloadPtr += 1; // Next word
                uint16_t drive_number = payloadPtr[0];
                DPRINTF("drive_number: %x\n", drive_number);
                break;
            case 0x19: // Dgetdrv
                DPRINTF("Dgetdrv\n");
                payloadPtr += 1; // Next word
                break;
            case 0x1a: // Fsetdta
                DPRINTF("Fsetdta\n");
                payloadPtr += 2; // Skip the first two words
                uint32_t ndta = ((uint32_t)payloadPtr[1] << 16) | payloadPtr[0];
                DPRINTF("ndta: %x\n", ndta);
                break;
            case 0x3b: // Dsetpath
                DPRINTF("Dsetpath\n");
                payloadPtr += 2; // Next word
                uint32_t path = ((uint32_t)payloadPtr[1] << 16) | payloadPtr[0];
                DPRINTF("path: %x\n", path);
                break;
            case 0x3d: // Fopen
                DPRINTF("Fopen\n");
                payloadPtr += 1; // Next word
                uint16_t mode = payloadPtr[0];
                payloadPtr += 1; // Next word
                uint32_t fname = ((uint32_t)payloadPtr[1] << 16) | payloadPtr[0];
                DPRINTF("fname: %x\n", fname);
                DPRINTF("mode: %x\n", mode);
                break;
            case 0x4e: // Fsfirst
                DPRINTF("Fsfirst\n");
                payloadPtr += 1; // Next word
                uint16_t attribs = payloadPtr[0];
                payloadPtr += 1; // Next word
                uint32_t fspec = ((uint32_t)payloadPtr[1] << 16) | payloadPtr[0];
                DPRINTF("fspec: %x\n", fspec);
                DPRINTF("attribs: %x\n", attribs);
                break;
            default:
                DPRINTF("Trap call not implemented: %x\n", trap_call);
            }
            trap_call = 0xFFFF;
            *((volatile uint32_t *)(memory_shared_address + GEMDRVEMUL_RANDOM_TOKEN)) = random_token;
        }

        if (dgetdrive_value != 0xFFFF)
        {
            DPRINTF("Dgetdrv value: %x\n", dgetdrive_value);
            dgetdrive_value = 0xFFFF;
            *((volatile uint32_t *)(memory_shared_address + GEMDRVEMUL_RANDOM_TOKEN)) = random_token;
        }

        if (fsetdta)
        {
            DPRINTF("ndta: %x\n", ndta);
            DTA data = {{0}, 0, 0, 0, 0, "filename"};
            insertDTA(ndta, data);
            fsetdta = false;
            *((volatile uint32_t *)(memory_shared_address + GEMDRVEMUL_RANDOM_TOKEN)) = random_token;
        }

        if (fsfirst)
        {
            fsfirst = false;
            DPRINTF("ndta: %x\n", ndta);
            DPRINTF("attribs: %x\n", attribs);
            DPRINTF("fspec: %x\n", fspec);

            // Obtain the fspec string and keep it in memory
            char *origin = (char *)payloadPtr;
            for (int i = 0; i < 64; i += 2)
            {
                fspec_string[i] = (char)*(origin + i + 1);
                fspec_string[i + 1] = (char)*(origin + i);
            }
            DPRINTF("fspec string: %s\n", fspec_string);

            find(fspec_string, (uint8_t)attribs);
            populate_dta(memory_shared_address, ndta);

            *((volatile uint32_t *)(memory_shared_address + GEMDRVEMUL_RANDOM_TOKEN)) = random_token;
        }

        if (fsnext)
        {
            fsnext = false;
            populate_dta(memory_shared_address, ndta);
            *((volatile uint32_t *)(memory_shared_address + GEMDRVEMUL_RANDOM_TOKEN)) = random_token;
        }

        if (fopen_call)
        {
            fopen_call = false;
            DPRINTF("mode: %x\n", fopen_mode);
            // Obtain the fname string and keep it in memory
            char fname_string[128] = {0};
            char *origin = (char *)payloadPtr;
            for (int i = 0; i < 64; i += 2)
            {
                fname_string[i] = (char)*(origin + i + 1);
                fname_string[i + 1] = (char)*(origin + i);
            }
            DPRINTF("fname string: %s\n", fname_string);
            *((volatile uint32_t *)(memory_shared_address + GEMDRVEMUL_RANDOM_TOKEN)) = random_token;
        }

        if (dgetpath_call)
        {
            dgetpath_call = false;
            DPRINTF("dpath_drive: %x\n", dpath_drive);
            *((volatile uint32_t *)(memory_shared_address + GEMDRVEMUL_RANDOM_TOKEN)) = random_token;
        }

        if (dsetpath_call)
        {
            dsetpath_call = false;
            // Obtain the fname string and keep it in memory
            char *origin = (char *)payloadPtr;
            for (int i = 0; i < 64; i += 2)
            {
                dpath_string[i] = (char)*(origin + i + 1);
                dpath_string[i + 1] = (char)*(origin + i);
            }
            DPRINTF("Default path string: %s\n", dpath_string);
            // Check if the directory exists
            char tmp_path[128] = {0};

            // Concatenate the path with the hd_folder
            snprintf(tmp_path, sizeof(tmp_path), "%s/%s", hd_folder, dpath_string);
            back_2_forwardslash(tmp_path);

            // Remove duplicated forward slashes
            for (char *p = tmp_path; *p != '\0'; p++)
                if (*p == '/' && *(p + 1) == '/')
                    memmove(p, p + 1, strlen(p));

            if (directory_exists(tmp_path))
            {
                DPRINTF("Directory exists: %s\n", tmp_path);
                *((volatile uint16_t *)(memory_shared_address + GEMDRVEMUL_SET_DPATH_STATUS)) = GEMDOS_EOK;
            }
            else
            {
                DPRINTF("Directory does not exist: %s\n", tmp_path);
                *((volatile uint16_t *)(memory_shared_address + GEMDRVEMUL_SET_DPATH_STATUS)) = GEMDOS_EDRIVE;
            }
            *((volatile uint32_t *)(memory_shared_address + GEMDRVEMUL_RANDOM_TOKEN)) = random_token;
        }
        if (reentry_lock != 0xFFFF)
        {
            DPRINTF("Reentry lock: %x\n", reentry_lock);
            *((volatile uint16_t *)(memory_shared_address + GEMDRVEMUL_REENTRY_TRAP)) = 0xFFFF * reentry_lock;
            reentry_lock = 0xFFFF;
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
