proc main():
    os_write_char(10)
    os_write_str("SageOS kernel ")
    os_write_str(os_version_string())
    os_write_str(" modular x86_64")
    os_write_char(10)

main()
