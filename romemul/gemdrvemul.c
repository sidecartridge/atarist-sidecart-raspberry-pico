/**
 * File: gemdrvemul.c
 * Author: Diego Parrilla Santamaría
 * Date: November 2023
 * Copyright: 2023 - GOODDATA LABS SL
 * Description: Emulate GEMDOS hard disk driver
 */

#include "include/gemdrvemul.h"

// Let's substitute the flags
static uint16_t active_command_id = 0xFFFF;

static uint16_t *payloadPtr = NULL;
static uint32_t random_token;

static char *fullpath_a = NULL;
static char *hd_folder = NULL;
static bool debug = false;
static char drive_letter = 'C';

// Save Dgetdrv variables
static uint16_t dgetdrive_value = 0xFFFF;

// Save Fcreate variables
static bool fcreate_call = false;
static uint16_t fcreate_mode = 0xFFFF;

// Save Dsetpath variables
static char dpath_string[MAX_FOLDER_LENGTH] = {0};

// Save Fsetdta variables
static DTANode *dtaTbl[DTA_HASH_TABLE_SIZE];

// Structures to store the file descriptors
static FileDescriptors *fdescriptors = NULL; // Initialize the head of the list to NULL
static PD *pexec_pd = NULL;
static ExecHeader *pexec_exec_header = NULL;

static inline void __not_in_flash_func(generate_random_token_seed)(const TransmissionProtocol *protocol)
{
    random_token = ((*((uint32_t *)protocol->payload) & 0xFFFF0000) >> 16) | ((*((uint32_t *)protocol->payload) & 0x0000FFFF) << 16);
}

static void __not_in_flash_func(write_random_token)(uint32_t memory_shared_address)
{
    *((volatile uint32_t *)(memory_shared_address + GEMDRVEMUL_RANDOM_TOKEN)) = random_token;
}

// Hash function
static unsigned int __not_in_flash_func(hash)(uint32_t key)
{
    return key % DTA_HASH_TABLE_SIZE;
}

// Insert function
static void __not_in_flash_func(insertDTA)(uint32_t key, DTA data, DIR *dj, FILINFO *fno)
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
    newNode->dj = dj;
    newNode->fno = fno;
    newNode->next = NULL;

    if (dtaTbl[index] == NULL)
    {
        dtaTbl[index] = newNode;
    }
    else
    {
        // Handle collision with separate chaining
        newNode->next = dtaTbl[index];
        dtaTbl[index] = newNode;
    }
}

// Lookup function
static DTANode *__not_in_flash_func(lookupDTA)(uint32_t key)
{
    unsigned int index = hash(key);
    DTANode *current = dtaTbl[index];

    while (current != NULL)
    {
        if (current->key == key)
        {
            return current;
        }
        current = current->next;
    }

    return NULL;
}

// Release function
static void __not_in_flash_func(releaseDTA)(uint32_t key)
{
    unsigned int index = hash(key);
    DTANode *current = dtaTbl[index];
    DTANode *prev = NULL;

    while (current != NULL)
    {
        if (current->key == key)
        {
            if (prev == NULL)
            {
                // The node to be deleted is the first node in the list
                dtaTbl[index] = current->next;
            }
            else
            {
                // The node to be deleted is not the first node
                prev->next = current->next;
            }
            if (current->dj != NULL)
            {
                f_closedir(current->dj); // Close the directory
                free(current->dj);       // Free the memory of the DIR
            }
            if (current->fno != NULL)
            {
                free(current->fno); // Free the memory of the FILINFO
            }
            free(current); // Free the memory of the node
            return;
        }

        prev = current;
        current = current->next;
    }
}

// Count the number of elements in the hash table
unsigned int __not_in_flash_func(countDTA)()
{
    unsigned int totalCount = 0;
    for (int i = 0; i < DTA_HASH_TABLE_SIZE; i++)
    {
        DTANode *currentNode = dtaTbl[i];
        while (currentNode != NULL)
        {
            totalCount++;
            currentNode = currentNode->next;
        }
    }
    return totalCount;
}
// Initialize the hash table
static void __not_in_flash_func(initializeDTAHashTable)()
{
    for (int i = 0; i < DTA_HASH_TABLE_SIZE; ++i)
    {
        dtaTbl[i] = NULL;
    }
}

static void __not_in_flash_func(swap_string_endiannes)(const char *origin, char *swap_str)
{
    // Obtain the fspec string and keep it in memory
    for (int i = 0; i < 64; i += 2)
    {
        swap_str[i] = (char)*(origin + i + 1);
        swap_str[i + 1] = (char)*(origin + i);
    }
}

static void __not_in_flash_func(seach_path_2_st)(const char *fspec_str, char *internal_path, char *name_pattern)
{
    char drive[2] = {0};
    char path_forwardslash[MAX_FOLDER_LENGTH] = {0};
    char path[MAX_FOLDER_LENGTH] = {0};
    split_fullpath(fspec_str, drive, path_forwardslash, name_pattern);

    // Concatenate again the drive with the path
    snprintf(path, sizeof(path), "%s%s", drive, path_forwardslash);

    back_2_forwardslash(path_forwardslash);

    // Concatenate the path with the hd_folder
    snprintf(internal_path, MAX_FOLDER_LENGTH * 2, "%s/%s", hd_folder, path_forwardslash);

    // Remove duplicated forward slashes
    char *p = internal_path;
    while ((p = strstr(p, "//")) != NULL)
    {
        memmove(p, p + 1, strlen(p));
    }

    // Patterns do not work with FatFs as Atari ST expects, so we need to adjust them
    // Remove the asterisk if it is the last character behind a dot
    size_t np_len = strlen(name_pattern);
    if (np_len >= 2 && name_pattern[np_len - 1] == '*' && name_pattern[np_len - 2] == '.')
    {
        name_pattern[np_len - 2] = '\0'; // Handle pattern ending with ".*"
    }

    // If the pattern starts with a slash or a backslash, remove it
    if (name_pattern[0] == '/' || name_pattern[0] == '\\')
    {
        memmove(name_pattern, name_pattern + 1, strlen(name_pattern)); // Handle leading slashes or backslashes
    }
}

static void __not_in_flash_func(populate_dta)(uint32_t memory_address_dta, uint32_t dta_address, int16_t gemdos_err_code)
{
    // Search the folder for the files
    DTANode *dataNode = lookupDTA(dta_address);
    DTA *data = dataNode != NULL ? &dataNode->data : NULL;
    if (data != NULL)
    {
        *((volatile uint16_t *)(memory_address_dta + GEMDRVEMUL_DTA_F_FOUND)) = 0;
        FILINFO *fno = dataNode->fno;
        if (fno)
        {
            strcpy(data->d_name, fno->fname);
            strcpy(data->d_fname, fno->fname);
            data->d_offset_drive = 0;
            data->d_curbyt = 0;
            data->d_curcl = 0;
            data->d_attr = attribs_fat2st(fno->fattrib);
            data->d_attrib = attribs_fat2st(fno->fattrib);
            data->d_time = fno->ftime;
            data->d_date = fno->fdate;
            data->d_length = (uint32_t)fno->fsize;
            // Ignore the reserved field

            // Transfer the DTA to the Atari ST
            // Copy the DTA to the shared memory
            for (uint8_t i = 0; i < 12; i += 1)
            {
                *((volatile uint8_t *)(memory_address_dta + GEMDRVEMUL_DTA_TRANSFER + i)) = (uint8_t)data->d_name[i];
            }
            swap_words((uint16_t *)(memory_address_dta + GEMDRVEMUL_DTA_TRANSFER + 30), 14);
            *((volatile uint32_t *)(memory_address_dta + GEMDRVEMUL_DTA_TRANSFER + 12)) = data->d_offset_drive;
            *((volatile uint16_t *)(memory_address_dta + GEMDRVEMUL_DTA_TRANSFER + 16)) = data->d_curbyt;
            *((volatile uint16_t *)(memory_address_dta + GEMDRVEMUL_DTA_TRANSFER + 18)) = data->d_curcl;
            *((volatile uint8_t *)(memory_address_dta + GEMDRVEMUL_DTA_TRANSFER + 20)) = data->d_attr;
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
            char attribs_str[7] = "";
            get_attribs_st_str(attribs_str, *((volatile uint8_t *)(memory_address_dta + GEMDRVEMUL_DTA_TRANSFER + 21)));
            DPRINTF("Populate DTA. addr: %x - attrib: %s - time: %d - date: %d - length: %x - filename: %s\n",
                    dta_address,
                    attribs_str,
                    *((volatile uint16_t *)(memory_address_dta + GEMDRVEMUL_DTA_TRANSFER + 22)),
                    *((volatile uint16_t *)(memory_address_dta + GEMDRVEMUL_DTA_TRANSFER + 24)),
                    ((uint32_t)address[0] << 16) | address[1],
                    (char *)(memory_address_dta + GEMDRVEMUL_DTA_TRANSFER + 30));
        }
        else
        {
            // If no more files found, return ENMFIL for Fsnext
            // If no files found, return EFILNF for Fsfirst
            DPRINTF("DTA at %x showing error code: %x\n", dta_address, gemdos_err_code);
            *((volatile int16_t *)(memory_address_dta + GEMDRVEMUL_DTA_F_FOUND)) = (int16_t)gemdos_err_code;
            for (uint8_t i = 0; i < DTA_SIZE_ON_ST; i += 1)
            {
                *((volatile uint8_t *)(memory_address_dta + GEMDRVEMUL_DTA_TRANSFER + i)) = 0;
            }
            // release the memory allocated for the hash table
            releaseDTA(dta_address);
            DPRINTF("DTA at %x released. DTA table elements: %d\n", dta_address, countDTA());
            if (gemdos_err_code == GEMDOS_EFILNF)
            {
                DPRINTF("Files not found in FSFIRST.\n");
            }
            else
            {
                DPRINTF("No more files found in FSNEXT.\n");
            }
        }
    }
    else
    {
        // No DTA structure found, return error
        DPRINTF("DTA not found at %x\n", dta_address);
        *((volatile uint16_t *)(memory_address_dta + GEMDRVEMUL_DTA_F_FOUND)) = 0xFFFF;
    }
}

static FileDescriptors *__not_in_flash_func(add_file)(FileDescriptors **head, const char *fpath, FIL fobject, int new_fd)
{
    FileDescriptors *newFDescriptor = malloc(sizeof(FileDescriptors));
    if (newFDescriptor == NULL)
    {
        DPRINTF("Memory allocation failed for new FileDescriptors\n");
        return NULL; // Allocation failed
    }
    strncpy(newFDescriptor->fpath, fpath, 127);
    newFDescriptor->fpath[127] = '\0'; // Ensure null-termination
    newFDescriptor->fobject = fobject;
    newFDescriptor->fd = new_fd;
    newFDescriptor->offset = 0;
    newFDescriptor->next = *head;
    *head = newFDescriptor;
    DPRINTF("File %s added with fd %i\n", fpath, new_fd);
    return newFDescriptor;
}

static FileDescriptors *__not_in_flash_func(get_file_by_fpath)(FileDescriptors *head, const char *fpath)
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

static FileDescriptors *__not_in_flash_func(get_file_by_fdesc)(FileDescriptors *head, int fd)
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

static void __not_in_flash_func(delete_file_by_fpath)(FileDescriptors **head, const char *fpath)
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

static void __not_in_flash_func(delete_file_by_fdesc)(FileDescriptors **head, int fd)
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
static int __not_in_flash_func(compare_fd)(const void *a, const void *b)
{
    return (*(int *)a - *(int *)b);
}

static int __not_in_flash_func(get_first_available_fd)(FileDescriptors *head)
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

// payloadPtr, dpath_string, hd_folder are global variables
static void __not_in_flash_func(get_local_full_pathname)(char *tmp_filepath)
{
    // Obtain the fname string and keep it in memory
    // concatenated path and filename
    char path_filename[MAX_FOLDER_LENGTH] = {0};

    char tmp_path[MAX_FOLDER_LENGTH] = {0};

    char *origin = (char *)payloadPtr;
    for (int i = 0; i < MAX_FOLDER_LENGTH / 2; i += 2)
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

static void print_payload(uint8_t *payloadShowBytesPtr)
{
    // Display the first 256 bytes of the payload in hexadecimal showing 16 bytes per line and the ASCII representation
    for (int i = 0; i < 256 - 10; i += 16)
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

static void set_shared_var(uint32_t p_shared_variable_index, uint32_t p_shared_variable_value, uint32_t memory_shared_address)
{
    DPRINTF("Setting shared variable %d to %x\n", p_shared_variable_index, p_shared_variable_value);
    *((volatile uint16_t *)(memory_shared_address + GEMDRVEMUL_SHARED_VARIABLES + (p_shared_variable_index * 4) + 2)) = p_shared_variable_value & 0xFFFF;
    *((volatile uint16_t *)(memory_shared_address + GEMDRVEMUL_SHARED_VARIABLES + (p_shared_variable_index * 4))) = p_shared_variable_value >> 16;
}

inline const char *__not_in_flash_func(get_command_name)(unsigned int value)
{
    for (int i = 0; i < numCommands; i++)
    {
        if (commandStr[i].value == value)
        {
            return commandStr[i].name;
        }
    }
    return "COMMAND NOT DEFINED";
}

static inline void __not_in_flash_func(handle_protocol_command)(const TransmissionProtocol *protocol)
{
    if (active_command_id == 0xFFFF)
    {
        payloadPtr = (uint16_t *)protocol->payload + 2;
        DPRINTF("Command %s(%i) received: %d\n", get_command_name(protocol->command_id), protocol->command_id, protocol->payload_size);
        generate_random_token_seed(protocol);
        active_command_id = protocol->command_id;
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
    bool hd_folder_ready = false;
    bool network_ready = false;

    char *ntp_server_host = NULL;
    int ntp_server_port = NTP_DEFAULT_PORT;
    u_int16_t network_poll_counter = 0;

    // Local wifi password in the local file
    char *wifi_password_file_content = NULL;

    srand(time(0));
    printf("Initializing GEMDRIVE...\n"); // Print alwayse

    initializeDTAHashTable();

    dpath_string[0] = '\\'; // Set the root folder as default
    dpath_string[1] = '\0';

    bool write_config_only_once = true;
    active_command_id = 0xFFFF;

    DPRINTF("Waiting for commands...\n");
    uint32_t memory_shared_address = ROM3_START_ADDRESS; // Start of the shared memory buffer
    uint32_t memory_firmware_code = ROM4_START_ADDRESS;  // Start of the firmware code

    *((volatile uint32_t *)(memory_shared_address + GEMDRVEMUL_RTC_STATUS)) = 0x0;
    *((volatile uint32_t *)(memory_shared_address + GEMDRVEMUL_NETWORK_STATUS)) = 0x0;

    ConfigEntry *gemdrive_rtc = find_entry(PARAM_GEMDRIVE_RTC);
    bool gemdrive_rtc_enabled = true;
    if (gemdrive_rtc != NULL)
    {
        gemdrive_rtc_enabled = gemdrive_rtc->value[0] == 't' || gemdrive_rtc->value[0] == 'T';
    }
#if defined(_DEBUG) && (_DEBUG != 0)
    DPRINTF("RTC DISABLED FOR DEBUGGING\n");
    gemdrive_rtc_enabled = false;
#endif
    *((volatile uint32_t *)(memory_shared_address + GEMDRVEMUL_NETWORK_ENABLED)) = gemdrive_rtc_enabled;
    DPRINTF("Network enabled? %s\n", gemdrive_rtc_enabled ? "Yes" : "No");

    ConfigEntry *gemdrive_timeout = find_entry(PARAM_GEMDRIVE_TIMEOUT_SEC);
    uint32_t gemdrive_timeout_sec = 0;
    if (gemdrive_timeout != NULL)
    {
        gemdrive_timeout_sec = atoi(gemdrive_timeout->value);
    }
    set_and_swap_longword(memory_shared_address + GEMDRVEMUL_TIMEOUT_SEC, gemdrive_timeout_sec);
    gemdrive_timeout_sec = gemdrive_timeout_sec * 0.7; // Adjust the timeout to 70% of the value
    DPRINTF("Timeout in seconds: %d\n", gemdrive_timeout_sec);

    // Only try to get the datetime from the network if the wifi is configured
    if (gemdrive_rtc_enabled && strlen(find_entry(PARAM_WIFI_SSID)->value) > 0)
    {
        // Initialize SD card
        if (!sd_init_driver())
        {
            DPRINTF("ERROR: Could not initialize SD card\r\n");
        }
        else
        {
            FRESULT err = read_and_trim_file(WIFI_PASS_FILE_NAME, &wifi_password_file_content);
            if (err == FR_OK)
            {
                DPRINTF("Wifi password file found. Content: %s\n", wifi_password_file_content);
            }
            else
            {
                DPRINTF("Wifi password file not found.\n");
            }
        }

        // Start the network.
        network_connect(false, NETWORK_CONNECTION_ASYNC, &wifi_password_file_content);

        // Start the internal RTC
        rtc_init();

        // Start the NTP client
        ntp_init();

        ntp_server_host = find_entry("RTC_NTP_SERVER_HOST")->value;
        ntp_server_port = atoi(find_entry("RTC_NTP_SERVER_PORT")->value);

        DPRINTF("NTP server host: %s\n", ntp_server_host);
        DPRINTF("NTP server port: %d\n", ntp_server_port);

        char *utc_offset_entry = find_entry("RTC_UTC_OFFSET")->value;
        if (strlen(utc_offset_entry) > 0)
        {
            // The offset can be in decimal format
            set_utc_offset_seconds((long)(atoi(utc_offset_entry) * 60 * 60));
        }
        DPRINTF("UTC offset: %ld\n", get_utc_offset_seconds());

        get_net_time()->ntp_server_found = false;

        network_ready = true;

        // Wait until the RTC is set by the NTP server
        while (gemdrive_timeout_sec > 0 && get_rtc_time()->year == 0)
        {
            tight_loop_contents();
#if PICO_CYW43_ARCH_POLL
            cyw43_arch_lwip_begin();
            network_poll();
            cyw43_arch_wait_for_work_until(make_timeout_time_ms(1));
            cyw43_arch_lwip_end();
#elif PICO_CYW43_ARCH_THREADSAFE_BACKGROUND
            cyw43_arch_lwip_begin();
            cyw43_arch_wait_for_work_until(make_timeout_time_ms(10));
            cyw43_arch_lwip_end();
#endif
            sleep_ms(1000); // Wait 1 sec per test
            if (network_poll_counter == 0)
            {
                if (strlen(find_entry(PARAM_WIFI_SSID)->value) > 0)
                {
                    // Only display when changes status to avoid flooding the console
                    ConnectionStatus previous_status = get_previous_connection_status();
                    ConnectionStatus current_status = get_network_connection_status();
                    if (current_status != previous_status)
                    {
                        DPRINTF("Network status: %d\n", current_status);
                        DPRINTF("Network previous status: %d\n", previous_status);
                        ConnectionData *connection_data = malloc(sizeof(ConnectionData));
                        get_connection_data(connection_data);
                        DPRINTF("SSID: %s - Status: %d - IPv4: %s - IPv6: %s - GW:%s - Mask:%s - MAC:%s\n",
                                connection_data->ssid,
                                connection_data->network_status,
                                connection_data->ipv4_address,
                                connection_data->ipv6_address,
                                print_ipv4(get_gateway()),
                                print_ipv4(get_netmask()),
                                print_mac(get_mac_address()));
                        free(connection_data);
                        if ((current_status >= TIMEOUT_ERROR) && (current_status <= INSUFFICIENT_RESOURCES_ERROR))
                        {
                            DPRINTF("Connection failed. Retrying...\n");
                            // Need to deinit and init again the full network stack to be able to scan again
                            cyw43_arch_deinit();
                            cyw43_arch_init();
                            network_init();
                            // Start the network.
                            network_connect(true, NETWORK_CONNECTION_ASYNC, &wifi_password_file_content);
                        }
                    }
                    // Get the IP address from the DNS server if the wifi is connected and no IP address is found yet
                    if (get_rtc_time()->year == 0 && current_status == CONNECTED_WIFI_IP && !get_net_time()->ntp_server_found)
                    {
                        // Let's connect to ntp server
                        DPRINTF("Querying the DNS...\n");
                        err_t dns_ret = dns_gethostbyname(ntp_server_host, &get_net_time()->ntp_ipaddr, host_found_callback, get_net_time());
                        if (dns_ret == ERR_ARG)
                        {
                            DPRINTF("Invalid DNS argument\n");
                        }
                        DPRINTF("DNS query done\n");
                        sleep_ms(1000);
                    }
                    // If connected to the wifi then set the network status to 1, otherwise set it to 0
                    *((volatile uint32_t *)(memory_shared_address + GEMDRVEMUL_NETWORK_STATUS)) = (current_status == CONNECTED_WIFI_IP);
                }
            }
            // If the NTP server is found, then send the NTP request and set the clock
            if (get_net_time()->ntp_server_found)
            {
                DPRINTF("NTP server found. Connecting to NTP server...\n");
                get_net_time()->ntp_server_found = false;
                set_internal_rtc();
            }
            gemdrive_timeout_sec--;
        }
        if (gemdrive_timeout_sec > 0)
        {
            // Set the RTC time for the Atari ST to read
            rtc_get_datetime(get_rtc_time());
            uint8_t *rtc_time_ptr = (uint8_t *)(memory_shared_address + GEMDRVEMUL_RTC_STATUS);
            // Change order for the endianess
            rtc_time_ptr[1] = 0x1b;
            rtc_time_ptr[0] = add_bcd(to_bcd((get_rtc_time()->year % 100)), to_bcd((2000 - 1980) + (80 - 30))); // Fix Y2K issue
            rtc_time_ptr[3] = to_bcd(get_rtc_time()->month);
            rtc_time_ptr[2] = to_bcd(get_rtc_time()->day);
            rtc_time_ptr[5] = to_bcd(get_rtc_time()->hour);
            rtc_time_ptr[4] = to_bcd(get_rtc_time()->min);
            rtc_time_ptr[7] = to_bcd(get_rtc_time()->sec);
            rtc_time_ptr[6] = 0x0;
        }
        else
        {
            DPRINTF("Timeout reached. RTC not set.\n");
            // Just be sure to deinit the network stack
            network_disconnect();
            cyw43_arch_deinit();
            DPRINTF("No wifi configured. Skipping network initialization.\n");
        }
    }
    else
    {
        // Just be sure to deinit the network stack
        network_disconnect();
        cyw43_arch_deinit();
        DPRINTF("No wifi configured. Skipping network initialization.\n");
    }

    ConfigEntry *drive_letter_conf = find_entry(PARAM_GEMDRIVE_DRIVE);
    char drive_letter = 'C';
    if (drive_letter_conf != NULL)
    {
        drive_letter = drive_letter_conf->value[0];
    }
    uint16_t drive_letter_num = (uint8_t)toupper(drive_letter);
    uint16_t drive_number = drive_letter_num - 65; // Convert the drive letter to a number. Add 1 because 0 is the current drive

    ConfigEntry *buffer_type_conf = find_entry(PARAM_GEMDRIVE_BUFF_TYPE);
    uint16_t buffer_type = 0; // 0: Diskbuffer, 1: Stack
    if (buffer_type_conf != NULL)
    {
        buffer_type = atoi(buffer_type_conf->value);
    }

    set_shared_var(SHARED_VARIABLE_FIRST_FILE_DESCRIPTOR, FIRST_FILE_DESCRIPTOR, memory_shared_address);
    set_shared_var(SHARED_VARIABLE_DRIVE_LETTER, drive_letter_num, memory_shared_address);
    set_shared_var(SHARED_VARIABLE_DRIVE_NUMBER, drive_number, memory_shared_address);
    set_shared_var(SHARED_VARIABLE_BUFFER_TYPE, buffer_type, memory_shared_address);

    for (int i = 0; i < SHARED_VARIABLES_SIZE; i++)
    {
        uint32_t value = *((volatile uint32_t *)(memory_shared_address + GEMDRVEMUL_SHARED_VARIABLES + (i * 4)));
        DPRINTF("Shared variable %d: %04x%04x\n", i, value & 0xFFFF, value >> 16);
    }

    while (true)
    {
        *((volatile uint32_t *)(memory_shared_address + GEMDRVEMUL_RANDOM_TOKEN_SEED)) = rand() % 0xFFFFFFFF;
        tight_loop_contents();

        switch (active_command_id)
        {
        case GEMDRVEMUL_DEBUG:
        {
            uint32_t d3 = ((uint32_t)payloadPtr[1] << 16) | payloadPtr[0];
            DPRINTF("DEBUG: %x\n", d3);
            payloadPtr += 2;
            uint32_t d4 = ((uint32_t)payloadPtr[1] << 16) | payloadPtr[0];
            DPRINTF("DEBUG: %x\n", d4);
            payloadPtr += 2;
            uint32_t d5 = ((uint32_t)payloadPtr[1] << 16) | payloadPtr[0];
            DPRINTF("DEBUG: %x\n", d5);
            payloadPtr += 2;
            uint8_t *payloadShowBytesPtr = (uint8_t *)payloadPtr;
            print_payload(payloadShowBytesPtr);
            write_random_token(memory_shared_address);
            active_command_id = 0xFFFF;
            break;
        }
        case GEMDRVEMUL_SAVE_VECTORS:
        {
            DPRINTF("Saving vectors\n");
            uint32_t gemdos_trap_address_old = ((uint32_t)payloadPtr[0] << 16) | payloadPtr[1];
            payloadPtr += 2;
            uint32_t gemdos_trap_address_xbra = ((uint32_t)payloadPtr[1] << 16) | payloadPtr[0];
            // Save the vectors needed for the floppy emulation
            DPRINTF("gemdos_trap_addres_xbra: %x\n", gemdos_trap_address_xbra);
            DPRINTF("gemdos_trap_address_old: %x\n", gemdos_trap_address_old);
            // DPRINTF("random token: %x\n", random_token);
            // Self modifying code to create the old and venerable XBRA structure
            *((volatile uint16_t *)(memory_firmware_code + gemdos_trap_address_xbra - ATARI_ROM4_START_ADDRESS)) = gemdos_trap_address_old & 0xFFFF;
            *((volatile uint16_t *)(memory_firmware_code + gemdos_trap_address_xbra - ATARI_ROM4_START_ADDRESS + 2)) = gemdos_trap_address_old >> 16;
            write_random_token(memory_shared_address);
            active_command_id = 0xFFFF;
            break;
        }
        case GEMDRVEMUL_PING:
        {
            if (!hd_folder_ready)
            {
                // Initialize SD card
                if (!sd_init_driver())
                {
                    DPRINTF("ERROR: Could not initialize SD card\r\n");
                    *((volatile uint16_t *)(memory_shared_address + GEMDRVEMUL_PING_STATUS)) = 0x0;
                }
                else
                {
                    // Mount drive
                    FRESULT fr; /* FatFs function common result code */
                    fr = f_mount(&fs, "0:", 1);
                    bool microsd_mounted = (fr == FR_OK);
                    if (!microsd_mounted)
                    {
                        DPRINTF("ERROR: Could not mount filesystem (%d)\r\n", fr);
                        *((volatile uint16_t *)(memory_shared_address + GEMDRVEMUL_PING_STATUS)) = 0x0;
                    }
                    else
                    {
                        hd_folder = find_entry(PARAM_GEMDRIVE_FOLDERS)->value;
                        DPRINTF("Emulating GEMDRIVE in folder: %s\n", hd_folder);
                        hd_folder_ready = true;
                        *((volatile uint16_t *)(memory_shared_address + GEMDRVEMUL_PING_STATUS)) = 0xFFFF;
                    }
                }
            }
            DPRINTF("PING received. Answering with: %d\n", *((volatile uint16_t *)(memory_shared_address + GEMDRVEMUL_PING_STATUS)));
            write_random_token(memory_shared_address);
            active_command_id = 0xFFFF;
            break;
        }
        case GEMDRVEMUL_SHOW_VECTOR_CALL:
        {
            uint16_t trap_call = (uint16_t)payloadPtr[0];
            bool isBlacklisted = false;
            for (int i = 0; i < sizeof(BLACKLISTED_GEMDOS_CALLS); i++)
            {
                if (trap_call == BLACKLISTED_GEMDOS_CALLS[i])
                {
                    isBlacklisted = true; // Found the call in the blacklist
                    break;
                }
            }
            // if (!isBlacklisted)
            // {
            // If the call is not blacklisted, print its information
            DPRINTF("GEMDOS CALL: %s (%x)\n", GEMDOS_CALLS[trap_call], trap_call);
            // }
            write_random_token(memory_shared_address);
            active_command_id = 0xFFFF;
            break;
        }
        case GEMDRVEMUL_SET_SHARED_VAR:
        {
            // Shared variables
            uint32_t shared_variable_index = ((uint32_t)payloadPtr[1] << 16) | payloadPtr[0]; // d3 register
            payloadPtr += 2;                                                                  // Skip two words
            uint32_t shared_variable_value = ((uint32_t)payloadPtr[1] << 16) | payloadPtr[0]; // d4 register
            set_shared_var(shared_variable_index, shared_variable_value, memory_shared_address);
            write_random_token(memory_shared_address);
            active_command_id = 0xFFFF;
            break;
        }
        case GEMDRVEMUL_DGETDRV_CALL:
        {
            // Get the drive letter
            uint16_t dgetdrive_value = (uint16_t)payloadPtr[0];
            write_random_token(memory_shared_address);
            active_command_id = 0xFFFF;
            break;
        }
        case GEMDRVEMUL_REENTRY_LOCK:
        {
            *((volatile uint16_t *)(memory_shared_address + GEMDRVEMUL_REENTRY_TRAP)) = 0xFFFF;
            write_random_token(memory_shared_address);
            active_command_id = 0xFFFF;
            break;
        }
        case GEMDRVEMUL_REENTRY_UNLOCK:
        {
            *((volatile uint16_t *)(memory_shared_address + GEMDRVEMUL_REENTRY_TRAP)) = 0x0;
            write_random_token(memory_shared_address);
            active_command_id = 0xFFFF;
            break;
        }
        case GEMDRVEMUL_DFREE_CALL:
        {
            uint32_t dfree_unit = ((uint32_t)payloadPtr[1] << 16) | payloadPtr[0];
            // Check the free space
            DWORD fre_clust;
            FATFS *fs;
            FRESULT fr;
            // Get free space
            fr = f_getfree(hd_folder, &fre_clust, &fs);
            if (fr != FR_OK)
            {
                *((volatile uint32_t *)(memory_shared_address + GEMDRVEMUL_DFREE_STATUS)) = GEMDOS_ERROR;
            }
            else
            {
                // Calculate the total number of free bytes
                uint64_t freeBytes = fre_clust * fs->csize * NUM_BYTES_PER_SECTOR;
                DPRINTF("Total clusters: %d, free clusters: %d, bytes per sector: %d, sectors per cluster: %d\n", fs->n_fatent - 2, fre_clust, NUM_BYTES_PER_SECTOR, fs->csize);
                set_and_swap_longword(memory_shared_address + GEMDRVEMUL_DFREE_STRUCT, fre_clust);
                set_and_swap_longword(memory_shared_address + GEMDRVEMUL_DFREE_STRUCT + 4, fs->n_fatent - 2);
                set_and_swap_longword(memory_shared_address + GEMDRVEMUL_DFREE_STRUCT + 8, NUM_BYTES_PER_SECTOR);
                set_and_swap_longword(memory_shared_address + GEMDRVEMUL_DFREE_STRUCT + 12, fs->csize);
                *((volatile uint32_t *)(memory_shared_address + GEMDRVEMUL_DFREE_STATUS)) = GEMDOS_EOK;
            }
            write_random_token(memory_shared_address);
            active_command_id = 0xFFFF;
        }
        case GEMDRVEMUL_DGETPATH_CALL:
        {
            uint16_t dpath_drive = payloadPtr[0]; // d3 register

            DPRINTF("Dpath drive: %x\n", dpath_drive);
            // Copy the content of the path variable to memory_shared_address + GEMDRVEMUL_DEFAULT_PATH
            for (int i = 0; i < MAX_FOLDER_LENGTH; i++)
            {
                *((volatile uint8_t *)(memory_shared_address + GEMDRVEMUL_DEFAULT_PATH + i)) = dpath_string[i];
            }
            // Swap the bytes
            swap_words((uint16_t *)(memory_shared_address + GEMDRVEMUL_DEFAULT_PATH), MAX_FOLDER_LENGTH);
            write_random_token(memory_shared_address);
            active_command_id = 0xFFFF;
            break;
        }
        case GEMDRVEMUL_DSETPATH_CALL:
        {
            payloadPtr += 6; // Skip six words
            // Obtain the fname string and keep it in memory
            char *origin = (char *)payloadPtr;
            char dpath_tmp[MAX_FOLDER_LENGTH] = {};
            for (int i = 0; i < 64; i += 2)
            {
                dpath_tmp[i] = (char)*(origin + i + 1);
                dpath_tmp[i + 1] = (char)*(origin + i);
            }
            DPRINTF("Default path string: %s\n", dpath_tmp);
            // Check if the directory exists
            char tmp_path[MAX_FOLDER_LENGTH] = {0};

            if (dpath_tmp[0] == drive_letter)
            {
                DPRINTF("Drive letter found: %c. Removing it.\n", drive_letter);
                // Remove the drive letter and the colon from the path
                memmove(dpath_tmp, dpath_tmp + 2, strlen(dpath_tmp));
            }

            if (dpath_tmp[0] != '\\')
            {
                // Concatenate the path with the existing dpath_string
                DPRINTF("Dpath string: %s\n", dpath_string);
                DPRINTF("Dpath tmp: %s\n", dpath_tmp);
                char tmp_path_concat[MAX_FOLDER_LENGTH] = {0};
                snprintf(tmp_path_concat, sizeof(tmp_path_concat), "%s/%s", dpath_string, dpath_tmp);
                DPRINTF("Concatenated path: %s\n", tmp_path_concat);
                strcpy(dpath_tmp, tmp_path_concat);
                DPRINTF("Dpath tmp: %s\n", dpath_tmp);
            }
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
            write_random_token(memory_shared_address);
            active_command_id = 0xFFFF;
            break;
        }
        case GEMDRVEMUL_DCREATE_CALL:
        {
            // Obtain the pathname string and keep it in memory
            // concatenated with the local harddisk folder and the default path (if any)
            payloadPtr += 6; // Skip six words
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
            write_random_token(memory_shared_address);
            active_command_id = 0xFFFF;
            break;
        }
        case GEMDRVEMUL_DDELETE_CALL:
        {
            // Obtain the pathname string and keep it in memory
            // concatenated with the local harddisk folder and the default path (if any)
            payloadPtr += 6; // Skip six words
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
                        DPRINTF("ERROR: Internal error: %d\n", fr);
                        *((volatile uint16_t *)(memory_shared_address + GEMDRVEMUL_DDELETE_STATUS)) = GEMDOS_EINTRN;
                    }
                }
                else
                {
                    DPRINTF("Folder deleted\n");
                    *((volatile uint16_t *)(memory_shared_address + GEMDRVEMUL_DDELETE_STATUS)) = GEMDOS_EOK;
                }
            }
            write_random_token(memory_shared_address);
            active_command_id = 0xFFFF;
            break;
        }
        case GEMDRVEMUL_FSETDTA_CALL:
        {
            uint32_t ndta = ((uint32_t)payloadPtr[1] << 16) | payloadPtr[0];
            bool ndta_exists = lookupDTA(ndta);
            if (ndta_exists)
            {
                // We don't release the DTA if it already exists. Wait for FsFirst to do it
                DPRINTF("DTA at %x already exists.\n", ndta);
            }
            else
            {
                DTA data = {"filename", 0, 0, 0, 0, 0, 0, 0, 0, "filename"};
                insertDTA(ndta, data, NULL, NULL);
                DPRINTF("Added ndta: %x.\n", ndta);
            }
            write_random_token(memory_shared_address);
            active_command_id = 0xFFFF;
            break;
        }
        case GEMDRVEMUL_DTA_EXIST_CALL:
        {
            uint32_t ndta = ((uint32_t)payloadPtr[1] << 16) | payloadPtr[0];
            bool ndta_exists = lookupDTA(ndta);
            DPRINTF("DTA exists: %s\n", (ndta_exists) ? "TRUE" : "FALSE");
            set_and_swap_longword(memory_shared_address + GEMDRVEMUL_DTA_EXIST, (ndta_exists ? ndta : 0));
            write_random_token(memory_shared_address);
            active_command_id = 0xFFFF;
            break;
        }
        case GEMDRVEMUL_DTA_RELEASE_CALL:
        {
            uint32_t ndta = ((uint32_t)payloadPtr[1] << 16) | payloadPtr[0];
            DPRINTF("Releasing DTA: %x\n", ndta);
            DTANode *dtaNode = lookupDTA(ndta);
            if (dtaNode != NULL)
            {
                releaseDTA(ndta);
                DPRINTF("Existing DTA at %x released. DTA table elements: %d\n", ndta, countDTA());
            }
            for (uint8_t i = 0; i < DTA_SIZE_ON_ST; i += 1)
            {
                *((volatile uint8_t *)(memory_shared_address + GEMDRVEMUL_DTA_TRANSFER + i)) = 0;
            }
            set_and_swap_longword(memory_shared_address + GEMDRVEMUL_DTA_RELEASE, countDTA());
            write_random_token(memory_shared_address);
            active_command_id = 0xFFFF;
            break;
        }
        case GEMDRVEMUL_FSFIRST_CALL:
        {
            uint32_t ndta = ((uint32_t)payloadPtr[1] << 16) | payloadPtr[0];  // d3 register
            payloadPtr += 2;                                                  // Skip two words
            uint32_t attribs = payloadPtr[0];                                 // d4 register
            payloadPtr += 2;                                                  // Skip two words
            uint32_t fspec = ((uint32_t)payloadPtr[1] << 16) | payloadPtr[0]; // d5 register
            payloadPtr += 2;                                                  // Skip two words
            char attribs_str[7] = "";
            char internal_path[MAX_FOLDER_LENGTH * 2] = {0};
            char pattern[32] = {0};
            char fspec_string[MAX_FOLDER_LENGTH] = {0};
            swap_string_endiannes((char *)payloadPtr, fspec_string);
            DPRINTF("Fspec string: %s\n", fspec_string);
            get_attribs_st_str(attribs_str, attribs);
            seach_path_2_st(fspec_string, internal_path, pattern);
            DPRINTF("Fsfirst ndta: %x, attribs: %s, fspec: %x, fspec string: %s\n", ndta, attribs_str, fspec, fspec_string);
            DPRINTF("Fsfirst Full internal path: %s, filename pattern: %s\n", internal_path, pattern);

            bool ndta_exists = lookupDTA(ndta) ? true : false;

            FRESULT fr;   /* Return value */
            DIR *dj;      /* Directory object */
            FILINFO *fno; /* File information */
            dj = (DIR *)malloc(sizeof(DIR));
            fno = (FILINFO *)malloc(sizeof(FILINFO));

            char raw_filename[2] = "._";
            fr = FR_OK;
            bool first_time = true;
            while (fr == FR_OK && (raw_filename[0] == '.' || raw_filename[1] == '_'))
            {
                if (first_time)
                {
                    first_time = false;
                    fr = f_findfirst(dj, fno, internal_path, pattern);
                }
                else
                {
                    fr = f_findnext(dj, fno);
                }
                if (fno->fname[0])
                {
                    if (fr == FR_OK)
                    {
                        raw_filename[0] = fno->fname[0];
                        raw_filename[1] = fno->fname[1];
                    }
                }
                else
                {
                    raw_filename[0] = 'x'; // Force exit, no more elements
                    raw_filename[1] = 'x'; // Force exit, no more elements
                }
            }

            if (fr == FR_OK && fno->fname[0])
            {
                uint8_t attribs_conv_st = attribs_fat2st(fno->fattrib);
                if (!(attribs & FS_ST_LABEL))
                {
                    attribs |= FS_ST_ARCH;
                }
                char attribs_str[7] = "";
                get_attribs_st_str(attribs_str, attribs_conv_st);
                char shorten_filename[14];
                char upper_filename[14];
                char filtered_filename[14];
                filter_fname(fno->fname, filtered_filename);
                upper_fname(filtered_filename, upper_filename);
                shorten_fname(upper_filename, shorten_filename);

                strcpy(fno->fname, shorten_filename);

                // Filter out elements that do not match the attributes
                if (attribs_conv_st & attribs)
                {
                    DPRINTF("Found: %s, attr: %s\n", fno->fname, attribs_str);
                    if (ndta_exists)
                    {
                        releaseDTA(ndta);
                        DPRINTF("Existing DTA at %x released. DTA table elements: %d\n", ndta, countDTA());
                        for (uint8_t i = 0; i < DTA_SIZE_ON_ST; i += 1)
                        {
                            *((volatile uint8_t *)(memory_shared_address + GEMDRVEMUL_DTA_TRANSFER + i)) = 0;
                        }
                    }
                    DTA data = {"filename.typ", 0, 0, 0, 0, 0, 0, 0, 0, "filename.typ"};
                    insertDTA(ndta, data, dj, fno);
                    // Populate the DTA with the first file found
                    populate_dta(memory_shared_address, ndta, GEMDOS_EFILNF);
                }
                else
                {
                    DPRINTF("Skipped: %s, attr: %s\n", fno->fname, attribs_str);
                    int16_t error_code = GEMDOS_EFILNF;
                    DPRINTF("DTA at %x showing error code: %x\n", ndta, error_code);
                    *((volatile uint16_t *)(memory_shared_address + GEMDRVEMUL_DTA_F_FOUND)) = error_code;
                    if (ndta_exists)
                    {
                        releaseDTA(ndta);
                        DPRINTF("Existing DTA at %x released. DTA table elements: %d\n", ndta, countDTA());
                        for (uint8_t i = 0; i < DTA_SIZE_ON_ST; i += 1)
                        {
                            *((volatile uint8_t *)(memory_shared_address + GEMDRVEMUL_DTA_TRANSFER + i)) = 0;
                        }
                    }
                }
            }
            else
            {
                DPRINTF("Nothing returned from Fsfirst\n");
                int16_t error_code = GEMDOS_EFILNF;
                DPRINTF("DTA at %x showing error code: %x\n", ndta, error_code);
                *((volatile uint16_t *)(memory_shared_address + GEMDRVEMUL_DTA_F_FOUND)) = error_code;
                for (uint8_t i = 0; i < DTA_SIZE_ON_ST; i += 1)
                {
                    *((volatile uint8_t *)(memory_shared_address + GEMDRVEMUL_DTA_TRANSFER + i)) = 0;
                }
                if (ndta_exists)
                {
                    releaseDTA(ndta);
                    DPRINTF("Existing DTA at %x released. DTA table elements: %d\n", ndta, countDTA());
                }
            }
            write_random_token(memory_shared_address);
            active_command_id = 0xFFFF;
            break;
        }
        case GEMDRVEMUL_FSNEXT_CALL:
        {
            uint32_t ndta = ((uint32_t)payloadPtr[1] << 16) | payloadPtr[0]; // d3 register
            DPRINTF("Fsnext ndta: %x\n", ndta);

            FRESULT fr; /* Return value */
            DTANode *dtaNode = lookupDTA(ndta);

            bool ndta_exists = dtaNode ? true : false;
            if (dtaNode != NULL && dtaNode->dj != NULL && dtaNode->fno != NULL && ndta_exists)
            {
                // We need to filter out the elements that does not make sense in the FsFat environment
                // And in the Atari ST environment
                char raw_filename[2] = "._";
                fr = FR_OK;
                while (fr == FR_OK && (raw_filename[0] == '.' || raw_filename[1] == '_'))
                {
                    fr = f_findnext(dtaNode->dj, dtaNode->fno);
                    if (dtaNode->fno->fname[0])
                    {
                        if (fr == FR_OK)
                        {
                            raw_filename[0] = dtaNode->fno->fname[0];
                            raw_filename[1] = dtaNode->fno->fname[1];
                        }
                    }
                    else
                    {
                        raw_filename[0] = 'X'; // Force exit, no more elements
                        raw_filename[1] = 'X'; // Force exit, no more elements
                    }
                }
                if (fr == FR_OK && dtaNode->fno->fname[0])
                {
                    char shorten_filename[14];
                    char upper_filename[14];
                    char filtered_filename[14];
                    filter_fname(dtaNode->fno->fname, filtered_filename);
                    upper_fname(filtered_filename, upper_filename);
                    shorten_fname(upper_filename, shorten_filename);
                    strcpy(dtaNode->fno->fname, shorten_filename);

                    uint8_t attribs = dtaNode->fno->fattrib;
                    uint8_t attribs_conv_st = attribs_fat2st(attribs);
                    if (!(attribs & (FS_ST_LABEL | FS_ST_FOLDER)))
                    {
                        attribs |= FS_ST_ARCH;
                    }
                    char attribs_str[7] = "";
                    get_attribs_st_str(attribs_str, attribs_conv_st);
                    DPRINTF("Found: %s, attr: %s\n", dtaNode->fno->fname, attribs_str);
                    // Populate the DTA with the next file found
                    populate_dta(memory_shared_address, ndta, GEMDOS_ENMFIL);
                }
                else
                {
                    DPRINTF("Nothing found\n");
                    int16_t error_code = GEMDOS_ENMFIL;
                    DPRINTF("DTA at %x showing error code: %x\n", ndta, error_code);
                    *((volatile uint16_t *)(memory_shared_address + GEMDRVEMUL_DTA_F_FOUND)) = error_code;
                    if (ndta_exists)
                    {
                        releaseDTA(ndta);
                        DPRINTF("Existing DTA at %x released. DTA table elements: %d\n", ndta, countDTA());
                    }
                    for (uint8_t i = 0; i < DTA_SIZE_ON_ST; i += 1)
                    {
                        *((volatile uint8_t *)(memory_shared_address + GEMDRVEMUL_DTA_TRANSFER + i)) = 0;
                    }
                }
            }
            else
            {
                DPRINTF("FsFirst not initalized\n");
                int16_t error_code = GEMDOS_EINTRN;
                DPRINTF("DTA at %x showing error code: %x\n", ndta, error_code);
                *((volatile uint16_t *)(memory_shared_address + GEMDRVEMUL_DTA_F_FOUND)) = error_code;
                if (ndta_exists)
                {
                    releaseDTA(ndta);
                    DPRINTF("Existing DTA at %x released. DTA table elements: %d\n", ndta, countDTA());
                }
                for (uint8_t i = 0; i < DTA_SIZE_ON_ST; i += 1)
                {
                    *((volatile uint8_t *)(memory_shared_address + GEMDRVEMUL_DTA_TRANSFER + i)) = 0;
                }
            }
            write_random_token(memory_shared_address);
            active_command_id = 0xFFFF;
            break;
        }
        case GEMDRVEMUL_FOPEN_CALL:
        {
            uint16_t fopen_mode = payloadPtr[0]; // d3 register
            payloadPtr += 6;                     // Skip six words
            // Obtain the fname string and keep it in memory
            // concatenated path and filename
            char tmp_filepath[MAX_FOLDER_LENGTH] = {0};
            get_local_full_pathname(tmp_filepath);
            DPRINTF("Opening file: %s with mode: %x\n", tmp_filepath, fopen_mode);
            // Convert the fopen_mode to FatFs mode
            DPRINTF("Fopen mode: %x\n", fopen_mode);
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
                    DPRINTF("Opening file with new file descriptor: %d\n", fd_counter);
                    FileDescriptors *new_fd = add_file(&fdescriptors, tmp_filepath, file_object, fd_counter);
                    if (new_fd == NULL)
                    {
                        DPRINTF("ERROR: Could not add file to the list of open files\n");
                        *((volatile uint16_t *)(memory_shared_address + GEMDRVEMUL_FOPEN_HANDLE)) = GEMDOS_EINTRN;
                    }
                    else
                    {
                        DPRINTF("File opened with file descriptor: %d\n", fd_counter);
                        // Return the file descriptor
                        *((volatile uint16_t *)(memory_shared_address + GEMDRVEMUL_FOPEN_HANDLE)) = fd_counter;
                    }
                }
            }
            write_random_token(memory_shared_address);
            active_command_id = 0xFFFF;
            break;
        }
        case GEMDRVEMUL_FCLOSE_CALL:
        {
            uint16_t fclose_fd = payloadPtr[0]; // d3 register
            DPRINTF("Closing file with fd: %x\n", fclose_fd);
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
            write_random_token(memory_shared_address);
            active_command_id = 0xFFFF;
            break;
        }

        case GEMDRVEMUL_FCREATE_CALL:
        {
            fcreate_mode = payloadPtr[0]; // d3 register
            payloadPtr += 6;              // Skip six words
            // Obtain the fname string and keep it in memory
            // concatenated path and filename
            char tmp_filepath[MAX_FOLDER_LENGTH] = {0};
            get_local_full_pathname(tmp_filepath);
            DPRINTF("Creating file: %s\n with mode: %x", tmp_filepath, fcreate_mode);

            // CREATE ALWAYS MODE
            BYTE fatfs_create_mode = FA_READ | FA_WRITE | FA_CREATE_ALWAYS;
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

                // MISSING ATTRIBUTE MODIFICATION

                // Return the file descriptor
                *((volatile uint32_t *)(memory_shared_address + GEMDRVEMUL_FCREATE_HANDLE)) = SWAP_LONGWORD(fd_counter);
            }

            write_random_token(memory_shared_address);
            active_command_id = 0xFFFF;
            break;
        }
        case GEMDRVEMUL_FDELETE_CALL:
        {
            payloadPtr += 6; // Skip six words
            // Obtain the fname string and keep it in memory
            // concatenated path and filename
            char tmp_filepath[MAX_FOLDER_LENGTH] = {0};
            get_local_full_pathname(tmp_filepath);
            DPRINTF("Deleting file: %s\n", tmp_filepath);
            // Delete the file
            fr = f_unlink(tmp_filepath);
            uint32_t status;
            if (fr != FR_OK)
            {
                DPRINTF("ERROR: Could not delete file (%d)\r\n", fr);
                if (fr == FR_DENIED)
                {
                    DPRINTF("ERROR: Not enough permissions to delete file\n");
                    status = GEMDOS_EACCDN;
                }
                else if (fr == FR_NO_PATH)
                {
                    DPRINTF("ERROR: Folder does not exist\n");
                    status = GEMDOS_EPTHNF;
                }
                else
                {
                    DPRINTF("ERROR: Internal error\n");
                    status = GEMDOS_EINTRN;
                }
            }
            else
            {
                DPRINTF("File deleted\n");
                status = GEMDOS_EOK;
            }
            *((volatile uint32_t *)(memory_shared_address + GEMDRVEMUL_FDELETE_STATUS)) = SWAP_LONGWORD(status);

            write_random_token(memory_shared_address);
            active_command_id = 0xFFFF;
            break;
        }
        case GEMDRVEMUL_FSEEK_CALL:
        {
            uint16_t fseek_fd = payloadPtr[0];                                       // d3 register
            payloadPtr += 2;                                                         // Skip two words
            uint32_t fseek_offset = ((uint32_t)payloadPtr[1] << 16) | payloadPtr[0]; // d4 register
            payloadPtr += 2;                                                         // Skip two words
            uint16_t fseek_mode = payloadPtr[0];                                     // d5 register
            DPRINTF("Fseek in the file with fd: %x, offset: %x, mode: %x\n", fseek_fd, fseek_offset, fseek_mode);
            // Obtain the file descriptor
            FileDescriptors *file = get_file_by_fdesc(fdescriptors, fseek_fd);
            if (file == NULL)
            {
                DPRINTF("ERROR: File descriptor not found\n");
                set_and_swap_longword(memory_shared_address + GEMDRVEMUL_FSEEK_STATUS, GEMDOS_EIHNDL);
            }
            else
            {
                switch (fseek_mode)
                {
                case 0: // SEEK_SET 0 offset specifies the positive number of bytes from the beginning of the file
                    file->offset = fseek_offset;
                    break;
                case 1: // SEEK_CUR 1 offset specifies offset specifies the negative or positive number of bytes from the current file position
                    file->offset += fseek_offset;
                    if (file->offset < 0)
                    {
                        file->offset = 0;
                    }
                    break;
                case 2: // SEEK_END 2 offset specifies the negative number of bytes from the end of the file
                    // Get file size
                    file->offset = f_size(&(file->fobject)) + fseek_offset;
                    if (file->offset < 0)
                    {
                        file->offset = 0;
                    }
                    break;
                }
                // We don't really need to do the lseek here, because it will be performed in the read operation
                if (fr != FR_OK)
                {
                    DPRINTF("ERROR: Could not seek file (%d)\r\n", fr);
                    set_and_swap_longword(memory_shared_address + GEMDRVEMUL_FSEEK_STATUS, GEMDOS_EINTRN);
                }
                else
                {
                    DPRINTF("File seeked\n");
                    set_and_swap_longword(memory_shared_address + GEMDRVEMUL_FSEEK_STATUS, file->offset);
                }
            }
            write_random_token(memory_shared_address);
            active_command_id = 0xFFFF;
            break;
        }
        case GEMDRVEMUL_FATTRIB_CALL:
        {
            uint16_t fattrib_flag = payloadPtr[0]; // d3 register
            payloadPtr += 2;                       // Skip two words
            // Obtain the new attributes, if FATTRIB_SET is set
            uint16_t fattrib_new = payloadPtr[0]; // d4 register
            payloadPtr += 4;                      // Skip four words
            // Obtain the fname string and keep it in memory
            // concatenated path and filename
            char tmp_filepath[MAX_FOLDER_LENGTH] = {0};
            get_local_full_pathname(tmp_filepath);
            DPRINTF("Fattrib flag: %x, new attributes: %x\n", fattrib_flag, fattrib_new);
            DPRINTF("Getting attributes of file: %s\n", tmp_filepath);

            // Get the attributes of the file
            FILINFO fno;
            fr = f_stat(tmp_filepath, &fno);
            if (fr != FR_OK)
            {
                DPRINTF("ERROR: Could not get file attributes (%d)\r\n", fr);
                set_and_swap_longword(memory_shared_address + GEMDRVEMUL_FATTRIB_STATUS, GEMDOS_EFILNF);
            }
            else
            {
                uint32_t fattrib_st = attribs_fat2st(fno.fattrib);
                set_and_swap_longword(memory_shared_address + GEMDRVEMUL_FATTRIB_STATUS, fattrib_st);
                char fattrib_st_str[7] = "";
                get_attribs_st_str(fattrib_st_str, fattrib_st);
                if (fattrib_flag == FATTRIB_INQUIRE)
                {
                    DPRINTF("File attributes: %s\n", fattrib_st_str);
                }
                else
                {
                    // WE will assume here FATTRIB_SET
                    // Set the attributes of the file
                    char fattrib_st_str[7] = "";
                    get_attribs_st_str(fattrib_st_str, fattrib_new);
                    DPRINTF("New file attributes: %s\n", fattrib_st_str);
                    BYTE fattrib_fatfs_new = (BYTE)attribs_st2fat(fattrib_new);
                    fr = f_chmod(tmp_filepath, fattrib_fatfs_new, AM_RDO | AM_HID | AM_SYS);
                    if (fr != FR_OK)
                    {
                        DPRINTF("ERROR: Could not set file attributes (%d)\r\n", fr);
                        set_and_swap_longword(memory_shared_address + GEMDRVEMUL_FATTRIB_STATUS, GEMDOS_EACCDN);
                    }
                }
            }
            write_random_token(memory_shared_address);
            active_command_id = 0xFFFF;
            break;
        }
        case GEMDRVEMUL_FRENAME_CALL:
        {
            payloadPtr += 6; // Skip six words
            // Obtain the src name from the payload
            char *origin = (char *)payloadPtr;
            char frename_fname_src[MAX_FOLDER_LENGTH] = {0};
            char frename_fname_dst[MAX_FOLDER_LENGTH] = {0};
            for (int i = 0; i < MAX_FOLDER_LENGTH; i += 2)
            {
                frename_fname_src[i] = (char)*(origin + i + 1);
                frename_fname_src[i + 1] = (char)*(origin + i);
            }
            for (int i = MAX_FOLDER_LENGTH; i < MAX_FOLDER_LENGTH * 2; i += 2)
            {
                frename_fname_dst[i - MAX_FOLDER_LENGTH] = (char)*(origin + i + 1);
                frename_fname_dst[i - MAX_FOLDER_LENGTH + 1] = (char)*(origin + i);
            }
            // DPRINTF("Renaming file: %s to %s\n", frename_fname_src, frename_fname_dst);
            // get_local_full_pathname(frename_fname_src);
            // get_local_full_pathname(frename_fname_dst);
            // DPRINTF("Renaming file: %s to %s\n", frename_fname_src, frename_fname_dst);

            char drive_src[3] = {0};
            char folders_src[MAX_FOLDER_LENGTH] = {0};
            char filePattern_src[MAX_FOLDER_LENGTH] = {0};
            char drive_dst[3] = {0};
            char folders_dst[MAX_FOLDER_LENGTH] = {0};
            char filePattern_dst[MAX_FOLDER_LENGTH] = {0};
            split_fullpath(frename_fname_src, drive_src, folders_src, filePattern_src);
            DPRINTF("Drive: %s, Folders: %s, FilePattern: %s\n", drive_src, folders_src, filePattern_src);
            split_fullpath(frename_fname_dst, drive_dst, folders_dst, filePattern_dst);
            DPRINTF("Drive: %s, Folders: %s, FilePattern: %s\n", drive_dst, folders_dst, filePattern_dst);

            if (strcasecmp(drive_src, drive_dst) != 0)
            {
                DPRINTF("ERROR: Different drives\n");
                *((volatile uint32_t *)(memory_shared_address + GEMDRVEMUL_FRENAME_STATUS)) = SWAP_LONGWORD(GEMDOS_EPTHNF);
            }
            else if (strcasecmp(folders_src, folders_dst) != 0)
            {
                DPRINTF("ERROR: Different folders\n");
                *((volatile uint32_t *)(memory_shared_address + GEMDRVEMUL_FRENAME_STATUS)) = SWAP_LONGWORD(GEMDOS_EPTHNF);
            }
            else
            {
                DPRINTF("Renaming file: %s to %s\n", frename_fname_src, frename_fname_dst);
                get_local_full_pathname(frename_fname_src);
                payloadPtr += MAX_FOLDER_LENGTH / 2; // MAX_FOLDER_LENGTH * 2 bytes per uint16_t
                get_local_full_pathname(frename_fname_dst);
                DPRINTF("Renaming file: %s to %s\n", frename_fname_src, frename_fname_dst);
                // Rename the file
                fr = f_rename(frename_fname_src, frename_fname_dst);
                if (fr != FR_OK)
                {
                    DPRINTF("ERROR: Could not rename file (%d)\r\n", fr);
                    if (fr == FR_DENIED)
                    {
                        DPRINTF("ERROR: Not enough premissions to rename file\n");
                        *((volatile uint32_t *)(memory_shared_address + GEMDRVEMUL_FRENAME_STATUS)) = SWAP_LONGWORD(GEMDOS_EACCDN);
                    }
                    else if (fr == FR_NO_PATH)
                    {
                        DPRINTF("ERROR: Folder does not exist\n");
                        *((volatile uint32_t *)(memory_shared_address + GEMDRVEMUL_FRENAME_STATUS)) = SWAP_LONGWORD(GEMDOS_EPTHNF);
                    }
                    else
                    {
                        DPRINTF("ERROR: Internal error\n");
                        *((volatile uint32_t *)(memory_shared_address + GEMDRVEMUL_FRENAME_STATUS)) = SWAP_LONGWORD(GEMDOS_EINTRN);
                    }
                }
                else
                {
                    DPRINTF("File renamed\n");
                    *((volatile uint32_t *)(memory_shared_address + GEMDRVEMUL_FRENAME_STATUS)) = GEMDOS_EOK;
                }
            }

            write_random_token(memory_shared_address);
            active_command_id = 0xFFFF;
            break;
        }
        case GEMDRVEMUL_FDATETIME_CALL:
        {
            uint16_t fdatetime_flag = payloadPtr[0]; // d3 register
            payloadPtr += 2;                         // Skip two words
            // Obtain the file descriptor to change the date and time
            uint16_t fdatetime_fd = payloadPtr[0]; // d4 register
            payloadPtr += 4;                       // Skip four words

            FileDescriptors *fd = get_file_by_fdesc(fdescriptors, fdatetime_fd);
            if (fd == NULL)
            {
                DPRINTF("ERROR: File descriptor not found\n");
                set_and_swap_longword(memory_shared_address + GEMDRVEMUL_FDATETIME_STATUS, GEMDOS_EIHNDL);
                set_and_swap_longword(memory_shared_address + GEMDRVEMUL_FDATETIME_DATE, 0);
                set_and_swap_longword(memory_shared_address + GEMDRVEMUL_FDATETIME_TIME, 0);
            }
            else
            {
                DPRINTF("Modify file date and time: %s fd: %d\n", fd->fpath, fdatetime_fd);
                if (fdatetime_flag == FDATETIME_INQUIRE)
                {
                    FILINFO fno;
                    FRESULT fr;
                    fr = f_stat(fd->fpath, &fno);
                    if (fr == FR_OK)
                    {
                        // File information is now in fno
                        // Convert the date and time
                        unsigned int year = (fno.fdate >> 9);
                        unsigned int month = (fno.fdate >> 5) & 0x0F;
                        unsigned int day = fno.fdate & 0x1F;

                        unsigned int hour = fno.ftime >> 11;
                        unsigned int minute = (fno.ftime >> 5) & 0x3F;
                        unsigned int second = (fno.ftime & 0x1F);

                        DPRINTF("Get file date and time: %02d:%02d:%02d %02d/%02d/%02d\n", hour, minute, second * 2, day, month, year + 1980);
                        set_and_swap_longword(memory_shared_address + GEMDRVEMUL_FDATETIME_STATUS, GEMDOS_EOK);
                        set_and_swap_longword(memory_shared_address + GEMDRVEMUL_FDATETIME_DATE, (year << 16) | (month << 8) | day);
                        set_and_swap_longword(memory_shared_address + GEMDRVEMUL_FDATETIME_TIME, (hour << 16) | (minute << 8) | second);
                    }
                    else
                    {
                        DPRINTF("ERROR: Could not get file date and time from file %s (%d)\r\n", fd->fpath, fr);
                        set_and_swap_longword(memory_shared_address + GEMDRVEMUL_FDATETIME_STATUS, GEMDOS_EFILNF);
                        set_and_swap_longword(memory_shared_address + GEMDRVEMUL_FDATETIME_DATE, 0);
                        set_and_swap_longword(memory_shared_address + GEMDRVEMUL_FDATETIME_TIME, 0);
                    }
                }
                else
                {
                    uint8_t hour = payloadPtr[2];
                    uint8_t minute = payloadPtr[4];
                    uint8_t second = payloadPtr[5];
                    uint8_t year = payloadPtr[1];
                    uint8_t month = payloadPtr[0];
                    uint8_t day = payloadPtr[3];
                    DPRINTF("Show in hex the values: %02x:%02x:%02x %02x/%02x/%02x\n", hour, minute, second, day, month, year);
                    DPRINTF("File date and time: %02d:%02d:%02d %02d/%02d/%02d\n", hour, minute, second * 2, day, month, year + 1980);

                    FILINFO fno;

                    fno.fdate = (WORD)(((year) * 512U) | month * 32U | day);
                    fno.ftime = (WORD)(hour * 2048U | minute * 32U | second);

                    // Shockinly I need to close the file to set the date and time!!!! WTF???
                    FRESULT fr = f_close(&fd->fobject);
                    fr = f_utime(fd->fpath, &fno);
                    // Remove the file from the list of open files
                    delete_file_by_fdesc(&fdescriptors, fdatetime_fd);
                    if (fr == FR_OK)
                    {
                        // File exists and date and time set
                        // So now we can return the status
                        DPRINTF("Set the file date and time: %02d:%02d:%02d %02d/%02d/%02d\n", hour, minute, second * 2, day, month, year + 1980);
                        set_and_swap_longword(memory_shared_address + GEMDRVEMUL_FDATETIME_STATUS, GEMDOS_EOK);
                        set_and_swap_longword(memory_shared_address + GEMDRVEMUL_FDATETIME_DATE, 0);
                        set_and_swap_longword(memory_shared_address + GEMDRVEMUL_FDATETIME_TIME, 0);
                    }
                    else
                    {
                        DPRINTF("ERROR: Could not set file date and time to file %s (%d)\r\n", fd->fpath, fr);
                        set_and_swap_longword(memory_shared_address + GEMDRVEMUL_FDATETIME_STATUS, GEMDOS_EFILNF);
                        set_and_swap_longword(memory_shared_address + GEMDRVEMUL_FDATETIME_DATE, 0);
                        set_and_swap_longword(memory_shared_address + GEMDRVEMUL_FDATETIME_TIME, 0);
                    }

                    // // Set the date and time of the file with the values read from the payload
                    // datetime_t rtc_time = {0};
                    // rtc_get_datetime(&rtc_time);
                    // // Ensure the values fit into the designated bit sizes
                    // uint16_t hours = rtc_time.hour & 0x1F;        // Take only the lowest 5 bits
                    // uint16_t minutes = rtc_time.min & 0x3F;       // Take only the lowest 6 bits
                    // uint16_t seconds = rtc_time.sec & 0x1F;       // Take only the lowest 5 bits
                    // uint16_t day = rtc_time.day & 0x1F;           // Take only the lowest 5 bits
                    // uint16_t month = rtc_time.month & 0xF;        // Take only the lowest 4 bits
                    // uint16_t year = rtc_time.year & 0x7FFF >> 16; // Take only the lowest 7 bits
                }
            }
            write_random_token(memory_shared_address);
            active_command_id = 0xFFFF;
            break;
        }

        case GEMDRVEMUL_READ_BUFF_CALL:
        {
            uint16_t readbuff_fd = payloadPtr[0];                                                      // d3 register
            payloadPtr += 2;                                                                           // Skip two words
            uint32_t readbuff_bytes_to_read = ((uint32_t)payloadPtr[1] << 16) | payloadPtr[0];         // d4 register constains the number of bytes to read
            payloadPtr += 2;                                                                           // Skip two words
            uint32_t readbuff_pending_bytes_to_read = ((uint32_t)payloadPtr[1] << 16) | payloadPtr[0]; // d5 register constains the number of bytes to read
            DPRINTF("Read buffering file with fd: x%x, bytes_to_read: x%08x, pending_bytes_to_read: x%08x\n", readbuff_fd, readbuff_bytes_to_read, readbuff_pending_bytes_to_read);
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
            write_random_token(memory_shared_address);
            active_command_id = 0xFFFF;
            break;
        }
        case GEMDRVEMUL_WRITE_BUFF_CALL:
        {
            uint16_t writebuff_fd = payloadPtr[0];                                                       // d3 register
            payloadPtr += 2;                                                                             // Skip two words
            uint32_t writebuff_bytes_to_write = ((uint32_t)payloadPtr[1] << 16) | payloadPtr[0];         // d4 register constains the number of bytes to write
            payloadPtr += 2;                                                                             // Skip two words
            uint32_t writebuff_pending_bytes_to_write = ((uint32_t)payloadPtr[1] << 16) | payloadPtr[0]; // d5 register constains the number of bytes to write
            payloadPtr += 2;
            DPRINTF("Write buffering file with fd: x%x, bytes_to_write: x%08x, pending_bytes_to_write: x%08x\n", writebuff_fd, writebuff_bytes_to_write, writebuff_pending_bytes_to_write);
            // Obtain the file descriptor
            FileDescriptors *file = get_file_by_fdesc(fdescriptors, writebuff_fd);
            if (file == NULL)
            {
                DPRINTF("ERROR: File descriptor not found\n");
                set_and_swap_longword(memory_shared_address + GEMDRVEMUL_WRITE_BYTES, GEMDOS_EIHNDL);
            }
            else
            {
                uint32_t writebuff_offset = file->offset;
                UINT bytes_write = 0;
                // Reposition the file pointer with FatFs
                fr = f_lseek(&file->fobject, writebuff_offset);
                if (fr != FR_OK)
                {
                    DPRINTF("ERROR: Could not change write offset of the file (%d)\r\n", fr);
                    set_and_swap_longword(memory_shared_address + GEMDRVEMUL_WRITE_BYTES, GEMDOS_EINTRN);
                }
                else
                {
                    // Only write DEFAULT_FWRITE_BUFFER_SIZE bytes at a time
                    uint16_t buff_size = writebuff_pending_bytes_to_write > DEFAULT_FWRITE_BUFFER_SIZE ? DEFAULT_FWRITE_BUFFER_SIZE : writebuff_pending_bytes_to_write;
                    // Transform buffer's words from little endian to big endian inline
                    uint16_t *target = payloadPtr;
                    // Change the endianness of the bytes read
                    swap_words(target, ((buff_size + 1) * 2) / 2);
                    // Write the bytes
                    DPRINTF("Write x%x bytes from the file at offset x%x\n", buff_size, writebuff_offset);
                    fr = f_write(&file->fobject, (void *)target, buff_size, &bytes_write);
                    if (fr != FR_OK)
                    {
                        DPRINTF("ERROR: Could not write file (%d)\r\n", fr);
                        set_and_swap_longword(memory_shared_address + GEMDRVEMUL_WRITE_BYTES, GEMDOS_EINTRN);
                    }
                    else
                    {
                        // Calculate the checksum of the buffer
                        uint8_t *target8 = (uint8_t *)target;
                        uint32_t chk = 0;
                        for (int i = 0; i < bytes_write; i++)
                        {
                            chk += target8[i];
                        }
                        DPRINTF("Checksum: x%x\n", chk);
                        set_and_swap_longword(memory_shared_address + GEMDRVEMUL_WRITE_CHK, chk);
                        set_and_swap_longword(memory_shared_address + GEMDRVEMUL_WRITE_BYTES, bytes_write);
                    }
                }
            }
            write_random_token(memory_shared_address);
            active_command_id = 0xFFFF;
            break;
        }
        case GEMDRVEMUL_WRITE_BUFF_CHECK:
        {
            uint16_t writebuff_fd = payloadPtr[0];                                              // d3 register
            payloadPtr += 2;                                                                    // Skip two words
            uint32_t writebuff_forward_bytes = ((uint32_t)payloadPtr[1] << 16) | payloadPtr[0]; // d4 register constains the number of bytes to forward the offset
            DPRINTF("Write buffering confirm fd: x%x, forward: x%08x\n", writebuff_fd, writebuff_forward_bytes);
            // Obtain the file descriptor
            FileDescriptors *file = get_file_by_fdesc(fdescriptors, writebuff_fd);
            if (file == NULL)
            {
                DPRINTF("ERROR: File descriptor not found\n");
                set_and_swap_longword(memory_shared_address + GEMDRVEMUL_WRITE_CONFIRM_STATUS, GEMDOS_EIHNDL);
            }
            else
            {
                // Update the offset of the file
                file->offset += writebuff_forward_bytes;
                uint32_t current_offset = file->offset;
                DPRINTF("New offset: x%x after writing x%x bytes\n", current_offset, writebuff_forward_bytes);
                set_and_swap_longword(memory_shared_address + GEMDRVEMUL_WRITE_CONFIRM_STATUS, GEMDOS_EOK);
            }
            write_random_token(memory_shared_address);
            active_command_id = 0xFFFF;
            break;
        }
        case GEMDRVEMUL_PEXEC_CALL:
        {
            uint16_t pexec_mode = payloadPtr[0];                                         // d3 register
            payloadPtr += 2;                                                             // Skip 2 words
            uint32_t pexec_stack_addr = ((uint32_t)payloadPtr[1] << 16) | payloadPtr[0]; // d4 register
            payloadPtr += 2;                                                             // Skip 2 words
            uint32_t pexec_fname = ((uint32_t)payloadPtr[1] << 16) | payloadPtr[0];      // d5 register
            payloadPtr += 2;                                                             // Skip 2 words
            uint32_t pexec_cmdline = ((uint32_t)payloadPtr[1] << 16) | payloadPtr[0];    // d6 register
            payloadPtr += 2;                                                             // Skip 2 words
            uint32_t pexec_envstr = ((uint32_t)payloadPtr[1] << 16) | payloadPtr[0];     // d7 register
            DPRINTF("Pexec mode: %x\n", pexec_mode);
            DPRINTF("Pexec stack addr: %x\n", pexec_stack_addr);
            DPRINTF("Pexec fname: %x\n", pexec_fname);
            DPRINTF("Pexec cmdline: %x\n", pexec_cmdline);
            DPRINTF("Pexec envstr: %x\n", pexec_envstr);
            *((volatile uint16_t *)(memory_shared_address + GEMDRVEMUL_PEXEC_MODE)) = pexec_mode;
            set_and_swap_longword(memory_shared_address + GEMDRVEMUL_PEXEC_STACK_ADDR, pexec_stack_addr);
            set_and_swap_longword(memory_shared_address + GEMDRVEMUL_PEXEC_FNAME, pexec_fname);
            set_and_swap_longword(memory_shared_address + GEMDRVEMUL_PEXEC_CMDLINE, pexec_cmdline);
            set_and_swap_longword(memory_shared_address + GEMDRVEMUL_PEXEC_ENVSTR, pexec_envstr);
            write_random_token(memory_shared_address);
            active_command_id = 0xFFFF;
            break;
        }
        case GEMDRVEMUL_SAVE_BASEPAGE:
        {
            payloadPtr += 6; // Skip eight words
            // Copy the from the shared memory the basepagea está to pexec_pd
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
            write_random_token(memory_shared_address);
            active_command_id = 0xFFFF;
            break;
        }
        case GEMDRVEMUL_SAVE_EXEC_HEADER:
        {
            payloadPtr += 6; // Skip eight words
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
            write_random_token(memory_shared_address);
            active_command_id = 0xFFFF;
            break;
        }
        default:
        {
            if (active_command_id != 0xFFFF)
            {
                DPRINTF("ERROR: Unknown command: %x\n", active_command_id);
                uint32_t d3 = ((uint32_t)payloadPtr[1] << 16) | payloadPtr[0];
                DPRINTF("DEBUG: %x\n", d3);
                payloadPtr += 2;
                uint32_t d4 = ((uint32_t)payloadPtr[1] << 16) | payloadPtr[0];
                DPRINTF("DEBUG: %x\n", d4);
                payloadPtr += 2;
                uint32_t d5 = ((uint32_t)payloadPtr[1] << 16) | payloadPtr[0];
                DPRINTF("DEBUG: %x\n", d5);
                payloadPtr += 2;
                uint8_t *payloadShowBytesPtr = (uint8_t *)payloadPtr;
                print_payload(payloadShowBytesPtr);
                write_random_token(memory_shared_address);
                active_command_id = 0xFFFF;
                break;
            }
        }
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