import re
import sys

def add_top_links(file_path):
    with open(file_path, 'r') as f:
        lines = f.readlines()

    new_lines = []
    for i in range(len(lines)):
        line = lines[i]
        # If the next line is a header (## or ###) or it's the end of the file
        # and the current line is not a header and not empty (to avoid double [Top])
        
        is_header = re.match(r'^(##|###) ', line)
        
        next_line_is_header = False
        if i + 1 < len(lines):
            next_line_is_header = re.match(r'^(#|##|###) ', lines[i+1])
        else:
            next_line_is_header = True # End of file
            
        new_lines.append(line)
        
        if next_line_is_header:
            # Check if we should add [Top](#top)
            # Find the last non-empty line before this header
            last_content_idx = -1
            for j in range(len(new_lines) - 1, -1, -1):
                if new_lines[j].strip() != "" and not re.match(r'^(#|##|###) ', new_lines[j]):
                    last_content_idx = j
                    break
            
            if last_content_idx != -1:
                # If [Top](#top) is not already there
                if "[Top](#top)" not in new_lines[last_content_idx]:
                    # Add it after the last content line of the section
                    # But we need to be careful about where we are.
                    # Actually, it's easier to just check if the current section we just finished needs it.
                    pass

    # Better approach:
    content = "".join(lines)
    # Split by headers but keep headers
    parts = re.split(r'^(##+ .*)$', content, flags=re.MULTILINE)
    
    output = []
    if parts:
        output.append(parts[0]) # Everything before the first ## header
        
    for i in range(1, len(parts), 2):
        header = parts[i]
        section_content = parts[i+1] if i+1 < len(parts) else ""
        
        # Trim trailing whitespace from section_content
        trimmed_content = section_content.rstrip()
        
        if trimmed_content:
            new_section = header + trimmed_content + "\n\n[Top](#top)\n\n"
        else:
            new_section = header + "\n\n[Top](#top)\n\n"
            
        output.append(new_section)
        
    with open(file_path, 'w') as f:
        f.write("".join(output))

if __name__ == "__main__":
    for arg in sys.argv[1:]:
        add_top_links(arg)
