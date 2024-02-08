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

// Save Fcreate variables
static bool fcreate_call = false;
static uint16_t fcreate_mode = 0xFFFF;

// Save Dgetpath variables
static bool dgetpath_call = false;
static uint16_t dpath_drive = 0xFFFF;

// Save Dsetpath variables
static bool dsetpath_call = false;
static char dpath_string[128] = {0};

//
static DTANode *hashTable[DTA_HASH_TABLE_SIZE];

// Structures to store the file descriptors
static FileDescriptors *fdescriptors = NULL; // Initialize the head of the list to NULL
static uint16_t fd_counter = 5;              // Start the counter at 3 to avoid conflicts with default file descriptors

// Read buffer variables
static bool readbuff_call = false;
static uint16_t readbuff_fd = 0;
static uint32_t readbuff_bytes_to_read = 0;
static uint32_t readbuff_pending_bytes_to_read = 0;

// FClose variables
static bool fclose_call = false;
static uint16_t fclose_fd = 0;

// FDelete variables
static bool fdelete_call = false;

// Dcreate variables
static bool dcreate_call = false;
static bool ddelete_call = false;

// Pexec variables
static uint16_t pexec_mode = 0xFFFF;
static bool pexec_call = false;
static char pexec_filename[128] = {0};
static PD *pexec_pd = NULL;
static ExecHeader *pexec_exec_header = NULL;
static bool save_basepage = false;
static bool save_exec_header = false;

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
static void find(const char *fspec_str, char path[], uint8_t attribs)
{
    FRESULT fr;  /* Return value */
    DIR dj;      /* Directory object */
    FILINFO fno; /* File information */

    char drive[2] = {0};
    char internal_path[MAX_FOLDER_LENGTH * 2] = {0};
    char pattern[32] = {0};
    char path_forwardslash[MAX_FOLDER_LENGTH] = {0};
    split_fullpath(fspec_str, drive, path_forwardslash, pattern);

    // Concatenate again the drive with the path
    snprintf(path, MAX_FOLDER_LENGTH, "%s%s", drive, path_forwardslash);

    back_2_forwardslash(path_forwardslash);

    // Concatenate the path with the hd_folder
    snprintf(internal_path, sizeof(internal_path), "%s/%s", hd_folder, path_forwardslash);

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
    DPRINTF("path: %s\n", path);
    DPRINTF("full internal path: %s\n", internal_path);
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

            // Transfer the DTA to the Atari ST
            // Copy the DTA to the shared memory
            *((volatile uint8_t *)(memory_address_dta + GEMDRVEMUL_DTA_TRANSFER + 21)) = data->d_attrib;
            swap_words((uint16_t *)(memory_address_dta + GEMDRVEMUL_DTA_TRANSFER + 20), 2);
            *((volatile uint16_t *)(memory_address_dta + GEMDRVEMUL_DTA_TRANSFER + 22)) = data->d_time;
            *((volatile uint16_t *)(memory_address_dta + GEMDRVEMUL_DTA_TRANSFER + 24)) = data->d_date;
            // Assuming memory_address_dta is a byte-addressable pointer (e.g., uint8_t*)
            uint32_t value = ((data->d_length << 16) & 0xFFFF0000) | ((data->d_length >> 16) & 0xFFFF);
            uint16_t *address = (uint16_t *)(memory_address_dta + GEMDRVEMUL_DTA_TRANSFER + 26);
            address[1] = (value >> 16) & 0xFFFF; // Most significant 16 bits
            address[0] = value & 0xFFFF;         // Least significant 16 bits
            for (uint8_t i = 0; i < 14; i += 1)
            {
                *((volatile uint8_t *)(memory_address_dta + GEMDRVEMUL_DTA_TRANSFER + 30 + i)) = (uint8_t)data->d_fname[i];
            }
            swap_words((uint16_t *)(memory_address_dta + GEMDRVEMUL_DTA_TRANSFER + 30), 14);
            DPRINTF("DTA: %x - size: %i - attrib: %d - time: %d - date: %d - length: %x - filename: %s\n",
                    ndta,
                    sizeof(DTA),
                    *((volatile uint8_t *)(memory_address_dta + GEMDRVEMUL_DTA_TRANSFER + 21)),
                    *((volatile uint16_t *)(memory_address_dta + GEMDRVEMUL_DTA_TRANSFER + 22)),
                    *((volatile uint16_t *)(memory_address_dta + GEMDRVEMUL_DTA_TRANSFER + 24)),
                    ((uint32_t)address[0] << 16) | address[1],
                    (char *)(memory_address_dta + GEMDRVEMUL_DTA_TRANSFER + 30));
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

static FileDescriptors *add_file(FileDescriptors **head, const char *fpath, FIL fobject, int new_fd)
{
    FileDescriptors *newFD = malloc(sizeof(FileDescriptors));
    if (newFD == NULL)
    {
        return NULL; // Allocation failed
    }
    strncpy(newFD->fpath, fpath, 127);
    newFD->fpath[127] = '\0'; // Ensure null-termination
    newFD->fobject = fobject;
    newFD->fd = new_fd;
    newFD->offset = 0;
    newFD->next = *head;
    *head = newFD;
    return newFD;
}

static FileDescriptors *get_file_by_fpath(FileDescriptors *head, const char *fpath)
{
    while (head != NULL)
    {
        if (strcmp(head->fpath, fpath) == 0)
        {
            return head;
        }
        head = head->next;
    }
    return NULL;
}

static FileDescriptors *get_file_by_fdesc(FileDescriptors *head, int fd)
{
    while (head != NULL)
    {
        if (head->fd == fd)
        { // Adjust this line based on the actual structure of FIL
            {
                return head;
            }
            head = head->next;
        }
        return NULL;
    }
}

static void delete_file_by_fpath(FileDescriptors **head, const char *fpath)
{
    FileDescriptors *current = *head;
    FileDescriptors *prev = NULL;

    while (current != NULL)
    {
        if (strcmp(current->fpath, fpath) == 0)
        {
            if (prev == NULL)
            {
                *head = current->next;
            }
            else
            {
                prev->next = current->next;
            }
            free(current);
            return;
        }
        prev = current;
        current = current->next;
    }
}

static void delete_file_by_fdesc(FileDescriptors **head, int fd)
{
    FileDescriptors *current = *head;
    FileDescriptors *prev = NULL;

    while (current != NULL)
    {
        if (current->fd == fd)
        { // Adjust this line based on the actual structure of FIL
            {
                if (prev == NULL)
                {
                    *head = current->next;
                }
                else
                {
                    prev->next = current->next;
                }
                free(current);
                return;
            }
            prev = current;
            current = current->next;
        }
    }
}

// Comparator function for qsort
static int compare_fd(const void *a, const void *b)
{
    return (*(int *)a - *(int *)b);
}

static int get_first_available_fd(FileDescriptors *head)
{
    if (head == NULL)
    {
        DPRINTF("List is empty. Returning %i\n", FIRST_FILE_DESCRIPTOR);
        return FIRST_FILE_DESCRIPTOR; // Return if the list is empty
    }

    // Count the number of nodes
    int count = 0;
    FileDescriptors *current = head;
    while (current != NULL)
    {
        count++;
        current = current->next;
    }

    count++;
    int *fdArray = (int *)malloc((count) * sizeof(int));
    if (!fdArray)
    {
        printf("Memory allocation failed\n");
        return INT_MAX;
    }

    fdArray[0] = FIRST_FILE_DESCRIPTOR; // Add the first file descriptor to the array always
    current = head;
    for (int i = 1; i < count; i++)
    {
        fdArray[i] = current->fd;
        current = current->next;
    }

    // Sort the array by fd
    qsort(fdArray, count, sizeof(int), compare_fd);

    // Find the smallest gap
    for (int i = 0; i < count - 1; i++)
    {
        if (fdArray[i + 1] - fdArray[i] > 1)
        {
            int val = fdArray[i] + 1; // Found a gap, return the first available number
            free(fdArray);            // Free the allocated memory
            return val;
        }
    }
    int val = fdArray[count - 1] + 1; // No gap found, return the last value plus 1
    free(fdArray);                    // Free the allocated memory
    return val;
}

static void split_str(const char *input, char *first_part, char *second_part, char delimiter)
{
    int delimiter_position = -1;

    for (int i = 0; i < 128; i++)
    {
        if (input[i] == delimiter && delimiter_position == -1)
        {
            delimiter_position = i;
            break;
        }
        else
        {
            first_part[i] = input[i];
        }
    }

    if (delimiter_position != -1)
    {
        for (int i = delimiter_position + 1, j = 0; i < 128; i++, j++)
        {
            second_part[j] = input[i];
        }
    }
}

// payloadPtr, dpath_string, hd_folder are global variables
static void get_local_full_pathname(char *tmp_filepath)
{
    // Obtain the fname string and keep it in memory
    // concatenated path and filename
    char path_filename[128] = {0};

    char tmp_path[MAX_FOLDER_LENGTH] = {0};

    char *origin = (char *)payloadPtr;
    for (int i = 0; i < 64; i += 2)
    {
        path_filename[i] = (char)*(origin + i + 1);
        path_filename[i + 1] = (char)*(origin + i);
    }
    DPRINTF("dpath_string: %s\n", dpath_string);
    DPRINTF("path_filename: %s\n", path_filename);
    if (path_filename[1] == ':')
    {
        // If the path has the drive letter, jump two positions
        // and ignore the dpath_string
        snprintf(path_filename, MAX_FOLDER_LENGTH, "%s", path_filename + 2);
        DPRINTF("New path_filename: %s\n", path_filename);
        snprintf(tmp_path, MAX_FOLDER_LENGTH, "%s/", hd_folder);
    }
    else
    {
        // If the path filename does not have a drive letter,
        // concatenate the path with the hd_folder and the filename
        // If the path has the drive letter, jump two positions
        if (dpath_string[1] == ':')
        {
            snprintf(tmp_path, MAX_FOLDER_LENGTH, "%s/%s", hd_folder, dpath_string + 2);
        }
        else
        {
            snprintf(tmp_path, MAX_FOLDER_LENGTH, "%s/%s", hd_folder, dpath_string);
        }
    }
    snprintf(tmp_filepath, MAX_FOLDER_LENGTH, "%s/%s", tmp_path, path_filename);
    back_2_forwardslash(tmp_filepath);

    // Remove duplicated forward slashes
    remove_dup_slashes(tmp_filepath);
    DPRINTF("tmp_filepath: %s\n", tmp_filepath);
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
    case GEMDRVEMUL_DEBUG:
        DPRINTF("Command DEBUG (%i) received: %d\n", protocol->command_id, protocol->payload_size);
        payloadPtr = (uint16_t *)protocol->payload + 2;
        if (protocol->payload_size == 8)
        {
            uint32_t debug_payload = ((uint32_t)payloadPtr[1] << 16) | payloadPtr[0];
            DPRINTF("DEBUG: %x\n", debug_payload);
        }
        if (protocol->payload_size < 8)
        {
            uint16_t debug_payload = payloadPtr[0];
            DPRINTF("DEBUG: %x\n", debug_payload);
        }
        if (protocol->payload_size > 8)
        {
            payloadPtr += 6;
            uint8_t *payloadShowBytesPtr = (uint8_t *)payloadPtr;
            // Display the first 256 bytes of the payload in hexadecimal showing 16 bytes per line and the ASCII representation
            for (int i = 0; i < protocol->payload_size - 10; i += 16)
            {
                DPRINTF("%04x - %02x %02x %02x %02x %02x %02x %02x %02x  %02x %02x %02x %02x %02x %02x %02x %02x  |%c %c %c %c %c %c %c %c  %c %c %c %c %c %c %c %c|\n",
                        i,
                        payloadShowBytesPtr[i + 1], payloadShowBytesPtr[i], payloadShowBytesPtr[i + 3], payloadShowBytesPtr[i + 2],
                        payloadShowBytesPtr[i + 5], payloadShowBytesPtr[i + 4], payloadShowBytesPtr[i + 7], payloadShowBytesPtr[i + 6],
                        payloadShowBytesPtr[i + 9], payloadShowBytesPtr[i + 8], payloadShowBytesPtr[i + 11], payloadShowBytesPtr[i + 10],
                        payloadShowBytesPtr[i + 13], payloadShowBytesPtr[i + 12], payloadShowBytesPtr[i + 15], payloadShowBytesPtr[i + 14],
                        (payloadShowBytesPtr[i + 1] >= 32 && payloadShowBytesPtr[i + 1] <= 126) ? payloadShowBytesPtr[i + 1] : '.',
                        (payloadShowBytesPtr[i] >= 32 && payloadShowBytesPtr[i] <= 126) ? payloadShowBytesPtr[i] : '.',
                        (payloadShowBytesPtr[i + 3] >= 32 && payloadShowBytesPtr[i + 3] <= 126) ? payloadShowBytesPtr[i + 3] : '.',
                        (payloadShowBytesPtr[i + 2] >= 32 && payloadShowBytesPtr[i + 2] <= 126) ? payloadShowBytesPtr[i + 2] : '.',
                        (payloadShowBytesPtr[i + 5] >= 32 && payloadShowBytesPtr[i + 5] <= 126) ? payloadShowBytesPtr[i + 5] : '.',
                        (payloadShowBytesPtr[i + 4] >= 32 && payloadShowBytesPtr[i + 4] <= 126) ? payloadShowBytesPtr[i + 4] : '.',
                        (payloadShowBytesPtr[i + 7] >= 32 && payloadShowBytesPtr[i + 7] <= 126) ? payloadShowBytesPtr[i + 7] : '.',
                        (payloadShowBytesPtr[i + 6] >= 32 && payloadShowBytesPtr[i + 6] <= 126) ? payloadShowBytesPtr[i + 6] : '.',
                        (payloadShowBytesPtr[i + 9] >= 32 && payloadShowBytesPtr[i + 9] <= 126) ? payloadShowBytesPtr[i + 9] : '.',
                        (payloadShowBytesPtr[i + 8] >= 32 && payloadShowBytesPtr[i + 8] <= 126) ? payloadShowBytesPtr[i + 8] : '.',
                        (payloadShowBytesPtr[i + 11] >= 32 && payloadShowBytesPtr[i + 11] <= 126) ? payloadShowBytesPtr[i + 11] : '.',
                        (payloadShowBytesPtr[i + 10] >= 32 && payloadShowBytesPtr[i + 10] <= 126) ? payloadShowBytesPtr[i + 10] : '.',
                        (payloadShowBytesPtr[i + 13] >= 32 && payloadShowBytesPtr[i + 13] <= 126) ? payloadShowBytesPtr[i + 13] : '.',
                        (payloadShowBytesPtr[i + 12] >= 32 && payloadShowBytesPtr[i + 12] <= 126) ? payloadShowBytesPtr[i + 12] : '.',
                        (payloadShowBytesPtr[i + 15] >= 32 && payloadShowBytesPtr[i + 15] <= 126) ? payloadShowBytesPtr[i + 15] : '.',
                        (payloadShowBytesPtr[i + 14] >= 32 && payloadShowBytesPtr[i + 14] <= 126) ? payloadShowBytesPtr[i + 14] : '.');
            }
        }
        random_token = ((*((uint32_t *)protocol->payload) & 0xFFFF0000) >> 16) | ((*((uint32_t *)protocol->payload) & 0x0000FFFF) << 16);
        break;
    case GEMDRVEMUL_PING:
        DPRINTF("Command PING (%i) received: %d\n", protocol->command_id, protocol->payload_size);
        random_token = ((*((uint32_t *)protocol->payload) & 0xFFFF0000) >> 16) | ((*((uint32_t *)protocol->payload) & 0x0000FFFF) << 16);
        ping_received = true;
        break;
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
    case GEMDRVEMUL_DCREATE_CALL:
        DPRINTF("Command DCREATE_CALL (%i) received: %d\n", protocol->command_id, protocol->payload_size);
        payloadPtr = (uint16_t *)protocol->payload + 8; // Skip eight words
        dcreate_call = true;
        random_token = ((*((uint32_t *)protocol->payload) & 0xFFFF0000) >> 16) | ((*((uint32_t *)protocol->payload) & 0x0000FFFF) << 16);
        break;
    case GEMDRVEMUL_DDELETE_CALL:
        DPRINTF("Command DDELETE_CALL (%i) received: %d\n", protocol->command_id, protocol->payload_size);
        payloadPtr = (uint16_t *)protocol->payload + 8; // Skip eight words
        ddelete_call = true;
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
    case GEMDRVEMUL_FCREATE_CALL:
        DPRINTF("Command FCREATE_CALL (%i) received: %d\n", protocol->command_id, protocol->payload_size);
        payloadPtr = (uint16_t *)protocol->payload + 2; // Skip 2 words
        fcreate_mode = payloadPtr[0];                   // d3 register
        payloadPtr += 6;                                // Skip six words
        fcreate_call = true;
        random_token = ((*((uint32_t *)protocol->payload) & 0xFFFF0000) >> 16) | ((*((uint32_t *)protocol->payload) & 0x0000FFFF) << 16);
        break;
    case GEMDRVEMUL_FCLOSE_CALL:
        DPRINTF("Command FCLOSE_CALL (%i) received: %d\n", protocol->command_id, protocol->payload_size);
        payloadPtr = (uint16_t *)protocol->payload + 2;
        fclose_fd = payloadPtr[0]; // d3 register
        fclose_call = true;
        random_token = ((*((uint32_t *)protocol->payload) & 0xFFFF0000) >> 16) | ((*((uint32_t *)protocol->payload) & 0x0000FFFF) << 16);
        break;
    case GEMDRVEMUL_FDELETE_CALL:
        DPRINTF("Command FDELETE_CALL (%i) received: %d\n", protocol->command_id, protocol->payload_size);
        payloadPtr = (uint16_t *)protocol->payload + 8; // Skip eight words
        fdelete_call = true;
        random_token = ((*((uint32_t *)protocol->payload) & 0xFFFF0000) >> 16) | ((*((uint32_t *)protocol->payload) & 0x0000FFFF) << 16);
        break;
    case GEMDRVEMUL_READ_BUFF_CALL:
        DPRINTF("Command READ_BUFF_CALL (%i) received: %d\n", protocol->command_id, protocol->payload_size);
        payloadPtr = (uint16_t *)protocol->payload + 2;
        readbuff_fd = payloadPtr[0];                                                      // d3 register
        payloadPtr += 2;                                                                  // Skip two words
        readbuff_bytes_to_read = ((uint32_t)payloadPtr[1] << 16) | payloadPtr[0];         // d4 register constains the number of bytes to read
        payloadPtr += 2;                                                                  // Skip two words
        readbuff_pending_bytes_to_read = ((uint32_t)payloadPtr[1] << 16) | payloadPtr[0]; // d5 register constains the number of bytes to read
        readbuff_call = true;
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
    case GEMDRVEMUL_PEXEC_CALL:
        DPRINTF("Command PEXEC_CALL (%i) received: %d\n", protocol->command_id, protocol->payload_size);
        payloadPtr = (uint16_t *)protocol->payload + 2;
        pexec_mode = payloadPtr[0]; // d3 register
        payloadPtr += 6;            // Skip six words
        pexec_call = true;
        break;
    case GEMDRVEMUL_SAVE_BASEPAGE:
        DPRINTF("Command SAVE_BASEPAGE (%i) received: %d\n", protocol->command_id, protocol->payload_size);
        payloadPtr = (uint16_t *)protocol->payload + 2; // Skip eight words
        {
            uint32_t d3 = ((uint32_t)payloadPtr[1] << 16) | payloadPtr[0]; // d3 register
            uint32_t d4 = ((uint32_t)payloadPtr[3] << 16) | payloadPtr[2]; // d4 register
            uint32_t d5 = ((uint32_t)payloadPtr[5] << 16) | payloadPtr[4]; // d5 register
            DPRINTF("d3: %x\n", d3);
            DPRINTF("d4: %x\n", d4);
            DPRINTF("d5: %x\n", d5);
        }
        payloadPtr += 6; // Skip eight words
        save_basepage = true;
        random_token = ((*((uint32_t *)protocol->payload) & 0xFFFF0000) >> 16) | ((*((uint32_t *)protocol->payload) & 0x0000FFFF) << 16);
        break;
    case GEMDRVEMUL_SAVE_EXEC_HEADER:
        DPRINTF("Command SAVE_EXEC_HEADER (%i) received: %d\n", protocol->command_id, protocol->payload_size);
        payloadPtr = (uint16_t *)protocol->payload + 8; // Skip the registries and parse the buffer
        save_exec_header = true;
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

    dpath_string[0] = '\\'; // Set the root folder as default
    dpath_string[1] = '\0';

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
            trap_call = 0xFFFF;
            *((volatile uint32_t *)(memory_shared_address + GEMDRVEMUL_RANDOM_TOKEN)) = random_token;
        }

        if (dgetdrive_value != 0xFFFF)
        {
            DPRINTF("Dgetdrv value: %x\n", dgetdrive_value);
            dgetdrive_value = 0xFFFF;
            *((volatile uint32_t *)(memory_shared_address + GEMDRVEMUL_RANDOM_TOKEN)) = random_token;
        }
        if (dcreate_call)
        {
            // Obtain the pathname string and keep it in memory
            // concatenated with the local harddisk folder and the default path (if any)
            char tmp_pathname[MAX_FOLDER_LENGTH] = {0};
            get_local_full_pathname(tmp_pathname);
            DPRINTF("Folder to create: %s\n", tmp_pathname);

            // Check if the folder exists. If not, return an error
            if (directory_exists(tmp_pathname) != FR_OK)
            {
                DPRINTF("ERROR: Folder does not exist\n");
                *((volatile uint16_t *)(memory_shared_address + GEMDRVEMUL_DCREATE_STATUS)) = GEMDOS_EPTHNF;
            }
            else
            {
                // Create the folder
                fr = f_mkdir(tmp_pathname);
                if (fr != FR_OK)
                {
                    DPRINTF("ERROR: Could not create folder (%d)\r\n", fr);
                    *((volatile uint16_t *)(memory_shared_address + GEMDRVEMUL_DCREATE_STATUS)) = GEMDOS_EACCDN;
                }
                else
                {
                    DPRINTF("Folder created\n");
                    *((volatile uint16_t *)(memory_shared_address + GEMDRVEMUL_DCREATE_STATUS)) = GEMDOS_EOK;
                }
            }
            dcreate_call = false;
            *((volatile uint32_t *)(memory_shared_address + GEMDRVEMUL_RANDOM_TOKEN)) = random_token;
        }
        if (ddelete_call)
        {
            // Obtain the pathname string and keep it in memory
            // concatenated with the local harddisk folder and the default path (if any)
            char tmp_pathname[MAX_FOLDER_LENGTH] = {0};
            get_local_full_pathname(tmp_pathname);
            DPRINTF("Folder to delete: %s\n", tmp_pathname);

            // Check if the folder exists. If not, return an error
            if (directory_exists(tmp_pathname) == 0)
            {
                DPRINTF("ERROR: Folder does not exist\n");
                *((volatile uint16_t *)(memory_shared_address + GEMDRVEMUL_DDELETE_STATUS)) = GEMDOS_EPTHNF;
            }
            else
            {
                // Delete the folder
                fr = f_unlink(tmp_pathname);
                if (fr != FR_OK)
                {
                    DPRINTF("ERROR: Could not delete folder (%d)\r\n", fr);
                    if (fr == FR_DENIED)
                    {
                        DPRINTF("ERROR: Folder is not empty\n");
                        *((volatile uint16_t *)(memory_shared_address + GEMDRVEMUL_DDELETE_STATUS)) = GEMDOS_EACCDN;
                    }
                    else if (fr == FR_NO_PATH)
                    {
                        DPRINTF("ERROR: Folder does not exist\n");
                        *((volatile uint16_t *)(memory_shared_address + GEMDRVEMUL_DDELETE_STATUS)) = GEMDOS_EPTHNF;
                    }
                    else
                    {
                        DPRINTF("ERROR: Internal error\n");
                        *((volatile uint16_t *)(memory_shared_address + GEMDRVEMUL_DDELETE_STATUS)) = GEMDOS_EINTRN;
                    }
                }
                else
                {
                    DPRINTF("Folder deleted\n");
                    *((volatile uint16_t *)(memory_shared_address + GEMDRVEMUL_DDELETE_STATUS)) = GEMDOS_EOK;
                }
            }
            ddelete_call = false;
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

            // We need to search for directory and files, so we need to add the AM_ARC attribute
            find(fspec_string, dpath_string, (uint8_t)(attribs | AM_ARC));
            populate_dta(memory_shared_address, ndta);

            // Copy the content of the path variable to memory_shared_address + GEMDRVEMUL_DEFAULT_PATH
            for (int i = 0; i < 128; i++)
            {
                *((volatile uint8_t *)(memory_shared_address + GEMDRVEMUL_DEFAULT_PATH + i)) = dpath_string[i];
            }
            // Swap the bytes
            swap_words((uint16_t *)(memory_shared_address + GEMDRVEMUL_DEFAULT_PATH), 128);
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
            // concatenated path and filename
            char tmp_filepath[MAX_FOLDER_LENGTH] = {0};
            get_local_full_pathname(tmp_filepath);
            DPRINTF("Opening file: %s\n", tmp_filepath);

            // Convert the fopen_mode to FatFs mode
            BYTE fatfs_open_mode = 0;
            switch (fopen_mode)
            {
            case 0: // Read only
                fatfs_open_mode = FA_READ;
                break;
            case 1: // Write only
                fatfs_open_mode = FA_WRITE | FA_CREATE_ALWAYS;
                break;
            case 2: // Read/Write
                fatfs_open_mode = FA_READ | FA_WRITE | FA_CREATE_ALWAYS;
                break;
            default:
                DPRINTF("ERROR: Invalid mode: %x\n", fopen_mode);
                *((volatile uint16_t *)(memory_shared_address + GEMDRVEMUL_FOPEN_HANDLE)) = GEMDOS_EACCDN;
                break;
            }
            DPRINTF("FatFs open mode: %x\n", fatfs_open_mode);
            if (fopen_mode <= 2)
            {
                // Open the file with FatFs
                FIL file_object;
                fr = f_open(&file_object, tmp_filepath, fatfs_open_mode);
                if (fr != FR_OK)
                {
                    DPRINTF("ERROR: Could not open file (%d)\r\n", fr);
                    *((volatile uint16_t *)(memory_shared_address + GEMDRVEMUL_FOPEN_HANDLE)) = GEMDOS_EFILNF;
                }
                else
                {
                    // Add the file to the list of open files
                    int fd_counter = get_first_available_fd(fdescriptors);
                    DPRINTF("File opened with file descriptor: %d\n", fd_counter);
                    add_file(&fdescriptors, tmp_filepath, file_object, fd_counter);
                    // Return the file descriptor
                    *((volatile uint16_t *)(memory_shared_address + GEMDRVEMUL_FOPEN_HANDLE)) = fd_counter;
                }
            }

            *((volatile uint32_t *)(memory_shared_address + GEMDRVEMUL_RANDOM_TOKEN)) = random_token;
        }
        if (fclose_call)
        {
            fclose_call = false;
            DPRINTF("fd: %x\n", fclose_fd);
            // Obtain the file descriptor
            FileDescriptors *file = get_file_by_fdesc(fdescriptors, fclose_fd);
            if (file == NULL)
            {
                DPRINTF("ERROR: File descriptor not found\n");
                *((volatile uint16_t *)(memory_shared_address + GEMDRVEMUL_FCLOSE_STATUS)) = GEMDOS_EIHNDL;
            }
            else
            {
                // Close the file with FatFs
                fr = f_close(&file->fobject);
                if (fr != FR_OK)
                {
                    DPRINTF("ERROR: Could not close file (%d)\r\n", fr);
                    *((volatile uint16_t *)(memory_shared_address + GEMDRVEMUL_FCLOSE_STATUS)) = GEMDOS_EINTRN;
                }
                else
                {
                    // Remove the file from the list of open files
                    delete_file_by_fdesc(&fdescriptors, fclose_fd);
                    DPRINTF("File closed\n");
                    // Return the file descriptor
                    *((volatile uint16_t *)(memory_shared_address + GEMDRVEMUL_FCLOSE_STATUS)) = GEMDOS_EOK;
                }
            }
            *((volatile uint32_t *)(memory_shared_address + GEMDRVEMUL_RANDOM_TOKEN)) = random_token;
        }

        if (fcreate_call)
        {
            fcreate_call = false;
            DPRINTF("mode: %x\n", fcreate_mode);

            // Obtain the fname string and keep it in memory
            // concatenated path and filename
            char tmp_filepath[MAX_FOLDER_LENGTH] = {0};
            get_local_full_pathname(tmp_filepath);
            DPRINTF("Creating file: %s\n", tmp_filepath);

            // CREATE ALWAYS MODE
            BYTE fatfs_create_mode = FA_READ | FA_WRITE | FA_CREATE_ALWAYS;
            // fatfs_create_mode = FA_READ | FA_WRITE | FA_CREATE_ALWAYS;
            DPRINTF("FatFs create mode: %x\n", fatfs_create_mode);

            // Open the file with FatFs
            FIL file_object;
            fr = f_open(&file_object, tmp_filepath, fatfs_create_mode);
            if (fr != FR_OK)
            {
                DPRINTF("ERROR: Could not create file (%d)\r\n", fr);
                *((volatile uint32_t *)(memory_shared_address + GEMDRVEMUL_FCREATE_HANDLE)) = SWAP_LONGWORD(GEMDOS_EPTHNF);
            }
            else
            {
                // Add the file to the list of open files
                int fd_counter = get_first_available_fd(fdescriptors);
                DPRINTF("File created with file descriptor: %d\n", fd_counter);
                add_file(&fdescriptors, tmp_filepath, file_object, fd_counter);

                // Missing attribute modification

                // Return the file descriptor
                *((volatile uint32_t *)(memory_shared_address + GEMDRVEMUL_FCREATE_HANDLE)) = SWAP_LONGWORD(fd_counter);
            }

            *((volatile uint32_t *)(memory_shared_address + GEMDRVEMUL_RANDOM_TOKEN)) = random_token;
        }

        if (fdelete_call)
        {
            fdelete_call = false;
            // Obtain the fname string and keep it in memory
            // concatenated path and filename
            char tmp_filepath[MAX_FOLDER_LENGTH] = {0};
            get_local_full_pathname(tmp_filepath);
            DPRINTF("Deleting file: %s\n", tmp_filepath);

            // Delete the file
            fr = f_unlink(tmp_filepath);
            if (fr != FR_OK)
            {
                DPRINTF("ERROR: Could not delete file (%d)\r\n", fr);
                if (fr == FR_DENIED)
                {
                    DPRINTF("ERROR: Not enough premissions to delete file\n");
                    *((volatile uint32_t *)(memory_shared_address + GEMDRVEMUL_FDELETE_STATUS)) = SWAP_LONGWORD(GEMDOS_EACCDN);
                }
                else if (fr == FR_NO_PATH)
                {
                    DPRINTF("ERROR: Folder does not exist\n");
                    *((volatile uint32_t *)(memory_shared_address + GEMDRVEMUL_FDELETE_STATUS)) = SWAP_LONGWORD(GEMDOS_EPTHNF);
                }
                else
                {
                    DPRINTF("ERROR: Internal error\n");
                    *((volatile uint32_t *)(memory_shared_address + GEMDRVEMUL_FDELETE_STATUS)) = SWAP_LONGWORD(GEMDOS_EINTRN);
                }
            }
            else
            {
                DPRINTF("File deleted\n");
                *((volatile uint32_t *)(memory_shared_address + GEMDRVEMUL_FDELETE_STATUS)) = GEMDOS_EOK;
            }

            *((volatile uint32_t *)(memory_shared_address + GEMDRVEMUL_RANDOM_TOKEN)) = random_token;
        }

        if (readbuff_call)
        {
            readbuff_call = false;
            DPRINTF("fd: x%x, bytes_to_read: x%08x, pending_bytes_to_read: x%08x\n", readbuff_fd, readbuff_bytes_to_read, readbuff_pending_bytes_to_read);
            // Obtain the file descriptor
            FileDescriptors *file = get_file_by_fdesc(fdescriptors, readbuff_fd);
            if (file == NULL)
            {
                DPRINTF("ERROR: File descriptor not found\n");
                set_and_swap_longword(memory_shared_address + GEMDRVEMUL_READ_BYTES, GEMDOS_EIHNDL);
            }
            else
            {
                uint32_t readbuff_offset = file->offset;
                UINT bytes_read = 0;
                // Read the file with FatFs
                fr = f_lseek(&file->fobject, readbuff_offset);
                if (fr != FR_OK)
                {
                    DPRINTF("ERROR: Could not change read offset of the file (%d)\r\n", fr);
                    set_and_swap_longword(memory_shared_address + GEMDRVEMUL_READ_BYTES, GEMDOS_EINTRN);
                }
                else
                {
                    // Only read DEFAULT_FOPEN_READ_BUFFER_SIZE bytes at a time
                    uint16_t buff_size = readbuff_pending_bytes_to_read > DEFAULT_FOPEN_READ_BUFFER_SIZE ? DEFAULT_FOPEN_READ_BUFFER_SIZE : readbuff_pending_bytes_to_read;
                    DPRINTF("Reading x%x bytes from the file at offset x%x\n", buff_size, readbuff_offset);
                    fr = f_read(&file->fobject, (void *)(memory_shared_address + GEMDRVEMUL_READ_BUFF), buff_size, &bytes_read);
                    if (fr != FR_OK)
                    {
                        DPRINTF("ERROR: Could not read file (%d)\r\n", fr);
                        set_and_swap_longword(memory_shared_address + GEMDRVEMUL_READ_BYTES, GEMDOS_EINTRN);
                    }
                    else
                    {
                        // Update the offset of the file
                        file->offset += bytes_read;
                        uint32_t current_offset = file->offset;
                        DPRINTF("New offset: x%x after reading x%x bytes\n", current_offset, bytes_read);
                        // Change the endianness of the bytes read
                        swap_words((uint16_t *)(memory_shared_address + GEMDRVEMUL_READ_BUFF), ((buff_size + 1) * 2) / 2);
                        // Return the number of bytes read
                        set_and_swap_longword(memory_shared_address + GEMDRVEMUL_READ_BYTES, bytes_read);
                    }
                }
            }
            *((volatile uint32_t *)(memory_shared_address + GEMDRVEMUL_RANDOM_TOKEN)) = random_token;
        }

        if (dgetpath_call)
        {
            dgetpath_call = false;
            DPRINTF("dpath_drive: %x\n", dpath_drive);
            // Copy the content of the path variable to memory_shared_address + GEMDRVEMUL_DEFAULT_PATH
            for (int i = 0; i < 128; i++)
            {
                *((volatile uint8_t *)(memory_shared_address + GEMDRVEMUL_DEFAULT_PATH + i)) = dpath_string[i];
            }
            // Swap the bytes
            swap_words((uint16_t *)(memory_shared_address + GEMDRVEMUL_DEFAULT_PATH), 128);
            *((volatile uint32_t *)(memory_shared_address + GEMDRVEMUL_RANDOM_TOKEN)) = random_token;
        }

        if (dsetpath_call)
        {
            dsetpath_call = false;
            // Obtain the fname string and keep it in memory
            char *origin = (char *)payloadPtr;
            char dpath_tmp[128] = {};
            for (int i = 0; i < 64; i += 2)
            {
                dpath_tmp[i] = (char)*(origin + i + 1);
                dpath_tmp[i + 1] = (char)*(origin + i);
            }
            DPRINTF("Default path string: %s\n", dpath_tmp);
            // Check if the directory exists
            char tmp_path[128] = {0};

            // Concatenate the path with the hd_folder
            snprintf(tmp_path, sizeof(tmp_path), "%s/%s", hd_folder, dpath_tmp);
            back_2_forwardslash(tmp_path);

            // Remove duplicated forward slashes
            remove_dup_slashes(tmp_path);

            if (directory_exists(tmp_path))
            {
                DPRINTF("Directory exists: %s\n", tmp_path);
                // Copy dpath_tmp to dpath_string
                strcpy(dpath_string, dpath_tmp);
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
            //            DPRINTF("Reentry lock: %x\n", reentry_lock);
            *((volatile uint16_t *)(memory_shared_address + GEMDRVEMUL_REENTRY_TRAP)) = 0xFFFF * reentry_lock;
            reentry_lock = 0xFFFF;
            *((volatile uint32_t *)(memory_shared_address + GEMDRVEMUL_RANDOM_TOKEN)) = random_token;
        }

        if (pexec_call)
        {
            DPRINTF("Pexec mode: %x\n", pexec_mode);

            // Obtain the fname string and keep it in memory
            // concatenated path and filename
            get_local_full_pathname(pexec_filename);
            DPRINTF("Execute file: %s\n", pexec_filename);
            pexec_call = false;
            *((volatile uint32_t *)(memory_shared_address + GEMDRVEMUL_RANDOM_TOKEN)) = random_token;
        }

        if (save_basepage)
        {
            // Copy the from the shared memory the basepage to pexec_pd
            DPRINTF("Saving basepage\n");
            PD *origin = (PD *)(payloadPtr);
            // Reserve and copy the memory from origin to pexec_pd
            if (pexec_pd == NULL)
            {
                pexec_pd = (PD *)(memory_shared_address + GEMDRVEMUL_EXEC_PD);
            }
            memcpy(pexec_pd, origin, sizeof(PD));
            DPRINTF("pexec_pd->p_lowtpa: %x\n", SWAP_LONGWORD(pexec_pd->p_lowtpa));
            DPRINTF("pexec_pd->p_hitpa: %x\n", SWAP_LONGWORD(pexec_pd->p_hitpa));
            DPRINTF("pexec_pd->p_tbase: %x\n", SWAP_LONGWORD(pexec_pd->p_tbase));
            DPRINTF("pexec_pd->p_tlen: %x\n", SWAP_LONGWORD(pexec_pd->p_tlen));
            DPRINTF("pexec_pd->p_dbase: %x\n", SWAP_LONGWORD(pexec_pd->p_dbase));
            DPRINTF("pexec_pd->p_dlen: %x\n", SWAP_LONGWORD(pexec_pd->p_dlen));
            DPRINTF("pexec_pd->p_bbase: %x\n", SWAP_LONGWORD(pexec_pd->p_bbase));
            DPRINTF("pexec_pd->p_blen: %x\n", SWAP_LONGWORD(pexec_pd->p_blen));
            DPRINTF("pexec_pd->p_xdta: %x\n", SWAP_LONGWORD(pexec_pd->p_xdta));
            DPRINTF("pexec_pd->p_parent: %x\n", SWAP_LONGWORD(pexec_pd->p_parent));
            DPRINTF("pexec_pd->p_hflags: %x\n", SWAP_LONGWORD(pexec_pd->p_hflags));
            DPRINTF("pexec_pd->p_env: %x\n", SWAP_LONGWORD(pexec_pd->p_env));
            DPRINTF("pexec_pd->p_1fill\n");
            DPRINTF("pexec_pd->p_curdrv: %x\n", SWAP_LONGWORD(pexec_pd->p_curdrv));
            DPRINTF("pexec_pd->p_uftsize: %x\n", SWAP_LONGWORD(pexec_pd->p_uftsize));
            DPRINTF("pexec_pd->p_uft: %x\n", SWAP_LONGWORD(pexec_pd->p_uft));
            DPRINTF("pexec_pd->p_cmdlin: %x\n", SWAP_LONGWORD(pexec_pd->p_cmdlin));
            save_basepage = false;
            *((volatile uint32_t *)(memory_shared_address + GEMDRVEMUL_RANDOM_TOKEN)) = random_token;
        }

        if (save_exec_header)
        {
            // Copy the from the shared memory the basepage to pexec_exec_header
            DPRINTF("Saving exec header\n");
            ExecHeader *origin = (ExecHeader *)(payloadPtr);
            // Reserve and copy the memory from origin to pexec_exec_header
            if (pexec_exec_header == NULL)
            {
                pexec_exec_header = (ExecHeader *)(memory_shared_address + GEMDRVEMUL_EXEC_HEADER);
            }
            memcpy(pexec_exec_header, origin, sizeof(ExecHeader));
            DPRINTF("pexec_exec->magic: %x\n", pexec_exec_header->magic);
            DPRINTF("pexec_exec->text: %x\n", (uint32_t)(pexec_exec_header->text_h << 16 | pexec_exec_header->text_l));
            DPRINTF("pexec_exec->data: %x\n", (uint32_t)(pexec_exec_header->data_h << 16 | pexec_exec_header->data_l));
            DPRINTF("pexec_exec->bss: %x\n", (uint32_t)(pexec_exec_header->bss_h << 16 | pexec_exec_header->bss_l));
            DPRINTF("pexec_exec->syms: %x\n", (uint32_t)(pexec_exec_header->syms_h << 16 | pexec_exec_header->syms_l));
            DPRINTF("pexec_exec->reserved1: %x\n", (uint32_t)(pexec_exec_header->reserved1_h << 16 | pexec_exec_header->reserved1_l));
            DPRINTF("pexec_exec->prgflags: %x\n", (uint32_t)(pexec_exec_header->prgflags_h << 16 | pexec_exec_header->prgflags_l));
            DPRINTF("pexec_exec->absflag: %x\n", pexec_exec_header->absflag);
            save_exec_header = false;
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
