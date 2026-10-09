/* =============================================================================
 *  FalconOS — network tools (ip, ping, arp, netstat)
 * -----------------------------------------------------------------------------
 *  Network diagnostic tools for FalconOS. These tools provide basic
 *  network functionality using the virtio-net driver.
 *
 *  Commands:
 *      ip addr            - show IP configuration
 *      ip config <ip>     - set static IP address
 *      ip dhcp            - request DHCP address
 *      ping <host>       - ping a host
 *      arp -a             - show ARP table
 *      netstat            - show network statistics
 * ============================================================================= */
#include "falcon.h"

/* ---- ip command ------------------------------------------------------------ */
static void cmd_ip_addr(char *out)
{
    if (!net_present()) {
        k_strcpy(out, "network: no adapter detected");
        return;
    }

    k_strcpy(out, "network: ");
    if (!net_connected()) {
        k_strcat(out, "not connected");
        return;
    }

    k_strcat(out, net_ip_addr());
    k_strcat(out, " netmask ");
    k_strcat(out, net_netmask());
    k_strcat(out, " gateway ");
    k_strcat(out, net_gateway());

    char mac[24];
    net_mac_string(mac);
    k_strcat(out, " mac ");
    k_strcat(out, mac);
}

static void cmd_ip_config(char *out, const char *ip)
{
    if (!net_present()) {
        k_strcpy(out, "network: no adapter detected");
        return;
    }

    /* Simple IP validation */
    i32 dots = 0, nums = 0;
    for (i32 i = 0; ip[i]; i++) {
        if (ip[i] == '.') { dots++; nums = 0; }
        else if (ip[i] >= '0' && ip[i] <= '9') nums++;
        else { k_strcpy(out, "ip: invalid address"); return; }
        if (nums > 3) { k_strcpy(out, "ip: invalid address"); return; }
    }
    if (dots != 3) { k_strcpy(out, "ip: invalid address"); return; }

    net_set_ip(ip);
    net_set_gateway("0.0.0.0");
    k_strcpy(out, "ip: configured ");
    k_strcat(out, ip);
}

static void cmd_ip_dhcp(char *out)
{
    if (!net_present()) {
        k_strcpy(out, "network: no adapter detected");
        return;
    }

    if (net_dhcp()) {
        k_strcpy(out, "ip: DHCP assigned ");
        k_strcat(out, net_ip_addr());
    } else {
        k_strcpy(out, "ip: DHCP not implemented; QEMU uses static 10.0.2.15");
    }
}

/* The shell reports real ICMP replies, never fabricated latency/output. */
static void cmd_ping(char *out, const char *host)
{
    if (!net_present() || !net_connected()) {
        k_strcpy(out, "ping: network not available"); return;
    }
    u8 addr[4];
    if (!host || !native_net_parse_ipv4(host, addr)) {
        k_strcpy(out, "ping: IPv4 address required (DNS not yet available)"); return;
    }
    bool ok = native_net_ping(addr);
    k_strcpy(out, "ping ");
    k_strcat(out, host);
    k_strcat(out, ok ? ": ICMP echo reply received" : ": timeout (no echo reply)");
}
static void cmd_arp(char *out)
{
    if (!net_present()) { k_strcpy(out, "arp: no adapter"); return; }
    if (!native_net_arp_known()) { k_strcpy(out, "arp: no resolved neighbor"); return; }
    u8 mac[6]; native_net_arp_mac(mac);
    k_strcpy(out, "ARP neighbor MAC: ");
    for (i32 i=0;i<6;i++) {
        i32 n=k_strlen(out);
        out[n]="0123456789ABCDEF"[mac[i] >> 4];
        out[n+1]="0123456789ABCDEF"[mac[i]&15];
        out[n+2]=(i==5) ? 0 : ':';
        out[n+3]=0;
    }
}

/* ---- netstat command -------------------------------------------------------- */
static void cmd_netstat(char *out)
{
    if (!net_present()) {
        k_strcpy(out, "netstat: no network adapter");
        return;
    }

    u32 tx_pkt, rx_pkt, tx_by, rx_by, tx_err, rx_err;
    net_stats(&tx_pkt, &rx_pkt, &tx_by, &rx_by, &tx_err, &rx_err);

    k_strcpy(out, "Network Interface Stats:\n");
    k_strcat(out, "TX packets: ");
    char tmp[16];
    k_itoa(tx_pkt, tmp, 10); k_strcat(out, tmp);
    k_strcat(out, "  RX packets: ");
    k_itoa(rx_pkt, tmp, 10); k_strcat(out, tmp);
    k_strcat(out, "\nTX bytes: ");
    k_itoa(tx_by, tmp, 10); k_strcat(out, tmp);
    k_strcat(out, "  RX bytes: ");
    k_itoa(rx_by, tmp, 10); k_strcat(out, tmp);
    k_strcat(out, "\nTX errors: ");
    k_itoa(tx_err, tmp, 10); k_strcat(out, tmp);
    k_strcat(out, "  RX errors: ");
    k_itoa(rx_err, tmp, 10); k_strcat(out, tmp);
}

/* ---- Public API for shell integration --------------------------------------- */
void net_tools_dispatch(const char *cmd, char *out)
{
    if (!cmd || !cmd[0]) {
        k_strcpy(out, "net-tools: try 'ip', 'ping', 'arp', 'netstat'");
        return;
    }

    /* Parse command */
    if (k_strcmp(cmd, "ip") == 0) {
        cmd_ip_addr(out);
    } else if (k_strncmp(cmd, "ip ", 3) == 0) {
        const char *arg = cmd + 3;
        if (k_strncmp(arg, "addr", 4) == 0) {
            cmd_ip_addr(out);
        } else if (k_strncmp(arg, "config ", 7) == 0) {
            cmd_ip_config(out, arg + 7);
        } else if (k_strncmp(arg, "dhcp", 4) == 0) {
            cmd_ip_dhcp(out);
        } else {
            k_strcpy(out, "ip: usage: ip addr | ip config <ip> | ip dhcp");
        }
    } else if (k_strncmp(cmd, "ping ", 5) == 0) {
        cmd_ping(out, cmd + 5);
    } else if (k_strcmp(cmd, "ping") == 0) {
        k_strcpy(out, "ping: usage: ping <host>");
    } else if (k_strcmp(cmd, "arp") == 0 || k_strcmp(cmd, "arp -a") == 0) {
        cmd_arp(out);
    } else if (k_strcmp(cmd, "netstat") == 0) {
        cmd_netstat(out);
    } else {
        k_strcpy(out, "unknown command: ");
        k_strcat(out, cmd);
    }
}