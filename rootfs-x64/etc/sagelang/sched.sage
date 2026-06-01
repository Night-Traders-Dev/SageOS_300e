# sched.sage - Pure Sage process scheduler table

proc main():
    let tasks = os_get_tasks()
    if tasks == nil:
        os_write_str("\nError: Could not fetch task list")
        return

    os_write_str("\n\033[1;36m+-----+------------------+----------+-----+\033[0m")
    os_write_str("\n\033[1;36m| ID  | NAME             | STATE    | CPU |\033[0m")
    os_write_str("\n\033[1;36m+-----+------------------+----------+-----+\033[0m")

    let i = 0
    let t_count = len(tasks)
    while i < t_count:
        let t = tasks[i]
        let id = os_num_to_str(t["id"])
        while len(id) < 3: id = " " + id
        
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
        
        let cpu = os_num_to_str(t["cpu"])
        while len(cpu) < 3: cpu = " " + cpu
        
        os_write_str("\n| " + id + " | " + name + " | " + state + " | " + cpu + " |")
        i = i + 1

    os_write_str("\n\033[1;36m+-----+------------------+----------+-----+\033[0m")
    os_write_str("\nTotal threads: " + os_num_to_str(t_count) + "\n")

main()
