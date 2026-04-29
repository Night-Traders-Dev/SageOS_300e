# SageOS Init System
# Written in SageLang

# Set color to green (0x79FFB0)
os_set_color_hex(7995312)

os_write_str("  ____    _    ____ _____ ___  ____  \n")
os_write_str(" / ___|  / \\  / ___| ____/ _ \\/ ___| \n")
os_write_str(" \\___ \\ / _ \\| |  _|  _|| | | \\___ \\ \n")
os_write_str("  ___) / ___ \\ |_| | |__| |_| |___) |\n")
os_write_str(" |____/_/   \\_\\____|_____\\___/|____/ \n")

# Reset color
os_set_color_hex(15263976)

os_write_str("\n--- SageOS Init System (SageInit) ---\n")
os_write_str("Initializing system services...\n")

# 1. Display MOTD
if os_path_exists("/etc/motd"):
    os_cat("/etc/motd")

# 2. Check for installer hint
if os_path_exists("/dev/sda"):
    os_write_str("Local storage detected.\n")

# 3. Mount additional filesystems if any (placeholder)
# os_shell_exec("mount /dev/sda1 /mnt")

# 4. Start background services (placeholder)
# os_write_str("Starting network stack...\n")
# os_shell_exec("net start")

os_write_str("System ready.\n\n")

# The kernel will launch the shell after this script finishes.
