import os
import sys

def generate_header(etc_dir, output_header):
    # Process multiple directories
    # 1. /etc (root)
    # 2. /etc/commands
    # 3. /bin (for package manager and compiled tools)
    
    all_files = []
    
    # Root /etc files
    if os.path.exists(etc_dir):
        for f in os.listdir(etc_dir):
            if (f.endswith('.sage') or f.endswith('.json') or f.endswith('.sgvm')) and os.path.isfile(os.path.join(etc_dir, f)):
                all_files.append((f, etc_dir, f"/etc/{f}"))
                
    # /etc/commands files
    commands_dir = os.path.join(etc_dir, "commands")
    if os.path.exists(commands_dir):
        for f in os.listdir(commands_dir):
            if (f.endswith('.sage') or f.endswith('.json') or f.endswith('.sgvm')) and os.path.isfile(os.path.join(commands_dir, f)):
                all_files.append((f, commands_dir, f"/etc/commands/{f}"))

    # /bin files (from the workspace root's bin directory if it exists)
    # Note: We'll point the script to the kernel/etc dir, so we look for ../bin relative to it
    bin_dir = os.path.join(os.path.dirname(etc_dir), "bin")
    if os.path.exists(bin_dir):
        for f in os.listdir(bin_dir):
            if os.path.isfile(os.path.join(bin_dir, f)):
                all_files.append((f, bin_dir, f"/bin/{f}"))
    
    all_files.sort()

    with open(output_header, 'w') as f:
        f.write("/* Auto-generated command embeddings */\n#pragma once\n\n")
        
        for filename, src_dir, target_path in all_files:
            # Clean name for C variable
            clean_name = target_path.replace("/", "_").replace(".", "_").replace("-", "_")
            var_name = f"embedded_file{clean_name}"
            path = os.path.join(src_dir, filename)
            
            with open(path, 'rb') as src:
                bytes_data = src.read()
            
            f.write(f"static const unsigned char {var_name}[] = {{\n")
            for i in range(0, len(bytes_data), 12):
                chunk = bytes_data[i:i+12]
                f.write("    " + ", ".join(f"0x{b:02x}" for b in chunk) + ",\n")
            
            # Add null terminator for safety if it's a script/json
            if target_path.endswith('.sage') or target_path.endswith('.json') or target_path.endswith('.sgvm'):
                f.write("    0x00\n")
            
            f.write("};\n\n")

if __name__ == "__main__":
    generate_header(sys.argv[1], sys.argv[2])
