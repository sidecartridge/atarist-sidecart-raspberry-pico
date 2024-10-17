/**
 * File: floppyemul.c
 * Author: Diego Parrilla Santamaría
 * Date: August 2023
 * Copyright: 2023 - GOODDATA LABS SL
 * Description: Load floppy images files from SD card
 */

#include "include/floppyemul.h"

static BPBData BpbData_A = {
    512,       /* recsize     */
    2,         /* clsiz       */
    1024,      /* clsizb      */
    8,         /* rdlen       */
    6,         /* fsiz        */
    7,         /* fatrec      */
    21,        /* datrec      */
    1015,      /* numcl       */
    0,         /* bflags      */
    0,         /* trackcnt    */
    0,         /* sidecnt     */
    0,         /* secpcyl     */
    0,         /* secptrack   */
    {0, 0, 0}, /* reserved  */
    0          /* disk_number */
};

static BPBData BpbData_B = {
    512,       /* recsize     */
    2,         /* clsiz       */
    1024,      /* clsizb      */
    8,         /* rdlen       */
    6,         /* fsiz        */
    7,         /* fatrec      */
    21,        /* datrec      */
    1015,      /* numcl       */
    0,         /* bflags      */
    0,         /* trackcnt    */
    0,         /* sidecnt     */
    0,         /* secpcyl     */
    0,         /* secptrack   */
    {0, 0, 0}, /* reserved  */
    1          /* disk_number */
};

static uint32_t memory_shared_address = 0;
static uint32_t memory_code_address = 0;
static uint16_t *payloadPtr = NULL;
static uint32_t random_token;
static uint32_t vector_call;
static ConnectionData connection_data = {};
static uint32_t flags = 0;

// Emulation modes:
// 0: No emulation (00)
// 1: Emulation mode A, Physical A becomes B (01)
// 2: Emulation mode B, Physical A no change (10)
// 3: Emulation mode A, Emulation mode B (11)

static uint16_t logical_sector = 0;
static uint16_t sector_size = 512;
static uint32_t disk_number = 0;

static DiskVectors disk_vectors = {
    .hdv_bpb_payload = 0,
    .hdv_rw_payload = 0,
    .hdv_mediach_payload = 0,
    .XBIOS_trap_payload = 0,
    .hdv_bpb_payload_set = false,
    .hdv_rw_payload_set = false,
    .hdv_mediach_payload_set = false,
    .XBIOS_trap_payload_set = false};

static HardwareType hardware_type = {
    .machine = 0,
    .start_function = 0,
    .end_function = 0};

static FloppyCatalog floppy_catalog = {
    .list = NULL,
    .size = 0};

/**
 * @brief Creates the BIOS Parameter Block (BPB) from the first sector of the floppy image.
 *
 * This function reads the first sector of the floppy image file and extracts the necessary information
 * to create the BPB. The BPB is a data structure used by the file system to store information about the disk.
 *
 * @param fsrc Pointer to the file object representing the floppy image file.
 * @param bpb Pointer to the BPBData structure to be populated.
 * @return FRESULT The result of the operation. FR_OK if successful, an error code otherwise.
 */
static FRESULT floppyemul_create_BPB(FIL *fsrc, BPBData *bpb)
{
    BYTE buffer[512] = {0}; /* File copy buffer */
    unsigned int br = 0;    /* File read/write count */
    FRESULT fr;

    DPRINTF("Creating BPB from first sector of floppy image\n");

    /* Set read/write pointer to logical sector position */
    fr = f_lseek(fsrc, 0);
    if (fr)
    {
        DPRINTF("ERROR: Could not seek to the start of the first sector to create BPB\n");
        f_close(fsrc);
        return fr; // Check for error in reading
    }

    fr = f_read(fsrc, buffer, sizeof buffer, &br); /* Read a chunk of data from the source file */
    if (fr)
    {
        DPRINTF("ERROR: Could not read the first boot sector to create the BPBP\n");
        f_close(fsrc);
        return fr; // Check for error in reading
    }

    BPBData bpb_tmp; // Temporary BPBData structure

    bpb_tmp.recsize = ((uint16_t)buffer[11]) | ((uint16_t)buffer[12] << 8);                                  // Sector size in bytes
    bpb_tmp.clsiz = (uint16_t)buffer[13];                                                                    // Cluster size
    bpb_tmp.clsizb = bpb_tmp.clsiz * bpb_tmp.recsize;                                                        // Cluster size in bytes
    bpb_tmp.rdlen = ((uint16_t)buffer[17] >> 4) | ((uint16_t)buffer[18] << 8);                               // Root directory length in sectors
    bpb_tmp.fsiz = (uint16_t)buffer[22];                                                                     // FAT size in sectors
    bpb_tmp.fatrec = bpb_tmp.fsiz + 1;                                                                       // Sector number of second FAT
    bpb_tmp.datrec = bpb_tmp.rdlen + bpb_tmp.fatrec + bpb_tmp.fsiz;                                          // Sector number of first data cluster
    bpb_tmp.numcl = ((((uint16_t)buffer[20] << 8) | (uint16_t)buffer[19]) - bpb_tmp.datrec) / bpb_tmp.clsiz; // Number of data clusters on the disk
    bpb_tmp.bflags = 0;                                                                                      // Magic flags
    bpb_tmp.trackcnt = 0;                                                                                    // Track count
    bpb_tmp.sidecnt = (uint16_t)buffer[26];                                                                  // Side count
    bpb_tmp.secpcyl = (uint16_t)(buffer[24] * bpb_tmp.sidecnt);                                              // Sectors per cylinder
    bpb_tmp.secptrack = (uint16_t)buffer[24];                                                                // Sectors per track
    bpb_tmp.reserved[0] = 0;                                                                                 // Reserved
    bpb_tmp.reserved[1] = 0;                                                                                 // Reserved
    bpb_tmp.reserved[2] = 0;                                                                                 // Reserved
                                                                                                             //    bpb_tmp.disk_number = disknumber;

    // Copy the temporary BPB data to the provided BPB structure
    *bpb = bpb_tmp;

    return FR_OK;
}

/**
 * @brief Opens a file on the floppy drive
 *
 * This function opens the specified file on the floppy drive and performs additional operations
 * such as enabling fast seek mode, creating a CLMT (Cluster Link Map Table), and retrieving the file size.
 *
 * @param fullpath The full path of the file to open.
 * @param floppy_read_write Specifies whether the file should be opened for both reading and writing.
 * @param error Pointer to a boolean variable indicating whether an error occurred during the operation.
 * @param fsrc Pointer to a FIL structure representing the opened file.
 * @return The result of the file open operation.
 */
static FRESULT floppyemul_open(const char *fullpath, bool floppy_read_write, FIL *fsrc)
{
    /* Open source file on the drive 0 */
    FRESULT fr = f_open(fsrc, fullpath, floppy_read_write ? FA_READ | FA_WRITE : FA_READ);
    if (fr)
    {
        DPRINTF("ERROR: Could not open file %s (%d)\r\n", fullpath, fr);
        return fr;
    }
    // Get file size
    uint32_t size = f_size(fsrc);
    fr = f_lseek(fsrc, size);
    if (fr)
    {
        DPRINTF("ERROR: Could not seek file %s (%d). Closing file.\r\n", fullpath, fr);
        f_close(fsrc);
        return fr;
    }
    fr = f_lseek(fsrc, 0);
    if (fr)
    {
        DPRINTF("ERROR: Could not seek file %s (%d). Closing file.\r\n", fullpath, fr);
        f_close(fsrc);
        return fr;
    }
    DPRINTF("File size of %s: %i bytes\n", fullpath, size);

    return FR_OK;
}

/**
 * @brief Closes an opened file on the floppy drive
 *
 * This function closes the specified file on the floppy drive and performs any necessary cleanup operations.
 *
 * @param fsrc Pointer to a FIL structure representing the opened file.
 * @return The result of the file close operation.
 */
static FRESULT floppyemul_close(FIL *fsrc)
{
    FRESULT fr = f_close(fsrc);
    if (fr)
    {
        DPRINTF("ERROR: Could not close file (%d)\r\n", fr);
        return fr;
    }

    DPRINTF("File successfully closed.\n");
    return FR_OK;
}

/**
 * @brief Copies the file names from a directory to a floppy catalog.
 *
 * This function retrieves the file names from a specified directory and copies them to a floppy catalog.
 * The files are filtered based on their extensions and only files with allowed extensions are considered.
 * The copied filenames are dynamically allocated and stored in the floppy catalog.
 *
 * @param dir The directory path from which to retrieve the files.
 * @param fs The FATFS structure representing the file system.
 * @param floppy_catalog The floppy catalog structure to store the copied filenames.
 */
static void floppyemul_filelist(const char *dir, FATFS *fs, FloppyCatalog *floppy_catalog)
{
    const char *allowed_extensions[] = {"st", "rw", NULL};
    int num_files = 0;
    char **files = NULL;
    bool success = get_dir_files(dir, allowed_extensions, &files, &num_files, fs);
    if (!success)
    {
        DPRINTF("ERROR: Could not get files from the floppy folder\n");
    }
    else
    {
        DPRINTF("Floppy folder: %s\n", dir);
        DPRINTF("Number of files: %d\n", num_files);

        // Copy the array char *floppy_catalog.list[] with the files in files
        // the filename is dynamic, so we have to copy it one by one
        // Let's iterate over the files and copy them to the floppy_catalog
        floppy_catalog->list = malloc(num_files * sizeof(char *));
        int MAX_FILENAME_HTTP = 48;
        for (int i = 0; i < num_files; i++)
        {
            size_t length = strlen(files[i]);
            if (length > MAX_FILENAME_HTTP - 1)
            {
                length = MAX_FILENAME_HTTP - 1;
            }
            floppy_catalog->list[i] = malloc(length + 1);
            strncpy(floppy_catalog->list[i], files[i], length);
            floppy_catalog->list[i][length] = '\0'; // Null-terminate the string
            DPRINTF("File %d: %s\n", i, floppy_catalog->list[i]);
        }
        release_memory_files(files, num_files);
        floppy_catalog->size = num_files;
    }
}
/**
 * @brief Selects a floppy disk image for a specific drive.
 *
 * This function is a CGI handler called when a floppy disk image is selected for a specific drive.
 * It retrieves the selected floppy disk image from the parameters and updates the corresponding
 * drive's floppy image parameter. It also sets the appropriate flag to indicate that the drive
 * is ready with the selected floppy disk image.
 *
 * @param iIndex The index of the CGI handler.
 * @param iNumParams The number of parameters passed to the CGI handler.
 * @param pcParam An array of parameter names.
 * @param pcValue An array of parameter values.
 * @param drv The drive identifier ('a' or 'b') for which the floppy disk image is selected.
 * @return The URL of the page to redirect to after the floppy disk image is selected.
 */
const char *cgi_floppy_select(int iIndex, int iNumParams, char *pcParam[], char *pcValue[], char drv)
{
    DPRINTF("cgi_floppy_select called with index %d\n", iIndex);
    for (size_t i = 0; i < iNumParams; i++)
    {
        /* check if parameter is "id" */
        if (strcmp(pcParam[i], "id") == 0)
        {
            DPRINTF("Floppy selected: %s\n", pcValue[i]);
            char *floppy_name = floppy_catalog.list[atoi(pcValue[i])];
            DPRINTF("Floppy name: %s\n", floppy_name);
            char *param = drv == 'a' ? PARAM_FLOPPY_IMAGE_A : PARAM_FLOPPY_IMAGE_B;
            put_string(param, floppy_name);
            SET_FLAG(drv == 'a' ? MOUNT_DRIVE_A_FLAG : MOUNT_DRIVE_B_FLAG);
            write_all_entries();
        }
    }
    return "/floppies_select.shtml";
}

const char *cgi_floppy_select_a(int iIndex, int iNumParams, char *pcParam[], char *pcValue[])
{
    return cgi_floppy_select(iIndex, iNumParams, pcParam, pcValue, 'a');
}
const char *cgi_floppy_select_b(int iIndex, int iNumParams, char *pcParam[], char *pcValue[])
{
    return cgi_floppy_select(iIndex, iNumParams, pcParam, pcValue, 'b');
}

/**
 * @brief Ejects the floppy disk image for a specific drive.
 *
 * This function is a CGI handler for ejecting the floppy disk image for a specific drive in an HTTP server.
 * It is called when the user requests to eject the floppy disk image for the specified drive.
 * The function clears the corresponding floppy image parameter, sets the appropriate flag to indicate that
 * the drive should be unmounted, and writes all entries to update the configuration.
 *
 * @param iIndex The index of the CGI handler.
 * @param iNumParams The number of parameters passed to the CGI handler.
 * @param pcParam An array of parameter names.
 * @param pcValue An array of parameter values.
 * @param drv The drive identifier ('a' or 'b') for which the floppy disk image should be ejected.
 * @return The URL of the page to redirect to after the floppy disk image is ejected.
 */
const char *cgi_floppy_eject(int iIndex, int iNumParams, char *pcParam[], char *pcValue[], char drv)
{
    DPRINTF("cgi_floppy_eject called\n");
    char *param = drv == 'a' ? PARAM_FLOPPY_IMAGE_A : PARAM_FLOPPY_IMAGE_B;
    put_string(param, "");
    SET_FLAG(drv == 'a' ? UMOUNT_DRIVE_A_FLAG : UMOUNT_DRIVE_B_FLAG);
    write_all_entries();
    return "/floppies_eject.shtml";
}

const char *cgi_floppy_eject_a(int iIndex, int iNumParams, char *pcParam[], char *pcValue[])
{
    return cgi_floppy_eject(iIndex, iNumParams, pcParam, pcValue, 'a');
}

const char *cgi_floppy_eject_b(int iIndex, int iNumParams, char *pcParam[], char *pcValue[])
{
    return cgi_floppy_eject(iIndex, iNumParams, pcParam, pcValue, 'b');
}

/**
 * @brief Array of CGI handlers for floppy select and eject operations.
 *
 * This array contains the mappings between the CGI paths and the corresponding handler functions
 * for selecting and ejecting floppy disk images for drive A and drive B.
 */
static const tCGI cgi_handlers[] = {
    {"/floppy_select_a.cgi", cgi_floppy_select_a},
    {"/floppy_select_b.cgi", cgi_floppy_select_b},
    {"/floppy_eject_a.cgi", cgi_floppy_eject_a},
    {"/floppy_eject_b.cgi", cgi_floppy_eject_b}};

/**
 * @brief Array of SSI tags for the HTTP server.
 *
 * This array contains the SSI tags used by the HTTP server to dynamically insert content into web pages.
 * Each tag corresponds to a specific piece of information that can be updated or retrieved from the server.
 */
const char *ssi_tags[] = {
    "DRIVE_A",  // 0
    "DRIVE_B",  // 1
    "AACTION",  // 2
    "BACTION",  // 3
    "FOLDER",   // 4
    "ACATALOG", // 5
    "BCATALOG", // 6
};

/**
 * @brief Server Side Include (SSI) handler for the HTTPD server.
 *
 * This function is called when the server needs to dynamically insert content into web pages using SSI tags.
 * It handles different SSI tags and generates the corresponding content to be inserted into the web page.
 *
 * @param iIndex The index of the SSI handler.
 * @param pcInsert A pointer to the buffer where the generated content should be inserted.
 * @param iInsertLen The length of the buffer.
 * @param current_tag_part The current part of the SSI tag being processed (used for multipart SSI tags).
 * @param next_tag_part A pointer to the next part of the SSI tag to be processed (used for multipart SSI tags).
 * @return The length of the generated content.
 */
u16_t ssi_handler(int iIndex, char *pcInsert, int iInsertLen
#if LWIP_HTTPD_SSI_MULTIPART
                  ,
                  u16_t current_tag_part, u16_t *next_tag_part
#endif /* LWIP_HTTPD_SSI_MULTIPART */
)
{
    DPRINTF("SSI handler called with index %d\n", iIndex);
    size_t printed;
    char drv = 'b';
    switch (iIndex)
    {
    case 0: /* "DRIVE_A" */
        printed = snprintf(pcInsert, iInsertLen, "%s", strlen(find_entry(PARAM_FLOPPY_IMAGE_A)->value) > 0 ? find_entry(PARAM_FLOPPY_IMAGE_A)->value : "INSERT DISK");
        break;
    case 1: /* "DRIVE_B" */
        printed = snprintf(pcInsert, iInsertLen, "%s", strlen(find_entry(PARAM_FLOPPY_IMAGE_B)->value) > 0 ? find_entry(PARAM_FLOPPY_IMAGE_B)->value : "INSERT DISK");
        break;
    case 2: /* "AACTION" */
    {
        char *msg = NULL;
        char *icon = NULL;
        char *url_action = NULL;
        if (strlen(find_entry(PARAM_FLOPPY_IMAGE_A)->value) == 0)
        {
            msg = "INSERT DISK\0";
            icon = "fas fa-folder-open\0";
            url_action = "/floppies_catalog_a.shtml";
        }
        else
        {
            msg = "EJECT DISK\0";
            icon = "fas fa-eject\0";
            url_action = "/floppy_eject_a.cgi";
        }
        if (current_tag_part == 0)
        {
            printed = snprintf(pcInsert, iInsertLen, "<a href='%s' class='ml-2 text-navy-700 hover:text-blue-500 relative group'><i class='fas %s'></i>", url_action, icon);
            *next_tag_part = current_tag_part + 1;
        }
        else
        {
            printed = snprintf(pcInsert, iInsertLen, "<span class='absolute bottom-full mb-1 hidden group-hover:block bg-gray-800 text-white text-xs rounded py-1 px-2'>%s</span></a>", msg);
        }
        break;
    }
    case 3: /* "BACTION" */
    {
        char *msg = NULL;
        char *icon = NULL;
        char *url_action = NULL;
        if (strlen(find_entry(PARAM_FLOPPY_IMAGE_B)->value) == 0)
        {
            msg = "INSERT DISK\0";
            icon = "fas fa-folder-open\0";
            url_action = "/floppies_catalog_b.shtml";
        }
        else
        {
            msg = "EJECT DISK\0";
            icon = "fas fa-eject\0";
            url_action = "/floppy_eject_b.cgi";
        }
        if (current_tag_part == 0)
        {
            printed = snprintf(pcInsert, iInsertLen, "<a href='%s' class='ml-2 text-navy-700 hover:text-blue-500 relative group'><i class='fas %s'></i>", url_action, icon);
            *next_tag_part = current_tag_part + 1;
        }
        else
        {
            printed = snprintf(pcInsert, iInsertLen, "<span class='absolute bottom-full mb-1 hidden group-hover:block bg-gray-800 text-white text-xs rounded py-1 px-2'>%s</span></a>", msg);
        }
        break;
    }
    case 4: /* "FOLDER" */
        printed = snprintf(pcInsert, iInsertLen, "%s", find_entry(PARAM_FLOPPIES_FOLDER)->value);
        break;
    case 5: /* "ACATALOG" */
        drv = 'a';
    case 6: /* "BCATALOG" */
    {
        // Iterate over the floppy catalog an create a large string with the HTML code as follows
        // <option value="floppy1.st">floppy1.st</option>
        // <option value="floppy2.st">floppy2.st</option>
        // ...
        DPRINTF("Current tag part: %d\n", current_tag_part);
        if (floppy_catalog.list != NULL)
        {
            printed = snprintf(pcInsert, iInsertLen, "<div class='font-mono ml-2 text-navy-700 hover:text-blue-500 hover:text-underline relative group'><a href='/floppy_select_%c.cgi?id=%d'>%s</a></div>\n", drv, current_tag_part, floppy_catalog.list[current_tag_part]);
            DPRINTF("Printed: %d\n", printed);

            if (current_tag_part < (floppy_catalog.size - 1))
            {
                *next_tag_part = current_tag_part + 1;
            }
        }
        else
        {
            printed = snprintf(pcInsert, iInsertLen, "NO DISKS FOUND");
        }
        break;
    }
    default: /* unknown tag */
        printed = 0;
        break;
    }
    LWIP_ASSERT("sane length", printed <= 0xFFFF);
    return (u16_t)printed;
}

/**
 * @brief Callback that handles the protocol command received.
 *
 * This callback is responsible for handling the protocol command received.
 * It reads the random token from the command and increments the payload pointer
 * to the first parameter available in the payload.
 * It then handles the protocol command based on the command ID.
 *
 * @param protocol The TransmissionProtocol structure containing the protocol information.
 */
static void __not_in_flash_func(handle_protocol_command)(const TransmissionProtocol *protocol)
{
    // Shared by all commands
    // Read the random token from the command and increment the payload pointer to the first parameter
    // available in the payload
    random_token = GET_RANDOM_TOKEN(protocol->payload);
    payloadPtr = ((uint16_t *)(protocol)->payload);

    // Handle the protocol
    switch (protocol->command_id)
    {
    case FLOPPYEMUL_SET_SHARED_VAR:
    {
        // Shared variables
        DPRINTF("Command SET_SHARED_VAR (%i) received: %d\n", protocol->command_id, protocol->payload_size);
        uint32_t shared_variable_index = GET_NEXT32_PAYLOAD_PARAM32(payloadPtr); // d3 register
        uint32_t shared_variable_value = GET_NEXT32_PAYLOAD_PARAM32(payloadPtr); // d4 register
        SET_SHARED_VAR(shared_variable_index, shared_variable_value, memory_shared_address, FLOPPYEMUL_SHARED_VARIABLES);
        SET_RANDOM_TOKEN(memory_shared_address + FLOPPYEMUL_RANDOM_TOKEN, random_token);
        break;
    }
    case FLOPPYEMUL_SAVE_VECTORS:
        // Save the vectors needed for the floppy emulation
        DPRINTF("Command SAVE_VECTORS (%i) received: %d\n", protocol->command_id, protocol->payload_size);
        disk_vectors.hdv_bpb_payload = GET_NEXT32_PAYLOAD_PARAM32(payloadPtr);     // d3 register
        disk_vectors.hdv_rw_payload = GET_NEXT32_PAYLOAD_PARAM32(payloadPtr);      // d4 register
        disk_vectors.hdv_mediach_payload = GET_NEXT32_PAYLOAD_PARAM32(payloadPtr); // d5 register
        disk_vectors.XBIOS_trap_payload = GET_NEXT32_PAYLOAD_PARAM32(payloadPtr);  // d6 register
        SET_FLAG(SAVE_VECTORS_FLAG);
        break;
    case FLOPPYEMUL_READ_SECTORS:
        // Read sectors from the floppy emulator
        DPRINTF("Command READ_SECTORS (%i) received: %d\n", protocol->command_id, protocol->payload_size);
        sector_size = GET_NEXT32_PAYLOAD_PARAM16(payloadPtr);    // d3.l register
        logical_sector = GET_NEXT16_PAYLOAD_PARAM16(payloadPtr); // d3.h register
        disk_number = GET_NEXT16_PAYLOAD_PARAM16(payloadPtr);    // d4.l register
        SET_FLAG(SECTOR_READ_FLAG);
        break;
    case FLOPPYEMUL_WRITE_SECTORS:
        // Write sectors from the floppy emulator
        DPRINTF("Command WRITE_SECTORS (%i) received: %d\n", protocol->command_id, protocol->payload_size);
        sector_size = GET_NEXT32_PAYLOAD_PARAM16(payloadPtr);    // d3.l register
        logical_sector = GET_NEXT16_PAYLOAD_PARAM16(payloadPtr); // d3.h register
        disk_number = GET_NEXT16_PAYLOAD_PARAM16(payloadPtr);    // d4.l register
        NEXT32_PAYLOAD_PTR(payloadPtr);
        NEXT32_PAYLOAD_PTR(payloadPtr); // Increment four extra words (the previous d4.l with disk_number and d5.l not used)
        SET_FLAG(SECTOR_WRITE_FLAG);
        break;
    case FLOPPYEMUL_PING:
        DPRINTF("Command PING (%i) received: %d\n", protocol->command_id, protocol->payload_size);
        SET_FLAG(PING_RECEIVED_FLAG);
        break;
    case FLOPPYEMUL_SAVE_HARDWARE:
        DPRINTF("Command SAVE_HARDWARE (%i) received: %d\n", protocol->command_id, protocol->payload_size);
        hardware_type.machine = GET_NEXT32_PAYLOAD_PARAM32(payloadPtr);        // d3 register
        hardware_type.start_function = GET_NEXT32_PAYLOAD_PARAM32(payloadPtr); // d4 register
        hardware_type.end_function = GET_NEXT32_PAYLOAD_PARAM32(payloadPtr);   // d5 register
        SET_FLAG(SAVE_HARDWARE_FLAG);
        break; // ... handle other commands
    case FLOPPYEMUL_RESET:
        DPRINTF("Command RESET (%i) received: %d\n", protocol->command_id, protocol->payload_size);
        //        SET_RANDOM_TOKEN(memory_shared_address + FLOPPYEMUL_RANDOM_TOKEN, random_token);
        reboot();
        break;
    case FLOPPYEMUL_MOUNT_DRIVE_A:
        DPRINTF("Command MOUNT_DRIVE_A (%i) received: %d\n", protocol->command_id, protocol->payload_size);
        SET_FLAG(MOUNT_DRIVE_A_FLAG);
        break;
    case FLOPPYEMUL_UNMOUNT_DRIVE_A:
        DPRINTF("Command UNMOUNT_DRIVE_A (%i) received: %d\n", protocol->command_id, protocol->payload_size);
        CLEAR_FLAG(MOUNT_DRIVE_A_FLAG);
        break;
    case FLOPPYEMUL_MOUNT_DRIVE_B:
        DPRINTF("Command MOUNT_DRIVE_B (%i) received: %d\n", protocol->command_id, protocol->payload_size);
        SET_FLAG(MOUNT_DRIVE_B_FLAG);
        break;
    case FLOPPYEMUL_UNMOUNT_DRIVE_B:
        DPRINTF("Command UNMOUNT_DRIVE_B (%i) received: %d\n", protocol->command_id, protocol->payload_size);
        CLEAR_FLAG(MOUNT_DRIVE_B_FLAG);
        break;
    case FLOPPYEMUL_SHOW_VECTOR_CALL:
        DPRINTF("Command SHOW_VECTOR_CALL (%i) received: %d\n", protocol->command_id, protocol->payload_size);
        vector_call = GET_NEXT32_PAYLOAD_PARAM16(payloadPtr); // d3.l register
        SET_FLAG(SHOW_VECTOR_CALL_FLAG);
        break;
    default:
        DPRINTF("Unknown command: %d\n", protocol->command_id);
        random_token = 0;
    }
}

/**
 * @brief Interrupt handler callback for the read memory address
 *
 * This function is the interrupt handler callback read memory address.
 * It reads the address to process and checks if the address is in the
 * boundaries of ROM3_START_ADDRESS. If it is, it calls the parse_protocol function
 * passing the lower 16 bits of the address and the handle_protocol_command
 * callback function as arguments.
 */
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

/**
 * @brief Initializes the floppy emulator.
 *
 * This function initializes and launch the floppy emulator.
 *
 * @param safe_config_reboot A boolean value indicating whether to perform a safe configuration reboot.
 */
void init_floppyemul(bool safe_config_reboot)
{

    char *fullpath_a = NULL;
    char *fullpath_b = NULL;
    bool floppy_rw_a = true;
    bool floppy_rw_b = true;
    bool write_config_only_once = true;
    FATFS fs;              /* File system object */
    FRESULT fr;            /* FatFs function common result code */
    FIL fsrc_a;            /* File objects for drive A*/
    FIL fsrc_b;            /* File objects for drive B */
    unsigned int br_a = 0; /* File read/write count */
    unsigned int br_b = 0; /* File read/write count */

    DPRINTF("Waiting for commands...\n");
    memory_shared_address = ROM3_START_ADDRESS; // Start of the shared memory buffer
    memory_code_address = ROM4_START_ADDRESS;   // Start of the code memory

    ConfigEntry *xbios_enabled = find_entry(PARAM_FLOPPY_XBIOS_ENABLED);
    bool floppy_xbios_enabled = true;
    if (xbios_enabled != NULL)
    {
        floppy_xbios_enabled = (xbios_enabled->value[0] == 't' || xbios_enabled->value[0] == 'T');
    }
    ConfigEntry *boot_enabled = find_entry(PARAM_FLOPPY_BOOT_ENABLED);
    bool floppy_boot_enabled = true;
    if (boot_enabled != NULL)
    {
        floppy_boot_enabled = (boot_enabled->value[0] == 't' || boot_enabled->value[0] == 'T');
    }
    ConfigEntry *buffer_type = find_entry(PARAM_FLOPPY_BUFFER_TYPE);
    uint32_t buffer_type_value = 0;
    if (buffer_type != NULL)
    {
        buffer_type_value = atoi(buffer_type->value);
    }
    ConfigEntry *network_enabled = find_entry(PARAM_FLOPPY_NET_ENABLED);
    bool floppy_network_enabled = true;
    if (network_enabled != NULL)
    {
        floppy_network_enabled = (network_enabled->value[0] == 't' || network_enabled->value[0] == 'T');
    }

    SET_SHARED_VAR(SHARED_VARIABLE_BUFFER_TYPE, buffer_type_value, memory_shared_address, FLOPPYEMUL_SHARED_VARIABLES); // 0: _diskbuff, 1: heap
    SET_SHARED_PRIVATE_VAR(FLOPPYEMUL_SVAR_XBIOS_TRAP_ENABLED, floppy_xbios_enabled ? 0xFFFFFFFF : 0, memory_shared_address, FLOPPYEMUL_SHARED_VARIABLES);
    SET_SHARED_PRIVATE_VAR(FLOPPYEMUL_SVAR_BOOT_ENABLED, floppy_boot_enabled ? 0xFFFFFFFF : 0, memory_shared_address, FLOPPYEMUL_SHARED_VARIABLES);
    SET_SHARED_PRIVATE_VAR(FLOPPYEMUL_SVAR_PING_STATUS, 0, memory_shared_address, FLOPPYEMUL_SHARED_VARIABLES); // 0: No ok, 1: Ready
    SET_SHARED_PRIVATE_VAR(FLOPPYEMUL_SVAR_MEDIA_CHANGED_A, MED_NOCHANGE, memory_shared_address, FLOPPYEMUL_SHARED_VARIABLES);
    SET_SHARED_PRIVATE_VAR(FLOPPYEMUL_SVAR_MEDIA_CHANGED_B, MED_NOCHANGE, memory_shared_address, FLOPPYEMUL_SHARED_VARIABLES);
    SET_SHARED_PRIVATE_VAR(FLOPPYEMUL_SVAR_EMULATION_MODE, 0, memory_shared_address, FLOPPYEMUL_SHARED_VARIABLES); // 0: No emulation (00)

    //
    // Init network
    //
    bool network_ready = false;
    memset((void *)(memory_shared_address + FLOPPYEMUL_IP_ADDRESS), 0, 128);
    memset((void *)(memory_shared_address + FLOPPYEMUL_HOSTNAME), 0, 128);

    // Local wifi password in the local file
    char *wifi_password_file_content = NULL;
    ConfigEntry *floppy_network_timeout = find_entry(PARAM_FLOPPY_NET_TOUT_SEC);
    uint32_t floppy_network_timeout_sec = 0;
    if (floppy_network_timeout != NULL)
    {
        floppy_network_timeout_sec = atoi(floppy_network_timeout->value);
    }
    // The ping timeout is the same as the network timeout
    SET_SHARED_PRIVATE_VAR(FLOPPYEMUL_SVAR_PING_TIMEOUT, floppy_network_timeout_sec, memory_shared_address, FLOPPYEMUL_SHARED_VARIABLES);
    floppy_network_timeout_sec = floppy_network_timeout_sec * 0.7; // Adjust the timeout to 70% of the value
    DPRINTF("Timeout in seconds: %d\n", floppy_network_timeout_sec);
    CLEAR_FLAG(PING_RECEIVED_FLAG);

    DPRINTF("Floppy network enabled? %s\n", floppy_network_enabled ? "YES" : "NO");

    bool show_blink = true;
    // Only try to get the datetime from the network if the wifi is configured
    // and the network configuration is enabled
    if ((strlen(find_entry(PARAM_WIFI_SSID)->value) > 0) && (floppy_network_enabled))
    {
        // Initialize SD card
        if (!sd_init_driver())
        {
            DPRINTF("ERROR: Could not initialize SD card\r\n");
        }
        else
        {
            FRESULT err = read_and_trim_file(WIFI_PASS_FILE_NAME, &wifi_password_file_content, MAX_WIFI_PASSWORD_LENGTH);
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
        cyw43_arch_deinit();
        cyw43_arch_init();
        network_init();
        network_connect(false, NETWORK_CONNECTION_ASYNC, &wifi_password_file_content);
        network_ready = false;

        blink_morse('F');

        // Wait until timeout
        while ((!network_ready) && (floppy_network_timeout_sec > 0) && (strlen(find_entry(PARAM_WIFI_SSID)->value) > 0))
        {
            tight_loop_contents();
#if PICO_CYW43_ARCH_POLL
            network_poll();
#endif
            cyw43_arch_lwip_begin();
            cyw43_arch_lwip_check();
            cyw43_arch_lwip_end();
            sleep_ms(1000); // Wait 1 sec per test

            // Only display when changes status to avoid flooding the console
            ConnectionStatus previous_status = get_previous_connection_status();
            ConnectionStatus current_status = get_network_connection_status();
            if (current_status != previous_status)
            {
                DPRINTF("Network status: %d\n", current_status);
                DPRINTF("Network previous status: %d\n", previous_status);
                get_connection_data(&connection_data);
                DPRINTF("SSID: %s - Status: %d - IPv4: %s - IPv6: %s - GW:%s - Mask:%s - MAC:%s\n",
                        connection_data.ssid,
                        connection_data.network_status,
                        connection_data.ipv4_address,
                        connection_data.ipv6_address,
                        print_ipv4(get_gateway()),
                        print_ipv4(get_netmask()),
                        print_mac(get_mac_address()));
                if ((current_status >= TIMEOUT_ERROR) && (current_status <= INSUFFICIENT_RESOURCES_ERROR))
                {
                    DPRINTF("Connection failed. Retrying...\n");
                    // Need to deinit and init again the full network stack to be able to scan again
                    cyw43_arch_deinit();
                    sleep_ms(1000);
                    cyw43_arch_init();
                    network_init();
                    // Start the network.
                    network_connect(true, NETWORK_CONNECTION_ASYNC, &wifi_password_file_content);
                }
            }
            network_ready = (current_status == CONNECTED_WIFI_IP);
            floppy_network_timeout_sec--;
            DPRINTF("Timeout in seconds: %d\n", floppy_network_timeout_sec);

            if (IS_FLAG_SET(PING_RECEIVED_FLAG))
            {
                DPRINTF("Ping received, but forced not ready yet.\n");
                CLEAR_FLAG(PING_RECEIVED_FLAG);
                SET_SHARED_PRIVATE_VAR(FLOPPYEMUL_SVAR_PING_STATUS, 0, memory_shared_address, FLOPPYEMUL_SHARED_VARIABLES); // Not ready yet
                SET_RANDOM_TOKEN(memory_shared_address + FLOPPYEMUL_RANDOM_TOKEN, random_token);
            }

            // If SELECT button is pressed, launch the configurator
            if (gpio_get(SELECT_GPIO) != 0)
            {
                select_button_action(safe_config_reboot, write_config_only_once);
                // Write config only once to avoid hitting the flash too much
                write_config_only_once = false;
            }
        }
        if (floppy_network_timeout_sec == 0)
        {
            DPRINTF("Timeout reached. No network.\n");
            // Just be sure to deinit the network stack
            network_disconnect();
            blink_morse('F');
            cyw43_arch_deinit();
            // Null connection_data
            memset(&connection_data, 0, sizeof(ConnectionData));
            DPRINTF("No wifi configured. Skipping network initialization.\n");
        }
    }
    else
    {
        // Just be sure to deinit the network stack
        network_disconnect();
        blink_morse('F');
        cyw43_arch_deinit();
        // Null connection_data
        memset(&connection_data, 0, sizeof(ConnectionData));
        DPRINTF("No wifi configured. Skipping network initialization.\n");
    }

    if (network_ready)
    {
        // Start the httpd server
        httpd_server_init(ssi_tags, LWIP_ARRAYSIZE(ssi_tags), ssi_handler, cgi_handlers, LWIP_ARRAYSIZE(cgi_handlers));

        // Copy the ip address and host
        char *ip_address = connection_data.ipv4_address;
        char *host = find_entry(PARAM_HOSTNAME)->value;
        if (strlen(ip_address) > 0)
        {
            int ip_address_words_len = ((strlen(ip_address) / 2) + 1) * 2;
            int host_words_len = ((strlen(host) / 2) + 1) * 2;
            memset((void *)(memory_shared_address + FLOPPYEMUL_IP_ADDRESS), 0, 128);
            memset((void *)(memory_shared_address + FLOPPYEMUL_HOSTNAME), 0, 128);
            memcpy((void *)(memory_shared_address + FLOPPYEMUL_IP_ADDRESS), ip_address, ip_address_words_len);
            memcpy((void *)(memory_shared_address + FLOPPYEMUL_HOSTNAME), host, host_words_len);
            CHANGE_ENDIANESS_BLOCK16(memory_shared_address + FLOPPYEMUL_IP_ADDRESS, ip_address_words_len);
            CHANGE_ENDIANESS_BLOCK16(memory_shared_address + FLOPPYEMUL_HOSTNAME, host_words_len);
            DPRINTF("IP Address: %s - Host: %s\n", ip_address, host);
        }
    }

    bool error = false;
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

    // Create list of floppy images
    if (!error)
    {
        char *dir = find_entry(PARAM_FLOPPIES_FOLDER)->value;
        floppyemul_filelist(dir, &fs, &floppy_catalog);
    }

    SET_FLAG(MOUNT_DRIVE_A_FLAG);
    SET_FLAG(MOUNT_DRIVE_B_FLAG);
    srand(time(0)); // Seed the random number generator
    while (!error)
    {
        // *((volatile uint32_t *)(memory_shared_address + FLOPPYEMUL_RANDOM_TOKEN_SEED)) = rand() % 0xFFFFFFFF;
        WRITE_LONGWORD(memory_shared_address, FLOPPYEMUL_RANDOM_TOKEN_SEED, rand() % 0xFFFFFFFF);
        tight_loop_contents();
        if (network_ready)
        {
#if PICO_CYW43_ARCH_POLL
            network_poll();
#endif
            cyw43_arch_lwip_begin();
            cyw43_arch_lwip_check();
            cyw43_arch_lwip_end();
        }
        if (IS_FLAG_SET(SHOW_VECTOR_CALL_FLAG))
        {
            DPRINTF("VECTOR CALL: $%x\n", vector_call);
            CLEAR_FLAG(SHOW_VECTOR_CALL_FLAG);
            SET_RANDOM_TOKEN(memory_shared_address + FLOPPYEMUL_RANDOM_TOKEN, random_token);
        }

        if (IS_FLAG_SET(MOUNT_DRIVE_A_FLAG))
        {
            CLEAR_FLAG(MOUNT_DRIVE_A_FLAG);
            if (!IS_FLAG_SET(FILE_READY_A_FLAG))
            {

                char *dir = find_entry(PARAM_FLOPPIES_FOLDER)->value;
                char *filename_a = find_entry(PARAM_FLOPPY_IMAGE_A)->value;

                if (!dir || strlen(dir) == 0)
                {
                    DPRINTF("Error: Missing directory drive A.\n");
                    error = true;
                }
                else if (!filename_a || strlen(filename_a) == 0)
                {
                    DPRINTF("Error: Missing filename drive A.\n");
                    // it's ok if there is no floppy image in drive A
                }
                else if (strcmp(filename_a, find_entry(PARAM_FLOPPY_IMAGE_B)->value) == 0)
                {
                    DPRINTF("Error: Drive A image is the same as drive B.\n");
                    error = true;
                }
                else
                {
                    size_t fullpath_a_len = strlen(dir) + strlen(filename_a) + 2;
                    fullpath_a = malloc(fullpath_a_len);

                    if (!fullpath_a)
                    {
                        DPRINTF("Error: Unable to allocate memory.\n");
                        error = true;
                    }
                    else
                    {

                        snprintf(fullpath_a, fullpath_a_len, "%s/%s", dir, filename_a);

                        DPRINTF("Emulating floppy image in drive A: %s\n", fullpath_a);

                        floppy_rw_a = is_floppy_rw(fullpath_a);
                        DPRINTF("Floppy image is %s\n", floppy_rw_a ? "read/write" : "read only");

                        // Invoke the function
                        dma_channel_set_irq1_enabled(lookup_data_rom_dma_channel, false);
                        FRESULT err = floppyemul_open(fullpath_a, floppy_rw_a, &fsrc_a);
                        dma_channel_set_irq1_enabled(lookup_data_rom_dma_channel, true);
                        if (err != FR_OK)
                        {
                            DPRINTF("ERROR: Could not open floppy image %s (%d)\r\n", fullpath_a, err);
                            error = true;
                        }
                        else
                        {
                            DPRINTF("Floppy image %s opened successfully\n", fullpath_a);
                            // Set the BPB of the floppy
                            // Create BPB for disk A
                            FRESULT bpb_found = floppyemul_create_BPB(&fsrc_a, &BpbData_A);
                            if (bpb_found != FR_OK)
                            {
                                DPRINTF("ERROR: Could not create BPB for image file  %s (%d)\r\n", fullpath_a, fr);
                                error = true;
                            }
                            else
                            {
                                CLEAR_FLAG(SET_BPB_FLAG);
                                BPBData *bpb_ptr = &BpbData_A;
                                memcpy((void *)(memory_shared_address + FLOPPYEMUL_BPB_DATA_A), bpb_ptr, sizeof(BpbData_A));
                                SET_SHARED_PRIVATE_VAR_BIT(FLOPPYEMUL_SVAR_EMULATION_MODE, 0, memory_shared_address, FLOPPYEMUL_SHARED_VARIABLES); // Bit 0 = 1: Floppy emulation A
                                SET_FLAG(FILE_READY_A_FLAG);
                            }
                        }
                    }
                }
            }
            SET_RANDOM_TOKEN(memory_shared_address + FLOPPYEMUL_RANDOM_TOKEN, random_token);
        }
        if (IS_FLAG_SET(MOUNT_DRIVE_B_FLAG))
        {
            CLEAR_FLAG(MOUNT_DRIVE_B_FLAG);
            if (!IS_FLAG_SET(FILE_READY_B_FLAG))
            {

                char *dir = find_entry(PARAM_FLOPPIES_FOLDER)->value;
                char *filename_b = find_entry(PARAM_FLOPPY_IMAGE_B)->value;

                if (!dir || strlen(dir) == 0)
                {
                    DPRINTF("Error: Missing directory or filename drive B.\n");
                    error = true;
                }
                else if (!filename_b || strlen(filename_b) == 0)
                {
                    DPRINTF("Error: Missing filename drive B.\n");
                    // it's ok if there is no floppy image in drive B
                }
                else if (strcmp(filename_b, find_entry(PARAM_FLOPPY_IMAGE_A)->value) == 0)
                {
                    DPRINTF("Error: Drive B image is the same as drive A.\n");
                    error = true;
                }
                else
                {

                    size_t fullpath_b_len = strlen(dir) + strlen(filename_b) + 2;
                    fullpath_b = malloc(fullpath_b_len);

                    if (!fullpath_b)
                    {
                        DPRINTF("Error: Unable to allocate memory.\n");
                        error = true;
                    }
                    else
                    {

                        snprintf(fullpath_b, fullpath_b_len, "%s/%s", dir, filename_b);

                        DPRINTF("Emulating floppy image in drive B: %s\n", fullpath_b);

                        floppy_rw_b = is_floppy_rw(fullpath_b);
                        DPRINTF("Floppy image is %s\n", floppy_rw_b ? "read/write" : "read only");

                        // Invoke the function
                        dma_channel_set_irq1_enabled(lookup_data_rom_dma_channel, false);
                        FRESULT err = floppyemul_open(fullpath_b, floppy_rw_b, &fsrc_b);
                        dma_channel_set_irq1_enabled(lookup_data_rom_dma_channel, true);
                        if (err != FR_OK)
                        {
                            DPRINTF("ERROR: Could not open floppy image %s (%d)\r\n", fullpath_b, err);
                            error = true;
                        }
                        else
                        {
                            DPRINTF("Floppy image %s opened successfully\n", fullpath_b);
                            // Set the BPB of the floppy
                            // Create BPB for disk B
                            FRESULT bpb_found = floppyemul_create_BPB(&fsrc_b, &BpbData_B);
                            if (bpb_found != FR_OK)
                            {
                                DPRINTF("ERROR: Could not create BPB for image file  %s (%d)\r\n", fullpath_b, fr);
                                error = true;
                            }
                            else
                            {
                                CLEAR_FLAG(SET_BPB_FLAG);
                                BPBData *bpb_ptr = &BpbData_B;
                                memcpy((void *)(memory_shared_address + FLOPPYEMUL_BPB_DATA_B), bpb_ptr, sizeof(BpbData_B));
                                SET_SHARED_PRIVATE_VAR_BIT(FLOPPYEMUL_SVAR_EMULATION_MODE, 1, memory_shared_address, FLOPPYEMUL_SHARED_VARIABLES); // Bit 1 = 1: Floppy emulation B
                                SET_FLAG(FILE_READY_B_FLAG);
                            }
                        }
                    }
                }
            }
            SET_RANDOM_TOKEN(memory_shared_address + FLOPPYEMUL_RANDOM_TOKEN, random_token);
        }
        if (IS_FLAG_SET(UMOUNT_DRIVE_A_FLAG))
        {
            CLEAR_FLAG(UMOUNT_DRIVE_A_FLAG);
            // Umount the A drive
            dma_channel_set_irq1_enabled(lookup_data_rom_dma_channel, false);
            FRESULT fr = floppyemul_close(&fsrc_a);
            dma_channel_set_irq1_enabled(lookup_data_rom_dma_channel, true);
            if (fr != FR_OK)
            {
                DPRINTF("ERROR: Could not close floppy image %s (%d)\r\n", fullpath_a, fr);
                error = true;
            }
            else
            {
                memset((void *)(memory_shared_address + FLOPPYEMUL_BPB_DATA_A), 0, sizeof(BpbData_A));
                SET_SHARED_PRIVATE_VAR(FLOPPYEMUL_SVAR_MEDIA_CHANGED_A, MED_CHANGED, memory_shared_address, FLOPPYEMUL_SHARED_VARIABLES);
                CLEAR_SHARED_PRIVATE_VAR_BIT(FLOPPYEMUL_SVAR_EMULATION_MODE, 0, memory_shared_address, FLOPPYEMUL_SHARED_VARIABLES); // Bit 0 = 0: No floppy emulation A
                CLEAR_FLAG(FILE_READY_A_FLAG);
            }
            SET_RANDOM_TOKEN(memory_shared_address + FLOPPYEMUL_RANDOM_TOKEN, random_token);
        }
        if (IS_FLAG_SET(UMOUNT_DRIVE_B_FLAG))
        {
            CLEAR_FLAG(UMOUNT_DRIVE_B_FLAG);
            // Umount the B drive
            dma_channel_set_irq1_enabled(lookup_data_rom_dma_channel, false);
            FRESULT fr = floppyemul_close(&fsrc_b);
            dma_channel_set_irq1_enabled(lookup_data_rom_dma_channel, true);
            if (fr != FR_OK)
            {
                DPRINTF("ERROR: Could not close floppy image %s (%d)\r\n", fullpath_b, fr);
                error = true;
            }
            else
            {
                memset((void *)(memory_shared_address + FLOPPYEMUL_BPB_DATA_B), 0, sizeof(BpbData_B));
                SET_SHARED_PRIVATE_VAR(FLOPPYEMUL_SVAR_MEDIA_CHANGED_B, MED_CHANGED, memory_shared_address, FLOPPYEMUL_SHARED_VARIABLES);
                CLEAR_SHARED_PRIVATE_VAR_BIT(FLOPPYEMUL_SVAR_EMULATION_MODE, 1, memory_shared_address, FLOPPYEMUL_SHARED_VARIABLES); // Bit 1 = 0: No floppy emulation B
                CLEAR_FLAG(FILE_READY_B_FLAG);
            }
            SET_RANDOM_TOKEN(memory_shared_address + FLOPPYEMUL_RANDOM_TOKEN, random_token);
        }

        if (IS_FLAG_SET(PING_RECEIVED_FLAG))
        {
            DPRINTF("Ping received\n");
            CLEAR_FLAG(PING_RECEIVED_FLAG);
            // If we are here, means there is network configured. Fine.
            // Also check if the SD card is mounted or not
            bool ok_to_read = microsd_mounted && !error && (IS_FLAG_SET(FILE_READY_A_FLAG) || IS_FLAG_SET(FILE_READY_B_FLAG));
            DPRINTF("Ok to read: %d\n", ok_to_read);
            SET_SHARED_PRIVATE_VAR(FLOPPYEMUL_SVAR_PING_STATUS, ok_to_read ? 0xFFFFFFFF : 0, memory_shared_address, FLOPPYEMUL_SHARED_VARIABLES);
            SET_RANDOM_TOKEN(memory_shared_address + FLOPPYEMUL_RANDOM_TOKEN, random_token);
        }

        if (IS_FLAG_SET(SAVE_VECTORS_FLAG))
        {
            CLEAR_FLAG(SAVE_VECTORS_FLAG);
            // Save the vectors needed for the floppy emulation
            DPRINTF("Saving vectors\n");
            // DPRINTF("random token: %x\n", random_token);
            if (!disk_vectors.XBIOS_trap_payload_set)
            {
                WRITE_AND_SWAP_LONGWORD(memory_shared_address, FLOPPYEMUL_OLD_XBIOS_TRAP, disk_vectors.XBIOS_trap_payload);
                disk_vectors.XBIOS_trap_payload_set = true;
            }
            else
            {
                DPRINTF("XBIOS_trap_payload previously set.\n");
            }
            DPRINTF("XBIOS_trap_payload: %x\n", disk_vectors.XBIOS_trap_payload);

            if (!disk_vectors.hdv_bpb_payload_set)
            {
                WRITE_AND_SWAP_LONGWORD(memory_shared_address, FLOPPYEMUL_OLD_HDV_BPB, disk_vectors.hdv_bpb_payload);
                disk_vectors.hdv_bpb_payload_set = true;
            }
            else
            {
                DPRINTF("hdv_bpb_payload previously set.\n");
            }
            DPRINTF("hdv_bpb_payload: %x\n", disk_vectors.hdv_bpb_payload);

            if (!disk_vectors.hdv_rw_payload_set)
            {
                WRITE_AND_SWAP_LONGWORD(memory_shared_address, FLOPPYEMUL_OLD_HDV_RW, disk_vectors.hdv_rw_payload);
                disk_vectors.hdv_rw_payload_set = true;
            }
            else
            {
                DPRINTF("hdv_rw_payload previously set.\n");
            }
            DPRINTF("hdv_rw_payload: %x\n", disk_vectors.hdv_rw_payload);

            if (!disk_vectors.hdv_mediach_payload_set)
            {
                WRITE_AND_SWAP_LONGWORD(memory_shared_address, FLOPPYEMUL_OLD_HDV_MEDIACH, disk_vectors.hdv_mediach_payload);
                disk_vectors.hdv_mediach_payload_set = true;
            }
            else
            {
                DPRINTF("hdv_mediach_payload previously set.\n");
            }
            DPRINTF("hdv_mediach_payload: %x\n", disk_vectors.hdv_mediach_payload);
            SET_RANDOM_TOKEN(memory_shared_address + FLOPPYEMUL_RANDOM_TOKEN, random_token);
        }
        if (IS_FLAG_SET(SAVE_HARDWARE_FLAG))
        {
            CLEAR_FLAG(SAVE_HARDWARE_FLAG);
            DPRINTF("Setting hardware type: %x\n", hardware_type.machine);
            DPRINTF("Setting hardware type start function: %x\n", hardware_type.start_function);
            DPRINTF("Setting hardware type end function: %x\n", hardware_type.end_function);

            WRITE_AND_SWAP_LONGWORD(memory_shared_address, FLOPPYEMUL_HARDWARE_TYPE, hardware_type.machine);
            // Self-modifying code to change the speed of the cpu and cache or not. Not strictly needed, but can avoid bus errors
            // Check if the hardware type is 0x00010010 (Atari MegaSTe)
            if (hardware_type.machine != 0x00010010)
            {
                // write the 0x4E71 opcode (NOP) at the beginning of the function 8 times
                MEMSET16BIT(memory_code_address, (hardware_type.start_function & 0xFFFF), 8, 0x4E71); // NOP
                // write the 0x4E71 opcode (NOP) at the end of the function 2 times
                MEMSET16BIT(memory_code_address, (hardware_type.end_function & 0xFFFF), 2, 0x4E71); // NOP
            }
            SET_RANDOM_TOKEN(memory_shared_address + FLOPPYEMUL_RANDOM_TOKEN, random_token);
        }

        if (IS_FLAG_SET(SECTOR_READ_FLAG))
        {
            CLEAR_FLAG(SECTOR_READ_FLAG);
            DPRINTF("DISK %s (%d) - LSECTOR: %i / SSIZE: %i\n", disk_number == 0 ? "A:" : "B:", disk_number, logical_sector, sector_size);

            FIL fsrc_tmp = {0};
            char *fullpath_tmp = NULL;
            unsigned int br_tmp = {0};
            if (disk_number == 0)
            {
                fsrc_tmp = fsrc_a;
                fullpath_tmp = fullpath_a;
                br_tmp = br_a;
            }
            else
            {
                fsrc_tmp = fsrc_b;
                fullpath_tmp = fullpath_b;
                br_tmp = br_b;
            }

            dma_channel_set_irq1_enabled(lookup_data_rom_dma_channel, false);
            /* Set read/write pointer to logical sector position */
            fr = f_lseek(&fsrc_tmp, logical_sector * sector_size);
            if (fr)
            {
                DPRINTF("ERROR: Could not seek file %s (%d). Closing file.\n", fullpath_tmp, fr);
                f_close(&fsrc_tmp);
                error = true;
            }
            fr = f_read(&fsrc_tmp, (void *)(memory_shared_address + FLOPPYEMUL_IMAGE), sector_size, &br_tmp); /* Read a chunk of data from the source file */
            dma_channel_set_irq1_enabled(lookup_data_rom_dma_channel, true);
            if (fr)
            {
                DPRINTF("ERROR: Could not read file %s (%d). Closing file.\n", fullpath_tmp, fr);
                f_close(&fsrc_tmp);
                error = true;
            }
            else
            {
                // After reading from the file, we need to calculate the checksum
                // Checksum is calculated by adding all the words in the sector
                uint16_t checksum = 0;
                for (int i = 0; i < sector_size / 2; i++)
                {
                    uint16_t tmp = READ_WORD(memory_shared_address, FLOPPYEMUL_IMAGE + i * 2);
                    checksum += SWAP_WORD(tmp);
                }
                // Set the checksum in the shared memory
                DPRINTF("Checksum: %x\n", checksum);
                WRITE_WORD(memory_shared_address, FLOPPYEMUL_READ_CHECKSUM, checksum);
            }
            CHANGE_ENDIANESS_BLOCK16(memory_shared_address + FLOPPYEMUL_IMAGE, sector_size);
            SET_RANDOM_TOKEN(memory_shared_address + FLOPPYEMUL_RANDOM_TOKEN, random_token);
        }
        if (IS_FLAG_SET(SECTOR_WRITE_FLAG))
        {
            CLEAR_FLAG(SECTOR_WRITE_FLAG);
            // Only write if the floppy image is read/write. It's important because the FatFS seems to ignore the FA_READ flag
            if (disk_number == 0 ? floppy_rw_a : floppy_rw_b)
            {
                DPRINTF("DISK %s (%d) - LSECTOR: %i / SSIZE: %i\n", disk_number == 0 ? "A:" : "B:", disk_number, logical_sector, sector_size);

                uint16_t chk = 0;
                uint16_t remote_chk = 1;

                // Copy shared memory to a local buffer
                uint16_t buff_tmp[(sector_size + 2) / 2];
                memset(buff_tmp, 0, sizeof(buff_tmp)); // Initialize all elements to zero
                memcpy(buff_tmp, payloadPtr, sector_size + 2);

                uint16_t *target_start = &buff_tmp[0];
                // Calculate the checksum of the buffer
                // Use a 16 bit checksum to minimize the number of loops
                uint16_t words_to_write = (sector_size) / 2;
                uint16_t *target16 = (uint16_t *)target_start;
                // Read the checksum from the last word
                remote_chk = target16[words_to_write];
                chk = 0; // Reset the checksum
                for (int i = 0; i < words_to_write; i++)
                {
                    // Sum the value
                    chk += target16[i];
                }
                if (chk == remote_chk)
                {
                    // Change the endianness of the bytes read
                    CHANGE_ENDIANESS_BLOCK16(target16, ((sector_size + 1) * 2) / 2);
                    FIL fsrc_tmp = {0};
                    char *fullpath_tmp = NULL;
                    unsigned int br_tmp = {0};
                    if (disk_number == 0)
                    {
                        fsrc_tmp = fsrc_a;
                        fullpath_tmp = fullpath_a;
                        br_tmp = br_a;
                    }
                    else
                    {
                        fsrc_tmp = fsrc_b;
                        fullpath_tmp = fullpath_b;
                        br_tmp = br_b;
                    }

                    dma_channel_set_irq1_enabled(lookup_data_rom_dma_channel, false);
                    /* Set read/write pointer to logical sector position */
                    fr = f_lseek(&fsrc_tmp, logical_sector * sector_size);
                    if (fr)
                    {
                        DPRINTF("ERROR: Could not seek file %s (%d). Closing file.\r\n", fullpath_tmp, fr);
                        f_close(&fsrc_a);
                        error = true;
                    }
                    fr = f_write(&fsrc_tmp, target_start, sector_size, &br_tmp); /* Write a chunk of data from the source file */
                    if (fr)
                    {
                        DPRINTF("ERROR: Could not read file %s (%d). Closing file.\r\n", fullpath_tmp, fr);
                        f_close(&fsrc_a);
                        error = true;
                    }
                    dma_channel_set_irq1_enabled(lookup_data_rom_dma_channel, true);
                }
                else
                {
                    DPRINTF("Checksum: x%x. Remote checksum: x%x. Checksum error. Not writing to disk.\n", chk, remote_chk);
                    // Force the error writing a random token different from the one received
                    random_token = 0xFFFFFFFF;
                }
            }
            else
            {
                DPRINTF("ERROR: Trying to write to a read-only floppy image.\r\n");
            }
            SET_RANDOM_TOKEN(memory_shared_address + FLOPPYEMUL_RANDOM_TOKEN, random_token);
        }
        // If SELECT button is pressed, launch the configurator
        if (gpio_get(SELECT_GPIO) != 0)
        {
            select_button_action(safe_config_reboot, write_config_only_once);
            // Write config only once to avoid hitting the flash too much
            write_config_only_once = false;
        }
    }
    // Init the CYW43 WiFi module. Needed to show the error message in the LED
    // cyw43_arch_init();
    blink_error();
}
