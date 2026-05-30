# btop.sage - High-performance resource monitor for SageOS

# --- ANSI Constants ---
let ESC = os_chr(27)
let RESET = ESC + "[0m"
let BOLD = ESC + "[1m"
let DIM = ESC + "[2m"
let ITALIC = ESC + "[3m"
let UNDERLINE = ESC + "[4m"

let FG_BLACK = ESC + "[30m"
let FG_RED = ESC + "[31m"
let FG_GREEN = ESC + "[32m"
let FG_YELLOW = ESC + "[33m"
let FG_BLUE = ESC + "[34m"
let FG_MAGENTA = ESC + "[35m"
let FG_CYAN = ESC + "[36m"
let FG_WHITE = ESC + "[37m"
let FG_GRAY = ESC + "[90m"

let BG_BLACK = ESC + "[40m"
let BG_RED = ESC + "[41m"
let BG_GREEN = ESC + "[42m"
let BG_YELLOW = ESC + "[43m"
let BG_BLUE = ESC + "[44m"
let BG_MAGENTA = ESC + "[45m"
let BG_CYAN = ESC + "[46m"
let BG_WHITE = ESC + "[47m"

# --- Box Drawing (UTF-8) ---
let B_TL = os_chr(226) + os_chr(148) + os_chr(140) # ┌
let B_TR = os_chr(226) + os_chr(148) + os_chr(144) # ┐
let B_BL = os_chr(226) + os_chr(148) + os_chr(148) # └
let B_BR = os_chr(226) + os_chr(148) + os_chr(152) # ┘
let B_H  = os_chr(226) + os_chr(148) + os_chr(128) # ─
let B_V  = os_chr(226) + os_chr(148) + os_chr(130) # │
let B_TJ = os_chr(226) + os_chr(148) + os_chr(172) # ┬
let B_BJ = os_chr(226) + os_chr(148) + os_chr(180) # ┴
let B_LJ = os_chr(226) + os_chr(148) + os_chr(156) # ├
let B_RJ = os_chr(226) + os_chr(148) + os_chr(164) # ┤
let B_CJ = os_chr(226) + os_chr(148) + os_chr(188) # ┼

# --- Helpers ---
proc move_cursor(row, col):
    os_move_cursor(row, col)
end

proc get_braille_char(bits):
    let b1 = 226 # 0xE2
    let b2 = 160 + (bits / 64) # 0xA0 | (bits >> 6)
    let b3 = 128 + (bits % 64) # 0x80 | (bits & 0x3F)
    return os_chr(b1) + os_chr(b2) + os_chr(b3)
end

proc draw_box(row, col, w, h, title, color):
    os_write_str(color)
    move_cursor(row, col)
    let top = B_TL + " " + title + " "
    let i = len(top)
    while i < w - 1:
        top = top + B_H
        i = i + 1
    end
    top = top + B_TR
    os_write_str(top)
    
    let r = 1
    while r < h - 1:
        move_cursor(row + r, col)
        os_write_str(B_V)
        move_cursor(row + r, col + w - 1)
        os_write_str(B_V)
        r = r + 1
    end
    
    move_cursor(row + h - 1, col)
    let bot = B_BL
    i = 1
    while i < w - 1:
        bot = bot + B_H
        i = i + 1
    end
    bot = bot + B_BR
    os_write_str(bot)
    os_write_str(RESET)
end

proc draw_bar(row, col, width, val, max_val, color):
    move_cursor(row, col)
    os_write_str(color)
    os_draw_bar(val, max_val, width)
    os_write_str(RESET)
end

# Simple braille graph (1 row high)
proc draw_graph(row, col, width, history, color):
    move_cursor(row, col)
    os_write_str(color)
    let h_len = len(history)
    let start = 0
    if h_len > width * 2:
        start = h_len - (width * 2)
    end
    
    let i = 0
    while i < width:
        let h_idx = start + (i * 2)
        let bits = 0
        if h_idx < h_len:
            let v1 = history[h_idx]
            if v1 > 80: bits = bits + 1 + 2 + 4 + 64
            elif v1 > 60: bits = bits + 1 + 2 + 4
            elif v1 > 40: bits = bits + 1 + 2
            elif v1 > 20: bits = bits + 1
            end
            
            if h_idx + 1 < h_len:
                let v2 = history[h_idx + 1]
                if v2 > 80: bits = bits + 8 + 16 + 32 + 128
                elif v2 > 60: bits = bits + 8 + 16 + 32
                elif v2 > 40: bits = bits + 8 + 16
                elif v2 > 20: bits = bits + 8
                end
            end
        end
        os_write_str(get_braille_char(bits))
        i = i + 1
    end
    os_write_str(RESET)
end

proc main():
    os_write_str(ESC + "[?25l") # Hide cursor
    os_console_clear()
    
    let running = 1
    let cpu_history = []
    let mem_history = []
    
    let frame = 0
    
    while running == 1:
        let term = os_terminal_size()
        let cols = term["cols"]
        let rows = term["rows"]
        
        # Stats gathering
        os_timer_poll()
        let cpu_count = os_smp_cpu_count()
        let cpu_avg = os_cpu_percent()
        
        let mem = os_get_mem_stats()
        let mem_used = 0
        let mem_total = 1
        if mem != nil:
            mem_used = mem["used"]
            mem_total = mem["total"]
        end
        let mem_pct = (mem_used * 100) / mem_total
        
        # Update history
        _push(cpu_history, cpu_avg)
        _push(mem_history, mem_pct)
        if len(cpu_history) > 200:
            cpu_history = _slice(cpu_history, 100, 200)
            mem_history = _slice(mem_history, 100, 200)
        end
        
        # --- UI Drawing ---
        
        # Header
        move_cursor(1, 1)
        os_write_str(BOLD + FG_CYAN + " btop " + RESET + FG_GRAY + " SageOS " + os_version_string() + RESET)
        move_cursor(1, cols - 20)
        os_write_str(FG_GRAY + "q:quit r:refresh" + RESET)
        
        # CPU Box
        let cpu_w = (cols * 40) / 100
        let cpu_h = cpu_count + 6
        draw_box(2, 1, cpu_w, cpu_h, "CPU", FG_CYAN)
        
        let ci = 0
        while ci < cpu_count:
            let cp = os_timer_cpu_percent_at(ci)
            move_cursor(3 + ci, 3)
            os_write_str(FG_WHITE + "CPU" + os_num_to_str(ci) + RESET)
            draw_bar(3 + ci, 9, cpu_w - 15, cp, 100, FG_GREEN)
            os_write_str(" " + os_num_to_str(cp) + "%")
            ci = ci + 1
        end
        
        move_cursor(3 + cpu_count, 3)
        os_write_str(FG_CYAN + "Load Graph" + RESET)
        draw_graph(4 + cpu_count, 3, cpu_w - 6, cpu_history, FG_CYAN)
        
        # MEM Box
        let mem_w = (cols * 30) / 100
        let mem_h = 12
        draw_box(2, cpu_w + 1, mem_w, mem_h, "MEM", FG_MAGENTA)
        
        move_cursor(3, cpu_w + 3)
        os_write_str("RAM " + os_num_to_str(mem_used / (1024*1024)) + "MB / " + os_num_to_str(mem_total / (1024*1024)) + "MB")
        draw_bar(4, cpu_w + 3, mem_w - 6, mem_pct, 100, FG_MAGENTA)
        
        let swp_total = os_swap_total_mb()
        let swp_used = os_swap_used_mb()
        let swp_pct = 0
        if swp_total > 0:
            swp_pct = (swp_used * 100) / swp_total
        end
        
        move_cursor(5, cpu_w + 3)
        os_write_str("SWP " + os_num_to_str(swp_used) + "MB / " + os_num_to_str(swp_total) + "MB")
        draw_bar(6, cpu_w + 3, mem_w - 6, swp_pct, 100, FG_YELLOW)
        
        move_cursor(8, cpu_w + 3)
        os_write_str(FG_MAGENTA + "History" + RESET)
        draw_graph(9, cpu_w + 3, mem_w - 6, mem_history, FG_MAGENTA)
        
        # Task Box
        let task_w = cols - cpu_w - mem_w
        let task_h = 12
        draw_box(2, cpu_w + mem_w + 1, task_w, task_h, "TASKS", FG_YELLOW)
        
        let tasks = os_get_tasks()
        if tasks != nil:
            let ti = 0
            let t_count = len(tasks)
            let row = 0
            while ti < t_count and row < task_h - 3:
                let t = tasks[ti]
                if t["state"] != 0: # Not UNUSED
                    move_cursor(3 + row, cpu_w + mem_w + 3)
                    let name = t["name"]
                    if len(name) > 15: name = os_substr(name, 0, 15) end
                    os_write_str(FG_WHITE + name + RESET)
                    move_cursor(3 + row, cpu_w + mem_w + 20)
                    let s = t["state"]
                    if s == 2: os_write_str(FG_GREEN + "RUN" + RESET)
                    elif s == 3: os_write_str(FG_GRAY + "SLP" + RESET)
                    else: os_write_str(FG_YELLOW + "WT" + RESET)
                    end
                    row = row + 1
                end
                ti = ti + 1
            end
        end
        
        # Footer
        let bat = os_battery_percent()
        move_cursor(rows, 1)
        os_write_str(FG_GRAY + "Battery: " + RESET)
        if bat >= 0:
            os_write_str(os_num_to_str(bat) + "%")
        else:
            os_write_str("N/A")
        end
        os_write_str(FG_GRAY + "  Uptime: " + RESET + os_uptime_str())
        
        # Input and delay
        let wait_ticks = 0
        while wait_ticks < 10:
            let c = os_poll_char()
            if c != -1:
                if c == 113: # 'q'
                    running = 0
                    wait_ticks = 10
                elif c == 114: # 'r'
                    os_console_clear()
                    wait_ticks = 10
                end
            end
            os_delay_ms(50)
            wait_ticks = wait_ticks + 1
        end
        frame = frame + 1
    end
    
    os_write_str(ESC + "[?25h") # Show cursor
    os_console_clear()
end

proc _slice(arr, start, limit):
    let out = []
    let i = start
    while i < limit:
        _push(out, arr[i])
        i = i + 1
    end
    return out
end

proc _push(arr, val):
    arr[len(arr)] = val
end

main()
