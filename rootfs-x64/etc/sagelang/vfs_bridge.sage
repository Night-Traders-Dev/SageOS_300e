# vfs_bridge.sage
# Virtual Filesystem Bridge & Sage-native RamFS Driver for SageOS

# -----------------------------------------------------------------------------
# Sage-native RAM Filesystem (RamFS) Procedural Driver
# -----------------------------------------------------------------------------
let g_ramfs_root = {}
g_ramfs_root["type"] = "dir"
g_ramfs_root["name"] = "/"
g_ramfs_root["children"] = {}
g_ramfs_root["size"] = 0

proc ramfs_resolve(path):
    let cur = g_ramfs_root
    let len = os_strlen(path)
    if len == 0 or path == "/":
        return cur

    let pos = 0
    if os_char_at(path, 0) == 47: # '/'
        pos = 1

    while pos < len:
        let start = pos
        while pos < len:
            let c = os_char_at(path, pos)
            if c == 47: # '/'
                break
            pos = pos + 1
        end

        if pos > start:
            let comp = os_substr(path, start, pos)
            if cur["type"] != "dir":
                return nil
            end
            let child = cur["children"][comp]
            if child == nil:
                return nil
            end
            cur = child
        end

        if pos < len and os_char_at(path, pos) == 47: # '/'
            pos = pos + 1
        end
    end
    return cur

proc ramfs_resolve_parent(path):
    let len = os_strlen(path)
    let last_slash = -1
    let i = len - 1
    while i >= 0:
        if os_char_at(path, i) == 47: # '/'
            last_slash = i
            break
        i = i - 1

    if last_slash < 0:
        return [g_ramfs_root, path]

    let parent_path = "/"
    if last_slash > 0:
        parent_path = os_substr(path, 0, last_slash)

    let name = os_substr(path, last_slash + 1, len)
    let parent_node = ramfs_resolve(parent_path)
    return [parent_node, name]

proc ramfs_stat(path):
    let node = ramfs_resolve(path)
    if node == nil:
        return nil
    let st = {}
    st["name"] = node["name"]
    if node["type"] == "dir":
        st["type"] = 1 # VFS_DIRECTORY
    else:
        st["type"] = 0 # VFS_FILE
    end
    st["size"] = node["size"]
    return st

proc ramfs_readdir(path):
    let node = ramfs_resolve(path)
    if node == nil or node["type"] != "dir":
        return nil
    let arr = []
    let children = node["children"]
    let keys = dict_keys(children)
    if keys == nil: return arr end
    
    let i = 0
    let k_len = os_array_len(keys)
    while i < k_len:
        let name = keys[i]
        let child = children[name]
        if child != nil:
            let entry = {}
            entry["name"] = name
            if child["type"] == "dir":
                entry["type"] = 1
            else:
                entry["type"] = 0
            entry["size"] = child["size"]
            os_array_push(arr, entry)
        i = i + 1
    return arr

proc ramfs_read(path, offset, out_buf, size):
    let node = ramfs_resolve(path)
    if node == nil or node["type"] != "file":
        return nil
    let file_data = node["data"]
    let file_len = os_strlen(file_data)
    if offset >= file_len:
        return ""
    let end_idx = offset + size
    if end_idx > file_len:
        end_idx = file_len
    return os_substr(file_data, offset, end_idx)

proc ramfs_write(path, offset, data, size):
    let node = ramfs_resolve(path)
    if node == nil or node["type"] != "file":
        return -2 # VFS_ENOENT

    let data_len = os_strlen(data)
    let actual_size = size
    if actual_size > data_len:
        actual_size = data_len

    let old_data = node["data"]
    let old_len = os_strlen(old_data)

    let prefix = ""
    if offset > 0:
        if offset > old_len:
            prefix = old_data
            let pad_count = offset - old_len
            while pad_count > 0:
                prefix = prefix + " "
                pad_count = pad_count - 1
        else:
            prefix = os_substr(old_data, 0, offset)

    let suffix = ""
    let end_idx = offset + actual_size
    if end_idx < old_len:
        suffix = os_substr(old_data, end_idx, old_len)

    let write_data = data
    if actual_size < data_len:
        write_data = os_substr(data, 0, actual_size)

    node["data"] = prefix + write_data + suffix
    node["size"] = os_strlen(node["data"])
    return actual_size

proc ramfs_mkdir(path):
    let res = ramfs_resolve_parent(path)
    let parent = res[0]
    let name = res[1]

    if parent == nil or parent["type"] != "dir":
        return -2 # VFS_ENOENT
    if parent["children"][name] != nil:
        return -17 # VFS_EEXIST
    let node = {}
    node["type"] = "dir"
    node["name"] = name
    node["children"] = {}
    node["size"] = 0
    parent["children"][name] = node
    return 0

proc ramfs_create(path):
    let res = ramfs_resolve_parent(path)
    let parent = res[0]
    let name = res[1]
    if parent == nil or parent["type"] != "dir":
        return -2 # VFS_ENOENT
    if parent["children"][name] != nil:
        let node = parent["children"][name]
        if node["type"] == "file":
            node["size"] = 0
            node["data"] = ""
            return 0
        return -17 # VFS_EEXIST
    let node = {}
    node["type"] = "file"
    node["name"] = name
    node["size"] = 0
    node["data"] = ""
    parent["children"][name] = node
    return 0

proc ramfs_unlink(path):
    let res = ramfs_resolve_parent(path)
    let parent = res[0]
    let name = res[1]
    if parent == nil or parent["type"] != "dir":
        return -2 # VFS_ENOENT
    if parent["children"][name] == nil:
        return -2 # VFS_ENOENT
    parent["children"][name] = nil
    return 0

# Construct RamFS backend interface dictionary
let g_ramfs_backend = {}
g_ramfs_backend["stat"] = ramfs_stat
g_ramfs_backend["readdir"] = ramfs_readdir
g_ramfs_backend["read"] = ramfs_read
g_ramfs_backend["write"] = ramfs_write
g_ramfs_backend["mkdir"] = ramfs_mkdir
g_ramfs_backend["create"] = ramfs_create
g_ramfs_backend["unlink"] = ramfs_unlink

# -----------------------------------------------------------------------------
# Unified VFS Router (with Union Support)
# -----------------------------------------------------------------------------
let g_vfs_mounts = []

proc vfs_mount(path, backend_ptr):
    let m = {}
    m["path"] = path
    m["backend"] = backend_ptr
    m["is_sage"] = 0
    os_array_push(g_vfs_mounts, m)
    return 0

proc vfs_mount_sage(path, sage_backend):
    let m = {}
    m["path"] = path
    m["backend"] = sage_backend
    m["is_sage"] = 1
    os_array_push(g_vfs_mounts, m)
    return 0

proc vfs_resolve_best(path):
    if os_strlen(path) == 0:
        return []

    let matches = []
    let best_len = -1
    let i = 0
    let m_count = os_array_len(g_vfs_mounts)

    while i < m_count:
        let m = g_vfs_mounts[i]
        let m_path = m["path"]
        let m_len = os_strlen(m_path)

        # Longest prefix match
        if os_starts_with(path, m_path):
            let is_match = 0
            if m_len == 1:
                if os_char_at(m_path, 0) == 47: is_match = 1 end
            elif os_strlen(path) == m_len:
                is_match = 1
            elif os_char_at(path, m_len) == 47:
                is_match = 1
            end

            if is_match == 1:
                if m_len > best_len:
                    best_len = m_len
                    matches = []
                end
                
                if m_len == best_len:
                    let res = {}
                    res["mount"] = m
                    let rel = "/"
                    if m_len > 1:
                        rel = os_substr(path, m_len, os_strlen(path))
                        if os_strlen(rel) == 0: rel = "/"
                        elif os_char_at(rel, 0) != 47: rel = "/" + rel
                        end
                    else:
                        rel = path
                    end
                    res["rel"] = rel
                    os_array_push(matches, res)
                end
            end
        end
        i = i + 1
    end
    return matches

proc vfs_stat(path):
    let matches = vfs_resolve_best(path)
    let i = 0
    let m_count = os_array_len(matches)
    while i < m_count:
        let res = matches[i]
        let mount = res["mount"]
        let st = nil
        if mount["is_sage"] == 1:
            st = mount["backend"]["stat"](res["rel"])
        else:
            st = os_backend_stat(mount["backend"], res["rel"])
        end
        if st != nil: return st end
        i = i + 1
    end
    return nil

proc vfs_readdir(path):
    let matches = vfs_resolve_best(path)
    let entries = []
    
    # Union results from all best-matching mounts
    let i = 0
    let m_count = os_array_len(matches)
    while i < m_count:
        let res = matches[i]
        let mount = res["mount"]
        let e = nil
        if mount["is_sage"] == 1:
            e = mount["backend"]["readdir"](res["rel"])
        else:
            e = os_backend_readdir(mount["backend"], res["rel"])
        end
        
        if e != nil:
            let j = 0
            let e_count = os_array_len(e)
            while j < e_count:
                let entry = e[j]
                # Avoid duplicates in union
                let found = 0
                let k = 0
                let ent_count = os_array_len(entries)
                while k < ent_count:
                    if entries[k]["name"] == entry["name"]: found = 1 break end
                    k = k + 1
                end
                if found == 0: os_array_push(entries, entry) end
                j = j + 1
            end
        end
        i = i + 1
    end

    # Also add mount points that are children of this path
    # Ensure path ends with slash for prefix matching children
    let p_norm = path
    if os_strlen(p_norm) > 0:
        if os_char_at(p_norm, os_strlen(p_norm) - 1) != 47:
            p_norm = p_norm + "/"
        end
    else:
        p_norm = "/"
    end
    let p_len = os_strlen(p_norm)

    i = 0
    let v_count = os_array_len(g_vfs_mounts)
    while i < v_count:
        let m = g_vfs_mounts[i]
        let m_path = m["path"]
        if os_starts_with(m_path, p_norm) and m_path != "/" and m_path != path:
            let sub = os_substr(m_path, p_len, os_strlen(m_path))
            let j = 0
            let comp_name = ""
            while j < os_strlen(sub):
                let c = os_char_at(sub, j)
                if c == 47: break end
                comp_name = comp_name + os_chr(c)
                j = j + 1
            end

            if os_strlen(comp_name) > 0:
                let found = 0
                let k = 0
                let ent_count = os_array_len(entries)
                while k < ent_count:
                    if entries[k]["name"] == comp_name: found = 1 break end
                    k = k + 1
                end
                if found == 0:
                    let entry = {}
                    entry["name"] = comp_name
                    entry["type"] = 1 # Directory
                    entry["size"] = 0
                    os_array_push(entries, entry)
                end
            end
        end
        i = i + 1
    end

    return entries

proc vfs_read(path, offset, size):
    let matches = vfs_resolve_best(path)
    let i = 0
    let m_count = os_array_len(matches)
    while i < m_count:
        let res = matches[i]
        let mount = res["mount"]
        if mount["is_sage"] == 1:
            let data = mount["backend"]["read"](res["rel"], offset, size)
            if data != nil: return [data, os_strlen(data)] end
        else:
            let read_res = os_backend_read(mount["backend"], res["rel"], offset, size)
            if read_res != nil: return read_res end
        end
        i = i + 1
    end
    return nil

proc vfs_write(path, offset, data, size):
    let matches = vfs_resolve_best(path)
    if os_array_len(matches) == 0: return 0 end
    # Write to the first successful match (usually the overlay)
    let i = 0
    while i < os_array_len(matches):
        let res = matches[i]
        let mount = res["mount"]
        let n = 0
        if mount["is_sage"] == 1:
            n = mount["backend"]["write"](res["rel"], offset, data, size)
        else:
            n = os_backend_write(mount["backend"], res["rel"], offset, data, size)
        end
        if n > 0: return n end
        i = i + 1
    end
    return 0

proc vfs_mkdir(path):
    let matches = vfs_resolve_best(path)
    if os_array_len(matches) == 0: return -2 end
    let res = matches[0]
    let mount = res["mount"]
    if mount["is_sage"] == 1: return mount["backend"]["mkdir"](res["rel"])
    else: return -1 end

proc vfs_create(path):
    let matches = vfs_resolve_best(path)
    if os_array_len(matches) == 0: return -2 end
    let res = matches[0]
    let mount = res["mount"]
    if mount["is_sage"] == 1: return mount["backend"]["create"](res["rel"])
    else: return -1 end

proc vfs_unlink(path):
    let matches = vfs_resolve_best(path)
    if os_array_len(matches) == 0: return -2 end
    let res = matches[0]
    let mount = res["mount"]
    if mount["is_sage"] == 1: return mount["backend"]["unlink"](res["rel"])
    else: return -1 end

# -----------------------------------------------------------------------------
# Filesystem Initializer / Populator
# -----------------------------------------------------------------------------
proc vfs_init_fs():
    # Pre-create standard layout in RamFS
    ramfs_mkdir("/bin")
    ramfs_mkdir("/dev")
    ramfs_mkdir("/etc")
    ramfs_mkdir("/etc/commands")
    ramfs_mkdir("/lib")
    ramfs_mkdir("/mnt")
    ramfs_mkdir("/mnt/fat32")
    ramfs_mkdir("/mnt/btrfs")
    ramfs_mkdir("/opt")
    ramfs_mkdir("/proc")
    ramfs_mkdir("/tmp")
    ramfs_mkdir("/usr")
    ramfs_mkdir("/usr/bin")
    ramfs_mkdir("/usr/lib")
    ramfs_mkdir("/usr/include")
    ramfs_mkdir("/usr/libexec")
    ramfs_mkdir("/usr/share")
    ramfs_mkdir("/usr/local")
    ramfs_mkdir("/usr/local/bin")
    ramfs_mkdir("/usr/local/lib")
    ramfs_mkdir("/var")
    ramfs_mkdir("/var/log")
    ramfs_mkdir("/var/run")
    ramfs_mkdir("/var/tmp")
    ramfs_mkdir("/home")
    ramfs_mkdir("/root")

    # Fetch and populate all C-embedded files dynamically
    let count = os_get_embedded_count()
    let i = 0
    while i < count:
        let file_info = os_get_embedded_file(i)
        if file_info != nil:
            let path = file_info["path"]
            let data = file_info["data"]
            
            # Map /etc/system/ to /system/
            if os_starts_with(path, "/etc/system/"):
                let sys_path = "/system/" + os_substr(path, 12, os_strlen(path))
                # Ensure parent directories exist
                if ramfs_resolve("/system") == nil: ramfs_mkdir("/system") end
                if ramfs_resolve("/system/sagelang") == nil: ramfs_mkdir("/system/sagelang") end
                ramfs_create(sys_path)
                ramfs_write(sys_path, 0, data, os_strlen(data))
            end

            ramfs_create(path)
            ramfs_write(path, 0, data, os_strlen(data))
            
            # If it's a command, also populate into /bin/
            if os_starts_with(path, "/etc/commands/"):
                let name = os_substr(path, 14, os_strlen(path))
                let bin_path = "/bin/" + name
                ramfs_create(bin_path)
                ramfs_write(bin_path, 0, data, os_strlen(data))
            end
        end
        i = i + 1

    # Mount our clean Sage-native RamFS on "/embedded"
    vfs_mount_sage("/embedded", g_ramfs_backend)

# Automatically bootstrap filesystems on startup
vfs_init_fs()
