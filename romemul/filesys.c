/**
 * File: filesys.c
 * Author: Diego Parrilla Santamaría
 * Date: Novemeber 2023
 * Copyright: 2023 - GOODDATA LABS SL
 * Description: C file that contains the functions to convert from a file format to another
 * The original code comes from Hatari emulator:
 *      https://github.com/hatari/hatari/blob/69ce09390824a29c9708ae41290d6df28a5f766e/src/msa.c
 *
 * Please repect the original license
 */

#include "include/filesys.h"

/*
    .MSA FILE FORMAT
  --================------------------------------------------------------------

  For those interested, an MSA file is made up as follows:

  Header:

  Word  ID marker, should be $0E0F
  Word  Sectors per track
  Word  Sides (0 or 1; add 1 to this to get correct number of sides)
  Word  Starting track (0-based)
  Word  Ending track (0-based)

  Individual tracks follow the header in alternating side order, e.g. a double
  sided disk is stored as:

  TRACK 0, SIDE 0
  TRACK 0, SIDE 1
  TRACK 1, SIDE 0
  TRACK 1, SIDE 1
  TRACK 2, SIDE 0
  TRACK 2, SIDE 1

  ...and so on. Track blocks are made up as follows:

  Word  Data length
  Bytes  Data

  If the data length is equal to 512 x the sectors per track value, it is an
  uncompressed track and you can merely copy the data to the appropriate track
  of the disk. However, if the data length value is less than 512 x the sectors
  per track value it is a compressed track.

  Compressed tracks use simple a Run Length Encoding (RLE) compression method.
  You can directly copy any data bytes until you find an $E5 byte. This signals
  a compressed run, and is made up as follows:

  Byte  Marker - $E5
  Byte  Data byte
  Word  Run length

  So, if MSA found six $AA bytes in a row it would encode it as:

  $E5AA0006

  What happens if there's an actual $E5 byte on the disk? Well, logically
  enough, it is encoded as:

  $E5E50001

  This is obviously bad news if a disk consists of lots of data like
  $E500E500E500E500... but if MSA makes a track bigger when attempting to
  compress it, it just stores the uncompressed version instead.

  MSA only compresses runs of at least 4 identical bytes (after all, it would be
  wasteful to store 4 bytes for a run of only 3 identical bytes!). There is one
  exception to this rule: if a run of 2 or 3 $E5 bytes is found, that is stored
  appropriately enough as a run. Again, it would be wasteful to store 4 bytes
  for every single $E5 byte.

  The hacked release of MSA that enables the user to turn off compression
  completely simply stops MSA from trying this compression and produces MSA
  images that are completely uncompressed. This is okay because it is possible
  for MSA to produce such an image anyway, and such images are therefore 100%
  compatible with normal MSA versions (and MSA-to-ST of course).
*/

// Function to check if there is enough free disk space to create a file of size 'nDiskSize'
FRESULT checkDiskSpace(const char *folder, uint32_t nDiskSize)
{
    DWORD fre_clust, fre_sect, tot_sect;
    FATFS *fs;
    FRESULT fr;

    // Get free space
    fr = f_getfree(folder, &fre_clust, &fs);
    if (fr != FR_OK)
    {
        return fr; // Return error code if operation is not successful
    }

    // Calculate the total number of free bytes
    uint64_t freeBytes = fre_clust * fs->csize * NUM_BYTES_PER_SECTOR;

    // Check if there is enough space
    if ((uint64_t)nDiskSize > freeBytes)
    {
        return FR_DENIED; // Not enough space
    }
    return FR_OK; // Enough space available
}

/**
 * @brief Converts an MSA disk image file to an ST disk image file.
 *
 * This function takes a given MSA disk image file,
 * represented by `msaFilename` located within the `folder` directory, and
 * converts it into an ST (Atari ST disk image) file specified by `stFilename`.
 * If the `overwrite` is set to true, any existing file with the same
 * name as `stFilename` will be overwritten.
 *
 * @param folder The directory where the MSA file is located and the ST file will be saved.
 * @param msaFilename The name of the MSA file to convert.
 * @param stFilename The name of the ST file to be created.
 * @param overwrite If true, any existing ST file will be overwritten.
 *
 * @return FRESULT A FatFS result code indicating the status of the operation.
 */
FRESULT MSA_to_ST(const char *folder, char *msaFilename, char *stFilename, bool overwrite)
{
    MSAHEADERSTRUCT msaHeader;
    uint32_t nBytesLeft = 0;
    uint8_t *pMSAImageBuffer, *pImageBuffer;
    uint8_t Byte, Data;
    uint16_t Track, Side, DataLength, NumBytesUnCompressed, RunLength;
    uint8_t *pBuffer = NULL;
    FRESULT fr;   // FatFS function common result code
    FIL src_file; // File objects
    FIL dest_file;
    UINT br, bw; // File read/write count
    BYTE *buffer_in = NULL;
    BYTE *buffer_out = NULL;

    // Check if the folder exists, if not, exit
    DPRINTF("Checking folder %s\n", folder);
    if (f_stat(folder, NULL) != FR_OK)
    {
        DPRINTF("Folder %s not found!\n", folder);
        return FR_NO_PATH;
    }

    char src_path[256];
    char dest_path[256];

    // Create full paths for source and destination files
    sprintf(src_path, "%s/%s", folder, msaFilename);
    sprintf(dest_path, "%s/%s", folder, stFilename);
    DPRINTF("SRC PATH: %s\n", src_path);
    DPRINTF("DEST PATH: %s\n", dest_path);

    // Check if the destination file already exists
    fr = f_stat(dest_path, NULL);
    if (fr == FR_OK && !overwrite)
    {
        DPRINTF("Destination file exists and overwrite is false, canceling operation\n");
        return FR_FILE_EXISTS; // Destination file exists and overwrite is false, cancel the operation
    }

    // Check if the MSA source file exists in the SD card with FatFS
    if (f_open(&src_file, src_path, FA_READ) != FR_OK)
    {
        DPRINTF("MSA file not found!\n");
        return FR_NO_FILE;
    }
    // Calculate the size of the MSA file
    nBytesLeft = f_size(&src_file);

    // Check if the ST destination file exists in the SD card with FatFS
    if (f_open(&dest_file, dest_path, FA_WRITE | FA_CREATE_ALWAYS) != FR_OK)
    {
        DPRINTF("Error creating destination ST file!\n");
        return FR_NO_FILE;
    }

    buffer_in = malloc(sizeof(MSAHEADERSTRUCT) * +sizeof(uint16_t));
    // Read only the memory needed to read the header AND the first track info (a word)
    fr = f_read(&src_file, buffer_in, sizeof(MSAHEADERSTRUCT) + sizeof(uint16_t), &br); // Read a chunk of source file
    if (fr != FR_OK)
    {
        DPRINTF("Error reading source file!\n");
        if (buffer_in != NULL)
        {
            free(buffer_in);
        }
        return FR_DISK_ERR;
    }

    memcpy(&msaHeader, buffer_in, sizeof(MSAHEADERSTRUCT));
    /* First swap 'header' words around to PC format - easier later on */
    msaHeader.ID = bswap_16(msaHeader.ID);
    msaHeader.SectorsPerTrack = bswap_16(msaHeader.SectorsPerTrack);
    msaHeader.Sides = bswap_16(msaHeader.Sides);
    msaHeader.StartingTrack = bswap_16(msaHeader.StartingTrack);
    msaHeader.EndingTrack = bswap_16(msaHeader.EndingTrack);
    DPRINTF("MSA Header: ID: %x\n", msaHeader.ID);
    DPRINTF("MSA Header: SectorsPerTrack: %d\n", msaHeader.SectorsPerTrack);
    DPRINTF("MSA Header: Sides: %d\n", msaHeader.Sides);
    DPRINTF("MSA Header: StartingTrack: %d\n", msaHeader.StartingTrack);
    DPRINTF("MSA Header: EndingTrack: %d\n", msaHeader.EndingTrack);

    if (msaHeader.ID != 0x0E0F || msaHeader.EndingTrack > 86 || msaHeader.StartingTrack > msaHeader.EndingTrack || msaHeader.SectorsPerTrack > 56 || msaHeader.Sides > 1 || nBytesLeft <= (long)sizeof(MSAHEADERSTRUCT))
    {
        DPRINTF("MSA image has a bad header!\n");
        if (buffer_in != NULL)
        {
            free(buffer_in);
        }
        return FR_DISK_ERR;
    }

    if (checkDiskSpace(folder, NUM_BYTES_PER_SECTOR * msaHeader.SectorsPerTrack * (msaHeader.Sides + 1) * (msaHeader.EndingTrack - msaHeader.StartingTrack)) != FR_OK)
    {
        DPRINTF("Not enough space in the SD card!\n");
        if (buffer_in != NULL)
        {
            free(buffer_in);
        }
        return FR_DENIED;
    }

    nBytesLeft -= sizeof(MSAHEADERSTRUCT);
    // The length of the first track to read
    uint16_t currentTrackDataLength = bswap_16((uint16_t) * (uint16_t *)(buffer_in + sizeof(MSAHEADERSTRUCT)));

    /* Uncompress to memory as '.ST' disk image - NOTE: assumes NUM_BYTES_PER_SECTOR bytes
     * per sector (use NUM_BYTES_PER_SECTOR define)!!! */
    for (Track = msaHeader.StartingTrack; Track <= msaHeader.EndingTrack; Track++)
    {
        for (Side = 0; Side < (msaHeader.Sides + 1); Side++)
        {
            uint16_t nBytesPerTrack = NUM_BYTES_PER_SECTOR * msaHeader.SectorsPerTrack;
            nBytesLeft -= sizeof(uint16_t);
            DPRINTF("Track: %d\n", Track);
            DPRINTF("Side: %d\n", Side);
            DPRINTF("Current Track Size: %d\n", currentTrackDataLength);
            DPRINTF("Bytes per track: %d\n", nBytesPerTrack);
            DPRINTF("Bytes left: %d\n", nBytesLeft);

            if (nBytesLeft < 0)
                goto out;

            // Reserve write buffer
            if (buffer_out != NULL)
            {
                free(buffer_out);
            }
            buffer_out = malloc(nBytesPerTrack);

            if (buffer_in != NULL)
            {
                free(buffer_in);
            }
            buffer_in = malloc(currentTrackDataLength + sizeof(uint16_t));

            BYTE *buffer_in_tmp = buffer_in;
            fr = f_read(&src_file, buffer_in_tmp, currentTrackDataLength + sizeof(uint16_t), &br); // Read a chunk of source file
            if (fr != FR_OK)
            {
                DPRINTF("Error reading source file!\n");
                if (buffer_in != NULL)
                {
                    free(buffer_in);
                }
                if (buffer_out != NULL)
                {
                    free(buffer_out);
                }
                return FR_DISK_ERR;
            }

            // Check if it is not a compressed track
            if (currentTrackDataLength == nBytesPerTrack)
            {
                nBytesLeft -= currentTrackDataLength;
                if (nBytesLeft < 0)
                    goto out;

                // No compression, read the full track and write it to the destination file
                fr = f_write(&dest_file, buffer_in, nBytesPerTrack, &bw); // Write it to the destination file
                if (fr != FR_OK)
                {
                    DPRINTF("Error writing destination file!\n");
                    if (buffer_in != NULL)
                    {
                        free(buffer_in);
                    }
                    if (buffer_out != NULL)
                    {
                        free(buffer_out);
                    }
                    return FR_DISK_ERR;
                }
                buffer_in_tmp += currentTrackDataLength;
            }
            else
            {
                // Compressed track, uncompress it
                NumBytesUnCompressed = 0;
                BYTE *buffer_out_tmp = buffer_out;
                while (NumBytesUnCompressed < nBytesPerTrack)
                {
                    if (--nBytesLeft < 0)
                        goto out;
                    Byte = *buffer_in_tmp++;
                    if (Byte != 0xE5) /* Compressed header? */
                    {
                        *buffer_out_tmp++ = Byte; /* No, just copy byte */
                        NumBytesUnCompressed++;
                    }
                    else
                    {
                        nBytesLeft -= 3;
                        if (nBytesLeft < 0)
                            goto out;
                        Data = *buffer_in_tmp++; /* Byte to copy */
                        RunLength = (uint16_t)(buffer_in_tmp[1] | buffer_in_tmp[0] << 8);
                        /* Limit length to size of track, incorrect images may overflow */
                        if (RunLength + NumBytesUnCompressed > nBytesPerTrack)
                        {
                            DPRINTF("MSA_UnCompress: Illegal run length -> corrupted disk image?\n");
                            RunLength = nBytesPerTrack - NumBytesUnCompressed;
                        }
                        buffer_in_tmp += sizeof(uint16_t);
                        for (uint16_t i = 0; i < RunLength; i++)
                        {
                            *buffer_out_tmp++ = Data; /* Copy byte */
                        }
                        NumBytesUnCompressed += RunLength;
                    }
                }
                // No compression, read the full track and write it to the destination file
                fr = f_write(&dest_file, buffer_out, nBytesPerTrack, &bw); // Write it to the destination file
                if (fr != FR_OK)
                {
                    DPRINTF("Error writing destination file!\n");
                    if (buffer_in != NULL)
                    {
                        free(buffer_in);
                    }
                    if (buffer_out != NULL)
                    {
                        free(buffer_out);
                    }
                    return FR_DISK_ERR;
                }
            }
            if (nBytesLeft > 0)
            {
                currentTrackDataLength = (uint16_t)(buffer_in_tmp[1] | buffer_in_tmp[0] << 8);
            }
        }
    }
out:
    if (nBytesLeft < 0)
    {
        DPRINTF("MSA error: Premature end of file!\n");
    }

    // Close files
    f_close(&src_file);
    f_close(&dest_file);

    if (buffer_in != NULL)
    {
        free(buffer_in);
    }
    if (buffer_out != NULL)
    {
        free(buffer_out);
    }

    return FR_OK;
}

/**
 * Write a short integer to a given address in little endian byte order.
 * This function is primarily used to write 16-bit values into the boot sector of a
 * disk image which requires values to be in little endian format.
 *
 * @param addr Pointer to the address where the short integer should be written.
 * @param val The 16-bit value to be written in little endian byte order.
 */
static inline void write_short_le(void *addr, uint16_t val)
{
    /* Cast the address to a uint8_t pointer and write the value in little endian byte order. */
    uint8_t *p = (uint8_t *)addr;

    p[0] = (uint8_t)val;        // Write the low byte.
    p[1] = (uint8_t)(val >> 8); // Write the high byte shifted down.
}

/**
 * Create .ST image according to 'Tracks,Sector,Sides' and save
 *
            40 track SS   40 track DS   80 track SS   80 track DS
    0- 1   Branch instruction to boot program if executable
    2- 7   'Loader'
    8-10   24-bit serial number
    11-12   BPS    512           512           512           512
    13      SPC     1             2             2             2
    14-15   RES     1             1             1             1
    16      FAT     2             2             2             2
    17-18   DIR     64           112           112           112
    19-20   SEC    360           720           720          1440
    21      MEDIA  $FC           $FD           $F8           $F9  (isn't used by ST-BIOS)
    22-23   SPF     2             2             5             5
    24-25   SPT     9             9             9             9
    26-27   SIDE    1             2             1             2
    28-29   HID     0             0             0             0
    510-511 CHECKSUM
 */

/**
 * Create a blank Atari ST disk image file.
 *
 * This function creates a blank Atari ST formatted disk image with the
 * specified parameters. It can also set the volume label and allows
 * for the option to overwrite an existing file.
 *
 * @param folder The directory in which to create the disk image.
 * @param stFilename The name of the disk image file to create.
 * @param nTracks Number of tracks on the disk.
 * @param nSectors Number of sectors per track.
 * @param nSides Number of disk sides.
 * @param volLabel Optional volume label for the disk; pass NULL for no label.
 * @param overwrite If true, an existing file with the same name will be overwritten.
 *
 * @return FR_OK if the operation is successful, otherwise an error code.
 */
FRESULT create_blank_ST_image(const char *folder, char *stFilename, int nTracks, int nSectors, int nSides, const char *volLavel, bool overwrite)
{
    uint8_t *pDiskHeader;
    uint32_t nDiskSize;
    uint32_t nHeaderSize;
    uint32_t nDiskSizeNoHeader;
    uint16_t SPC, nDir, MediaByte, SPF;
    uint16_t drive;
    uint16_t LabelSize;
    uint8_t *pDirStart;

    FRESULT fr; // FatFS function common result code
    FIL dest_file;
    UINT bw; // File write count
    char dest_path[256];
    BYTE zeroBuff[NUM_BYTES_PER_SECTOR]; // Temporary buffer to hold zeros

    /* Calculate size of disk image */
    nDiskSize = nTracks * nSectors * nSides * NUM_BYTES_PER_SECTOR;

    // Calculate size of the header information
    nHeaderSize = 2 * (1 + SPF_MAX) * NUM_BYTES_PER_SECTOR;

    // Calculate the size of the disk without the header
    nDiskSizeNoHeader = nDiskSize - nHeaderSize;

    // Check if the folder exists, if not, exit
    DPRINTF("Checking folder %s\n", folder);
    if (f_stat(folder, NULL) != FR_OK)
    {
        DPRINTF("Folder %s not found!\n", folder);
        return FR_NO_PATH;
    }

    if (checkDiskSpace(folder, nDiskSize) != FR_OK)
    {
        DPRINTF("Not enough space in the SD card!\n");
        return FR_DENIED;
    }

    // Create the full path for the destination file
    sprintf(dest_path, "%s/%s", folder, stFilename);
    DPRINTF("DEST PATH: %s\n", dest_path);

    // Check if the destination file already exists
    fr = f_stat(dest_path, NULL);
    if (fr == FR_OK && !overwrite)
    {
        DPRINTF("Destination file exists and overwrite is false, canceling operation\n");
        return FR_FILE_EXISTS; // Destination file exists and overwrite is false, cancel the operation
    }

    /* HD/ED disks are all double sided */
    if (nSectors >= 18)
        nSides = 2;

    // Allocate space ONLY for the header. We don't have enough space in the RP2040
    pDiskHeader = malloc(nHeaderSize);
    if (pDiskHeader == NULL)
    {
        DPRINTF("Error while creating blank disk image");
        return FR_DISK_ERR;
    }
    memset(pDiskHeader, 0, nHeaderSize); /* Clear buffer */

    /* Fill in boot-sector */
    pDiskHeader[0] = 0xE9;            /* Needed for MS-DOS compatibility */
    memset(pDiskHeader + 2, 0x4e, 6); /* 2-7 'Loader' */

    write_short_le(pDiskHeader + 8, rand()); /* 8-10 24-bit serial number */
    pDiskHeader[10] = rand();

    write_short_le(pDiskHeader + 11, NUM_BYTES_PER_SECTOR); /* 11-12 BPS */

    if ((nTracks == 40) && (nSides == 1))
        SPC = 1;
    else
        SPC = 2;
    pDiskHeader[13] = SPC; /* 13 SPC */

    write_short_le(pDiskHeader + 14, 1); /* 14-15 RES */
    pDiskHeader[16] = 2;                 /* 16 FAT */

    if (SPC == 1)
        nDir = 64;
    else if (nSectors < 18)
        nDir = 112;
    else
        nDir = 224;
    write_short_le(pDiskHeader + 17, nDir); /* 17-18 DIR */

    write_short_le(pDiskHeader + 19, nTracks * nSectors * nSides); /* 19-20 SEC */

    if (nSectors >= 18)
        MediaByte = 0xF0;
    else
    {
        if (nTracks <= 42)
            MediaByte = 0xFC;
        else
            MediaByte = 0xF8;
        if (nSides == 2)
            MediaByte |= 0x01;
    }
    pDiskHeader[21] = MediaByte; /* 21 MEDIA */

    if (nSectors >= 18)
        SPF = SPF_MAX;
    else if (nTracks >= 80)
        SPF = 5;
    else
        SPF = 2;
    write_short_le(pDiskHeader + 22, SPF); /* 22-23 SPF */

    write_short_le(pDiskHeader + 24, nSectors); /* 24-25 SPT */
    write_short_le(pDiskHeader + 26, nSides);   /* 26-27 SIDE */
    write_short_le(pDiskHeader + 28, 0);        /* 28-29 HID */

    /* Set correct media bytes in the 1st FAT: */
    pDiskHeader[NUM_BYTES_PER_SECTOR] = MediaByte;
    pDiskHeader[NUM_BYTES_PER_SECTOR + 1] = pDiskHeader[NUM_BYTES_PER_SECTOR + 2] = 0xFF;
    /* Set correct media bytes in the 2nd FAT: */
    pDiskHeader[NUM_BYTES_PER_SECTOR + SPF * NUM_BYTES_PER_SECTOR] = MediaByte;
    pDiskHeader[(NUM_BYTES_PER_SECTOR + 1) + SPF * NUM_BYTES_PER_SECTOR] =
        pDiskHeader[(2 + NUM_BYTES_PER_SECTOR) + SPF * NUM_BYTES_PER_SECTOR] = 0xFF;

    /* Set volume label if needed (in 1st entry of the directory) */
    if (volLavel != NULL)
    {
        /* Set 1st dir entry as 'volume label' */
        pDirStart = pDiskHeader + (1 + SPF * 2) * NUM_BYTES_PER_SECTOR;
        memset(pDirStart, ' ', 8 + 3);
        LabelSize = strlen(volLavel);
        if (LabelSize <= 8 + 3)
            memcpy(pDirStart, volLavel, LabelSize);
        else
            memcpy(pDirStart, volLavel, 8 + 3);

        pDirStart[8 + 3] = GEMDOS_FILE_ATTRIB_VOLUME_LABEL;
    }

    // Always create a new file. We assume we have already checked if the file exists and the overwrite
    if (f_open(&dest_file, dest_path, FA_WRITE | FA_CREATE_ALWAYS) != FR_OK)
    {
        DPRINTF("Error creating the destination ST file!\n");
        free(pDiskHeader);
        return FR_NO_FILE;
    }

    // Write the header to the destination file
    fr = f_write(&dest_file, pDiskHeader, nHeaderSize, &bw); // Write it to the destination file
    if (fr != FR_OK)
    {
        DPRINTF("Error writing the header to the destination ST file!\n");
        free(pDiskHeader);
        return FR_DISK_ERR;
    }

    // Write zeros to the rest of the file

    memset(zeroBuff, 0, sizeof(zeroBuff)); // Set the buffer to zeros
    while (nDiskSizeNoHeader > 0)
    {
        UINT toWrite = sizeof(zeroBuff);
        if (nDiskSizeNoHeader < toWrite)
            toWrite = nDiskSizeNoHeader; // Write only as much as needed

        fr = f_write(&dest_file, zeroBuff, toWrite, &bw); // Write zeros to file
        if (fr != FR_OK || bw < toWrite)
        {
            fr = (fr == FR_OK) ? FR_DISK_ERR : fr; // If no error during write, set the error to disk error
            free(pDiskHeader);
            return fr;
        }
        nDiskSizeNoHeader -= bw; // Decrement the remaining size
    }

    // Close the file
    f_close(&dest_file);

    // Free buffer
    free(pDiskHeader);
    return FR_OK;
}

/**
 * @brief Copies a file within the same folder to a new file with a specified name.
 *
 * This function reads a specified file from a specified folder, and creates a new file
 * with a specified name in the same folder, copying the contents of the original file
 * to the new file. If a file with the specified new name already exists, the behavior
 * depends on the value of the overwrite_flag argument: if true, the existing file is
 * overwritten; if false, the function returns an error code and the operation is canceled.
 *
 * @param folder The path of the folder containing the source file, as a null-terminated string.
 * @param src_filename The name of the source file, as a null-terminated string.
 * @param dest_filename The name for the new file, as a null-terminated string.
 * @param overwrite_flag A flag indicating whether to overwrite the destination file
 *        if it already exists: true to overwrite, false to cancel the operation.
 *
 * @return A result code of type FRESULT, indicating the result of the operation:
 *         - FR_OK on success.
 *         - FR_FILE_EXISTS if the destination file exists and overwrite_flag is false.
 *         - Other FatFS error codes for other error conditions.
 *
 * @note This function uses the FatFS library to perform file operations, and is designed
 *       to work in environments where FatFS is available. It requires the ff.h header file.
 *
 * Usage:
 * @code
 * FRESULT result = copy_file("/folder", "source.txt", "destination.txt", true);  // Overwrite if destination.txt exists
 * @endcode
 */
FRESULT copy_file(const char *folder, const char *src_filename, const char *dest_filename, bool overwrite_flag)
{
    FRESULT fr;   // FatFS function common result code
    FIL src_file; // File objects
    FIL dest_file;
    UINT br, bw;       // File read/write count
    BYTE buffer[4096]; // File copy buffer

    char src_path[256];
    char dest_path[256];

    // Create full paths for source and destination files
    sprintf(src_path, "%s/%s", folder, src_filename);
    sprintf(dest_path, "%s/%s", folder, dest_filename);

    DPRINTF("Copying file '%s' to '%s'. Overwrite? %s\n", src_path, dest_path, overwrite_flag ? "YES" : "NO");

    // Check if the destination file already exists
    FILINFO fno;
    fr = f_stat(dest_path, &fno);
    if (fr == FR_OK && !overwrite_flag)
    {
        DPRINTF("Destination file exists and overwrite_flag is false, canceling operation\n");
        return FR_FILE_EXISTS; // Destination file exists and overwrite_flag is false, cancel the operation
    }

    // Open the source file
    fr = f_open(&src_file, src_path, FA_READ);
    if (fr != FR_OK)
    {
        DPRINTF("f_open error: %s (%d)\n", FRESULT_str(fr), fr);
        return fr;
    }

    // Create and open the destination file
    fr = f_open(&dest_file, dest_path, FA_CREATE_ALWAYS | FA_WRITE);
    if (fr != FR_OK)
    {
        DPRINTF("f_open error: %s (%d)\n", FRESULT_str(fr), fr);
        f_close(&src_file); // Close the source file if it was opened successfully
        return fr;
    }

    // Copy the file
    do
    {
        fr = f_read(&src_file, buffer, sizeof buffer, &br); // Read a chunk of source file
        if (fr != FR_OK)
            break;                                 // Break on error
        fr = f_write(&dest_file, buffer, br, &bw); // Write it to the destination file
    } while (fr == FR_OK && br == sizeof buffer);

    // Close files
    f_close(&src_file);
    f_close(&dest_file);

    DPRINTF("File copied\n");
    return fr; // Return the result
}

/**
 * @brief Checks if a directory exists in the filesystem.
 *
 * This function uses the FatFS library's f_stat() method to determine if
 * a specified directory exists on the filesystem. It returns 1 if the directory
 * exists and 0 otherwise.
 *
 * @param dir A pointer to a string containing the path of the directory to check.
 * @return int Returns 1 if the directory exists, 0 otherwise.
 */
int directory_exists(const char *dir)
{
    FILINFO fno;
    FRESULT res = f_stat(dir, &fno);

    // Check if the result is OK and if the attribute indicates it's a directory
    if (res == FR_OK && (fno.fattrib & AM_DIR))
    {
        return 1; // Directory exists
    }

    return 0; // Directory does not exist
}

/**
 * @brief Retrieves the total and free storage space of an SD card.
 *
 * This function queries the SD card's filesystem to obtain the total and free storage space.
 * It calculates the size in megabytes and updates the respective variables pointed by
 * `totalSize_MB` and `freeSpace_MB`.
 *
 * @param fs_ptr Pointer to the filesystem object associated with the SD card.
 * @param totalSize_MB Pointer to a variable where the total size of the SD card will be stored (in megabytes).
 * @param freeSpace_MB Pointer to a variable where the free space of the SD card will be stored (in megabytes).
 *
 * @note If the function fails to retrieve the free space information, the values pointed to by
 * `totalSize_MB` and `freeSpace_MB` are set to zero. This function assumes that `fs_ptr` is a valid
 * pointer to an initialized `FATFS` structure and that `totalSize_MB` and `freeSpace_MB` are valid
 * non-null pointers.
 *
 * Usage:
 *     FATFS fs;
 *     uint32_t totalSize, freeSpace;
 *     get_card_info(&fs, &totalSize, &freeSpace);
 */
void get_card_info(FATFS *fs_ptr, uint32_t *totalSize_MB, uint32_t *freeSpace_MB)
{
    DWORD fre_clust;

    // Set initial values to zero as a precaution
    *totalSize_MB = 0;
    *freeSpace_MB = 0;

    // Get volume information and free clusters of drive
    if (f_getfree("", &fre_clust, &fs_ptr) != FR_OK)
    {
        return; // Error handling: Set values to zero if getfree fails
    }

    // Calculate total sectors in the SD card
    uint64_t total_sectors = (fs_ptr->n_fatent - 2) * fs_ptr->csize;

    // Convert total sectors to bytes and then to megabytes
    *totalSize_MB = (total_sectors * NUM_BYTES_PER_SECTOR) / 1048576;

    // Convert free clusters to sectors and then to bytes
    uint64_t free_space_bytes = (uint64_t)fre_clust * fs_ptr->csize * NUM_BYTES_PER_SECTOR;

    // Convert bytes to megabytes
    *freeSpace_MB = free_space_bytes / 1048576;
}

/**
 * @brief Calculates the number of folders (including subfolders) within the given directory path.
 *
 * This function navigates through all directories and subdirectories starting from the given
 * `path` and counts each folder. It uses recursion to traverse the directory tree structure.
 * Files are not counted in the total - only directories contribute to the count.
 *
 * @param path A null-terminated string representing the path of the directory to count folders from.
 *
 * @return The function returns the total count of directories found in the provided path. If an error
 *         occurs during directory traversal (such as if the directory cannot be opened), the function
 *         returns 0.
 *
 * Usage:
 *     const char *directoryPath = "/my/folder/path";
 *     uint32_t folderCount = calculate_folder_count(directoryPath);
 *     printf("There are %u folders in '%s'\n", folderCount, directoryPath);
 */
uint32_t calculate_folder_count(const char *path)
{
    FRESULT res;
    DIR dir;
    FILINFO fno;
    uint32_t totalSize = 0;
    char newPath[256]; // Buffer to hold sub-directory paths

    res = f_opendir(&dir, path); // Open the directory
    if (res != FR_OK)
    {
        return 0; // Error, return 0
    }

    while (1)
    {
        res = f_readdir(&dir, &fno); // Read a directory item
        if (res != FR_OK || fno.fname[0] == 0)
        {
            break; // Error or end of dir, exit
        }

        if (fno.fattrib & AM_DIR)
        { // It's a directory
            sprintf(newPath, "%s/%s", path, fno.fname);
            totalSize += calculate_folder_count(newPath); // Recursive call
        }
        else
        { // It's a file
            totalSize += 1;
        }
    }

    f_closedir(&dir);
    return totalSize;
}

void get_sdcard_data(FATFS *fs, SdCardData *sd_data, bool microsd_mounted)
{

    sd_data->status = microsd_mounted ? SD_CARD_MOUNTED : SD_CARD_NOT_MOUNTED; // SD card status
    strncpy(sd_data->floppies_folder, find_entry("FLOPPIES_FOLDER")->value, MAX_FOLDER_LENGTH - 1);
    sd_data->floppies_folder[MAX_FOLDER_LENGTH - 1] = '\0'; // Ensure null termination

    strncpy(sd_data->roms_folder, find_entry("ROMS_FOLDER")->value, MAX_FOLDER_LENGTH - 1);
    sd_data->roms_folder[MAX_FOLDER_LENGTH - 1] = '\0'; // Ensure null termination

    strncpy(sd_data->harddisks_folder, "/harddisks", MAX_FOLDER_LENGTH - 1);
    sd_data->harddisks_folder[MAX_FOLDER_LENGTH - 1] = '\0'; // Ensure null termination

    if (microsd_mounted)
    {
        sd_data->floppies_folder_status = directory_exists(sd_data->floppies_folder) ? FLOPPIES_FOLDER_OK : FLOPPIES_FOLDER_NOTFOUND;
        sd_data->roms_folder_status = directory_exists(sd_data->roms_folder) ? ROMS_FOLDER_OK : ROMS_FOLDER_NOTFOUND;
        sd_data->harddisks_folder_status = directory_exists(sd_data->harddisks_folder) ? HARDDISKS_FOLDER_OK : HARDDISKS_FOLDER_NOTFOUND;

        uint32_t total, freeSpace;
        get_card_info(fs, &total, &freeSpace);

        sd_data->sd_size = total;
        sd_data->sd_free_space = freeSpace;

        sd_data->roms_folder_count = calculate_folder_count(sd_data->roms_folder);
        sd_data->floppies_folder_count = calculate_folder_count(sd_data->floppies_folder);
        sd_data->harddisks_folder_count = calculate_folder_count(sd_data->harddisks_folder);
    }
    else
    {
        sd_data->floppies_folder_status = FLOPPIES_FOLDER_NOTFOUND;
        sd_data->roms_folder_status = ROMS_FOLDER_NOTFOUND;
        sd_data->harddisks_folder_status = HARDDISKS_FOLDER_NOTFOUND;
        sd_data->sd_size = 0;
        sd_data->sd_free_space = 0;
        sd_data->roms_folder_count = 0;
        sd_data->floppies_folder_count = 0;
        sd_data->harddisks_folder_count = 0;
    }
    DPRINTF("SD card status: %d\n", sd_data->status);
    DPRINTF("SD card size: %d MB\n", sd_data->sd_size);
    DPRINTF("SD card free space: %d MB\n", sd_data->sd_free_space);
    DPRINTF("Floppies folder: %s\n", sd_data->floppies_folder);
    DPRINTF("Floppies folder status: %d\n", sd_data->floppies_folder_status);
    DPRINTF("Floppies folder file count: %d\n", sd_data->floppies_folder_count);
    DPRINTF("ROMs folder: %s\n", sd_data->roms_folder);
    DPRINTF("ROMs folder status: %d\n", sd_data->roms_folder_status);
    DPRINTF("ROMs folder file count: %d\n", sd_data->roms_folder_count);
    DPRINTF("Hard disks folder: %s\n", sd_data->harddisks_folder);
    DPRINTF("Hard disks folder status: %d\n", sd_data->harddisks_folder_status);
    DPRINTF("Hard disks folder file count: %d\n", sd_data->harddisks_folder_count);
}

/**
 * @brief Displays files in a given directory and returns an array of filenames.
 *
 * This function lists all files in the specified directory and allocates an array of strings
 * to store the filenames. It also returns the count of files found. If no directory is specified,
 * it uses the current working directory.
 *
 * @param dir A null-terminated string representing the directory path from which to list files.
 *            If this string is empty, the current working directory is used.
 * @param num_files A pointer to an integer where the function will store the number of files found.
 *
 * @return On success, the function returns a pointer to an array of string pointers, each
 *         representing a file name within the directory. On failure, it returns NULL.
 *
 * @note The returned array of filenames and the filenames themselves are dynamically allocated
 *       and should be freed by the caller to prevent memory leaks.
 *       Use `free()` to release each string and the array when done.
 *
 * @warning This function uses dynamic memory allocation for each file found. The caller is responsible
 *          for freeing the allocated memory to avoid memory leaks.
 *
 * Usage example:
 *     int fileCount = 0;
 *     char **fileList = show_dir_files("/my/directory", &fileCount);
 *     if (fileList) {
 *         release_memory_files(fileList, fileCount); // Free the memory when done
 *     }
 */
char **show_dir_files(const char *dir, int *num_files)
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
            DPRINTF("f_getcwd error: %s (%d)\n", FRESULT_str(fr), fr);
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
        DPRINTF("f_findfirst error: %s (%d)\n", FRESULT_str(fr), fr);
        return NULL;
    }

    while (fr == FR_OK && fno.fname[0] && fno.fname[0])
    {
        // Allocate space for a new pointer in the filenames array
        filenames = realloc(filenames, sizeof(char *) * (*num_files + 1));
        if (!filenames)
        {
            DPRINTF("Memory allocation failed\n");
            return NULL;
        }
        filenames[*num_files] = strdup(fno.fname); // Store the filename
        (*num_files)++;

        fr = f_findnext(&dj, &fno); // Search for next item
    }

    f_closedir(&dj);

    return filenames;
}

/**
 * @brief Frees memory allocated for the array of file names and its contents.
 *
 * This function is responsible for releasing the memory allocated for both the array of strings
 * that holds the file names and the strings themselves. It should be called when the list of file
 * names is no longer needed to prevent memory leaks.
 *
 * @param files A pointer to an array of string pointers, each pointing to a dynamically allocated
 *              string holding a file name. This should be the same array returned by the
 *              show_dir_files function.
 * @param num_files The number of file name strings in the array. This should be the same value
 *                  obtained from the show_dir_files function.
 *
 * @note This function should only be called with a pointer and count obtained from
 *       the show_dir_files function to avoid undefined behavior.
 *
 * Usage example:
 *     int fileCount = 0;
 *     char **fileList = show_dir_files("/my/directory", &fileCount);
 *     if (fileList) {
 *         // ... use the file list ...
 *         release_memory_files(fileList, fileCount); // Free the memory when done
 *     }
 */
void release_memory_files(char **files, int num_files)
{
    for (int i = 0; i < num_files; i++)
    {
        free(files[i]); // Free each string
    }
    free(files); // Free the list itself
}

/**
 * @brief Loads a ROM file from the filesystem into memory at a specified offset.
 *
 * This function opens a ROM file from the given path and filename, reads its contents into
 * a buffer, performs a byte order correction for endianness, and then programs it into
 * flash memory at a specified offset. It handles STEEM cartridge image peculiarities and
 * assumes certain ROM sizes for this check. The function outputs progress to a debug
 * interface and handles errors and end-of-file conditions.
 *
 * @param path A pointer to a string representing the directory path where the ROM file is located.
 * @param filename A pointer to a string representing the name of the ROM file to be loaded.
 * @param rom_load_offset The memory offset at which to start loading the ROM.
 *
 * @return (int)fr A cast integer value of the FatFs function common result code. If the value is
 *                 non-zero, it indicates an error occurred during file operations.
 *
 * @note The function is designed to work within a system with interrupt handling and flash
 *       memory programming capabilities, and it includes code to disable and restore interrupts
 *       during the flash programming process for system stability.
 *
 * Example usage:
 *     int loadResult = load_rom_from_fs("/roms", "game.rom", FLASH_MEMORY);
 *     if (loadResult != 0) {
 *         // Handle error
 *     }
 */
int load_rom_from_fs(char *path, char *filename, uint32_t rom_load_offset)
{
    FIL fsrc;                                           /* File objects */
    BYTE buffer[CONFIGURATOR_SHARED_MEMORY_SIZE_BYTES]; /* File copy buffer */
    FRESULT fr;                                         /* FatFs function common result code */
    unsigned int br = 0;                                /* File read/write count */
    unsigned int size = 0;                              // File size
    uint32_t dest_address = rom_load_offset;            // Initialize pointer to the ROM address

    char fullpath[512]; // Assuming 512 bytes as the max path+filename length. Adjust if necessary.
    snprintf(fullpath, sizeof(fullpath), "%s/%s", path, filename);

    DPRINTF("Loading file '%s'  ", fullpath);

    /* Open source file on the drive 0 */
    fr = f_open(&fsrc, fullpath, FA_READ);
    if (fr)
        return (int)fr;

    // Get file size
    size = f_size(&fsrc);
    DPRINTF("File size: %i bytes\n", size);

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
            DPRINTF("Skipping first 4 bytes. Looks like a STEEM cartridge image.\n");
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
        swap_words(buffer, br);

        // Transfer buffer to FLASH
        // WARNING! TRANSFER THE INFORMATION IN THE BUFFER AS LITTLE ENDIAN!!!!
        uint32_t ints = save_and_disable_interrupts();
        flash_range_program(dest_address, buffer, br);
        restore_interrupts(ints);

        dest_address += br; // Increment the pointer to the ROM address
        size += br;

        DPRINTF(".");
    }

    // Close open file
    f_close(&fsrc);

    DPRINTF(" %i bytes loaded\n", size);
    DPRINTF("File loaded at offset 0x%x\n", rom_load_offset);
    DPRINTF("Dest ROM address end is 0x%x\n", dest_address - 1);
    return (int)fr;
}

// Function to convert a string to lowercase
char *str_tolower(char *str)
{
    char *orig = str;
    // Convert each character in the string to lowercase
    while (*str)
    {
        *str = tolower((unsigned char)*str);
        str++;
    }
    return orig;
}

// Function to check if the file has an allowed extension
int has_allowed_extension(const char *filename, const char **allowed_extensions, size_t num_extensions)
{
    // Get the extension of the filename
    const char *ext = strrchr(filename, '.');
    if (!ext || ext == filename)
    {
        return 0; // No extension found
    }
    ext++; // Move past the dot

    // Convert to lowercase for case-insensitive comparison
    char lower_ext[256]; // Ensure this is large enough to hold the expected extensions
    strncpy(lower_ext, ext, sizeof(lower_ext));
    lower_ext[sizeof(lower_ext) - 1] = '\0'; // Ensure null-termination
    str_tolower(lower_ext);

    // Check against each allowed extension
    for (size_t i = 0; i < num_extensions; i++)
    {
        if (strcmp(lower_ext, allowed_extensions[i]) == 0)
        {
            return 1; // Extension is allowed
        }
    }
    return 0; // Extension is not allowed
}

/**
 * @brief Filters a list of filenames, removing any that start with a period.
 *
 * This function takes an array of strings where each string is a filename. It filters
 * out filenames that start with a period, which are typically hidden files in Unix-like
 * systems. The filtered list is allocated fresh memory and must be freed by the caller.
 *
 * @param file_list A pointer to an array of string pointers, each pointing to a filename.
 * @param file_count The total number of files in the initial file list.
 * @param num_files A pointer to an integer where the count of filtered filenames will be stored.
 *
 * @return char** A pointer to a newly allocated array of strings containing only the filtered
 *                filenames. If memory allocation fails, the program prints an error message
 *                and exits.
 *
 * @note It is the caller's responsibility to free the memory allocated for the filtered list
 *       as well as the individual strings within it. Failure to do so will result in a memory leak.
 *       Additionally, the function uses `strdup`, which also allocates memory for the new strings
 *       that needs to be managed.
 *
 * Example usage:
 *     int num_filtered_files;
 *     char **filtered_list = filter(original_file_list, original_file_count, &num_filtered_files);
 *     // Use filtered_list...
 *     // Eventually free the allocated memory for filtered_list and its contents.
 */
char **filter(char **file_list, int file_count, int *num_files, const char **allowed_extensions, size_t num_extensions)
{
    int validCount = 0;

    // Count valid filenames
    for (int i = 0; i < file_count; i++)
    {
        if ((file_list[i][0] != '.') && has_allowed_extension(file_list[i], allowed_extensions, num_extensions))
        {
            validCount++;
        }
    }

    // Allocate memory for the new array
    char **filtered_list = (char **)malloc(validCount * sizeof(char *));
    if (filtered_list == NULL)
    {
        DPRINTF("Failed to allocate memory");
        return NULL;
    }

    int index = 0;
    for (int i = 0; i < file_count; i++)
    {
        if (file_list[i][0] != '.' && has_allowed_extension(file_list[i], allowed_extensions, num_extensions))
        {
            filtered_list[index++] = strdup(file_list[i]);
            if (filtered_list[index - 1] == NULL)
            {
                DPRINTF("Failed to duplicate string");
                return NULL;
            }
        }
    }
    *num_files = validCount;
    return filtered_list;
}

/**
 * @brief Stores a list of file names in a specified memory location with specific formatting
 *       to be consumed remotely by the Atari ST configurator.
 *
 * This function takes a list of file names and stores them in a contiguous block of memory,
 * each file name is terminated by a null character (0x00). The function ensures that the
 * memory address after the last file name is even, adds an end-of-list marker, and then
 * performs an in-place conversion of the stored bytes from little endian to big endian format.
 * The function also checks if there is enough memory available to store the entire list.
 *
 * @param file_list An array of strings, where each string is a file name to store.
 * @param num_files The number of file names contained within the file_list array.
 * @param memory_location A pointer to the starting memory location where the file list should be stored.
 *
 * Example usage:
 *     uint8_t storage_area[CONFIGURATOR_SHARED_MEMORY_SIZE_BYTES];
 *     store_file_list(file_list, num_files, storage_area);
 *     // `storage_area` now contains the file names in the specified format.
 */
void store_file_list(char **file_list, int num_files, uint8_t *memory_location)
{
    uint8_t *dest_ptr = memory_location;

    int total_size = 0;
    // Iterate through each file in the file_list
    for (int i = 0; i < num_files; i++)
    {
        if ((total_size + strlen(file_list[i]) + 5) > CONFIGURATOR_SHARED_MEMORY_SIZE_BYTES)
        {
            DPRINTF("ERROR: Not enough memory to store the file list.\n");
            break;
        }
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
    swap_words(memory_location, total_size);
}

FRESULT read_and_trim_file(const char *path, char **content)
{
    FIL fil;              // File object
    FRESULT fr;           // FatFs return code
    UINT br;              // Read count
    char tempBuffer[512]; // Temporary buffer for reading file

    DPRINTF("Reading file: %s\n", path);
    // Check if the file exists
    fr = f_stat(path, NULL);
    if (fr != FR_OK)
    {
        DPRINTF("File does not exist or another error occur: %s\n", path);
        return fr; // File does not exist or another error occurred
    }

    DPRINTF("File exists: %s. Opening.\n", path);
    // Open the file
    fr = f_open(&fil, path, FA_READ);
    if (fr != FR_OK)
    {
        DPRINTF("Error opening file: %s\n", path);
        return fr; // Error opening file
    }

    long length = f_size(&fil);
    DPRINTF("File size: %ld\n", length);

    *content = malloc(length + 1);

    // Allocate or reallocate memory for content
    if (*content == NULL)
    {
        DPRINTF("Unable to allocate memory\n");
        f_close(&fil);
        return FR_INT_ERR; // Allocation error
    }

    DPRINTF("Reading file content\n");
    **content = '\0'; // Set first character to null

    // Read file content
    while (f_read(&fil, tempBuffer, sizeof(tempBuffer) - 1, &br) == FR_OK && br > 0)
    {
        tempBuffer[br] = '\0'; // Null-terminate the read string
        // Process and trim the buffer content here...
        // For simplicity, this code only trims spaces, CR, and LF from the start and end.
        char *start = tempBuffer;
        char *end = start + strlen(tempBuffer) - 1;

        // Trim leading spaces, CR, and LF
        while (*start && (isspace((unsigned char)*start) || *start == '\r' || *start == '\n'))
            start++;

        // Trim trailing spaces, CR, and LF
        while (end > start && (isspace((unsigned char)*end) || *end == '\r' || *end == '\n'))
            end--;
        *(end + 1) = '\0'; // Set new null terminator

        // Append trimmed content to *content
        size_t currentLength = strlen(*content);
        size_t newLength = currentLength + strlen(start) + 1;
        *content = realloc(*content, newLength);
        if (*content == NULL)
        {
            f_close(&fil);
            DPRINTF("Error allocating memory\n");
            return FR_INT_ERR; // Allocation error
        }
        strcat(*content, start);
    }

    // Close the file
    f_close(&fil);
    DPRINTF("File content: '%s'\n", *content);
    return FR_OK;
}