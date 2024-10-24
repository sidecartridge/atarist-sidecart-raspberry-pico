/**
 * File: usb_mass.c
 * Author: Diego Parrilla Santamaría
 * Date: June 2024
 * Copyright: 2024 - GOODDATA LABS SL
 * Description: USB Mass storage device for the SD card
 */

#include "include/usb_mass.h"

static DWORD sz_drv;
static DWORD sz_sect = 0;

void cdc_task(void);

void usb_mass_init()
{
    ConfigEntry *sd_card_mass_storage_enabled = find_entry(PARAM_SD_MASS_STORAGE);
    if ((sd_card_mass_storage_enabled != NULL) && (sd_card_mass_storage_enabled->value[0] == 't' || sd_card_mass_storage_enabled->value[0] == 'T'))
    {
        DPRINTF("USB Mass storage flag set to enabled\n");
        // Initialize SD card
        // Physical drive number
        BYTE const pdrv = 0;
        // Initialize the disk subsystem
        DSTATUS ds = disk_initialize(pdrv);
        if (!(0 == (ds & STA_NOINIT)))
        {
            DPRINTF("ERROR: Could not initialize SD card in block storage mode\r\n");
        }
        else
        {
            DPRINTF("SD card initialized\n");
            // Start the USB Mass storage device
            usb_mass_start();
        }
    }
    else
    {
        DPRINTF("USB Mass storage flag set to disabled\n");
    }
}

void usb_mass_start(void)
{
    // Init USB
    DPRINTF("Init USB\n");
    // init device stack on configured roothub port
    tud_init(BOARD_TUD_RHPORT);

    // Turn on the LED
    cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 1);

    // Loop while the USB is connected and the VBUS is high
    // Exit when the VBUS is low and reset the device
    // while (cyw43_arch_gpio_get(CYW43_WL_GPIO_VBUS_PIN))
    while (1)
    {
        tud_task(); // tinyusb device task
        cdc_task();
    }
    reboot();
    while (1)
        ;
}

//--------------------------------------------------------------------+
// Device callbacks
//--------------------------------------------------------------------+

// Invoked when device is mounted
void tud_mount_cb(void)
{
    DPRINTF("Device mounted\n");
    //   if (!mount_fatfs_disk())
    //     create_fatfs_disk();
}

// Invoked when device is unmounted
void tud_umount_cb(void)
{
    DPRINTF("Device unmounted\n");
}

// Invoked when usb bus is suspended
// remote_wakeup_en : if host allow us  to perform remote wakeup
// Within 7ms, device must draw an average of current less than 2.5 mA from bus
void tud_suspend_cb(bool remote_wakeup_en)
{
    (void)remote_wakeup_en;
    DPRINTF("Device suspended\n");
    //  blink_interval_ms = BLINK_SUSPENDED;
}

// Invoked when usb bus is resumed
void tud_resume_cb(void)
{
    DPRINTF("Device resumed\n");
    //  blink_interval_ms = BLINK_MOUNTED;
}

//--------------------------------------------------------------------+
// USB CDC
//--------------------------------------------------------------------+
void cdc_task(void)
{
    // connected() check for DTR bit
    // Most but not all terminal client set this when making connection
    // if ( tud_cdc_connected() )
    {
        // connected and there are data available
        if (tud_cdc_available())
        {
            // read data
            char buf[64];
            uint32_t count = tud_cdc_read(buf, sizeof(buf));
            (void)count;

            // Echo back
            // Note: Skip echo by commenting out write() and write_flush()
            // for throughput test e.g
            //    $ dd if=/dev/zero of=/dev/ttyACM0 count=10000
            tud_cdc_write(buf, count);
            tud_cdc_write_flush();
        }
    }
}

// Invoked when cdc when line state changed e.g connected/disconnected
void tud_cdc_line_state_cb(uint8_t itf, bool dtr, bool rts)
{
    (void)itf;
    (void)rts;

    // TODO set some indicator
    if (dtr)
    {
        // Terminal connected
    }
    else
    {
        // Terminal disconnected
    }
}

// Invoked when CDC interface received data from host
void tud_cdc_rx_cb(uint8_t itf)
{
    (void)itf;
}

// whether host does safe-eject
static bool ejected = false;

// Invoked when received SCSI_CMD_INQUIRY
// Application fill vendor id, product id and revision with string up to 8, 16, 4 characters respectively
void tud_msc_inquiry_cb(uint8_t lun, uint8_t vendor_id[8], uint8_t product_id[16], uint8_t product_rev[4])
{
    (void)lun;

    const char vid[] = "SidecarT";
    const char pid[] = "MultideviceMass";
    const char rev[] = RELEASE_VERSION;

    memcpy(vendor_id, vid, strlen(vid));
    memcpy(product_id, pid, strlen(pid));
    memcpy(product_rev, rev, strlen(rev));

    DPRINTF("Inquiry\n");
}

// Invoked when received Test Unit Ready command.
// return true allowing host to read/write this LUN e.g SD card inserted
bool tud_msc_test_unit_ready_cb(uint8_t lun)
{
    (void)lun;

    // RAM disk is ready until ejected
    if (ejected)
    {
        // Additional Sense 3A-00 is NOT_FOUND
        tud_msc_set_sense(lun, SCSI_SENSE_NOT_READY, 0x3a, 0x00);
        return false;
    }

    return true;
}

// Invoked when received SCSI_CMD_READ_CAPACITY_10 and SCSI_CMD_READ_FORMAT_CAPACITY to determine the disk size
// Application update block count and block size
void tud_msc_capacity_cb(uint8_t lun, uint32_t *block_count, uint16_t *block_size)
{
    (void)lun;

    DPRINTF("Capacity\n");
    BYTE pdrv = 0;
    DRESULT dr = disk_ioctl(pdrv, GET_SECTOR_COUNT, &sz_drv);
    if (dr != RES_OK)
    {
        DPRINTF("disk_ioctl GET_SECTOR_COUNT failed: %d\n", dr);
        return;
    }
    DPRINTF("Sector count: %lu\n", sz_drv);
#if FF_MAX_SS != FF_MIN_SS
    dr = disk_ioctl(pdrv, GET_SECTOR_SIZE, (UINT)&sz_sect);
    if (dr != RES_OK)
    {
        DPRINTF("disk_ioctl GET_SECTOR_SIZE failed: %d\n", dr);
        return;
    }
#else
    sz_sect = FF_MAX_SS;
#endif
    DPRINTF("Sector size: %u\n", sz_sect);

    *block_count = sz_drv;
    *block_size = sz_sect;
}

// Invoked when received Start Stop Unit command
// - Start = 0 : stopped power mode, if load_eject = 1 : unload disk storage
// - Start = 1 : active mode, if load_eject = 1 : load disk storage
bool tud_msc_start_stop_cb(uint8_t lun, uint8_t power_condition, bool start, bool load_eject)
{
    (void)lun;
    (void)power_condition;

    DPRINTF("Start/Stop Unit\n");

    if (load_eject)
    {
        if (start)
        {
            // load disk storage
            DPRINTF("LOAD DISK STORAGE\n");
        }
        else
        {
            // unload disk storage
            DPRINTF("UNLOAD DISK STORAGE\n");
            ejected = true;
        }
    }

    return true;
}

// Callback invoked when received READ10 command.
// Copy disk's data to buffer (up to bufsize) and return number of copied bytes.
int32_t tud_msc_read10_cb(uint8_t lun, uint32_t lba, uint32_t offset, void *buffer, uint32_t bufsize)
{
    (void)lun;

    if (offset != 0)
        return -1;
    if (bufsize != sz_sect)
        return -1;
    if (lba >= sz_drv)
        return -1;

    DPRINTF("Read10 LBA %lu, bufsize %lu, offset %lu\n", lba, bufsize, offset);

    DRESULT res = disk_read(0, buffer, lba, 1);

    if (res != RES_OK)
        return res;

    return (int32_t)bufsize;
}

bool tud_msc_is_writable_cb(uint8_t lun)
{
    (void)lun;
    return !USBDRIVE_READ_ONLY;
}

int32_t tud_msc_write10_cb(uint8_t lun, uint32_t lba, uint32_t offset, uint8_t *buffer, uint32_t bufsize)
{
    (void)lun;

    if (offset != 0)
        return -1;
    if (bufsize != sz_sect)
        return -1;
    if (lba >= sz_drv)
        return -1;

    DPRINTF("Write10 LBA %lu, Offset %lu, Size %lu\n", lba, offset, bufsize);

    DRESULT res = disk_write(0, buffer, lba, 1);
    if (res != RES_OK)
        return res;

    int32_t status = 0;

    return (int32_t)bufsize;
}

// Callback invoked when received an SCSI command not in built-in list below
// - READ_CAPACITY10, READ_FORMAT_CAPACITY, INQUIRY, MODE_SENSE6, REQUEST_SENSE
// - READ10 and WRITE10 has their own callbacks
int32_t tud_msc_scsi_cb(uint8_t lun, uint8_t const scsi_cmd[16], void *buffer, uint16_t bufsize)
{
    // read10 & write10 has their own callback and MUST not be handled here

    DPRINTF("SCSI Cmd %02X\n", scsi_cmd[0]);

    void const *response = NULL;
    int32_t resplen = 0;

    // most scsi handled is input
    bool in_xfer = true;

    switch (scsi_cmd[0])
    {
    case SCSI_CMD_PREVENT_ALLOW_MEDIUM_REMOVAL:
        // Host is about to read/write etc ... better not to disconnect disk
        resplen = 0;
        break;
    default:
        // Set Sense = Invalid Command Operation
        tud_msc_set_sense(lun, SCSI_SENSE_ILLEGAL_REQUEST, 0x20, 0x00);

        // negative means error -> tinyusb could stall and/or response with failed status
        resplen = -1;
        break;
    }

    // return resplen must not larger than bufsize
    if (resplen > bufsize)
        resplen = bufsize;

    if (response && (resplen > 0))
    {
        if (in_xfer)
        {
            memcpy(buffer, response, (size_t)resplen);
        }
        else
        {
            // SCSI output
        }
    }

    return (int32_t)resplen;
}