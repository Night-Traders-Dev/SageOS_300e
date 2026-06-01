let tasks = [
    {"id": 0, "name": "idle", "state": 2, "cpu": 0},
    {"id": 1, "name": "runtime_manager", "state": 1, "cpu": 0}
]

let i = 0
let t_count = len(tasks)

while i < t_count:
    let t = tasks[i]
    let id = str(t["id"])
    while len(id) < 3: 
        id = " " + id
    
    let name = t["name"]
    while len(name) < 16: name = name + " "
    
    let state_val = t["state"]
    let state = "UNKNOWN"
    if state_val == 0: state = "UNUSED  "
    elif state_val == 1: state = "READY   "
    elif state_val == 2: state = "RUNNING "
    elif state_val == 3: state = "SLEEPING"
    elif state_val == 4: state = "BLOCKED "
    elif state_val == 5: state = "EXITED  "
    
    let cpu = str(t["cpu"])
    while len(cpu) < 3: cpu = " " + cpu
    
    print "| " + id + " | " + name + " | " + state + " | " + cpu + " |"
    i = i + 1

print "DONE"

# Spawn interactive userspace shell (Stage 7)
os_spawn_task("shell", "lib/sagelang/sage_shell_combined.sage")
