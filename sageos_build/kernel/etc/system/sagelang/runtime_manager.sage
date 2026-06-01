# Runtime Manager (PID 1) — System Supervisor
# Manages services, dependencies, and self-healing.

import os
import sys
import ipc

let services = {}
let dependencies = {
    "vfs.root": [],
    "net.stack": ["pci.bus"],
    "dev.manager": ["vfs.root"],
    "shell": ["dev.manager", "vfs.root"],
    "sched": ["vfs.root"]
}

proc log(msg):
    print "[SUPERVISOR] " + msg
    os_dmesg_log("[SUPERVISOR] " + msg)

proc start_service(name):
    if dict_has(services, name) and services[name]["status"] == "active":
        return

    log("Starting service: " + name)
    # Check dependencies
    if dict_has(dependencies, name):
        let deps = dependencies[name]
        let i = 0
        while i < len(deps):
            let dep = deps[i]
            if not dict_has(services, dep) or services[dep]["status"] != "active":
                log("Dependency not met: " + dep + " for " + name)
                start_service(dep)
            i = i + 1

    # Spawn live task using our new FFI bridge or simulate if it is a platform service
    let pid = -1
    if name == "shell":
        pid = os_spawn_task("shell", "/etc/sagelang/shell.sage")
    elif name == "sched":
        pid = os_spawn_task("sched_monitor", "/etc/sagelang/sched.sage")
    else:
        # Mock or platform driver
        pid = 100 + len(services)

    services[name] = {"status": "active", "pid": pid}
    log("Service " + name + " is now active with PID " + str(pid) + ".")

proc check_services():
    let tasks = os_get_tasks()
    let service_names = dict_keys(services)
    let i = 0
    while i < len(service_names):
        let name = service_names[i]
        let svc = services[name]
        if svc["pid"] >= 0 and svc["pid"] < 100:
            let found = false
            let j = 0
            while j < len(tasks):
                if tasks[j]["id"] == svc["pid"]:
                    if tasks[j]["state"] != 4: # THREAD_STATE_TERMINATED = 4
                        found = true
                    break
                j = j + 1
            
            if not found:
                log("Service " + name + " (PID " + str(svc["pid"]) + ") has stopped! Restarting...")
                services[name]["status"] = "stopped"
                start_service(name)
        i = i + 1

proc monitor_loop():
    log("Supervisor monitoring loop started.")
    while true:
        sys.sleep(5)
        log("Pulse...")
        check_services()
        # Yield to allow GC to run
        os_gc_collect()

log("SageOS Runtime Manager initializing...")

# Bootstrap critical services in order
start_service("vfs.root")
start_service("dev.manager")
start_service("sched")
start_service("shell")

log("System bootstrap complete. Transitioning to monitor mode.")
monitor_loop()
