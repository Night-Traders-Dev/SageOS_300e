# Lenovo 300e Chromebook Gen 2 AST Hardware Notes

Sources checked:

- Lenovo PSREF for `Lenovo 300e Chromebook 2nd Gen AST`:
  https://psref.lenovo.com/syspool/Sys/PDF/Lenovo/Lenovo_300e_Chromebook_2nd_Gen_AST/Lenovo_300e_Chromebook_2nd_Gen_AST_Spec.PDF
- ChromiumOS EC command definitions:
  https://chromium.googlesource.com/chromiumos/platform/ec/+/HEAD/include/ec_commands.h
- UEFI ACPI 6.5 battery specification:
  https://uefi.org/specs/ACPI/6.5/10_Power_Source_and_Power_Meter_Devices.html
- Linux kernel `/proc/stat` CPU accounting documentation:
  https://www.kernel.org/doc/html/latest/filesystems/proc.html
- Linux Wireless ath10k firmware documentation:
  https://wireless.docs.kernel.org/en/latest/en/users/drivers/ath10k/firmware.html

## Confirmed Target Hardware

- CPU: AMD A4-9120C, 2 cores / 2 threads, 1.6 GHz base, 2.4 GHz max.
- SoC/chipset: AMD SoC platform.
- Battery: integrated Li-Polymer 47 Wh.
- Ethernet: no onboard Ethernet.
- Wi-Fi / Bluetooth: Qualcomm QCA6174A, 802.11ac dual-band 2x2 Wi-Fi plus Bluetooth 5.0, M.2 card.
- Storage: 32 GB eMMC 5.1 on system board.

## CPU Usage

The current early-kernel CPU% path should remain software accounting:

1. Tick at a fixed rate with PIT or HPET/APIC timer.
2. Increment total ticks or loop counters.
3. Increment idle counters only from the idle path.
4. Report `usage = 100 - idle_delta * 100 / total_delta` over a sliding window.

This matches the operating-system accounting model used by Linux-style CPU
statistics, where utilization is derived from time spent in active states versus
idle states. The existing `timer_irq()`, `timer_poll()`, and `timer_idle_poll()`
code is the right first-stage design once native interrupts are enabled.

Next step after firmware-input mode: keep a monotonic timer running even in the
firmware keyboard fallback path, or show CPU as unavailable there instead of
pretending IRQ accounting is live.

## Battery Value

Preferred path on this Chromebook is Chrome EC first, ACPI AML second:

1. Chrome EC LPC memory map at `0x900`.
2. Validate `EC_MEMMAP_ID` before trusting values.
3. Read:
   - `EC_MEMMAP_BATT_CAP` at offset `0x48` for remaining capacity.
   - `EC_MEMMAP_BATT_LFCC` at offset `0x58` for last full charge capacity.
   - `EC_MEMMAP_BATT_FLAG` at offset `0x4c` for charging/discharging/presence.
4. Percent is `remaining * 100 / last_full_charge`, ignoring
   `EC_MEMMAP_BATT_UNKNOWN_VALUE`.

Longer-term, implement Chrome EC host commands:

- `EC_CMD_BATTERY_GET_STATIC` (`0x0600`) for design capacity, voltage,
  manufacturer/model/serial/type, and cycle count.
- `EC_CMD_BATTERY_GET_DYNAMIC` (`0x0601`) for voltage, current, remaining
  capacity, full capacity, flags, and requested charge voltage/current.

ACPI fallback should evaluate `_BIF`/`_BIX` for static capacity and `_BST` for
state, present rate, remaining capacity, and voltage. For normal rechargeable
batteries, percent is remaining capacity divided by last full charge capacity.

## Wi-Fi And Networking

The target Wi-Fi device is Qualcomm QCA6174A. The expected PCI ID for this
family is `168c:003e`. Linux supports it with `ath10k`, and QCA6174 hw3.0 uses
the ath10k firmware API 6 path.

Driver bring-up order for SageOS:

1. PCI enumeration and config-space access.
2. PCI BAR mapping, bus mastering, DMA-safe memory allocation, and MSI/MSI-X or
   INTx interrupt routing.
3. Identify QCA6174A PCI device and load firmware assets from RAMFS or FAT:
   - `ath10k/QCA6174/hw3.0/firmware-6.bin`
   - `ath10k/QCA6174/hw3.0/board-2.bin`
4. Implement the minimal ath10k boot sequence: reset, copy firmware/board data,
   start target, WMI/HTT messaging, receive/transmit rings.
5. Add a small network stack before user-visible networking:
   - Ethernet frame layer
   - ARP
   - IPv4
   - ICMP ping
   - UDP
   - DHCP client
   - DNS
   - TCP later
6. Add Wi-Fi management:
   - scan
   - authentication/association
   - WPA2/WPA3 supplicant support

Pragmatic milestone: implement PCI discovery and report the QCA6174A vendor,
device, BARs, and interrupt first. Do not start with full Wi-Fi association.
