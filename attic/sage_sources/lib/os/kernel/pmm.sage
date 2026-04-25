gc_disable()

# pmm.sage — Physical Memory Manager (Runtime-Free)
# Bitmap-based allocator using raw memory.

# ----- Constants -----
let PAGE_SIZE = 4096

# ----- Internal state -----
let _bitmap_ptr = 0
let _bitmap_size_bytes = 0
let _total_pages = 0
let _used_pages = 0
let _memory_total = 0
let _pmm_ready = false

# ----- Alignment helpers -----

proc align_up(addr, alignment):
    let mask = alignment - 1
    return (addr + mask) & ~mask
end

proc align_down(addr, alignment):
    return addr & ~(alignment - 1)
end

# ----- Bitmap helpers (using raw memory access) -----

proc set_bit(page_num):
    if _bitmap_ptr == 0:
        return
    end
    let byte_off = page_num / 8
    let bit_off = page_num % 8
    if byte_off < _bitmap_size_bytes:
        let val = mem_read(_bitmap_ptr, byte_off, "byte")
        if (val & (1 << bit_off)) == 0:
            mem_write(_bitmap_ptr, byte_off, "byte", val | (1 << bit_off))
            _used_pages = _used_pages + 1
        end
    end
end

proc clear_bit(page_num):
    if _bitmap_ptr == 0:
        return
    end
    let byte_off = page_num / 8
    let bit_off = page_num % 8
    if byte_off < _bitmap_size_bytes:
        let val = mem_read(_bitmap_ptr, byte_off, "byte")
        if (val & (1 << bit_off)) != 0:
            mem_write(_bitmap_ptr, byte_off, "byte", val & ~(1 << bit_off))
            _used_pages = _used_pages - 1
        end
    end
end

proc test_bit(page_num):
    if _bitmap_ptr == 0:
        return true
    end
    let byte_off = page_num / 8
    let bit_off = page_num % 8
    if byte_off >= _bitmap_size_bytes:
        return true
    end
    let val = mem_read(_bitmap_ptr, byte_off, "byte")
    return (val & (1 << bit_off)) != 0
end

# ----- Initialize from memory map -----

proc init(memory_map, bitmap_base):
    _memory_total = 0
    if memory_map != nil:
        for i in range(len(memory_map)):
            let region = memory_map[i]
            let end_addr = region["base"] + region["length"]
            if end_addr > _memory_total:
                _memory_total = end_addr
            end
        end
    end

    if _memory_total == 0:
        _memory_total = 128 * 1024 * 1024 # Fallback 128MB
    end

    _total_pages = _memory_total / PAGE_SIZE
    _bitmap_size_bytes = (_total_pages / 8) + 1
    _bitmap_ptr = bitmap_base
    _used_pages = 0

    # Clear bitmap
    for i in range(_bitmap_size_bytes):
        mem_write(_bitmap_ptr, i, "byte", 0)
    end

    # Mark all as used initially, then free available regions
    _used_pages = _total_pages
    for i in range(_bitmap_size_bytes):
        mem_write(_bitmap_ptr, i, "byte", 0xFF)
    end

    if memory_map != nil:
        for i in range(len(memory_map)):
            let region = memory_map[i]
            if region["type"] == "available":
                mark_region(region["base"], region["base"] + region["length"], false)
            end
        end
    end

    _pmm_ready = true
end

proc mark_region(start, end_addr, used):
    let page_start = align_up(start, PAGE_SIZE) / PAGE_SIZE
    let page_end = align_down(end_addr, PAGE_SIZE) / PAGE_SIZE
    for p in range(page_start, page_end):
        if p < _total_pages:
            if used:
                set_bit(p)
            else:
                clear_bit(p)
            end
        end
    end
end

proc alloc_page():
    for p in range(_total_pages):
        if not test_bit(p):
            set_bit(p)
            return p * PAGE_SIZE
        end
    end
    return nil
end

proc free_page(addr):
    let page_num = addr / PAGE_SIZE
    if page_num < _total_pages:
        clear_bit(page_num)
    end
end

proc stats():
    return {
        "total_mb": _memory_total / 1048576,
        "used_pages": _used_pages,
        "free_pages": _total_pages - _used_pages
    }
end
