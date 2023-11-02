/**
 * File: imgformats.c
 * Author: Diego Parrilla Santamaría
 * Date: Novemeber 2023
 * Copyright: 2023 - GOODDATA LABS SL
 * Description: C file that contains the functions to convert from a file format to another
 * The original code comes from Hatari emulator (https://github.com/hatari/hatari/blob/69ce09390824a29c9708ae41290d6df28a5f766e/src/msa.c)
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

/**
 * @brief Converts an MSA disk image file to an ST disk image file.
 *
 * This function takes a given MSA disk image file,
 * represented by `msaFilename` located within the `folder` directory, and
 * converts it into an ST (Atari ST disk image) file specified by `stFilename`.
 * If the `overwrite_flag` is set to true, any existing file with the same
 * name as `stFilename` will be overwritten.
 *
 * @param folder The directory where the MSA file is located and the ST file will be saved.
 * @param msaFilename The name of the MSA file to convert.
 * @param stFilename The name of the ST file to be created.
 * @param overwrite_flag If true, any existing ST file will be overwritten.
 *
 * @return FRESULT A FatFS result code indicating the status of the operation.
 */
FRESULT MSA_to_ST(const char *folder, char *msaFilename, char *stFilename, bool overwrite_flag)
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
    if (fr == FR_OK && !overwrite_flag)
    {
        DPRINTF("Destination file exists and overwrite_flag is false, canceling operation\n");
        return FR_FILE_EXISTS; // Destination file exists and overwrite_flag is false, cancel the operation
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

    nBytesLeft -= sizeof(MSAHEADERSTRUCT);
    // The length of the first track to read
    uint16_t currentTrackDataLength = bswap_16((uint16_t) * (uint16_t *)(buffer_in + sizeof(MSAHEADERSTRUCT)));

    /* Uncompress to memory as '.ST' disk image - NOTE: assumes 512 bytes
     * per sector (use NUMBYTESPERSECTOR define)!!! */
    for (Track = msaHeader.StartingTrack; Track <= msaHeader.EndingTrack; Track++)
    {
        for (Side = 0; Side < (msaHeader.Sides + 1); Side++)
        {
            uint16_t nBytesPerTrack = NUMBYTESPERSECTOR * msaHeader.SectorsPerTrack;
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
