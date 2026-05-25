import io
import os.boot.build as bb

let arch = "aarch64"
let output_dir = "boot_test_aarch64"

# Ensure output directory exists
# (Sage io module doesn't have mkdir, so we'll use a trick or just assume it exists)
# Actually, I'll just use a shell command to create it first.

let build_script = bb.generate_build_script(arch, output_dir, "SageOS ARM64 Booting...")
io.writefile("build_kernel.sh", build_script)
print "Generated build_kernel.sh"
