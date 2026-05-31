#!/usr/bin/env sage
import sys
import io
import json
import string

let _sys = sys
let _io = io

# ============================================================================
# Version — single source of truth for self-update detection
# ============================================================================
let SAGEPKG_VERSION = "2.1.2"

# ANSI Colors
let ESC = chr(27)
let RESET = ESC + "[0m"
let BOLD = ESC + "[1m"
let GREEN = ESC + "[32m"
let BLUE = ESC + "[34m"
let CYAN = ESC + "[36m"
let RED = ESC + "[31m"
let YELLOW = ESC + "[33m"
let MAGENTA = ESC + "[35m"
let DIM = ESC + "[2m"

let REPO_URL = "https://raw.githubusercontent.com/Night-Traders-Dev/SagePkg/main"

let HOME = _sys.getenv("HOME")
if HOME == nil:
    HOME = "."

let CONFIG_DIR = HOME + "/.sagepkg"
let PKGS_DIR = CONFIG_DIR + "/pkgs"
let BIN_DIR = CONFIG_DIR + "/bin"
let INDEX_FILE = CONFIG_DIR + "/packages.json"
let INSTALLED_FILE = CONFIG_DIR + "/installed.json"

# ============================================================================
# Unique per-process temp file to avoid races between concurrent sagepkg runs
# ============================================================================
proc _make_temp_file():
    _sys.exec("printf '%d' $PPID > /tmp/_sagepkg_pid_tmp")
    let pid = trim(_io.readfile("/tmp/_sagepkg_pid_tmp"))
    _sys.exec("rm -f /tmp/_sagepkg_pid_tmp")
    if pid == "" or pid == nil:
        pid = "0"
    return CONFIG_DIR + "/.tmp_" + pid

# UI Helpers
proc ui_info(msg):
    print BLUE + BOLD + "info" + RESET + " " + msg

proc ui_success(msg):
    print GREEN + BOLD + "success" + RESET + " " + msg

proc ui_warn(msg):
    print YELLOW + BOLD + "warn" + RESET + " " + msg

proc ui_error(msg):
    print RED + BOLD + "error" + RESET + " " + msg

proc ui_step(msg):
    print CYAN + BOLD + "==>" + RESET + " " + BOLD + msg + RESET

proc ui_hint(msg):
    print DIM + "hint: " + msg + RESET

proc ui_progress(current, total, prefix):
    if total == 0:
        return
    let width = 20
    let progress = (current * width / total) | 0
    let bar = "["
    for i in range(width):
        if i < progress:
            bar = bar + "="
        elif i == progress:
            bar = bar + ">"
        else:
            bar = bar + " "
    bar = bar + "]"
    let percent = (current * 100 / total) | 0
    _sys.exec("printf '\\r" + ESC + "[K" + CYAN + prefix + RESET + " " + bar + " " + str(percent) + "%%'")

proc trim(s):
    if s == nil:
        return ""
    let start_idx = 0
    while start_idx < len(s) and (s[start_idx] == " " or s[start_idx] == "\n" or s[start_idx] == "\r" or s[start_idx] == "\t"):
        start_idx = start_idx + 1
    let end_idx = len(s) - 1
    while end_idx >= start_idx and (s[end_idx] == " " or s[end_idx] == "\n" or s[end_idx] == "\r" or s[end_idx] == "\t"):
        end_idx = end_idx - 1
    if end_idx < start_idx:
        return ""
    let result = ""
    for i in range(end_idx - start_idx + 1):
        result = result + s[start_idx + i]
    return result

proc ensure_dir(dir):
    if not _io.isdir(dir):
        _io.mkdir(dir)

# ============================================================================
# Path safety validation
# Rejects paths with ".." traversal or shell metacharacters.
# Allowed: a-z A-Z 0-9 - _ / .
# ============================================================================
proc is_safe_path(p):
    if p == nil or len(p) == 0:
        return false
    # Check for ".." manually to avoid external procedure dependencies
    for i in range(len(p) - 1):
        if p[i] == "." and p[i+1] == ".":
            return false
    for i in range(len(p)):
        let c = p[i]
        if not ((c >= "a" and c <= "z") or (c >= "A" and c <= "Z") or
                (c >= "0" and c <= "9") or c == "-" or c == "_" or
                c == "/" or c == "."):
            return false
    return true

proc sanitize_url(url):
    # Strip single quotes which would break out of our shell command
    let res = ""
    for i in range(len(url)):
        if url[i] != chr(39):
            res = res + url[i]
    return res

# ============================================================================
# Shell-safe file download. URL components are validated before use.
# ============================================================================
proc download_file(url, dest):
    # Basic sanity — dest must be a safe path we constructed
    if not is_safe_path(dest):
        ui_error("Refusing to download to unsafe path: " + dest)
        return false
    let safe_url = sanitize_url(url)
    let cmd = "curl -sL -H 'Cache-Control: no-cache' '" + safe_url + "' -o '" + dest + "'"
    let res = _sys.exec(cmd)
    if res != 0:
        ui_error("curl failed with exit code " + str(res))
    return res == 0

proc read_json(path):
    let content = _io.readfile(path)
    if content == nil:
        return nil
    let cjson = json.cJSON_Parse(content)
    if cjson == nil:
        return nil
    let data = json.cJSON_ToSage(cjson)
    json.cJSON_Delete(cjson)
    return data

proc write_json(path, data):
    let cjson = json.cJSON_FromSage(data)
    let content = json.cJSON_Print(cjson)
    _io.writefile(path, content)
    json.cJSON_Delete(cjson)

proc get_arch():
    ensure_dir(CONFIG_DIR)
    let tmp = _make_temp_file()
    _sys.exec("uname -m > '" + tmp + "'")
    let arch = trim(_io.readfile(tmp))
    _sys.exec("rm -f '" + tmp + "'")
    return arch

proc get_full_path(path):
    ensure_dir(CONFIG_DIR)
    let tmp = _make_temp_file()
    _sys.exec("readlink -f '" + path + "' > '" + tmp + "'")
    let full_path = trim(_io.readfile(tmp))
    _sys.exec("rm -f '" + tmp + "'")
    return full_path

proc get_date():
    ensure_dir(CONFIG_DIR)
    let tmp = _make_temp_file()
    _sys.exec("date +%Y-%m-%d > '" + tmp + "'")
    let d = trim(_io.readfile(tmp))
    _sys.exec("rm -f '" + tmp + "'")
    if d == "":
        return "2026-05-15"
    return d

proc cmd_init():
    let shell = _sys.getenv("SHELL")
    if shell == nil:
        shell = "/bin/sh"

    let home = _sys.getenv("HOME")
    if home == nil:
        return

    ensure_dir(CONFIG_DIR)
    ensure_dir(BIN_DIR)
    let full_bin_path = get_full_path(BIN_DIR)

    let config_file = nil
    let path_cmd = nil

    if string.contains(shell, "bash") or shell == "/bin/sh":
        config_file = home + "/.bashrc"
        path_cmd = "export PATH=" + chr(34) + full_bin_path + ":$PATH" + chr(34)
    elif string.contains(shell, "zsh"):
        config_file = home + "/.zshrc"
        path_cmd = "export PATH=" + chr(34) + full_bin_path + ":$PATH" + chr(34)
    elif string.contains(shell, "fish"):
        config_file = home + "/.config/fish/config.fish"
        path_cmd = "set -gx PATH " + full_bin_path + " $PATH"
    elif string.contains(shell, "SageShell"):
        config_file = home + "/.sageshellrc"
        path_cmd = "export PATH=" + full_bin_path + ":$PATH"

    if config_file != nil:
        if _io.exists(config_file):
            let content = _io.readfile(config_file)
            if not string.contains(content, full_bin_path):
                ui_info("Automatically adding " + full_bin_path + " to " + config_file + "...")
                _io.appendfile(config_file, chr(10) + "# SagePkg PATH" + chr(10) + path_cmd + chr(10))
                ui_success("PATH initialized. Please restart your shell.")
        else:
            if shell != "/bin/sh":
                ui_info("Creating " + config_file + " and setting PATH...")
                _io.writefile(config_file, "# SagePkg PATH" + chr(10) + path_cmd + chr(10))

let VERBOSE = false

proc ui_verbose(msg):
    if VERBOSE:
        print DIM + "  [debug] " + msg + RESET

proc cmd_update(force):
    cmd_init()
    ui_step("Updating package index...")
    ensure_dir(CONFIG_DIR)
    
    let url = REPO_URL + "/packages.json"
    let new_index_file = CONFIG_DIR + "/packages_new.json"

    ui_verbose("Remote URL: " + url)
    
    if force:
        ui_info("Forcing fresh download...")
    
    if not download_file(url, new_index_file):
        ui_error("Failed to download index from " + url)
        return

    let new_index = read_json(new_index_file)
    if new_index == nil:
        ui_error("Failed to parse new index.")
        return

    let installed = read_json(INSTALLED_FILE)
    if installed == nil:
        installed = {"packages": {}}

    let updates = []
    let new_pkgs = new_index["packages"]

    # Check all installed packages for available updates
    let installed_names = dict_keys(installed["packages"])
    for i in range(len(installed_names)):
        let name = installed_names[i]
        let current_ver = installed["packages"][name]["version"]
        for j in range(len(new_pkgs)):
            if new_pkgs[j]["name"] == name:
                if new_pkgs[j]["version"] != current_ver:
                    push(updates, {
                        "name": name,
                        "old": current_ver,
                        "new": new_pkgs[j]["version"]
                    })

    # Bootstrap case: sagepkg is not yet recorded in installed.json (first-time install).
    # Use the compiled-in SAGEPKG_VERSION constant — no hardcoded fallback string.
    if installed["packages"]["sagepkg"] == nil:
        for j in range(len(new_pkgs)):
            if new_pkgs[j]["name"] == "sagepkg":
                if new_pkgs[j]["version"] != SAGEPKG_VERSION:
                    push(updates, {
                        "name": "sagepkg",
                        "old": SAGEPKG_VERSION,
                        "new": new_pkgs[j]["version"]
                    })

    if len(updates) > 0:
        print ""
        ui_info("The following packages can be updated:")
        for i in range(len(updates)):
            let u = updates[i]
            print "  " + BOLD + u["name"] + RESET + ": " + u["old"] + " -> " + GREEN + u["new"] + RESET

        print ""
        let tmp = _make_temp_file()
        _sys.exec("printf '" + CYAN + BOLD + "?" + RESET + " Update these packages? (y/n): ' && read ans && echo \"$ans\" > '" + tmp + "'")
        let ans = trim(_io.readfile(tmp))
        _sys.exec("rm -f '" + tmp + "'")

        if ans == "y" or ans == "Y":
            _sys.exec("mv '" + new_index_file + "' '" + INDEX_FILE + "'")
            for i in range(len(updates)):
                cmd_install(updates[i]["name"], false)
            ui_success("All packages updated.")
        else:
            _sys.exec("mv '" + new_index_file + "' '" + INDEX_FILE + "'")
            ui_info("Update cancelled. Index updated.")
    else:
        _sys.exec("mv '" + new_index_file + "' '" + INDEX_FILE + "'")
        ui_success("Index updated. All packages are up to date.")

proc cmd_list():
    let data = read_json(INDEX_FILE)
    if data == nil:
        ui_error("No package index found. Run 'update' first.")
        return

    print BOLD + "Available packages:" + RESET
    print DIM + "-------------------" + RESET
    let pkgs = data["packages"]
    for i in range(len(pkgs)):
        let p = pkgs[i]
        print GREEN + BOLD + p["name"] + RESET + " (v" + p["version"] + ") - " + p["description"]

proc cmd_install(pkg_name, reinstall):
    if reinstall:
        cmd_remove(pkg_name)
    if pkg_name == nil or len(pkg_name) == 0:
        ui_error("Invalid package name.")
        return
    for i in range(len(pkg_name)):
        let c = pkg_name[i]
        if not ((c >= "a" and c <= "z") or (c >= "A" and c <= "Z") or (c >= "0" and c <= "9") or c == "-" or c == "_"):
            ui_error("Package name contains invalid characters.")
            return

    let index_data = read_json(INDEX_FILE)
    if index_data == nil:
        ui_error("No package index found. Run 'update' first.")
        return

    let pkg_info = nil
    let pkgs = index_data["packages"]
    for i in range(len(pkgs)):
        if pkgs[i]["name"] == pkg_name:
            pkg_info = pkgs[i]

    if pkg_info == nil:
        ui_error("Package '" + pkg_name + "' not found in index.")
        return

    let arch = get_arch()
    let arch_supported = false
    let arches = pkg_info["architectures"]
    for i in range(len(arches)):
        if arches[i] == arch or arches[i] == "universal":
            arch_supported = true

    if not arch_supported:
        ui_warn("Binary not available for architecture: " + arch + ". Falling back to source build.")
        cmd_build(pkg_name)
        return

    ui_step("Installing " + BOLD + pkg_name + RESET + " for " + arch + "...")
    ensure_dir(CONFIG_DIR)
    ensure_dir(PKGS_DIR)
    ensure_dir(BIN_DIR)
    let pkg_dir = PKGS_DIR + "/" + pkg_name
    ensure_dir(pkg_dir)

    ui_info("Downloading metadata...")
    let meta_url = REPO_URL + "/packages/" + pkg_name + "/metadata.json"
    let meta_file = pkg_dir + "/metadata.json"
    if not download_file(meta_url, meta_file):
        ui_error("Failed to download metadata.")
        return

    let meta = read_json(meta_file)
    if meta == nil:
        ui_error("Failed to parse metadata.")
        return

    # Validate main entry from untrusted downloaded metadata
    let main_file = meta["main"]
    if not is_safe_path(main_file):
        ui_error("Metadata contains unsafe 'main' path: " + str(main_file))
        return

    let files = meta["files"]
    print ""  # reserve a blank line so the progress bar's \r always has its own clean line
    for i in range(len(files)):
        let fname = files[i]

        # Validate every file path from untrusted metadata
        if not is_safe_path(fname):
            ui_error("Metadata contains unsafe file path: " + str(fname))
            return

        ui_progress(i, len(files), "Downloading files")

        let f_url = nil
        if string.contains(fname, "universal/"):
            f_url = REPO_URL + "/packages/" + pkg_name + "/" + fname
        else:
            f_url = REPO_URL + "/packages/" + pkg_name + "/" + arch + "/" + fname

        let f_dest = pkg_dir + "/" + fname
        if string.contains(fname, "/"):
            let parts = split(fname, "/")
            if len(parts) > 1:
                ensure_dir(pkg_dir + "/" + parts[0])

        if not download_file(f_url, f_dest):
            print ""
            ui_error("Failed to download file: " + fname)
            return

    ui_progress(len(files), len(files), "Downloading files")
    print ""

    ui_info("Creating binary wrappers...")
    let bin_path = BIN_DIR + "/" + pkg_name
    let has_binary = false
    for i in range(len(files)):
        if files[i] == pkg_name:
            has_binary = true

    if has_binary:
        let full_binary_path = get_full_path(pkg_dir + "/" + pkg_name)
        let wrapper = "#!/bin/sh" + chr(10) + "exec '" + full_binary_path + "' \"$@\"" + chr(10)
        _io.writefile(bin_path, wrapper)
        _sys.exec("chmod +x '" + full_binary_path + "'")
        _sys.exec("chmod +x '" + bin_path + "'")
    elif main_file != nil:
        let full_pkg_path = get_full_path(pkg_dir + "/" + main_file)
        let wrapper = "#!/bin/sh" + chr(10) + "exec sage '" + full_pkg_path + "' \"$@\"" + chr(10)
        _io.writefile(bin_path, wrapper)
        _sys.exec("chmod +x '" + bin_path + "'")

    # Extra named binaries: inject the binary name as first argument so main.sage
    # can route by command rather than by script basename (which is always main.sage).
    let extra_bins = meta["binaries"]
    if extra_bins != nil:
        let full_pkg_path = get_full_path(pkg_dir + "/" + main_file)
        for i in range(len(extra_bins)):
            let bname = extra_bins[i]
            if bname != pkg_name:
                let b_path = BIN_DIR + "/" + bname
                let wrapper = "#!/bin/sh" + chr(10) + "exec sage '" + full_pkg_path + "' " + bname + " \"$@\"" + chr(10)
                _io.writefile(b_path, wrapper)
                _sys.exec("chmod +x '" + b_path + "'")

    let installed = read_json(INSTALLED_FILE)
    if installed == nil:
        installed = {"packages": {}}

    installed["packages"][pkg_name] = {
        "version": meta["version"],
        "install_date": get_date(),
        "files": files
    }
    write_json(INSTALLED_FILE, installed)

    ui_success(BOLD + pkg_name + RESET + " installed successfully.")

    let full_bin_path = get_full_path(BIN_DIR)
    let path_env = _sys.getenv("PATH")
    if string.find(path_env, full_bin_path) == -1:
        print ""
        ui_hint("To run '" + BOLD + pkg_name + RESET + "' by name, add this to your PATH:")
        print "  export PATH=" + chr(34) + full_bin_path + ":$PATH" + chr(34)

proc cmd_build(pkg_name):
    if pkg_name == nil or len(pkg_name) == 0:
        ui_error("Invalid package name.")
        return
    for i in range(len(pkg_name)):
        let c = pkg_name[i]
        if not ((c >= "a" and c <= "z") or (c >= "A" and c <= "Z") or (c >= "0" and c <= "9") or c == "-" or c == "_"):
            ui_error("Package name contains invalid characters.")
            return

    let index_data = read_json(INDEX_FILE)
    if index_data == nil:
        ui_error("No package index found. Run 'update' first.")
        return

    let pkg_info = nil
    let pkgs = index_data["packages"]
    for i in range(len(pkgs)):
        if pkgs[i]["name"] == pkg_name:
            pkg_info = pkgs[i]

    if pkg_info == nil:
        ui_error("Package '" + pkg_name + "' not found in index.")
        return

    ui_step("Building " + BOLD + pkg_name + RESET + " from source...")
    ensure_dir(CONFIG_DIR)
    ensure_dir(PKGS_DIR)
    ensure_dir(BIN_DIR)
    let pkg_dir = PKGS_DIR + "/" + pkg_name
    ensure_dir(pkg_dir)

    ui_info("Downloading metadata...")
    let meta_url = REPO_URL + "/packages/" + pkg_name + "/metadata.json"
    let meta_file = pkg_dir + "/metadata.json"
    if not download_file(meta_url, meta_file):
        ui_error("Failed to download metadata.")
        return

    let meta = read_json(meta_file)
    if meta == nil:
        ui_error("Failed to parse metadata.")
        return

    # Validate main entry from untrusted downloaded metadata
    let main_file = meta["main"]
    if not is_safe_path(main_file):
        ui_error("Metadata contains unsafe 'main' path: " + str(main_file))
        return

    let files = meta["files"]
    let source_files = []
    print ""  # reserve a blank line for the progress bar
    for i in range(len(files)):
        let fname = files[i]

        # Validate every file path from untrusted metadata
        if not is_safe_path(fname):
            ui_error("Metadata contains unsafe file path: " + str(fname))
            return

        if string.contains(fname, "universal/"):
            ui_progress(i, len(files), "Downloading source")
            push(source_files, fname)
            let f_url = REPO_URL + "/packages/" + pkg_name + "/" + fname
            let f_dest = pkg_dir + "/" + fname

            if string.contains(fname, "/"):
                let parts = split(fname, "/")
                if len(parts) > 1:
                    ensure_dir(pkg_dir + "/" + parts[0])

            if not download_file(f_url, f_dest):
                print ""
                ui_error("Failed to download source file: " + fname)
                return

    ui_progress(len(files), len(files), "Downloading source")
    print ""

    if main_file == nil:
        ui_error("No main script defined in metadata.")
        return

    let target_bin = pkg_dir + "/" + pkg_name
    ui_info("Compiling with Sage...")
    let full_main = get_full_path(pkg_dir + "/" + main_file)
    let compile_cmd = "sage --compile '" + full_main + "' -o '" + target_bin + "'"
    let res = _sys.exec(compile_cmd)
    if res != 0:
        ui_error("Compilation failed with exit code " + str(res))
        return

    let bin_path = BIN_DIR + "/" + pkg_name
    let full_binary_path = get_full_path(target_bin)
    let wrapper = "#!/bin/sh" + chr(10) + "exec '" + full_binary_path + "' \"$@\"" + chr(10)
    _io.writefile(bin_path, wrapper)
    _sys.exec("chmod +x '" + full_binary_path + "'")
    _sys.exec("chmod +x '" + bin_path + "'")

    # Extra named binaries: inject command name for routing (same as cmd_install)
    let extra_bins = meta["binaries"]
    if extra_bins != nil:
        let full_pkg_path = get_full_path(pkg_dir + "/" + main_file)
        for i in range(len(extra_bins)):
            let bname = extra_bins[i]
            if bname != pkg_name:
                let b_path = BIN_DIR + "/" + bname
                let wrapper = "#!/bin/sh" + chr(10) + "exec sage '" + full_pkg_path + "' " + bname + " \"$@\"" + chr(10)
                _io.writefile(b_path, wrapper)
                _sys.exec("chmod +x '" + b_path + "'")

    let installed = read_json(INSTALLED_FILE)
    if installed == nil:
        installed = {"packages": {}}

    installed["packages"][pkg_name] = {
        "version": meta["version"],
        "install_date": get_date(),
        "files": source_files,
        "built_from_source": true
    }
    write_json(INSTALLED_FILE, installed)

    ui_success(BOLD + pkg_name + RESET + " built and installed successfully.")

proc cmd_remove(pkg_name):
    let installed = read_json(INSTALLED_FILE)
    if installed == nil or installed["packages"][pkg_name] == nil:
        ui_error("Package '" + pkg_name + "' is not installed.")
        return

    ui_step("Removing " + BOLD + pkg_name + RESET + "...")

    # Resolve real paths and verify they sit inside the expected directories
    # before running rm, guarding against an empty HOME / CONFIG_DIR expansion.
    let full_pkgs_dir = get_full_path(PKGS_DIR)
    let full_bins_dir = get_full_path(BIN_DIR)

    let pkg_dir = PKGS_DIR + "/" + pkg_name
    let bin_path = BIN_DIR + "/" + pkg_name
    let full_pkg = get_full_path(pkg_dir)
    let full_bin = get_full_path(bin_path)

    if len(full_pkg) == 0 or len(full_bin) == 0 or len(full_pkgs_dir) == 0:
        ui_error("Could not resolve package paths. Aborting removal.")
        return

    if not string.contains(full_pkg, full_pkgs_dir):
        ui_error("Resolved package path (" + full_pkg + ") is outside pkgs dir. Aborting.")
        return

    _sys.exec("rm -f '" + full_bin + "'")
    _sys.exec("rm -rf '" + full_pkg + "'")

    dict_delete(installed["packages"], pkg_name)
    write_json(INSTALLED_FILE, installed)
    ui_success(BOLD + pkg_name + RESET + " removed.")

proc cmd_installed():
    let installed = read_json(INSTALLED_FILE)
    if installed == nil or len(installed["packages"]) == 0:
        ui_info("No packages installed.")
        return

    print BOLD + "Installed packages:" + RESET
    print DIM + "-------------------" + RESET
    let names = dict_keys(installed["packages"])
    for i in range(len(names)):
        let name = names[i]
        let info = installed["packages"][name]
        print GREEN + BOLD + name + RESET + " (v" + info["version"] + ")"

proc main():
    let args = _sys.args()
    let cmd_idx = 2
    if len(args) < 3:
        print GREEN + BOLD + "   _____                      _____  _         " + RESET
        print GREEN + BOLD + "  / ____|                    |  __ \\| |        " + RESET
        print GREEN + BOLD + " | (___   __ _  __ _  ___    | |__) | | __  __ " + RESET
        print GREEN + BOLD + "  \\___ \\ / _` |/ _` |/ _ \\   |  ___/| |/ / / / " + RESET
        print GREEN + BOLD + "  ____) | (_| | (_| |  __/   | |    |   < /_/  " + RESET
        print GREEN + BOLD + " |_____/ \\__,_|\\__, |\\___|   |_|    |_|\\_\\\\__\\ " + RESET
        print GREEN + BOLD + "                __/ |                          " + RESET
        print GREEN + BOLD + "               |___/                           " + RESET
        print ""
        print BOLD + CYAN + " SagePkg" + RESET + " v" + SAGEPKG_VERSION + " - The SageLang Package Manager"
        print ""
        print BOLD + " Usage:" + RESET + " sagepkg <command> [args]"
        print ""
        print BOLD + "Commands:" + RESET
        print "  update       Sync package index and check for updates"
        print "  list         List all available packages"
        print "  install <p>  Install a package (binary or source build)"
        print "  build <p>    Force build a package from source"
        print "  remove <p>   Uninstall a package"
        print "  installed    List all installed packages"
        print "  init         Initialize shell PATH"
        return

    let cmd = args[cmd_idx]
    let force = false
    if len(args) > cmd_idx + 1 and args[cmd_idx+1] == "--force":
        force = true
    
    if cmd == "update":
        cmd_update(force)
    elif cmd == "sync":
        cmd_sync()
    elif cmd == "list":
        cmd_list()
    elif cmd == "install":
        if len(args) < 4:
            ui_error("Missing package name.")
        else:
            let pkg = args[3]
            let reinstall = false
            if len(args) > 4 and args[4] == "--reinstall":
                reinstall = true
            cmd_install(pkg, reinstall)
    elif cmd == "build":
        if len(args) < 4:
            ui_error("Missing package name.")
        else:
            cmd_build(args[3])
    elif cmd == "remove":
        if len(args) < 4:
            ui_error("Missing package name.")
        else:
            cmd_remove(args[3])
    elif cmd == "installed":
        cmd_installed()
    elif cmd == "init":
        cmd_init()
    else:
        ui_error("Unknown command: " + cmd)

main()
