# =============================================================================
# SageOS neofetch implementation
# neofetch.sage
# =============================================================================

proc neofetch_label(label):
    os_set_color_hex(0x79FFB0) # Greenish
    os_write_str(label)
    os_set_color_hex(0xE8E8E8) # White

proc neofetch_colors():
    let old = os_get_color()
    # 0x79FFB0, 0x80C8FF, 0xFFBF40, 0xFF7070, 0xDDA0FF, 0xE8E8E8
    os_set_color_hex(0x79FFB0)
    os_write_str("### ")
    os_set_color_hex(0x80C8FF)
    os_write_str("### ")
    os_set_color_hex(0xFFBF40)
    os_write_str("### ")
    os_set_color_hex(0xFF7070)
    os_write_str("### ")
    os_set_color_hex(0xDDA0FF)
    os_write_str("### ")
    os_set_color_hex(0xE8E8E8)
    os_write_str("###")
    os_set_color(old)

proc cmd_neofetch():
    let old_fg = os_get_color()
    
    let logo = [
        "      .::::.      ",
        "   .:++++++++:.   ",
        "  :+++:.  .:+++:  ",
        " /++:   SG   :++\\ ",
        "|++:  SageOS  :++|",
        " \\++:.      .:++/ ",
        "  `:++++++++++:`  ",
        "     `-::::-`     ",
        "                  ",
        "                  ",
        "                  ",
        "                  "
    ]

    os_write_char(10)
    let i = 0
    while i < 12:
        os_set_color_hex(0x79FFB0)
        os_write_str(logo[i])
        os_write_str("  ")
        os_set_color_hex(0xE8E8E8)

        if i == 0:
            os_set_color_hex(0x80C8FF)
            os_write_str("root")
            os_set_color_hex(0xE8E8E8)
            os_write_str("@")
            os_set_color_hex(0x80C8FF)
            os_write_str("sageos")
        elif i == 1:
            os_write_str("-----------")
        elif i == 2:
            neofetch_label("OS:       ")
            os_write_str("SageOS ")
            os_write_str(os_version_string())
            os_write_str(" x86_64")
        elif i == 3:
            neofetch_label("Host:     ")
            os_write_str("Lenovo 300e target")
        elif i == 4:
            neofetch_label("Kernel:   ")
            os_write_str("SageOS modular kernel")
        elif i == 5:
            neofetch_label("Uptime:   ")
            os_write_str(os_uptime_str())
        elif i == 6:
            neofetch_label("Packages: ")
            os_write_str("builtins (kernel shell)")
        elif i == 7:
            neofetch_label("Shell:    ")
            os_write_str("sage-sh")
        elif i == 8:
            neofetch_label("Terminal: ")
            if os_fb_available():
                os_write_str("framebuffer + serial")
            else:
                os_write_str("serial")
        elif i == 9:
            neofetch_label("CPU:      ")
            os_write_str("x86_64, ")
            os_write_str(os_num_to_str(os_smp_cpu_count()))
            os_write_str(" logical CPU(s)")
        elif i == 10:
            neofetch_label("Memory:   ")
            os_write_str(os_num_to_str(os_ram_used_mb()))
            os_write_str(" MB / ")
            os_write_str(os_num_to_str(os_ram_total_mb()))
            os_write_str(" MB")
        elif i == 11:
            neofetch_label("Colors:   ")
            neofetch_colors()
        
        os_write_char(10)
        i = i + 1

    if os_fb_available():
        os_set_color_hex(0x79FFB0)
        os_write_str("                    Resolution: ")
        os_set_color_hex(0xE8E8E8)
        os_write_str(os_fb_width_str())
        os_write_str("x")
        os_write_str(os_fb_height_str())
        os_write_char(10)

    os_set_color(old_fg)
