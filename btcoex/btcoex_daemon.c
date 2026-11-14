/*
 * Bluetooth/Wi-Fi coexistence daemon for Samsung Galaxy S Duos (GT-S7562)
 * Bridges CSR7820 HCI state to Atheros AR6003 (ath6kl_sdio) via NL80211_CMD_BTCOEX.
 */

#define LOG_TAG "btcoex_daemon"
#include <cutils/log.h>

#include <errno.h>
#include <net/if.h>
#include <poll.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>

#include <linux/netlink.h>

/* Generic Netlink definitions (not shipped in Gingerbread bionic headers) */
#ifndef NETLINK_GENERIC
#define NETLINK_GENERIC 16
#endif
#define GENL_ID_CTRL          0x10
#define CTRL_CMD_GETFAMILY    3
#define CTRL_ATTR_FAMILY_ID   1
#define CTRL_ATTR_FAMILY_NAME 2

struct genlmsghdr {
    uint8_t  cmd;
    uint8_t  version;
    uint16_t reserved;
};
#define GENL_HDRLEN NLMSG_ALIGN(sizeof(struct genlmsghdr))

/* Compat-wireless nl80211 constants matching prebuilt cfg80211.ko */
#define NL80211_CMD_SET_POWER_SAVE 64
#define NL80211_CMD_BTCOEX         89

#define NL80211_ATTR_IFINDEX       3
#define NL80211_ATTR_PS_STATE      93
#define NL80211_ATTR_BTCOEX_DATA   153

#define NL80211_PS_DISABLED        0
#define NL80211_PS_ENABLED         1

enum nl80211_btcoex_cmds {
    NL80211_WMI_SET_BT_STATUS = 0,
    NL80211_WMI_SET_BT_PARAMS,
    NL80211_WMI_SET_BT_FT_ANT,
    NL80211_WMI_SET_COLOCATED_BT_DEV,
    NL80211_WMI_SET_BT_INQUIRY_PAGE_CONFIG,
    NL80211_WMI_SET_BT_SCO_CONFIG,
    NL80211_WMI_SET_BT_A2DP_CONFIG,
    NL80211_WMI_SET_BT_ACLCOEX_CONFIG,
    NL80211_WMI_SET_BT_DEBUG,
    NL80211_WMI_SET_BT_OPSTATUS,
};

/* WMI BT-Coex stream & status codes (AR6003) */
#define BT_STREAM_SCO          1
#define BT_STREAM_A2DP         2
#define BT_STREAM_INQUIRY_PAGE 3
#define BT_STREAM_ACL          4

#define BT_STATUS_START        1
#define BT_STATUS_STOP         2

#define FE_ANT_SINGLE          1
#define BT_DEV_CSR             1

/* Linux BlueZ HCI socket & ioctl definitions */
#ifndef AF_BLUETOOTH
#define AF_BLUETOOTH 31
#endif
#define BTPROTO_HCI  1
#define HCI_DEV_NONE 0xffff

#define HCI_UP       0
#define HCI_INQUIRY  7

#define HCIGETDEVINFO  _IOR('H', 211, int)
#define HCIGETCONNLIST _IOR('H', 212, int)

#define SCO_LINK  0x00
#define ACL_LINK  0x01
#define ESCO_LINK 0x02

struct sockaddr_hci {
    sa_family_t hci_family;
    uint16_t    hci_dev;
    uint16_t    hci_channel;
};

struct hci_dev_stats {
    uint32_t err_rx, err_tx, cmd_tx, evt_rx, acl_tx, acl_rx, sco_tx, sco_rx, byte_rx, byte_tx;
};

struct hci_dev_info {
    uint16_t dev_id;
    char     name[8];
    uint8_t  bdaddr[6];
    uint32_t flags;
    uint8_t  type;
    uint8_t  features[8];
    uint32_t pkt_type;
    uint32_t link_policy;
    uint32_t link_mode;
    uint16_t acl_mtu;
    uint16_t acl_pkts;
    uint16_t sco_mtu;
    uint16_t sco_pkts;
    struct hci_dev_stats stat;
};

struct hci_conn_info {
    uint16_t handle;
    uint8_t  bdaddr[6];
    uint8_t  type;
    uint8_t  out;
    uint16_t state;
    uint32_t link_mode;
};

struct hci_conn_list_req {
    uint16_t dev_id;
    uint16_t conn_num;
    struct hci_conn_info conn_info[0];
};

static int g_nl_sock = -1;
static int g_nl80211_id = -1;
static uint32_t g_nl_seq = 1;

static int open_genl_socket(void) {
    struct sockaddr_nl addr;
    int fd = socket(AF_NETLINK, SOCK_RAW, NETLINK_GENERIC);
    if (fd < 0) return -1;
    memset(&addr, 0, sizeof(addr));
    addr.nl_family = AF_NETLINK;
    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        close(fd);
        return -1;
    }
    return fd;
}

static int nla_put(uint8_t *buf, int offset, uint16_t type, const void *data, int len) {
    struct nlattr *nla = (struct nlattr *)(buf + offset);
    nla->nla_type = type;
    nla->nla_len = NLA_HDRLEN + len;
    if (len > 0 && data) {
        memcpy(buf + offset + NLA_HDRLEN, data, len);
    }
    return offset + NLA_ALIGN(nla->nla_len);
}

static int send_genl_and_wait_ack(uint8_t *buf, int total_len) {
    struct nlmsghdr *nlh = (struct nlmsghdr *)buf;
    nlh->nlmsg_len = total_len;
    nlh->nlmsg_seq = g_nl_seq++;
    nlh->nlmsg_pid = getpid();

    if (send(g_nl_sock, buf, total_len, 0) < 0) {
        return -errno;
    }

    uint8_t rx[1024];
    ssize_t n = recv(g_nl_sock, rx, sizeof(rx), 0);
    if (n < 0) return -errno;
    struct nlmsghdr *rnh = (struct nlmsghdr *)rx;
    if (NLMSG_OK(rnh, (size_t)n) && rnh->nlmsg_type == NLMSG_ERROR) {
        struct nlmsgerr *err = (struct nlmsgerr *)NLMSG_DATA(rnh);
        return err->error;
    }
    return 0;
}

static int resolve_nl80211_id(void) {
    uint8_t buf[256];
    memset(buf, 0, sizeof(buf));
    struct nlmsghdr *nlh = (struct nlmsghdr *)buf;
    struct genlmsghdr *gh = (struct genlmsghdr *)(buf + NLMSG_HDRLEN);

    nlh->nlmsg_type = GENL_ID_CTRL;
    nlh->nlmsg_flags = NLM_F_REQUEST;
    nlh->nlmsg_seq = g_nl_seq++;
    nlh->nlmsg_pid = getpid();
    gh->cmd = CTRL_CMD_GETFAMILY;
    gh->version = 1;

    int off = NLMSG_HDRLEN + GENL_HDRLEN;
    const char name[] = "nl80211";
    off = nla_put(buf, off, CTRL_ATTR_FAMILY_NAME, name, sizeof(name));
    nlh->nlmsg_len = off;

    if (send(g_nl_sock, buf, off, 0) < 0) {
        LOGE("getfamily send failed: %s", strerror(errno));
        return -1;
    }

    uint8_t rx[4096];
    ssize_t n = recv(g_nl_sock, rx, sizeof(rx), 0);
    if (n < 0) {
        LOGE("getfamily recv failed: %s", strerror(errno));
        return -1;
    }
    struct nlmsghdr *rnh = (struct nlmsghdr *)rx;
    if (!NLMSG_OK(rnh, (size_t)n) || rnh->nlmsg_type == NLMSG_ERROR) {
        int err = (rnh->nlmsg_type == NLMSG_ERROR) ? ((struct nlmsgerr *)NLMSG_DATA(rnh))->error : 0;
        LOGE("getfamily nlmsg error: type=%d err=%d n=%d", rnh->nlmsg_type, err, (int)n);
        return -1;
    }

    int rem = rnh->nlmsg_len - NLMSG_HDRLEN - GENL_HDRLEN;
    struct nlattr *nla = (struct nlattr *)((uint8_t *)NLMSG_DATA(rnh) + GENL_HDRLEN);
    while (rem >= (int)NLA_HDRLEN && (int)nla->nla_len >= (int)NLA_HDRLEN && (int)nla->nla_len <= rem) {
        if (nla->nla_type == CTRL_ATTR_FAMILY_ID) {
            return *(uint16_t *)((uint8_t *)nla + NLA_HDRLEN);
        }
        int step = NLA_ALIGN(nla->nla_len);
        nla = (struct nlattr *)((uint8_t *)nla + step);
        rem -= step;
    }
    return -1;
}

static int send_btcoex_cmd(uint32_t ifindex, uint32_t bt_cmd, const void *payload, int payload_len) {
    if (g_nl_sock < 0 || g_nl80211_id < 0 || ifindex == 0) return -1;

    uint8_t buf[512];
    memset(buf, 0, sizeof(buf));
    struct nlmsghdr *nlh = (struct nlmsghdr *)buf;
    struct genlmsghdr *gh = (struct genlmsghdr *)(buf + NLMSG_HDRLEN);

    nlh->nlmsg_type = (uint16_t)g_nl80211_id;
    nlh->nlmsg_flags = NLM_F_REQUEST | NLM_F_ACK;
    gh->cmd = NL80211_CMD_BTCOEX;
    gh->version = 0;

    uint8_t data[128];
    if (payload_len + 4 > (int)sizeof(data)) return -1;
    memcpy(data, &bt_cmd, 4);
    if (payload_len > 0 && payload) {
        memcpy(data + 4, payload, payload_len);
    }

    int off = NLMSG_HDRLEN + GENL_HDRLEN;
    off = nla_put(buf, off, NL80211_ATTR_IFINDEX, &ifindex, sizeof(ifindex));
    off = nla_put(buf, off, NL80211_ATTR_BTCOEX_DATA, data, 4 + payload_len);
    return send_genl_and_wait_ack(buf, off);
}

static int set_wlan_power_save(uint32_t ifindex, uint32_t ps_state) {
    if (g_nl_sock < 0 || g_nl80211_id < 0 || ifindex == 0) return -1;

    uint8_t buf[256];
    memset(buf, 0, sizeof(buf));
    struct nlmsghdr *nlh = (struct nlmsghdr *)buf;
    struct genlmsghdr *gh = (struct genlmsghdr *)(buf + NLMSG_HDRLEN);

    nlh->nlmsg_type = (uint16_t)g_nl80211_id;
    nlh->nlmsg_flags = NLM_F_REQUEST | NLM_F_ACK;
    gh->cmd = NL80211_CMD_SET_POWER_SAVE;
    gh->version = 0;

    int off = NLMSG_HDRLEN + GENL_HDRLEN;
    off = nla_put(buf, off, NL80211_ATTR_IFINDEX, &ifindex, sizeof(ifindex));
    off = nla_put(buf, off, NL80211_ATTR_PS_STATE, &ps_state, sizeof(ps_state));
    return send_genl_and_wait_ack(buf, off);
}

static int init_wlan_btcoex(uint32_t ifindex) {
    uint8_t ant = FE_ANT_SINGLE;
    uint8_t dev = BT_DEV_CSR;
    int rc1 = send_btcoex_cmd(ifindex, NL80211_WMI_SET_BT_FT_ANT, &ant, sizeof(ant));
    int rc2 = send_btcoex_cmd(ifindex, NL80211_WMI_SET_COLOCATED_BT_DEV, &dev, sizeof(dev));
    LOGI("init_wlan_btcoex(ifindex=%u): ft_ant rc=%d, colocated_bt rc=%d", ifindex, rc1, rc2);
    return (rc1 == 0 && rc2 == 0) ? 0 : -1;
}

static int get_iface_flags(int sock, const char *ifname) {
    struct ifreq ifr;
    memset(&ifr, 0, sizeof(ifr));
    strncpy(ifr.ifr_name, ifname, IFNAMSIZ - 1);
    if (ioctl(sock, SIOCGIFFLAGS, &ifr) < 0) {
        return 0;
    }
    return ifr.ifr_flags;
}

static void send_bt_status(uint32_t ifindex, uint8_t stream, uint8_t status) {
    uint8_t cmd[2] = { stream, status };
    int rc = send_btcoex_cmd(ifindex, NL80211_WMI_SET_BT_STATUS, cmd, sizeof(cmd));
    LOGI("send_bt_status(stream=%u, status=%u) rc=%d", stream, status, rc);
}

int main(int argc, char **argv) {
    int oneshot = (argc > 1 && strcmp(argv[1], "--oneshot") == 0);

    g_nl_sock = open_genl_socket();
    if (g_nl_sock < 0) {
        LOGE("Failed to open NETLINK_GENERIC socket: %s", strerror(errno));
        return 1;
    }
    g_nl80211_id = resolve_nl80211_id();
    if (g_nl80211_id < 0) {
        LOGE("Failed to resolve nl80211 family id");
        return 1;
    }
    LOGI("Started (nl80211 id=%d)", g_nl80211_id);

    if (oneshot) {
        uint32_t ifindex = if_nametoindex("wlan0");
        if (ifindex == 0) {
            printf("wlan0 not found\n");
            return 1;
        }
        init_wlan_btcoex(ifindex);
        printf("Sent FE_ANT=1 and COLOCATED_BT=1 to wlan0 (ifindex=%u)\n", ifindex);
        return 0;
    }

    int hci_sock = socket(AF_BLUETOOTH, SOCK_RAW, BTPROTO_HCI);
    if (hci_sock < 0) {
        LOGE("Failed to open BTPROTO_HCI socket: %s", strerror(errno));
        return 1;
    }

    int ioctl_sock = socket(AF_INET, SOCK_DGRAM, 0);
    uint32_t last_ifindex = 0;
    int wlan_coex_ready = 0;
    int prev_running = 0;
    int prev_acl = 0;
    int prev_a2dp = 0;
    int prev_sco = 0;
    int prev_inq = 0;
    uint32_t last_acl_pkts = 0;
    int have_acl_stats = 0;
    int a2dp_hold_ticks = 0;
    int ps_refresh_ticks = 0;

    while (1) {
        uint32_t ifindex = if_nametoindex("wlan0");
        int flags = (ifindex != 0 && ioctl_sock >= 0) ? get_iface_flags(ioctl_sock, "wlan0") : 0;
        int iface_up = (flags & IFF_UP) ? 1 : 0;
        int iface_running = (flags & IFF_RUNNING) ? 1 : 0;

        if (!iface_up) {
            last_ifindex = 0;
            wlan_coex_ready = 0;
            prev_running = 0;
            prev_acl = 0;
            prev_a2dp = 0;
            prev_sco = 0;
            prev_inq = 0;
            a2dp_hold_ticks = 0;
            ps_refresh_ticks = 0;
        } else if (ifindex != last_ifindex || !wlan_coex_ready) {
            last_ifindex = ifindex;
            if (init_wlan_btcoex(ifindex) == 0) {
                wlan_coex_ready = 1;
                prev_running = 0;
                prev_acl = 0;
                prev_a2dp = 0;
                prev_sco = 0;
                prev_inq = 0;
                a2dp_hold_ticks = 0;
                ps_refresh_ticks = 0;
            }
        }

        int acl_active = 0;
        int a2dp_active = 0;
        int sco_active = 0;
        int inq_active = 0;

        struct hci_dev_info di;
        memset(&di, 0, sizeof(di));
        di.dev_id = 0;
        if (ioctl(hci_sock, HCIGETDEVINFO, &di) == 0 && (di.flags & (1 << HCI_UP))) {
            inq_active = (di.flags & (1 << HCI_INQUIRY)) ? 1 : 0;

            uint8_t cl_buf[sizeof(struct hci_conn_list_req) + 16 * sizeof(struct hci_conn_info)];
            struct hci_conn_list_req *cl = (struct hci_conn_list_req *)cl_buf;
            cl->dev_id = 0;
            cl->conn_num = 16;
            if (ioctl(hci_sock, HCIGETCONNLIST, cl) == 0) {
                int i;
                for (i = 0; i < cl->conn_num; i++) {
                    if (cl->conn_info[i].type == ACL_LINK) {
                        acl_active = 1;
                    } else if (cl->conn_info[i].type == SCO_LINK ||
                               cl->conn_info[i].type == ESCO_LINK) {
                        sco_active = 1;
                    }
                }
            }

            uint32_t cur_acl_pkts = di.stat.acl_tx + di.stat.acl_rx;
            if (acl_active && have_acl_stats) {
                uint32_t delta = cur_acl_pkts - last_acl_pkts;
                if (delta > 5) {
                    a2dp_hold_ticks = 3;
                } else if (a2dp_hold_ticks > 0) {
                    a2dp_hold_ticks--;
                }
                a2dp_active = (a2dp_hold_ticks > 0) ? 1 : 0;
            } else {
                a2dp_hold_ticks = 0;
                a2dp_active = 0;
            }
            last_acl_pkts = cur_acl_pkts;
            have_acl_stats = 1;
        } else {
            have_acl_stats = 0;
            a2dp_hold_ticks = 0;
        }

        if (wlan_coex_ready && ifindex != 0) {
            if (inq_active != prev_inq) {
                send_bt_status(ifindex, BT_STREAM_INQUIRY_PAGE,
                               inq_active ? BT_STATUS_START : BT_STATUS_STOP);
                prev_inq = inq_active;
            }
            if (acl_active != prev_acl) {
                send_bt_status(ifindex, BT_STREAM_ACL,
                               acl_active ? BT_STATUS_START : BT_STATUS_STOP);
                set_wlan_power_save(ifindex,
                                    (acl_active || sco_active) ? NL80211_PS_DISABLED : NL80211_PS_ENABLED);
                prev_acl = acl_active;
            }
            if (a2dp_active != prev_a2dp) {
                send_bt_status(ifindex, BT_STREAM_A2DP,
                               a2dp_active ? BT_STATUS_START : BT_STATUS_STOP);
                prev_a2dp = a2dp_active;
            }
            if (sco_active != prev_sco) {
                send_bt_status(ifindex, BT_STREAM_SCO,
                               sco_active ? BT_STATUS_START : BT_STATUS_STOP);
                if (!acl_active) {
                    set_wlan_power_save(ifindex,
                                        sco_active ? NL80211_PS_DISABLED : NL80211_PS_ENABLED);
                }
                prev_sco = sco_active;
            }
            if (iface_running != prev_running) {
                if (iface_running && (acl_active || sco_active)) {
                    set_wlan_power_save(ifindex, NL80211_PS_DISABLED);
                }
                prev_running = iface_running;
            }
            if (iface_running && (a2dp_active || sco_active)) {
                if (++ps_refresh_ticks >= 5) {
                    set_wlan_power_save(ifindex, NL80211_PS_DISABLED);
                    ps_refresh_ticks = 0;
                }
            } else {
                ps_refresh_ticks = 0;
            }
        }

        sleep(1);
    }

    if (ioctl_sock >= 0) close(ioctl_sock);
    close(hci_sock);
    close(g_nl_sock);
    return 0;
}
