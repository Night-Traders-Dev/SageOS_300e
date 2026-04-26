import subprocess
import time
import os
import fcntl

print("Starting QEMU...")
p = subprocess.Popen(['bash', 'lenovo_300e.sh', 'qemu'], stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)

# set stdout to non-blocking
fd = p.stdout.fileno()
fl = fcntl.fcntl(fd, fcntl.F_GETFL)
fcntl.fcntl(fd, fcntl.F_SETFL, fl | os.O_NONBLOCK)

def read_until_prompt():
    out = ""
    start = time.time()
    while True:
        if time.time() - start > 15:
            print("Timeout!")
            break
        try:
            chunk = p.stdout.read(1024)
            if chunk:
                s = chunk.decode('utf-8', errors='replace')
                out += s
                if "root@sageos:/#" in out:
                    break
        except BlockingIOError:
            time.sleep(0.1)
        
        if p.poll() is not None:
            # try to read any remaining output
            try:
                chunk = p.stdout.read()
                if chunk:
                    out += chunk.decode('utf-8', errors='replace')
            except BlockingIOError:
                pass
            break
            
    return out

print(read_until_prompt())

commands = [
    "ls\n",
    "ls /etc\n",
    "mkdir /mydir\n",
    "ls /\n",
    "touch /mydir/test.txt\n",
    "write /mydir/test.txt Hello from SageOS VFS\n",
    "cat /mydir/test.txt\n",
    "stat /mydir/test.txt\n",
    "ls /fat32\n",
    "exit\n"
]

for cmd in commands:
    if p.poll() is not None:
        print("Process exited early.")
        break
    print(f"\n--- Sending: {cmd.strip()} ---")
    p.stdin.write(cmd.encode('utf-8'))
    p.stdin.flush()
    print(read_until_prompt())
