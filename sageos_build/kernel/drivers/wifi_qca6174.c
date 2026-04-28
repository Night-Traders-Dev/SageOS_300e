#include <stddef.h>
#include <stdint.h>
#include "console.h"
#include "dmesg.h"
#include "net.h"
#include "pci.h"
#include "vfs.h"
#include "wifi_qca6174.h"

#define PCI_COMMAND_IO            0x0001
#define PCI_COMMAND_MEMORY        0x0002
#define PCI_COMMAND_BUSMASTER     0x0004

#define PCI_STATUS_CAP_LIST       0x0010

#define PCI_CAP_ID_PM             0x01
#define PCI_CAP_ID_MSI            0x05
#define PCI_CAP_ID_PCIE           0x10
#define PCI_CAP_ID_MSIX           0x11

#define QCA6174_FW_REL_PATH       "ath10k/QCA6174/hw3.0/firmware-6.bin"
#define QCA6174_BOARD_REL_PATH    "ath10k/QCA6174/hw3.0/board-2.bin"

typedef struct {
    int      present;
    uint8_t  bus;
    uint8_t  device;
    uint8_t  func;
    uint16_t vendor_id;
    uint16_t device_id;
    uint16_t subsystem_vendor_id;
    uint16_t subsystem_device_id;
    uint16_t command_before;
    uint16_t command_after;
    uint16_t status;
    uint32_t bar0_raw;
    uint32_t bar0_base;
    uint8_t  revision;
    uint8_t  irq_line;
    uint8_t  irq_pin;
    uint8_t  pm_cap;
    uint8_t  msi_cap;
    uint8_t  msix_cap;
    uint8_t  pcie_cap;
    int      fw_main_present;
    int      fw_board_present;
    char     fw_main_path[96];
    char     fw_board_path[96];
} Qca6174State;

static Qca6174State g_qca6174;

static uint8_t qca6174_cfg_read8(uint8_t offset) {
    uint32_t value = pci_config_read(g_qca6174.bus, g_qca6174.device, g_qca6174.func, offset);
    return (uint8_t)((value >> ((offset & 3u) * 8u)) & 0xFFu);
}

static uint16_t qca6174_cfg_read16(uint8_t offset) {
    uint32_t value = pci_config_read(g_qca6174.bus, g_qca6174.device, g_qca6174.func, offset);
    return (uint16_t)((value >> ((offset & 2u) * 8u)) & 0xFFFFu);
}

static void qca6174_cfg_write16(uint8_t offset, uint16_t value) {
    uint32_t reg = pci_config_read(g_qca6174.bus, g_qca6174.device, g_qca6174.func, offset);
    uint32_t shift = (uint32_t)(offset & 2u) * 8u;
    reg &= ~(0xFFFFu << shift);
    reg |= (uint32_t)value << shift;
    pci_config_write(g_qca6174.bus, g_qca6174.device, g_qca6174.func, offset, reg);
}

static uint8_t qca6174_find_capability(uint8_t cap_id) {
    uint8_t offset;

    if ((g_qca6174.status & PCI_STATUS_CAP_LIST) == 0) return 0;

    offset = qca6174_cfg_read8(0x34);
    for (int depth = 0; offset >= 0x40 && depth < 48; depth++) {
        uint32_t cap = pci_config_read(g_qca6174.bus, g_qca6174.device, g_qca6174.func, offset);
        if ((cap & 0xFFu) == cap_id) return offset;
        offset = (uint8_t)((cap >> 8) & 0xFFu);
    }

    return 0;
}

static int qca6174_path_exists(const char *path) {
    VfsStat st;
    return vfs_stat(path, &st) == 0 && st.type == VFS_FILE;
}

static int qca6174_locate_asset(const char *rel_path, char *out_path, size_t out_size) {
    static const char *const prefixes[] = {
        "/",
        "/fat32/",
        "/firmware/",
        "/fat32/firmware/",
    };

    for (size_t i = 0; i < sizeof(prefixes) / sizeof(prefixes[0]); i++) {
        out_path[0] = 0;
        snprintf(out_path, out_size, "%s%s", prefixes[i], rel_path);
        if (qca6174_path_exists(out_path)) return 1;
    }

    out_path[0] = 0;
    return 0;
}

static void qca6174_copy_text(char *dst, size_t dst_size, const char *src) {
    size_t i = 0;
    if (!dst || dst_size == 0) return;
    if (!src) {
        dst[0] = 0;
        return;
    }
    while (src[i] && i + 1 < dst_size) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = 0;
}

static void qca6174_print_hex16(uint16_t value) {
    static const char hex[] = "0123456789abcdef";
    char text[5];
    text[0] = hex[(value >> 12) & 0xF];
    text[1] = hex[(value >> 8) & 0xF];
    text[2] = hex[(value >> 4) & 0xF];
    text[3] = hex[value & 0xF];
    text[4] = 0;
    console_write(text);
}

static void qca6174_print_hex8(uint8_t value) {
    static const char hex[] = "0123456789abcdef";
    char text[3];
    text[0] = hex[(value >> 4) & 0xF];
    text[1] = hex[value & 0xF];
    text[2] = 0;
    console_write(text);
}

static char qca6174_irq_pin_letter(uint8_t pin) {
    if (pin >= 1 && pin <= 4) return (char)('A' + pin - 1);
    return '?';
}

int qca6174_init(void) {
    const PciDevice *dev = pci_find_device(PCI_VENDOR_QUALCOMM_ATH, PCI_DEVICE_QCA6174A);
    NetDevice netdev;

    memset(&g_qca6174, 0, sizeof(g_qca6174));
    memset(&netdev, 0, sizeof(netdev));

    if (!dev) {
        dmesg_log("wifi: qca6174 not found");
        return 0;
    }

    g_qca6174.present = 1;
    g_qca6174.bus = dev->bus;
    g_qca6174.device = dev->device;
    g_qca6174.func = dev->func;
    g_qca6174.vendor_id = dev->vendor_id;
    g_qca6174.device_id = dev->device_id;
    g_qca6174.bar0_raw = dev->bar[0];
    g_qca6174.bar0_base = dev->bar[0] & ~0x0Fu;
    g_qca6174.irq_line = dev->irq_line;
    g_qca6174.revision = qca6174_cfg_read8(0x08);
    g_qca6174.command_before = qca6174_cfg_read16(0x04);
    g_qca6174.status = qca6174_cfg_read16(0x06);
    g_qca6174.irq_pin = qca6174_cfg_read8(0x3D);
    g_qca6174.subsystem_vendor_id = qca6174_cfg_read16(0x2C);
    g_qca6174.subsystem_device_id = qca6174_cfg_read16(0x2E);

    {
        uint16_t command = g_qca6174.command_before | PCI_COMMAND_MEMORY | PCI_COMMAND_BUSMASTER;
        qca6174_cfg_write16(0x04, command);
        g_qca6174.command_after = qca6174_cfg_read16(0x04);
    }

    g_qca6174.pm_cap = qca6174_find_capability(PCI_CAP_ID_PM);
    g_qca6174.msi_cap = qca6174_find_capability(PCI_CAP_ID_MSI);
    g_qca6174.msix_cap = qca6174_find_capability(PCI_CAP_ID_MSIX);
    g_qca6174.pcie_cap = qca6174_find_capability(PCI_CAP_ID_PCIE);

    g_qca6174.fw_main_present = qca6174_locate_asset(QCA6174_FW_REL_PATH,
                                                     g_qca6174.fw_main_path,
                                                     sizeof(g_qca6174.fw_main_path));
    g_qca6174.fw_board_present = qca6174_locate_asset(QCA6174_BOARD_REL_PATH,
                                                      g_qca6174.fw_board_path,
                                                      sizeof(g_qca6174.fw_board_path));

    qca6174_copy_text(netdev.name, sizeof(netdev.name), "wlan0");
    qca6174_copy_text(netdev.driver, sizeof(netdev.driver), "qca6174");
    netdev.kind = NET_DEVICE_WIFI;
    netdev.state = (g_qca6174.fw_main_present && g_qca6174.fw_board_present)
        ? NET_STATE_FIRMWARE_STAGED
        : NET_STATE_PROBED;
    netdev.mtu = NET_MTU_ETHERNET;
    netdev.flags = NETDEV_FLAG_PRESENT;
    netdev.pci_backed = 1;
    netdev.bus = g_qca6174.bus;
    netdev.device = g_qca6174.device;
    netdev.func = g_qca6174.func;
    netdev.irq_line = g_qca6174.irq_line;
    netdev.irq_pin = g_qca6174.irq_pin;

    if (g_qca6174.command_after & PCI_COMMAND_MEMORY) netdev.flags |= NETDEV_FLAG_MMIO_ENABLED;
    if (g_qca6174.command_after & PCI_COMMAND_BUSMASTER) netdev.flags |= NETDEV_FLAG_BUSMASTER_ENABLED;
    if (g_qca6174.fw_main_present) netdev.flags |= NETDEV_FLAG_FW_MAIN;
    if (g_qca6174.fw_board_present) netdev.flags |= NETDEV_FLAG_FW_BOARD;
    if (g_qca6174.irq_pin >= 1 && g_qca6174.irq_pin <= 4) netdev.flags |= NETDEV_FLAG_INTX;
    if (g_qca6174.msi_cap) netdev.flags |= NETDEV_FLAG_MSI_CAP;
    if (g_qca6174.msix_cap) netdev.flags |= NETDEV_FLAG_MSIX_CAP;
    if (g_qca6174.pcie_cap) netdev.flags |= NETDEV_FLAG_PCIE_CAP;

    if (!net_register_device(&netdev)) {
        dmesg_log("wifi: failed to register qca6174 netdev");
        return 0;
    }

    if (g_qca6174.fw_main_present && g_qca6174.fw_board_present) {
        dmesg_log("wifi: qca6174 ready for firmware boot staging");
    } else {
        dmesg_log("wifi: qca6174 discovered but firmware assets are missing");
    }

    return 1;
}

int qca6174_is_present(void) {
    return g_qca6174.present;
}

void qca6174_cmd_info(void) {
    console_write("\nQCA6174A Wi-Fi:");

    if (!g_qca6174.present) {
        console_write("\n  Device not discovered on PCI.");
        console_write("\n  Expected PCI ID: 168c:003e");
        return;
    }

    console_write("\n  PCI location: ");
    qca6174_print_hex8(g_qca6174.bus);
    console_write(":");
    qca6174_print_hex8(g_qca6174.device);
    console_write(".");
    console_putc((char)('0' + g_qca6174.func));

    console_write("\n  Device IDs: ");
    qca6174_print_hex16(g_qca6174.vendor_id);
    console_write(":");
    qca6174_print_hex16(g_qca6174.device_id);
    console_write("  subsystem ");
    qca6174_print_hex16(g_qca6174.subsystem_vendor_id);
    console_write(":");
    qca6174_print_hex16(g_qca6174.subsystem_device_id);

    console_write("\n  Revision: ");
    qca6174_print_hex8(g_qca6174.revision);

    console_write("\n  BAR0 raw : ");
    console_hex64(g_qca6174.bar0_raw);
    console_write("\n  BAR0 MMIO: ");
    console_hex64(g_qca6174.bar0_base);

    console_write("\n  IRQ route: line ");
    console_u32(g_qca6174.irq_line);
    console_write("  pin INT");
    console_putc(qca6174_irq_pin_letter(g_qca6174.irq_pin));
    console_write("#");

    console_write("\n  PCI cmd  : before ");
    console_hex64(g_qca6174.command_before);
    console_write("  after ");
    console_hex64(g_qca6174.command_after);

    console_write("\n  Capabilities: PM ");
    console_write(g_qca6174.pm_cap ? "yes" : "no");
    console_write("  MSI ");
    console_write(g_qca6174.msi_cap ? "yes" : "no");
    console_write("  MSI-X ");
    console_write(g_qca6174.msix_cap ? "yes" : "no");
    console_write("  PCIe ");
    console_write(g_qca6174.pcie_cap ? "yes" : "no");

    console_write("\n  firmware-6.bin: ");
    if (g_qca6174.fw_main_present) console_write(g_qca6174.fw_main_path);
    else console_write("missing");

    console_write("\n  board-2.bin   : ");
    if (g_qca6174.fw_board_present) console_write(g_qca6174.fw_board_path);
    else console_write("missing");

    console_write("\n  Next steps    : target reset, firmware upload, HTT/WMI rings, scan/auth");
}
