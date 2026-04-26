import pexpect
import sys

def main():
    print("Starting QEMU...")
    child = pexpect.spawn('bash lenovo_300e.sh qemu', encoding='utf-8', timeout=15)
    
    # Wait for shell prompt
    child.expect('root@sageos:/#')
    print("Got prompt")

    commands = [
        "ls",
        "ls /etc",
        "mkdir /mydir",
        "ls /",
        "touch /mydir/test.txt",
        "write /mydir/test.txt Hello from SageOS VFS",
        "cat /mydir/test.txt",
        "stat /mydir/test.txt",
        "ls /fat32",
        "exit"
    ]

    for cmd in commands:
        print(f"Sending: {cmd}")
        child.sendline(cmd)
        try:
            child.expect('root@sageos:/#')
            print(f"Output for '{cmd}':\n{child.before}")
        except pexpect.TIMEOUT:
            print(f"Timeout waiting for prompt after '{cmd}'")
            print(f"Buffer: {child.buffer}")
            break

    child.close()

if __name__ == '__main__':
    main()
