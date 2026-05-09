/* =============================================================================
 *  FalconOS — Linux compatibility layer initialization
 * -----------------------------------------------------------------------------
 *  Initializes all Linux-compatible drivers and provides system summary.
 *  The actual implementations are in ata_pio.c and virtio_net.c.
 * ============================================================================= */
#include "uapi.h"

/* Forward declarations for driver initialization - defined in respective drivers */
extern void ata_init(void);
extern void hid_keymap_init(void);
extern bool net_init(void);
extern void usb_init(void);

/* Linux compat init is already defined in ata_pio.c - this file adds net init */
void linux_compat_init(void)
{
    /* Call ATA init (already defined in ata_pio.c) */
    ata_init();

    /* Initialize HID keymap */
    hid_keymap_init();

    /* Initialize network */
    net_init();

    /* Initialize USB (OHCI/EHCI) */
    usb_init();
}

/* Linux compat summary is already defined in ata_pio.c */
extern const char *linux_compat_summary_ata(void);

const char *linux_compat_summary(void)
{
    static char buf[128];
    i32 n = ata_probe_count();

    k_strcpy(buf, "linux-compat: ");
    if (n > 0) {
        k_strcat(buf, "ata ");
        char tmp[8];
        k_itoa(n, tmp, 10);
        k_strcat(buf, tmp);
        k_strcat(buf, " devs");
    }

    if (net_present()) {
        k_strcat(buf, " | ");
        if (net_connected()) {
            k_strcat(buf, "net ");
            k_strcat(buf, net_ip_addr());
        } else {
            k_strcat(buf, "net (disconnected)");
        }
    }

    return buf;
}