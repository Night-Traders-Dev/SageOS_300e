let tasks = [
    {"id": 0, "name": "idle", "state": 2, "cpu": 0},
    {"id": 1, "name": "runtime_manager", "state": 1, "cpu": 0}
]

let i = 0
let t_count = len(tasks)
print "t_count: "
print t_count

while i < t_count:
    let t = tasks[i]
    let id = str(t["id"])
    print "id before format: "
    print id
    while len(id) < 3: 
        print "LOOP PULSE id="
        print id
        print "len(id)="
        print len(id)
        id = " " + id
    print "id after format: "
    print id
    
    let name = t["name"]
    while len(name) < 16: name = name + " "
    print "name after format: "
    print name
    
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
