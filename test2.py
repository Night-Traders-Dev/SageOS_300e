import subprocess
import time
import fcntl
import os

p = subprocess.Popen(["bash", "lenovo_300e.sh", "qemu"], stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
fd = p.stdout.fileno()
fl = fcntl.fcntl(fd, fcntl.F_GETFL)
fcntl.fcntl(fd, fcntl.F_SETFL, fl | os.O_NONBLOCK)

def read_until_prompt():
    out = ""
    start = time.time()
    while True:
        if time.time() - start > 15: break
        try:
            chunk = p.stdout.read(1024)
            if chunk:
                out += chunk.decode("utf-8", errors="replace")
                if "root@sageos:/#" in out: break
        except BlockingIOError:
            time.sleep(0.1)
        if p.poll() is not None: break
    return out

print(read_until_prompt())
p.stdin.write(b"echo hello\n")
p.stdin.flush()
print("ECHO RESULT:", read_until_prompt())
