#ifndef IMGFORMATS_H
#define IMGFORMATS_H

#include "debug.h"
#include "constants.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#include "sd_card.h"
#include "f_util.h"

#define GEMDOS_FILE_ATTRIB_VOLUME_LABEL 8

#define NUM_BYTES_PER_SECTOR 512
#define SPF_MAX 9

#define bswap_16(x) (((x) >> 8) | (((x)&0xFF) << 8))

typedef struct
{
    uint16_t ID;              /* Word : ID marker, should be $0E0F */
    uint16_t SectorsPerTrack; /* Word : Sectors per track */
    uint16_t Sides;           /* Word : Sides (0 or 1; add 1 to this to get correct number of sides) */
    uint16_t StartingTrack;   /* Word : Starting track (0-based) */
    uint16_t EndingTrack;     /* Word : Ending track (0-based) */
} MSAHEADERSTRUCT;

// Define the structure to hold floppy image parameters
typedef struct
{
    uint16_t template;
    uint16_t num_tracks;
    uint16_t num_sectors;
    uint16_t num_sides;
    uint16_t overwrite;
    char volume_name[14]; // Round to 14 to avoid problems with odd addresses
    char floppy_name[256];
} FloppyImageHeader;

FRESULT checkDiskSpace(const char *path, uint32_t nDiskSize);
FRESULT MSA_to_ST(const char *folder, char *msaFilename, char *stFilename, bool overwrite_flag);
FRESULT create_blank_ST_image(const char *folder, char *stFilename, int nTracks, int nSectors, int nSides, const char *volLavel, bool overwrite);

#endif // IMGFORMATS_H