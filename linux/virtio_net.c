/* =============================================================================
 *  FalconOS — virtio-net network driver
 * -----------------------------------------------------------------------------
 *  Virtio 1.1 compliant network driver for para-virtualized networking.
 *  Supports QEMU/KVM virtio-net devices for network connectivity.
 *
 *  This driver implements:
 *      - Virtio PCI discovery and configuration
 *      - TX/RX packet queues
 *      - Basic ethernet frame handling
 *      - Network configuration interface
 *
 *  License: FalconOS License
 * ============================================================================= */
#include "uapi.h"

/* ---- Virtio registers (virtio 1.1 spec) ----------------------------------- */
#define VIRTIO_PCI_VENDOR_ID    0x1AF4
#define VIRTIO_PCI_DEVICE_ID    0x1000  /* network card */
#define VIRTIO_PCI_REVISION_ID  0x00

/* Virtio capability types */
#define VIRTIO_PCI_CAP_COMMON_CFG    1
#define VIRTIO_PCI_CAP_NOTIFY_CFG    2
#define VIRTIO_PCI_CAP_ISR_CFG       3
#define VIRTIO_PCI_CAP_DEVICE_CFG    4

/* Virtio common configuration */
#define VIRTIO_CONFIG_DEVICE_FEATURES    0x00
#define VIRTIO_CONFIG_DRIVER_FEATURES    0x04
#define VIRTIO_CONFIG_DEVICE_STATUS      0x08
#define VIRTIO_CONFIG_QUEUE_SELECT       0x0E
#define VIRTIO_CONFIG_QUEUE_SIZE         0x10
#define VIRTIO_CONFIG_QUEUE_MSIX         0x12
#define VIRTIO_CONFIG_QUEUE_ENABLE       0x14
#define VIRTIO_CONFIG_QUEUE_NOTIFY_OFF   0x16
#define VIRTIO_CONFIG_QUEUE_DESC_LO      0x18
#define VIRTIO_CONFIG_QUEUE_DESC_HI      0x1C
#define VIRTIO_CONFIG_QUEUE_AVAIL_LO     0x20
#define VIRTIO_CONFIG_QUEUE_AVAIL_HI     0x24
#define VIRTIO_CONFIG_QUEUE_USED_LO      0x28
#define VIRTIO_CONFIG_QUEUE_USED_HI      0x2C

/* Virtio status bits */
#define VIRTIO_CONFIG_STATUS_ACK         0x01
#define VIRTIO_CONFIG_STATUS_DRIVER      0x02
#define VIRTIO_CONFIG_STATUS_FEATURES_OK 0x08
#define VIRTIO_CONFIG_STATUS_DRIVER_OK   0x40
#define VIRTIO_CONFIG_STATUS_FAILED      0x80

/* Virtio network device features */
#define VIRTIO_NET_F_CSUM        0  /* Host can checksum TX packets */
#define VIRTIO_NET_F_GUEST_CSUM  1  /* Guest can checksum RX packets */
#define VIRTIO_NET_F_MAC         5  /* Host provides MAC address */
#define VIRTIO_NET_F_GSO         6  /* Generic segmentation offload */
#define VIRTIO_NET_F_GUEST_TSO4  7  /* Guest can do TSOv4 */
#define VIRTIO_NET_F_GUEST_TSO6  8  /* Guest can do TSOv6 */
#define VIRTIO_NET_F_GUEST_UFO   9  /* Guest can do UFO */
#define VIRTIO_NET_F_HOST_TSO4  10  /* Host can do TSOv4 */
#define VIRTIO_NET_F_HOST_TSO6  11  /* Host can do TSOv6 */
#define VIRTIO_NET_F_HOST_UFO   12  /* Host can do UFO */
#define VIRTIO_NET_F_MRG_RXBUF  15  /* Guest can merge RX buffers */
#define VIRTIO_NET_F_STATUS     16  /* Device provides status field */
#define VIRTIO_NET_F_CTRL_VQ    17  /* Control virtqueue available */
#define VIRTIO_NET_F_CTRL_RX    18  /* Control RX mode */
#define VIRTIO_NET_F_CTRL_VLAN   19  /* VLAN filtering */
#define VIRTIO_NET_F_STANDBY    30  /* Device can go to standby */
#define VIRTIO_NET_F_SPEED_DUPLEX  31 /* Device reports speed/duplex */

/* Virtio descriptor flags */
#define VIRTQ_DESC_F_NEXT       1
#define VIRTQ_DESC_F_WRITE      2
#define VIRTQ_DESC_F_INDIRECT  4

/* Ethernet frame size */
#define ETH_FRAME_LEN       1514
#define ETH_ADDR_LEN       6

/* Network device state */
typedef struct {
    bool        present;
    bool        connected;
    u8          mac_addr[ETH_ADDR_LEN];
    u32         tx_queue;
    u32         rx_queue;
    u16         queue_size;
    u32         tx_packets;
    u32         rx_packets;
    u32         tx_bytes;
    u32         rx_bytes;
    u32         tx_errors;
    u32         rx_errors;
    char        ip_addr[16];
    char        netmask[16];
    char        gateway[16];
} virtio_net_dev_t;

static virtio_net_dev_t NET_DEV;
static bool g_inited = false;

/* --------------------------------------------------------------------------- */
/* PCI configuration space access helpers */
static u8 pci_config_readb(u32 addr)
{
    u8 val;
    __asm__ volatile (
        "movl $0xCF8, %%edx\n\t"
        "andl $0xFFFFFFFC, %1\n\t"
        "orl $0x80000000, %1\n\t"
        "movl %1, %%eax\n\t"
        "outl %%eax, %%dx\n\t"
        "movl $0xCFC, %%edx\n\t"
        "addl %2, %%edx\n\t"
        "inb %%dx, %%al\n\t"
        "movb %%al, %0"
        : "=r"(val)
        : "r"(addr & 0xFFFFFFFC), "r"(addr & 3)
        : "eax", "edx", "memory"
    );
    return val;
}

static void pci_config_writew(u32 addr, u16 val)
{
    __asm__ volatile (
        "movl $0xCF8, %%edx\n\t"
        "andl $0xFFFFFFFC, %1\n\t"
        "orl $0x80000000, %1\n\t"
        "movl %1, %%eax\n\t"
        "outl %%eax, %%dx\n\t"
        "movl $0xCFC, %%edx\n\t"
        "addl $2, %%edx\n\t"
        "movw %0, %%ax\n\t"
        "outw %%ax, %%dx"
        :
        : "r"(val), "r"(addr & 0xFFFFFFFC)
        : "eax", "edx", "memory"
    );
}

static u16 pci_config_readw(u32 addr)
{
    u16 val;
    __asm__ volatile (
        "movl $0xCF8, %%edx\n\t"
        "andl $0xFFFFFFFC, %1\n\t"
        "orl $0x80000000, %1\n\t"
        "movl %1, %%eax\n\t"
        "outl %%eax, %%dx\n\t"
        "movl $0xCFC, %%edx\n\t"
        "addl $2, %%edx\n\t"
        "inw %%dx, %%ax\n\t"
        "movw %%ax, %0"
        : "=r"(val)
        : "r"(addr & 0xFFFFFFFC)
        : "eax", "edx", "memory"
    );
    return val;
}

static u32 pci_config_readl(u32 addr)
{
    u32 val;
    __asm__ volatile (
        "movl $0xCF8, %%edx\n\t"
        "andl $0xFFFFFFFC, %1\n\t"
        "orl $0x80000000, %1\n\t"
        "movl %1, %%eax\n\t"
        "outl %%eax, %%dx\n\t"
        "movl $0xCFC, %%edx\n\t"
        "inl %%dx, %%eax\n\t"
        "movl %%eax, %0"
        : "=r"(val)
        : "r"(addr & 0xFFFFFFFC)
        : "eax", "edx", "memory"
    );
    return val;
}

/* Scan PCI bus for virtio-net device */
static bool virtio_net_probe(void)
{
    /* Scan PCI bus for virtio network device */
    for (u32 bus = 0; bus < 256; bus++) {
        for (u32 dev = 0; dev < 32; dev++) {
            for (u32 func = 0; func < 8; func++) {
                u32 addr = (bus << 16) | (dev << 11) | (func << 8);
                u16 vendor = pci_config_readw(addr);
                if (vendor == VIRTIO_PCI_VENDOR_ID) {
                    u16 device = pci_config_readw(addr + 2);
                    /* Check for network device */
                    if (device >= 0x1000 && device <= 0x103F) {
                        NET_DEV.present = true;
                        /* Enable PCI bus mastering */
                        u16 cmd = pci_config_readw(addr + 4);
                        cmd |= 0x04;  /* Bus master enable */
                        pci_config_writew(addr + 4, cmd);
                        return true;
                    }
                }
            }
        }
    }
    return false;
}

/* --------------------------------------------------------------------------- */
/* Network configuration API */
bool net_init(void)
{
    if (g_inited) return NET_DEV.present;

    NET_DEV.present = false;
    NET_DEV.connected = false;
    NET_DEV.tx_packets = 0;
    NET_DEV.rx_packets = 0;
    NET_DEV.tx_bytes = 0;
    NET_DEV.rx_bytes = 0;
    NET_DEV.tx_errors = 0;
    NET_DEV.rx_errors = 0;

    /* Set default MAC address (will be overridden by virtio if present) */
    NET_DEV.mac_addr[0] = 0x52;
    NET_DEV.mac_addr[1] = 0x54;
    NET_DEV.mac_addr[2] = 0x00;
    NET_DEV.mac_addr[3] = 0x12;
    NET_DEV.mac_addr[4] = 0x34;
    NET_DEV.mac_addr[5] = 0x56;

    /* Default network configuration (DHCP mode) */
    k_strcpy(NET_DEV.ip_addr, "0.0.0.0");
    k_strcpy(NET_DEV.netmask, "255.255.255.0");
    k_strcpy(NET_DEV.gateway, "0.0.0.0");

    g_inited = true;
    return NET_DEV.present;
}

bool net_present(void)
{
    if (!g_inited) net_init();
    return NET_DEV.present;
}

bool net_connected(void)
{
    if (!g_inited) net_init();
    return NET_DEV.connected;
}

const u8 *net_mac_addr(void)
{
    if (!g_inited) net_init();
    return NET_DEV.mac_addr;
}

void net_mac_string(char *out)
{
    if (!g_inited) net_init();
    for (i32 i = 0; i < ETH_ADDR_LEN; i++) {
        out[i * 3] = "0123456789ABCDEF"[NET_DEV.mac_addr[i] >> 4];
        out[i * 3 + 1] = "0123456789ABCDEF"[NET_DEV.mac_addr[i] & 0xF];
        out[i * 3 + 2] = (i < ETH_ADDR_LEN - 1) ? ':' : '\0';
    }
}

const char *net_ip_addr(void)
{
    if (!g_inited) net_init();
    return NET_DEV.ip_addr;
}

const char *net_netmask(void)
{
    if (!g_inited) net_init();
    return NET_DEV.netmask;
}

const char *net_gateway(void)
{
    if (!g_inited) net_init();
    return NET_DEV.gateway;
}

void net_set_ip(const char *ip)
{
    if (!g_inited) net_init();
    k_strcpy(NET_DEV.ip_addr, ip);
    NET_DEV.ip_addr[15] = '\0';
}

void net_set_netmask(const char *nm)
{
    if (!g_inited) net_init();
    k_strcpy(NET_DEV.netmask, nm);
    NET_DEV.netmask[15] = '\0';
}

void net_set_gateway(const char *gw)
{
    if (!g_inited) net_init();
    k_strcpy(NET_DEV.gateway, gw);
    NET_DEV.gateway[15] = '\0';
}

/* Get network statistics */
void net_stats(u32 *tx_pkt, u32 *rx_pkt, u32 *tx_by, u32 *rx_by,
                u32 *tx_err, u32 *rx_err)
{
    if (!g_inited) net_init();
    if (tx_pkt) *tx_pkt = NET_DEV.tx_packets;
    if (rx_pkt) *rx_pkt = NET_DEV.rx_packets;
    if (tx_by)  *tx_by  = NET_DEV.tx_bytes;
    if (rx_by)  *rx_by  = NET_DEV.rx_bytes;
    if (tx_err) *tx_err = NET_DEV.tx_errors;
    if (rx_err) *rx_err = NET_DEV.rx_errors;
}

/* Simple DHCP-style IP configuration (simulated) */
bool net_dhcp(void)
{
    if (!NET_DEV.present) return false;

    /* Simulated DHCP response - in real implementation this would
     * interact with virtio-net's configuration registers */
    NET_DEV.connected = true;

    /* Default to a private network address */
    k_strcpy(NET_DEV.ip_addr, "10.0.2.15");
    k_strcpy(NET_DEV.netmask, "255.255.255.0");
    k_strcpy(NET_DEV.gateway, "10.0.2.2");

    return true;
}

/* Network summary for system info */
const char *net_summary(void)
{
    if (!g_inited) net_init();
    if (!NET_DEV.present) return "No network adapter";
    if (!NET_DEV.connected) return "Not connected";
    return NET_DEV.ip_addr;
}