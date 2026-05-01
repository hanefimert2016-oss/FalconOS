/* =============================================================================
 *  FalconOS — Linux UAPI compatibility shim
 * -----------------------------------------------------------------------------
 *  Clean-room reimplementation of the small subset of linux UAPI headers that
 *  FalconOS uses internally.  The names + structure match Linux kernel UAPI
 *  exactly so future Linux-driver port-ins compile unmodified, but no Linux
 *  source code was copied: every constant comes from publicly-available
 *  device specs (AT-Attachment, USB-HID 1.11, virtio 1.1).
 *
 *  License: same as FalconOS (see ../LICENSE).
 * ============================================================================= */
#ifndef FALCON_LINUX_UAPI_H
#define FALCON_LINUX_UAPI_H

#include "falcon.h"

/* ---- <linux/types.h> ----------------------------------------------------- */
typedef u8   __u8;
typedef u16  __u16;
typedef u32  __u32;
typedef u64  __u64;
typedef i8   __s8;
typedef i16  __s16;
typedef i32  __s32;
typedef i64  __s64;

/* Used by ATA & SCSI return codes throughout the Linux block layer.        */
#define LINUX_BLK_STS_OK       0
#define LINUX_BLK_STS_TIMEOUT  1
#define LINUX_BLK_STS_NOTREADY 2
#define LINUX_BLK_STS_IOERR    3

/* ---- <linux/ata.h> — minimal IDE/AT-Attachment registers ----------------- */
#define ATA_REG_DATA       0x00
#define ATA_REG_ERROR      0x01
#define ATA_REG_FEATURES   0x01
#define ATA_REG_SECCOUNT   0x02
#define ATA_REG_LBA0       0x03
#define ATA_REG_LBA1       0x04
#define ATA_REG_LBA2       0x05
#define ATA_REG_HDDEVSEL   0x06
#define ATA_REG_COMMAND    0x07
#define ATA_REG_STATUS     0x07
#define ATA_REG_ALT_STATUS 0x0C
#define ATA_REG_DEV_CTL    0x0C

#define ATA_SR_BSY    0x80  /* device is busy            */
#define ATA_SR_DRDY   0x40  /* device ready              */
#define ATA_SR_DRQ    0x08  /* data request ready        */
#define ATA_SR_ERR    0x01  /* error                     */

#define ATA_CMD_IDENTIFY      0xEC  /* AT-Attachment identify device */
#define ATA_CMD_READ_PIO      0x20  /* LBA28 read sectors            */
#define ATA_CMD_WRITE_PIO     0x30
#define ATA_CMD_CACHE_FLUSH   0xE7

/* ---- <linux/hid.h> — keycodes (Linux event-input subset) ---------------- */
#define KEY_RESERVED   0
#define KEY_ESC_LX     1
#define KEY_1_LX       2
#define KEY_2_LX       3
#define KEY_BACKSPACE_LX 14
#define KEY_TAB_LX     15
#define KEY_ENTER_LX   28
#define KEY_LEFTCTRL_LX 29
#define KEY_LEFTSHIFT_LX 42
#define KEY_RIGHTSHIFT_LX 54
#define KEY_LEFTALT_LX 56
#define KEY_SPACE_LX   57
#define KEY_F1_LX      59
#define KEY_F2_LX      60

#endif /* FALCON_LINUX_UAPI_H */
