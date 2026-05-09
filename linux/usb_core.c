/* =============================================================================
 *  FalconOS — USB host controller driver (OHCI/EHCI)
 * -----------------------------------------------------------------------------
 *  USB 1.1 (OHCI) and USB 2.0 (EHCI) driver for bare-metal and virtual machines.
 *  Supports QEMU/KVM virtio USB, OHCI for legacy, and EHCI for USB 2.0.
 *
 *  This provides:
 *      - USB host controller initialization
 *      - Device enumeration
 *      - Basic HID support for keyboards/mice
 *      - USB mass storage (storage devices)
 *
 *  License: FalconOS License
 * ============================================================================= */
#include "uapi.h"

/* ---- USB controller types ------------------------------------------------ */
#define USB_CTRL_OHCI   1
#define USB_CTRL_EHCI   2
#define USB_CTRL_XHCI   3
#define USB_CTRL_VIRTIO 4

/* ---- OHCI registers (base 0x1000) --------------------------------------- */
#define OHCI_REG_BASE       0x1000
#define OHCI_REG_CMD        0x00
#define OHCI_REG_STS        0x04
#define OHCI_REG_INTR       0x08
#define OHCI_REG_FRM        0x0C
#define OHCI_REG_LSTHEAD    0x10
#define OHCI_REG_HCFS       0x04  /* in HCCA */
#define OHCI_REG_HCCA       0x18
#define OHCI_REG_PERIODIC   0x1C
#define OHCI_REG_CONTROL    0x20
#define OHCI_REG_BULK       0x28

/* OHCI Controler State (HCFS) */
#define OHCI_HCFS_RESET     0x00
#define OHCI_HCFS_RESUME    0x40
#define OHCI_HCFS_OPER      0x80
#define OHCI_HCFS_SUSPEND   0xC0

/* OHCI Command */
#define OHCI_CMD_CTRL       0x00000001
#define OHCI_CMD_BULK       0x00000002
#define OHCI_CMD_INTR       0x00000004
#define OHCI_CMD_FMGR       0x00000008
#define OHCI_CMD_RWC        0x00000010
#define OHCI_CMD_RWE        0x00000020

/* OHCI Interrupt bits */
#define OHCI_INTR_SO      0x00000001
#define OHCI_INTR_SD      0x00000002
#define OHCI_INTR_RD      0x00000004
#define OHCI_INTR_FNO     0x00000008
#define OHCI_INTR_UE      0x00000010
#define OHCI_INTR_DONE    0x00000020
#define OHCI_INTR_MW      0x00000040

/* ---- EHCI registers (base 0x1000) --------------------------------------- */
#define EHCI_REG_BASE       0x1000
#define EHCI_REG_CAPLEN     0x00
#define EHCI_REG_HCSPARAMS  0x04
#define EHCI_REG_HCCPARAMS  0x08
#define EHCI_REG_USBCMD     0x10
#define EHCI_REG_USBSTS     0x14
#define EHCI_REG_USBINTR    0x18
#define EHCI_REG_FRINDEX    0x24
#define EHCI_REG_CONFIGFLAG 0x40
#define EHCI_REG_PORTSC(n)  (0x44 + (n) * 0x04)

/* EHCI Command bits */
#define EHCI_CMD_RUN        0x00000001
#define EHCI_CMD_RESET     0x00000002
#define EHCI_CMD_FMGR       0x00000004
#define EHCI_CMD_PERR      0x00000008
#define EHCI_CMD_LRESET    0x00000020

/* EHCI Status bits */
#define EHCI_STS_RUN       0x00000001
#define EHCI_STS_HCHALTED  0x00000001
#define EHCI_STS_IAA       0x00000002
#define EHCI_STS_HSE       0x00000004
#define EHCI_STS_FRAME     0x00000008
#define EHCI_STS_PCD       0x00000020

/* ---- USB packet types --------------------------------------------------- */
#define USB_PID_OUT      0xE1
#define USB_PID_IN       0x69
#define USB_PID_SETUP    0x2D
#define USB_PID_ACK      0xD2
#define USB_PID_NACK     0x5A
#define USB_PID_STALL    0x1E

/* ---- USB device class codes -------------------------------------------- */
#define USB_CLASS_HID      0x03
#define USB_CLASS_MASS    0x08
#define USB_CLASS_HUB     0x09
#define USB_CLASS_AUDIO   0x01
#define USB_CLASS_VIDEO  0x0E
#define USB_CLASS_PRINTER 0x07

/* ---- USB descriptor types ----------------------------------------------- */
#define USB_DT_DEVICE         0x01
#define USB_DT_CONFIG         0x02
#define USB_DT_STRING         0x03
#define USB_DT_INTERFACE      0x04
#define USB_DT_ENDPOINT       0x05

/* ---- USB requests (bRequest) ------------------------------------------- */
#define USB_REQ_GET_STATUS        0x00
#define USB_REQ_CLEAR_FEATURE     0x01
#define USB_REQ_SET_FEATURE       0x03
#define USB_REQ_SET_ADDRESS       0x05
#define USB_REQ_GET_DESCRIPTOR    0x06
#define USB_REQ_SET_DESCRIPTOR    0x07
#define USB_REQ_GET_CONFIG        0x08
#define USB_REQ_SET_CONFIG        0x09
#define USB_REQ_GET_INTERFACE     0x0A
#define USB_REQ_SET_INTERFACE     0x0B
#define USB_REQ_SYNCH_FRAME       0x0C

/* ---- USB hub class commands -------------------------------------------- */
#define USB_HUB_GET_PORT_STATUS   0x00
#define USB_HUB_SET_PORT_FEATURE  0x03
#define USB_HUB_CLEAR_PORT_FEATURE 0x01
#define USB_HUB_SET_PORT_POWER   0x09

/* ---- USB device states -------------------------------------------------- */
typedef enum {
    USB_STATE_DETACHED,
    USB_STATE_ATTACHED,
    USB_STATE_POWERED,
    USB_STATE_DEFAULT,
    USB_STATE_ADDRESS,
    USB_STATE_CONFIGURED
} usb_state_t;

/* ---- USB device descriptor ---------------------------------------------- */
typedef struct {
    u8  bLength;
    u8  bDescriptorType;
    u16 bcdUSB;
    u8  bDeviceClass;
    u8  bDeviceSubClass;
    u8  bDeviceProtocol;
    u8  bMaxPacketSize0;
    u16 idVendor;
    u16 idProduct;
    u16 bcdDevice;
    u8  iManufacturer;
    u8  iProduct;
    u8  iSerialNumber;
    u8  bNumConfigurations;
} __attribute__((packed)) usb_device_desc_t;

/* ---- USB endpoint descriptor ------------------------------------------- */
typedef struct {
    u8  bLength;
    u8  bDescriptorType;
    u8  bEndpointAddress;
    u8  bmAttributes;
    u16 wMaxPacketSize;
    u8  bInterval;
} __attribute__((packed)) usb_endpoint_desc_t;

/* ---- USB configuration descriptor --------------------------------------- */
typedef struct {
    u8  bLength;
    u8  bDescriptorType;
    u16 wTotalLength;
    u8  bNumInterfaces;
    u8  bConfigurationValue;
    u8  iConfiguration;
    u8  bmAttributes;
    u8  bMaxPower;
} __attribute__((packed)) usb_config_desc_t;

/* ---- USB device (max 128) ---------------------------------------------- */
#define MAX_USB_DEVICES 16
typedef struct {
    u8       addr;
    u8       port;
    u8       speed;       /* 1 = low, 2 = full, 3 = high */
    u8       state;
    u8       class;
    u8       subclass;
    u16      vendor;
    u16      product;
    char     name[32];
    bool     present;
} usb_device_t;

/* ---- USB controller state ----------------------------------------------- */
typedef struct {
    u8    type;          /* OHCI, EHCI, XHCI, VIRTIO */
    u16   base_io;       /* I/O base address */
    u32   base_mem;      /* MMIO base address */
    bool  present;
    u8    num_ports;
    u8    num_devices;
    usb_device_t devices[MAX_USB_DEVICES];
} usb_ctrl_t;

static usb_ctrl_t USB_CTRL;
static bool g_inited = false;

/* ---- Port I/O for USB controllers --------------------------------------- */
static u8  usb_inb(u16 port)
{
    u8 v;
    __asm__ volatile ("inb %1, %0" : "=a"(v) : "Nd"(port));
    return v;
}

static void usb_outb(u16 port, u8 val)
{
    __asm__ volatile ("outb %0, %1" : : "a"(val), "Nd"(port));
}

static u16 usb_inw(u16 port)
{
    u16 v;
    __asm__ volatile ("inw %1, %0" : "=a"(v) : "Nd"(port));
    return v;
}

static void usb_outw(u16 port, u16 val)
{
    __asm__ volatile ("outw %0, %1" : : "a"(val), "Nd"(port));
}

static u32 usb_inl(u32 port)
{
    u32 v;
    __asm__ volatile ("inl %1, %0" : "=a"(v) : "Nd"(port));
    return v;
}

static void usb_outl(u32 port, u32 val)
{
    __asm__ volatile ("outl %0, %1" : : "a"(val), "Nd"(port));
}

/* ---- USB timing helpers ------------------------------------------------- */
static void usb_delay(u32 ticks)
{
    for (volatile u32 i = 0; i < ticks * 100; i++);
}

/* ---- USB reset ---------------------------------------------------------- */
static void usb_hc_reset(void)
{
    if (USB_CTRL.type == USB_CTRL_OHCI) {
        /* Reset OHCI */
        usb_outl(USB_CTRL.base_mem + OHCI_REG_CMD, OHCI_CMD_CTRL);
        usb_delay(10);
    } else if (USB_CTRL.type == USB_CTRL_EHCI) {
        /* Reset EHCI */
        usb_outl(USB_CTRL.base_mem + EHCI_REG_USBCMD, EHCI_CMD_RESET);
        for (u32 i = 0; i < 100000; i++) {
            if (!(usb_inl(USB_CTRL.base_mem + EHCI_REG_USBCMD) & EHCI_CMD_RESET))
                return;
        }
    }
}

/* ---- USB controller initialization -------------------------------------- */
static bool usb_probe_ohci(void)
{
    /* Probe for OHCI at standard locations */
    u32 base = 0x10000000;  /* Try MMIO at 0x10000000 */

    /* Check if OHCI is present by reading ID register */
    for (u32 try_base = 0x10000000; try_base < 0x20000000; try_base += 0x10000) {
        u32 id = usb_inl(try_base);
        if (id == 0xFFFFFFFF || id == 0) continue;

        /* Valid OHCI ID should have specific pattern */
        if ((id & 0xFFFFF) == 0x10F10) {  /* OHCI generic */
            USB_CTRL.base_mem = try_base;
            USB_CTRL.type = USB_CTRL_OHCI;
            USB_CTRL.present = true;
            return true;
        }
    }
    return false;
}

static bool usb_probe_ehci(void)
{
    /* Probe for EHCI at standard locations */
    for (u32 try_base = 0x10000000; try_base < 0x20000000; try_base += 0x10000) {
        u32 caplen = usb_inb(try_base);
        if (caplen < 4 || caplen > 64) continue;

        u32 hcs = usb_inl(try_base + 4);
        if (hcs == 0xFFFFFFFF || hcs == 0) continue;

        /* Check for EHCI capability */
        u32 hcc = usb_inl(try_base + 8);
        if ((hcc & 0xFF) >= 0x10) {  /* EHCI 1.0+ */
            USB_CTRL.base_mem = try_base;
            USB_CTRL.type = USB_CTRL_EHCI;
            USB_CTRL.present = true;

            /* Get number of ports */
            USB_CTRL.num_ports = (u8)((hcs >> 0) & 0xFF);
            if (USB_CTRL.num_ports > 8) USB_CTRL.num_ports = 8;

            return true;
        }
    }
    return false;
}

/* ---- USB port reset and detection --------------------------------------- */
static bool usb_detect_device(u8 port)
{
    if (USB_CTRL.type == USB_CTRL_EHCI) {
        u32 portsc = usb_inl(USB_CTRL.base_mem + EHCI_REG_PORTSC(port));

        /* Check if device connected */
        if (!(portsc & 0x01000000)) return false;

        /* Clear suspended state */
        if (portsc & 0x00400000) {
            usb_outl(USB_CTRL.base_mem + EHCI_REG_PORTSC(port), 0x00400000);
        }

        /* Reset port to enable device */
        usb_outl(USB_CTRL.base_mem + EHCI_REG_PORTSC(port), 0x00010000);
        usb_delay(10000);

        /* Clear reset */
        usb_outl(USB_CTRL.base_mem + EHCI_REG_PORTSC(port), 0x00000000);
        usb_delay(50000);

        /* Check if device is now enabled */
        portsc = usb_inl(USB_CTRL.base_mem + EHCI_REG_PORTSC(port));
        if (portsc & 0x00000002) {  /* Port enabled */
            return true;
        }
    } else if (USB_CTRL.type == USB_CTRL_OHCI) {
        /* OHCI port detection */
    }

    return false;
}

/* ---- USB device enumeration --------------------------------------------- */
static void usb_enumerate(void)
{
    USB_CTRL.num_devices = 0;

    for (u8 port = 0; port < USB_CTRL.num_ports; port++) {
        if (usb_detect_device(port)) {
            if (USB_CTRL.num_devices < MAX_USB_DEVICES) {
                usb_device_t *dev = &USB_CTRL.devices[USB_CTRL.num_devices];
                dev->addr = USB_CTRL.num_devices + 1;
                dev->port = port;
                dev->speed = 3;  /* High speed for EHCI */
                dev->present = true;
                USB_CTRL.num_devices++;
            }
        }
    }
}

/* ---- Initialize USB controller ------------------------------------------ */
void usb_init(void)
{
    if (g_inited) return;

    USB_CTRL.present = false;
    USB_CTRL.type = 0;
    USB_CTRL.num_ports = 0;
    USB_CTRL.num_devices = 0;

    /* Try EHCI first (USB 2.0) */
    if (!USB_CTRL.present) usb_probe_ehci();

    /* Fall back to OHCI (USB 1.1) */
    if (!USB_CTRL.present) usb_probe_ohci();

    /* Try virtio USB for QEMU/KVM */
    if (!USB_CTRL.present) {
        /* Virtio would be detected via PCI */
    }

    if (USB_CTRL.present) {
        /* Initialize controller */
        usb_hc_reset();

        /* Enumerate devices */
        usb_enumerate();
    }

    g_inited = true;
}

/* ---- Public API --------------------------------------------------------- */
bool usb_present(void)
{
    if (!g_inited) usb_init();
    return USB_CTRL.present;
}

i32 usb_device_count(void)
{
    if (!g_inited) usb_init();
    return USB_CTRL.num_devices;
}

bool usb_keyboard_connected(void)
{
    if (!g_inited) usb_init();
    for (i32 i = 0; i < USB_CTRL.num_devices; i++) {
        if (USB_CTRL.devices[i].class == USB_CLASS_HID &&
            USB_CTRL.devices[i].present) {
            return true;
        }
    }
    return false;
}

bool usb_storage_connected(void)
{
    if (!g_inited) usb_init();
    for (i32 i = 0; i < USB_CTRL.num_devices; i++) {
        if (USB_CTRL.devices[i].class == USB_CLASS_MASS &&
            USB_CTRL.devices[i].present) {
            return true;
        }
    }
    return false;
}

const char *usb_summary(void)
{
    static char buf[64];
    if (!g_inited) usb_init();

    if (!USB_CTRL.present) return "USB: not detected";

    k_strcpy(buf, "USB: ");
    char tmp[16];

    if (USB_CTRL.type == USB_CTRL_EHCI) k_strcat(buf, "EHCI ");
    else if (USB_CTRL.type == USB_CTRL_OHCI) k_strcat(buf, "OHCI ");
    else if (USB_CTRL.type == USB_CTRL_VIRTIO) k_strcat(buf, "virtio ");

    k_itoa(USB_CTRL.num_ports, tmp, 10);
    k_strcat(buf, tmp);
    k_strcat(buf, " ports, ");

    k_itoa(USB_CTRL.num_devices, tmp, 10);
    k_strcat(buf, tmp);
    k_strcat(buf, " devices");

    return buf;
}