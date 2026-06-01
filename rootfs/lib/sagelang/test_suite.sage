# test_suite.sage - Unified Regression Test Suite for SageOS

proc assert_eq(val, expected, msg):
    if val == expected:
        os_write_str("\n[PASS] " + msg)
    else:
        os_write_str("\n[FAIL] " + msg + " (expected " + os_num_to_str(expected) + ", got " + os_num_to_str(val) + ")")

proc test_ramfs():
    os_write_str("\n--- Running RamFS Unit Tests ---")
    
    # Test directory creation
    let r1 = os_mkdir("/tmp/test_dir")
    assert_eq(r1, 0, "mkdir /tmp/test_dir")
    
    # Test file creation
    let r2 = os_touch("/tmp/test_dir/hello.txt")
    assert_eq(r2, 0, "touch /tmp/test_dir/hello.txt")
    
    # Test write
    let data = "Hello, SageOS!"
    let r3 = os_write("/tmp/test_dir/hello.txt", data)
    assert_eq(r3, len(data), "write to hello.txt")
    
    # Test stat
    let st = os_vfs_stat("/tmp/test_dir/hello.txt")
    if st != nil:
        assert_eq(st["size"], len(data), "stat size check")
        assert_eq(st["type"], 0, "stat type check (file)")
    else:
        os_write_str("\n[FAIL] stat hello.txt returned nil")
        
    # Test readdir
    let entries = os_vfs_readdir("/tmp/test_dir")
    if entries != nil:
        assert_eq(len(entries), 1, "readdir entries count")
        assert_eq(entries[0]["name"], "hello.txt", "readdir entry name")
    else:
        os_write_str("\n[FAIL] readdir /tmp/test_dir returned nil")
    
    # Test unlink
    let r4 = os_rm("/tmp/test_dir/hello.txt")
    assert_eq(r4, 0, "rm hello.txt")
    
    os_write_str("\nRamFS tests completed.")

proc test_telemetry():
    os_write_str("\n--- Running Telemetry Sanity Tests ---")
    
    let tasks = os_get_tasks()
    if tasks != nil and len(tasks) > 0:
        os_write_str("\n[PASS] os_get_tasks returned " + os_num_to_str(len(tasks)) + " tasks")
    else:
        os_write_str("\n[FAIL] os_get_tasks failed")
        
    let mem = os_get_mem_stats()
    if mem != nil:
        os_write_str("\n[PASS] os_get_mem_stats returned used=" + os_num_to_str(mem["used"]))
    else:
        os_write_str("\n[FAIL] os_get_mem_stats failed")

proc main():
    os_write_str("\n=== SageOS Unified Regression Test Suite ===")
    test_ramfs()
    test_telemetry()
    os_write_str("\n\nAll tests finished.\n")

main()
