# sageos_build/kernel/etc/commands/uname.sage
proc main():
    os_write_char(10)
    os_write_str("SageOS ")
    os_write_str(os_version_string())
    os_write_str(" x86_64 lenovo_300e")
    os_write_char(10)

main()
