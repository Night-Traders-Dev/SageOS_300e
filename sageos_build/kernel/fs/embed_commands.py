import os
import sys

def generate_header(commands_dir, output_header):
    files = sorted([f for f in os.listdir(commands_dir) if f.endswith('.sage')])
    
    with open(output_header, 'w') as f:
        f.write("/* Auto-generated command embeddings */\n#pragma once\n\n")
        
        for filename in files:
            cmd_name = filename[:-5]
            var_name = f"cmd_src_{cmd_name}"
            path = os.path.join(commands_dir, filename)
            
            with open(path, 'r') as src:
                content = src.read()
            
            # Use hex embedding to avoid escaping hell
            bytes_data = content.encode('utf-8')
            f.write(f"static const unsigned char {var_name}[] = {{\n")
            for i in range(0, len(bytes_data), 12):
                chunk = bytes_data[i:i+12]
                f.write("    " + ", ".join(f"0x{b:02x}" for b in chunk) + ",\n")
            f.write("    0x00\n")
            f.write("};\n\n")
        
        f.write("static void ramfs_embed_commands(void) {\n")
        for filename in files:
            cmd_name = filename[:-5]
            var_name = f"cmd_src_{cmd_name}"
            target_path = f"/etc/commands/{filename}"
            # sizeof() works on array with explicit/inferred size if defined in same unit
            f.write(f'    ramfs_create_file_ref("{target_path}", {var_name}, sizeof({var_name}) - 1);\n')
        f.write("}\n")

if __name__ == "__main__":
    generate_header(sys.argv[1], sys.argv[2])
