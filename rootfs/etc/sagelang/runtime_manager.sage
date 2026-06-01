# Runtime Manager (PID 1) — Production Specification
# Manages system services, dependency graphs, and self-healing.

import os
import ipc
import sched
import vfs
import log

# Service definition structure:
# { 
#   "name": str,
#   "exec": str,
#   "deps": list[str],
#   "status": str ("pending", "running", "failed"),
#   "pid": int
# }

let registry = {}

proc register_service(name, exec_path, dependencies):
    registry[name] = {
        "name": name,
        "exec": exec_path,
        "deps": dependencies,
        "status": "pending",
        "pid": 0
    }
    log.info("Registered service: " + name)

proc start_service(name):
    if registry[name]["status"] == "running":
        return

    # Check dependencies
    for dep in registry[name]["deps"]:
        if registry[dep]["status"] != "running":
            log.warn("Dependency " + dep + " not ready for " + name)
            return

    log.info("Launching service: " + name)
    let pid = os.spawn_task(registry[name]["exec"])
    if pid > 0:
        registry[name]["pid"] = pid
        registry[name]["status"] = "running"
    else:
        log.error("Failed to start service: " + name)
        registry[name]["status"] = "failed"

proc monitor_loop():
    log.info("Entering service monitoring loop...")
    while true:
        for name, service in registry:
            if service["status"] == "pending":
                start_service(name)
            elif service["status"] == "running":
                # Check heartbeat/process existence
                if not os.process_exists(service["pid"]):
                    log.error("Service " + name + " died! Attempting restart.")
                    service["status"] = "pending"
        
        # Co-operative yield to allow other services to run
        sched.yield()

# Main Bootstrapping
log.info("Initializing Service Registry...")

# Critical Base Services
register_service("dev.manager", "/bin/dev_mgr.bc", [])
register_service("vfs.root", "/bin/vfs_srv.bc", ["dev.manager"])
register_service("net.stack", "/bin/net_stack.bc", ["dev.manager"])

# System Services
register_service("shell", "/bin/sage_shell.bc", ["vfs.root", "dev.manager"])

monitor_loop()
