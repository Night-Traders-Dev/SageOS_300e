# toml.sage — Minimal TOML parser in SageLang

let NL = chr(10)

proc trim(s):
    let start_idx = 0
    let end_idx = len(s)
    while start_idx < end_idx and (s[start_idx] == " " or s[start_idx] == "\t" or s[start_idx] == "\r" or s[start_idx] == "\n"):
        start_idx = start_idx + 1
    end
    while end_idx > start_idx and (s[end_idx-1] == " " or s[end_idx-1] == "\t" or s[end_idx-1] == "\r" or s[end_idx-1] == "\n"):
        end_idx = end_idx - 1
    end
    return s[start_idx:end_idx]
end

proc parse_toml(content):
    let toml = {}
    let current_section = ""
    
    let i = 0
    let n = len(content)
    
    while i < n:
        # Read a line
        let line = ""
        while i < n and content[i] != "\n":
            line = line + content[i]
            i = i + 1
        end
        i = i + 1 # Skip newline
        
        line = trim(line)
        if len(line) == 0 or line[0] == "#":
            continue
        end

        # Handle multi-line arrays
        if contains(line, "=") and contains(line, "[") and not contains(line, "]"):
            let temp_line = line
            while i < n and not contains(temp_line, "]"):
                let next_line = ""
                while i < n and content[i] != "\n":
                    next_line = next_line + content[i]
                    i = i + 1
                end
                i = i + 1
                temp_line = temp_line + " " + trim(next_line)
            end
            line = temp_line
        end
        
        # Section header
        if line[0] == "[" and line[len(line)-1] == "]":
            current_section = line[1:len(line)-1]
            current_section = trim(current_section)
            if toml[current_section] == nil:
                toml[current_section] = {}
            end
            continue
        end
        
        # Key-value pair
        let eq_idx = -1
        let j = 0
        while j < len(line):
            if line[j] == "=":
                eq_idx = j
                break
            end
            j = j + 1
        end
        
        if eq_idx != -1:
            let key = trim(line[0:eq_idx])
            let val_part = trim(line[eq_idx+1:len(line)])
            
            let val = nil
            if len(val_part) > 1 and val_part[0] == "\"" and val_part[len(val_part)-1] == "\"":
                val = val_part[1:len(val_part)-1]
            elif len(val_part) > 1 and val_part[0] == "[" and val_part[len(val_part)-1] == "]":
                # Parse array of strings
                val = []
                let inner = trim(val_part[1:len(val_part)-1])
                let item = ""
                let in_quotes = false
                let k = 0
                while k < len(inner):
                    let ch = inner[k]
                    if ch == "\"":
                        in_quotes = not in_quotes
                    elif ch == "," and not in_quotes:
                        let trimmed_item = trim(item)
                        if len(trimmed_item) > 1 and trimmed_item[0] == "\"" and trimmed_item[len(trimmed_item)-1] == "\"":
                            let _u = push(val, trimmed_item[1:len(trimmed_item)-1])
                        else:
                            let _u = push(val, trimmed_item)
                        end
                        item = ""
                    else:
                        item = item + ch
                    end
                    k = k + 1
                end
                let trimmed_item = trim(item)
                if len(trimmed_item) > 0:
                    if len(trimmed_item) > 1 and trimmed_item[0] == "\"" and trimmed_item[len(trimmed_item)-1] == "\"":
                        let _u = push(val, trimmed_item[1:len(trimmed_item)-1])
                    else:
                        let _u = push(val, trimmed_item)
                    end
                end
            else:
                val = val_part
            end
            
            if current_section == "":
                toml[key] = val
            else:
                let sec_map = toml[current_section]
                sec_map[key] = val
            end
        end
    end
    
    return toml
end
