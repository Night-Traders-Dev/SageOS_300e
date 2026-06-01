let s = "hello"
let left = os_substr(s, 0, 0)
let right = os_substr(s, 0, 5)
os_write_str("left: [" + left + "]\n")
os_write_str("right: [" + right + "]\n")
os_write_str("inserted: [" + left + os_chr(65) + right + "]\n")
