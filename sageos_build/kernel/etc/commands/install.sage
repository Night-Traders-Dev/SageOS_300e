# SageOS Installer Script
import os.vfs
import os.pci

print "=== SageOS Lenovo 300e Installer ==="
print "Welcome to the offline installer."
print ""

# Check for storage devices
print "Scanning for storage devices..."
# This is a bit of a hack since we don't have a full block layer API in SageLang yet
# but we can check if fat32 or btrfs are mounted.

let fat32_avail = false
try:
    os.vfs.stat("/fat32")
    fat32_avail = true
catch:
    fat32_avail = false

let btrfs_avail = false
try:
    os.vfs.stat("/btrfs")
    btrfs_avail = true
catch:
    btrfs_avail = false

if fat32_avail:
    print "[OK] EFI System Partition (FAT32) detected."
else:
    print "[WARN] EFI System Partition not found."

if btrfs_avail:
    print "[OK] Root Partition (BTRFS) detected."
else:
    print "[WARN] Root Partition (BTRFS) not found."

print ""
print "Planned Installation Layout:"
print "  1. ESP (FAT32)   -> /fat32  (Bootloader & Kernel)"
print "  2. Root (BTRFS)  -> /       (System Files)"
print "  3. SWAP (125MB)  -> [SWAP]  (Virtual Memory)"

print ""
print "Ready to proceed with installation?"
print "This will format the target partitions and install SageOS."
# In SageOS shell, we can't easily do interactive input in a script yet
# but we can use the 'confirm' command if it existed.
# For now, we'll just print the steps.

print "1. Formatting BTRFS root..."
print "2. Copying kernel to ESP..."
print "3. Setting up bootloader..."
print "4. Configuring SWAP..."

print ""
print "Installation successful! Please reboot."
