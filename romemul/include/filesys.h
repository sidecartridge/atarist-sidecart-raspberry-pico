#ifndef FILESYS_H
#define FILESYS_H

#include "debug.h"
#include "constants.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#include "sd_card.h"
#include "f_util.h"

#include "config.h"

#define GEMDOS_FILE_ATTRIB_VOLUME_LABEL 8

#define NUM_BYTES_PER_SECTOR 512
#define SPF_MAX 9

#define MAX_FOLDER_LENGTH 128 // Max length of the folder names

#define STORAGE_POLL_INTERVAL 30000

#define FS_ST_READONLY 0x1 // Read only
#define FS_ST_HIDDEN 0x2   // Hidden
#define FS_ST_SYSTEM 0x4   // System
#define FS_ST_LABEL 0x8    // Volume label
#define FS_ST_FOLDER 0x10  // Directory
#define FS_ST_ARCH 0x20    // Archive

#define bswap_16(x) (((x) >> 8) | (((x) & 0xFF) << 8))

typedef enum
{
    SD_CARD_MOUNTED = 0,       // SD card is OK
    SD_CARD_NOT_MOUNTED,       // SD not mounted
    ROMS_FOLDER_OK = 100,      // ROMs folder is OK
    ROMS_FOLDER_NOTFOUND,      // ROMs folder error
    FLOPPIES_FOLDER_OK = 200,  // Floppies folder is OK
    FLOPPIES_FOLDER_NOTFOUND,  // Floppies folder error
    HARDDISKS_FOLDER_OK = 300, // Hard disks folder is OK
    HARDDISKS_FOLDER_NOTFOUND, // Hard disks folder error
} StorageStatus;

typedef struct sd_data
{
    char roms_folder[MAX_FOLDER_LENGTH];      // ROMs folder name
    char floppies_folder[MAX_FOLDER_LENGTH];  // Floppies folder name
    char harddisks_folder[MAX_FOLDER_LENGTH]; // Hard disks folder name
    uint32_t sd_size;                         // SD card size
    uint32_t sd_free_space;                   // SD card free space
    uint32_t roms_folder_count;               // ROMs folder number of files
    uint32_t floppies_folder_count;           // Floppies folder number of files
    uint32_t harddisks_folder_count;          // Hard disks folder number of files
    uint16_t status;                          // Status of the SD card
    uint16_t roms_folder_status;              // ROMs folder status
    uint16_t floppies_folder_status;          // Floppies folder status
    uint16_t harddisks_folder_status;         // Hard disks folder status
} SdCardData;

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
FRESULT copy_file(const char *folder, const char *src_filename, const char *dest_filename, bool overwrite_flag);
int directory_exists(const char *dir);
void get_card_info(FATFS *fs_ptr, uint32_t *totalSize_MB, uint32_t *freeSpace_MB);
uint32_t calculate_folder_count(const char *path);
void get_sdcard_data(FATFS *fs, SdCardData *sd_data, const SdCardData *sd_data_src, bool is_fcount_enabled);
bool is_sdcard_mounted(FATFS *fs_ptr);
char **show_dir_files(const char *dir, int *num_files);
void release_memory_files(char **files, int num_files);
int load_rom_from_fs(char *path, char *filename, uint32_t rom_load_offset);
char **filter(char **file_list, int file_count, int *num_files, const char **allowed_extensions, size_t num_extensions);
void store_file_list(char **file_list, int num_files, uint8_t *memory_location);
FRESULT read_and_trim_file(const char *path, char **content);
void split_fullpath(const char *fullPath, char *drive, char *folders, char *filePattern);
void back_2_forwardslash(char *path);
void shorten_fname(const char *originalName, char shortenedName[12]);
void remove_dup_slashes(char *str);
uint8_t attribs_st2fat(uint8_t st_attribs);
uint8_t attribs_fat2st(uint8_t fat_attribs);
void get_attribs_st_str(char attribs_str[6], uint8_t st_attribs);
void upper_fname(const char *originalName, char upperName[14]);
void filter_fname(const char *originalName, char filteredName[14]);
void extract_filename(const char *url, char filename[256]);

#endif // FILESYS_H