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

#define NUMBYTESPERSECTOR 512

#define bswap_16(x) (((x) >> 8) | (((x)&0xFF) << 8))

typedef struct
{
    uint16_t ID;              /* Word : ID marker, should be $0E0F */
    uint16_t SectorsPerTrack; /* Word : Sectors per track */
    uint16_t Sides;           /* Word : Sides (0 or 1; add 1 to this to get correct number of sides) */
    uint16_t StartingTrack;   /* Word : Starting track (0-based) */
    uint16_t EndingTrack;     /* Word : Ending track (0-based) */
} MSAHEADERSTRUCT;

FRESULT MSA_to_ST(const char *folder, char *msaFilename, char *stFilename, bool overwrite_flag);

#endif // IMGFORMATS_H