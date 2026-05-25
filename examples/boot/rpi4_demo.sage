import io
import os.boot.build as bb

let arch = "rpi4"
let output_dir = "rpi4_boot_demo"

# Create output directory
# (Using shell because Sage doesn't have mkdir)
# system("mkdir -p rpi4_boot_demo")

let build_script = bb.generate_build_script(arch, output_dir, "SageOS Raspberry Pi 4 Booting...")
io.writefile("build_rpi4.sh", build_script)
print "Generated build_rpi4.sh"
