import os
import sys

def generate_header(etc_dir, output_header):
    # Process both /etc and /etc/commands
    all_files = []
    
    # Root /etc files (only .sage)
    if os.path.exists(etc_dir):
        for f in os.listdir(etc_dir):
            if f.endswith('.sage') and os.path.isfile(os.path.join(etc_dir, f)):
                all_files.append((f, etc_dir, f"/etc/{f}"))
                
    # /etc/commands files
    commands_dir = os.path.join(etc_dir, "commands")
    if os.path.exists(commands_dir):
        for f in os.listdir(commands_dir):
            if f.endswith('.sage') and os.path.isfile(os.path.join(commands_dir, f)):
                all_files.append((f, commands_dir, f"/etc/commands/{f}"))
    
    all_files.sort()

    with open(output_header, 'w') as f:
        f.write("/* Auto-generated command embeddings */\n#pragma once\n\n")
        
        for filename, src_dir, target_path in all_files:
            # Clean name for C variable
            clean_name = target_path.replace("/", "_").replace(".", "_")
            var_name = f"embedded_file{clean_name}"
            path = os.path.join(src_dir, filename)
            
            with open(path, 'r') as src:
                content = src.read()
            
            bytes_data = content.encode('utf-8')
            f.write(f"static const unsigned char {var_name}[] = {{\n")
            for i in range(0, len(bytes_data), 12):
                chunk = bytes_data[i:i+12]
                f.write("    " + ", ".join(f"0x{b:02x}" for b in chunk) + ",\n")
            f.write("    0x00\n")
            f.write("};\n\n")
        
        f.write("static void ramfs_embed_commands(void) {\n")
        for filename, src_dir, target_path in all_files:
            clean_name = target_path.replace("/", "_").replace(".", "_")
            var_name = f"embedded_file{clean_name}"
            f.write(f'    ramfs_create_file_ref("{target_path}", {var_name}, sizeof({var_name}) - 1);\n')
        f.write("}\n")

if __name__ == "__main__":
    generate_header(sys.argv[1], sys.argv[2])
