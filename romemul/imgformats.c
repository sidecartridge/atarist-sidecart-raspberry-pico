/**
 * File: imgformats.c
 * Author: Diego Parrilla Santamaría
 * Date: Novemeber 2023
 * Copyright: 2023 - GOODDATA LABS SL
 * Description: C file that contains the functions to convert from a file format to another
 * The original code comes from Hatari emulator:
 *      https://github.com/hatari/hatari/blob/69ce09390824a29c9708ae41290d6df28a5f766e/src/msa.c
 *
 * Please repect the original license
 */

#include "include/imgformats.h"

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

    /* Uncompress to memory as '.ST' disk image - NOTE: assumes 512 bytes
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
    BYTE zeroBuff[512]; // Temporary buffer to hold zeros

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
    pDiskHeader[512] = MediaByte;
    pDiskHeader[513] = pDiskHeader[514] = 0xFF;
    /* Set correct media bytes in the 2nd FAT: */
    pDiskHeader[512 + SPF * 512] = MediaByte;
    pDiskHeader[513 + SPF * 512] = pDiskHeader[514 + SPF * 512] = 0xFF;

    /* Set volume label if needed (in 1st entry of the directory) */
    if (volLavel != NULL)
    {
        /* Set 1st dir entry as 'volume label' */
        pDirStart = pDiskHeader + (1 + SPF * 2) * 512;
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