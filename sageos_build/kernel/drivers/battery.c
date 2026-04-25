/*
 * battery.c — Chromebook EC memory-mapped battery driver for SageOS
 *
 * Protocol reference: chromium.googlesource.com/chromiumos/platform/ec
 *   include/ec_commands.h
 *
 * EC LPC memory map base: EC_LPC_ADDR_MEMMAP = 0x900
 *
 * Relevant offsets (all values are 32-bit unless noted):
 *   EC_MEMMAP_ID           0x20  — two bytes: 'E' (0x45), 'C' (0x43)
 *   EC_MEMMAP_BATT_CAP     0x48  — Battery Remaining Capacity (mAh or mWh)
 *   EC_MEMMAP_BATT_FLAG    0x4c  — Battery state flags (8-bit)
 *   EC_MEMMAP_BATT_LFCC    0x58  — Battery Last Full Charge Capacity
 *
 * EC_BATT_FLAG_BATT_PRESENT  0x02  — set when a battery is physically present
 * EC_BATT_FLAG_INVALID_DATA  0x20  — set when memmap data is stale/invalid
 */

#include "battery.h"
#include "acpi.h"
#include "console.h"
#include "io.h"
#include "serial.h"
#include "dmesg.h"

/* ── CrOS EC LPC constants ─────────────────────────────────────────────── */
#define EC_LPC_ADDR_MEMMAP          0x900u

/* Memory-map offsets */
#define EC_MEMMAP_ID                0x20u   /* 'E' at +0, 'C' at +1         */
#define EC_MEMMAP_BATT_CAP          0x48u   /* Remaining capacity (32-bit)   */
#define EC_MEMMAP_BATT_FLAG         0x4cu   /* Battery flags (8-bit)         */
#define EC_MEMMAP_BATT_LFCC         0x58u   /* Last full charge cap (32-bit) */

/* Battery flag bits */
#define EC_BATT_FLAG_BATT_PRESENT   0x02u
#define EC_BATT_FLAG_DISCHARGING    0x04u
#define EC_BATT_FLAG_CHARGING       0x08u
#define EC_BATT_FLAG_INVALID_DATA   0x20u

/* ── Module state ───────────────────────────────────────────────────────── */
static int battery_present;     /* ACPI hints */
static int ec_present;          /* ACPI EC hints */
static int percent;
static int percent_valid;
static int source_type;         /* 0=none 1=placeholder 2=EC memmap 3=EC no-sig */

static uint16_t ec_lpc_base = EC_LPC_ADDR_MEMMAP;

/* ── Low-level helpers ──────────────────────────────────────────────────── */

/*
 * read_ec_byte — read a single byte from the EC memory map.
 */
static uint8_t read_ec_byte(uint16_t offset)
{
    return inb((uint16_t)(ec_lpc_base + offset));
}

/*
 * read_ec_u32 — read a little-endian 32-bit value from the EC memory map.
 */
static uint32_t read_ec_u32(uint16_t offset)
{
    uint32_t val;
    val  = (uint32_t)inb((uint16_t)(ec_lpc_base + offset + 0));
    val |= (uint32_t)inb((uint16_t)(ec_lpc_base + offset + 1)) << 8;
    val |= (uint32_t)inb((uint16_t)(ec_lpc_base + offset + 2)) << 16;
    val |= (uint32_t)inb((uint16_t)(ec_lpc_base + offset + 3)) << 24;
    return val;
}

/*
 * check_ec_id — verify the two-byte EC identity at EC_MEMMAP_ID.
 *
 * Per ec_commands.h: offset 0x20 == 'E' (0x45), offset 0x21 == 'C' (0x43).
 * This is the correct and only identity marker in the CrOS EC memory map.
 * There is no "ECMAP" string in the protocol.
 */
static int check_ec_id_at(uint16_t base)
{
    uint8_t b0 = inb((uint16_t)(base + EC_MEMMAP_ID + 0));
    uint8_t b1 = inb((uint16_t)(base + EC_MEMMAP_ID + 1));
    return (b0 == 0x45u /* 'E' */ && b1 == 0x43u /* 'C' */);
}

/*
 * batt_data_valid — return 1 if EC_MEMMAP_BATT_FLAG says the battery data
 * is present and not marked invalid.
 */
static int batt_data_valid(void)
{
    uint8_t flags = read_ec_byte(EC_MEMMAP_BATT_FLAG);
    if (!(flags & EC_BATT_FLAG_BATT_PRESENT))  return 0;
    if (  flags & EC_BATT_FLAG_INVALID_DATA)   return 0;
    return 1;
}

/*
 * read_battery_percent — read remaining/lfcc from memmap and compute %.
 * Returns 0..100 on success, -1 on bad data.
 */
static int read_battery_percent(void)
{
    if (!batt_data_valid()) return -1;

    uint32_t remaining = read_ec_u32(EC_MEMMAP_BATT_CAP);
    uint32_t full      = read_ec_u32(EC_MEMMAP_BATT_LFCC);

    if (full == 0 || full == 0xFFFFFFFFu || remaining == 0xFFFFFFFFu)
        return -1;

    int pct = (int)((remaining * 100ULL) / full);
    if (pct > 100) pct = 100;
    if (pct < 0)   pct = 0;
    return pct;
}

/* ── Public API ─────────────────────────────────────────────────────────── */

void battery_init(void)
{
    /* Patch 3: Log every tried base so a serial capture reveals which
     * address range is visible on a given 300e firmware variant.      */
    serial_write("[battery] EC probe: trying 0x900, 0x880, 0x800 in order\r\n");
    dmesg_log("battery: probing Chromebook EC at 0x900/0x880/0x800...");
    /* EC_PROBE_BASES_LOGGED */
    battery_present = acpi_has_battery_device();
    ec_present      = acpi_has_ec_device();
    percent         = -1;
    percent_valid   = 0;
    source_type     = 0;

    /*
     * Search for the CrOS EC identity ('E','C' at offset 0x20) across the
     * candidate base addresses.  The standard base is 0x900; older boards
     * may mirror at 0x800 or 0x880.
     */
    static const uint16_t candidates[] = { 0x900u, 0x880u, 0x800u };
    for (int i = 0; i < 3; i++) {
        if (check_ec_id_at(candidates[i])) {
            ec_lpc_base = candidates[i];
            source_type = 2;
            dmesg_log("battery: Chromebook EC confirmed");
            break;
        }
    }

    if (source_type == 2) {
        int pct = read_battery_percent();
        if (pct >= 0) {
            percent       = pct;
            percent_valid = 1;
            dmesg_log("battery: initial data valid");
        }
        /* EC found but battery data not yet valid — still mark source=2. */
    } else {
        dmesg_log("battery: Chromebook EC memory map not confirmed");
    }

    /*
     * Fallback: ACPI battery hints were detected but EC memmap wasn't found
     * or returned invalid data.  Use a placeholder so the status bar shows
     * something other than "--" until real data arrives.
     */
    if (!percent_valid) {
        if (ec_present) {
            source_type   = 3;     /* EC detected, no valid ID/data yet */
        } else if (battery_present) {
            source_type   = 1;     /* ACPI hints only */
        }
        /* Leave percent_valid = 0; status bar will show "--" */
    }
}

int battery_percent(void)
{
    if (source_type == 2) {
        int pct = read_battery_percent();
        if (pct >= 0) {
            percent       = pct;
            percent_valid = 1;
        }
    }

    return percent_valid ? percent : -1;
}

void battery_cmd_info(void)
{
    console_write("\nBattery:");
    console_write("\n  ACPI battery hints: ");
    console_write(battery_present ? "present" : "not found");

    if (battery_present)
        console_write("\n  ACPI Methods: _BIF _BST detected");

    console_write("\n  Chromebook EC memmap: ");
    if (source_type == 2) {
        console_write("active at 0x");
        console_hex64(ec_lpc_base);
        console_write("\n  EC ID bytes: 'E','C' confirmed");
        uint8_t flags = read_ec_byte(EC_MEMMAP_BATT_FLAG);
        console_write("\n  BATT_FLAG: 0x");
        console_hex64(flags);
        console_write(
            (flags & EC_BATT_FLAG_BATT_PRESENT) ? "  [PRESENT]" : "  [NOT PRESENT]");
        console_write(
            (flags & EC_BATT_FLAG_INVALID_DATA) ? " [INVALID]" : " [VALID]");
        console_write(
            (flags & EC_BATT_FLAG_CHARGING)    ? " [CHARGING]"    : "");
        console_write(
            (flags & EC_BATT_FLAG_DISCHARGING) ? " [DISCHARGING]" : "");
    } else if (source_type == 3) {
        console_write("EC hinted by ACPI, ID not confirmed at 0x900/0x880/0x800");
    } else if (ec_present) {
        console_write("ACPI EC present, memmap ID not found");
    } else {
        console_write("not found");
    }

    console_write("\n  percentage: ");
    int pct = battery_percent();
    if (pct >= 0) {
        console_u32((uint32_t)pct);
        console_write("%");
    } else {
        console_write("unavailable");
    }

    console_write("\n  source: ");
    switch (source_type) {
    case 2: console_write("CrOS EC memory map (LPC)"); break;
    case 3: console_write("CrOS EC detected, memmap ID unconfirmed"); break;
    case 1: console_write("ACPI AML placeholder"); break;
    default: console_write("none"); break;
    }
}
