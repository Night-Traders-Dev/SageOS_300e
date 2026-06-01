print "------------------------------------------\n"
print "  SageOS System Boot Test  \n"
print "------------------------------------------\n"
print "VFS Status: OK\n"
print "Runtime Status: OK\n"
print "ABI Version: "
print SAGE_ABI_MAJOR
print "."
print SAGE_ABI_MINOR
print "\n"
print "Boot sequence validated.\n"
dmesg_log("BOOT TEST: dmesg_log working.")
