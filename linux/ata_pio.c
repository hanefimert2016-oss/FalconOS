/* =============================================================================
 *  FalconOS — ATA PIO (Linux libata-style) driver
 * -----------------------------------------------------------------------------
 *  Probe + IDENTIFY DEVICE + LBA28 PIO read for the primary IDE controller
 *  (IO base 0x1F0, control 0x3F6).  The driver layout mirrors how Linux's
 *  `drivers/ata/libata-core.c` partitions things:
 *
 *      ata_dev_init     – per-port reset + DRDY check
 *      ata_dev_identify – send IDENTIFY DEVICE, read 256 words
 *      ata_dev_read     – LBA28 PIO read of N sectors
 *
 *  This is a clean-room implementation against the AT-Attachment spec; no
 *  Linux source code was copied.  The naming is preserved so a real libata
 *  port-in can drop in alongside.
 * ============================================================================= */
#include "uapi.h"

#define ATA_PRI_IO   0x1F0
#define ATA_PRI_CTL  0x3F6

#define MAX_DEV 2
typedef struct {
    bool present;
    bool slave;
    u64  sectors;
    char model[41];     /* IDENTIFY returns 40 chars, + NUL */
} ata_dev_t;

static ata_dev_t DEV[MAX_DEV];
static i32       N_DEV = 0;

/* --------------------------------------------------------------------------- */
static void ata_io_wait(void)
{
    /* Reading the alt-status register four times is the canonical 400ns
     * delay — same trick libata uses (`ata_pause()`).                         */
    inb(ATA_PRI_CTL);
    inb(ATA_PRI_CTL);
    inb(ATA_PRI_CTL);
    inb(ATA_PRI_CTL);
}

static bool ata_wait_not_busy(u32 spin)
{
    while (spin--) {
        u8 s = inb(ATA_PRI_IO + ATA_REG_STATUS);
        if (!(s & ATA_SR_BSY)) return true;
    }
    return false;
}

static bool ata_wait_drq(u32 spin)
{
    while (spin--) {
        u8 s = inb(ATA_PRI_IO + ATA_REG_STATUS);
        if (s & ATA_SR_ERR)  return false;
        if (s & ATA_SR_DRQ)  return true;
    }
    return false;
}

/* --------------------------------------------------------------------------- */
/* ata_dev_identify — issues IDENTIFY DEVICE, populates dev->sectors, model. */
static bool ata_dev_identify(ata_dev_t *dev)
{
    /* Select master/slave  (LBA28: bit 6 = LBA, bit 4 = drive)              */
    outb(ATA_PRI_IO + ATA_REG_HDDEVSEL, dev->slave ? 0xB0 : 0xA0);
    ata_io_wait();

    /* Zero out non-essential regs */
    outb(ATA_PRI_IO + ATA_REG_SECCOUNT, 0);
    outb(ATA_PRI_IO + ATA_REG_LBA0,     0);
    outb(ATA_PRI_IO + ATA_REG_LBA1,     0);
    outb(ATA_PRI_IO + ATA_REG_LBA2,     0);

    /* IDENTIFY */
    outb(ATA_PRI_IO + ATA_REG_COMMAND, ATA_CMD_IDENTIFY);
    ata_io_wait();

    u8 s = inb(ATA_PRI_IO + ATA_REG_STATUS);
    if (s == 0)            return false;        /* no device on bus      */
    if (!ata_wait_not_busy(100000)) return false;

    /* If LBA1 / LBA2 are non-zero this is a non-ATA device (ATAPI/SATA). */
    if (inb(ATA_PRI_IO + ATA_REG_LBA1) != 0 ||
        inb(ATA_PRI_IO + ATA_REG_LBA2) != 0) return false;

    if (!ata_wait_drq(100000)) return false;

    /* Read 256 16-bit words of identify data ----------------------------- */
    u16 id[256];
    for (i32 i = 0; i < 256; i++) id[i] = inw(ATA_PRI_IO + ATA_REG_DATA);

    /* model — words 27..46, byte-swapped pairs                            */
    for (i32 i = 0; i < 20; i++) {
        u16 w = id[27 + i];
        dev->model[i*2]     = (char)(w >> 8);
        dev->model[i*2 + 1] = (char)(w & 0xFF);
    }
    dev->model[40] = 0;
    /* trim trailing spaces */
    for (i32 i = 39; i >= 0; i--) {
        if (dev->model[i] == ' ') dev->model[i] = 0;
        else break;
    }

    /* sector count: word 60 (low) + 61 (high)  (LBA28)                    */
    u32 lba28_sectors = ((u32)id[61] << 16) | id[60];
    dev->sectors = lba28_sectors;
    dev->present = true;
    return true;
}

/* --------------------------------------------------------------------------- */
/* ata_dev_pio28 — internal helper.  is_write = direction.                    */
static bool ata_dev_pio28(i32 idx, u32 lba, u8 *buf, u32 sectors, bool is_write)
{
    if (idx < 0 || idx >= MAX_DEV || !DEV[idx].present) return false;
    if (sectors == 0) return true;
    if (sectors > 255) return false;            /* LBA28 spec               */

    /* drive select (bit 6 = LBA, bit 4 = slave) + top 4 LBA bits           */
    u8 sel = (DEV[idx].slave ? 0xF0 : 0xE0) | ((lba >> 24) & 0x0F);
    outb(ATA_PRI_IO + ATA_REG_HDDEVSEL, sel);
    ata_io_wait();

    outb(ATA_PRI_IO + ATA_REG_FEATURES, 0);
    outb(ATA_PRI_IO + ATA_REG_SECCOUNT, (u8)sectors);
    outb(ATA_PRI_IO + ATA_REG_LBA0,     (u8)(lba       & 0xFF));
    outb(ATA_PRI_IO + ATA_REG_LBA1,     (u8)((lba >> 8) & 0xFF));
    outb(ATA_PRI_IO + ATA_REG_LBA2,     (u8)((lba >>16) & 0xFF));
    outb(ATA_PRI_IO + ATA_REG_COMMAND,
         is_write ? ATA_CMD_WRITE_PIO : ATA_CMD_READ_PIO);

    for (u32 s = 0; s < sectors; s++) {
        if (!ata_wait_not_busy(200000)) return false;
        if (!ata_wait_drq(200000))      return false;
        if (is_write) {
            outsw(ATA_PRI_IO + ATA_REG_DATA, buf + s * 512, 256);
            ata_io_wait();
        } else {
            insw (ATA_PRI_IO + ATA_REG_DATA, buf + s * 512, 256);
        }
    }
    if (is_write) {
        /* flush write cache; many emulators (incl. QEMU) require this */
        outb(ATA_PRI_IO + ATA_REG_COMMAND, ATA_CMD_CACHE_FLUSH);
        ata_wait_not_busy(400000);
    }
    return true;
}

/* Driver telemetry — visible from Settings ▸ Drivers and Developer mode. */
static u32 g_ata_reads = 0, g_ata_writes = 0;
static u32 g_ata_retries = 0, g_ata_failed = 0;

void ata_stats(u32 *reads, u32 *writes, u32 *retries, u32 *failed)
{
    if (reads)   *reads   = g_ata_reads;
    if (writes)  *writes  = g_ata_writes;
    if (retries) *retries = g_ata_retries;
    if (failed)  *failed  = g_ata_failed;
}

/* --------------------------------------------------------------------------- */
void ata_init(void)
{
    /* Probe for ATA devices on primary channel */
    ata_dev_t *dev0 = &DEV[0];
    dev0->present = false;
    dev0->slave = false;

    /* Try to identify master device */
    outb(ATA_PRI_IO + ATA_REG_HDDEVSEL, 0xA0);
    ata_io_wait();
    outb(ATA_PRI_IO + ATA_REG_SECCOUNT, 0);
    outb(ATA_PRI_IO + ATA_REG_LBA0, 0);
    outb(ATA_PRI_IO + ATA_REG_LBA1, 0);
    outb(ATA_PRI_IO + ATA_REG_LBA2, 0);
    outb(ATA_PRI_IO + ATA_REG_COMMAND, ATA_CMD_IDENTIFY);
    ata_io_wait();

    u8 s = inb(ATA_PRI_IO + ATA_REG_STATUS);
    if (s & ATA_SR_BSY) {
        return;
    }
    if (!(s & ATA_SR_DRDY)) {
        return;
    }

    u16 id[256];
    insw(ATA_PRI_IO + ATA_REG_DATA, id, 256);

    if (id[0] != 0 && id[0] != 0xFFFF) {
        for (i32 i = 0; i < 20; i++) {
            dev0->model[i*2] = (char)(id[27 + i] >> 8);
            dev0->model[i*2 + 1] = (char)(id[27 + i] & 0xFF);
        }
        dev0->model[40] = 0;
        for (i32 i = 39; i >= 0; i--) {
            if (dev0->model[i] == ' ') dev0->model[i] = 0;
            else break;
        }
        u32 lba28_sectors = ((u32)id[61] << 16) | id[60];
        if (lba28_sectors > 0) {
            dev0->sectors = lba28_sectors;
            dev0->present = true;
            N_DEV = 1;
        }
    }

    DEV[1].present = false;
    DEV[1].slave = true;
    outb(ATA_PRI_IO + ATA_REG_HDDEVSEL, 0xB0);
    ata_io_wait();
    outb(ATA_PRI_IO + ATA_REG_COMMAND, ATA_CMD_IDENTIFY);
    ata_io_wait();
    s = inb(ATA_PRI_IO + ATA_REG_STATUS);
    if (!(s & ATA_SR_BSY) && (s & ATA_SR_DRDY)) {
        insw(ATA_PRI_IO + ATA_REG_DATA, id, 256);
        if (id[0] != 0 && id[0] != 0xFFFF) {
            for (i32 i = 0; i < 20; i++) {
                DEV[1].model[i*2] = (char)(id[27 + i] >> 8);
                DEV[1].model[i*2 + 1] = (char)(id[27 + i] & 0xFF);
            }
            DEV[1].model[40] = 0;
            for (i32 i = 39; i >= 0; i--) {
                if (DEV[1].model[i] == ' ') DEV[1].model[i] = 0;
                else break;
            }
            u32 lba28_sectors = ((u32)id[61] << 16) | id[60];
            if (lba28_sectors > 0) {
                DEV[1].sectors = lba28_sectors;
                DEV[1].present = true;
                N_DEV = 2;
            }
        }
    }
}

/* Up to 3 attempts per logical request; intermediate attempts bump the
 * retry counter so the user can spot a flaky controller in Settings.   */
bool ata_read_lba28(i32 dev, u32 lba, u8 *buf512, u32 sectors)
{
    g_ata_reads++;
    for (i32 attempt = 0; attempt < 3; attempt++) {
        if (ata_dev_pio28(dev, lba, buf512, sectors, false)) return true;
        g_ata_retries++;
    }
    g_ata_failed++;
    return false;
}

bool ata_write_lba28(i32 dev, u32 lba, const u8 *buf512, u32 sectors)
{
    g_ata_writes++;
    for (i32 attempt = 0; attempt < 3; attempt++) {
        if (ata_dev_pio28(dev, lba, (u8 *)buf512, sectors, true)) return true;
        g_ata_retries++;
    }
    g_ata_failed++;
    return false;
}

/* --------------------------------------------------------------------------- */
i32 ata_probe_count(void) { return N_DEV; }

const char *ata_model(i32 idx)
{
    if (idx < 0 || idx >= MAX_DEV || !DEV[idx].present) return "(absent)";
    return DEV[idx].model;
}

u64 ata_sectors(i32 idx)
{
    if (idx < 0 || idx >= MAX_DEV || !DEV[idx].present) return 0;
    return DEV[idx].sectors;
}
