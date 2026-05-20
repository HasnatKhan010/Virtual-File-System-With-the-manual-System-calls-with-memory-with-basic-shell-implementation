import subprocess
import threading
import time
import os

def test_vfs():
    exe_path = "vfs.exe"
    if not os.path.exists(exe_path):
        print("FAIL: vfs.exe not found!")
        return

    print("Spawning vfs.exe...")
    process = subprocess.Popen(
        [exe_path],
        stdin=subprocess.PIPE,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        bufsize=0
    )

    output_lines = []
    
    def read_output():
        buffer = b""
        while True:
            char = process.stdout.read(1)
            if not char:
                break
            buffer += char
            if buffer.endswith(b"$ "):
                text_str = buffer.decode('utf-8', errors='replace').replace('\r\n', '\n')
                output_lines.append(text_str)
                buffer = b""

    t = threading.Thread(target=read_output)
    t.daemon = True
    t.start()

    time.sleep(1)
    if len(output_lines) == 0:
        print("FAIL: No welcome banner / initial prompt received!")
        return
    else:
        print("PASS: Received welcome banner!")
        print("BANNER OUTPUT:\n", output_lines[-1].encode('ascii', errors='replace').decode('ascii'))

    # Test Command 1: tree
    print("Testing command 'tree'...")
    process.stdin.write(b"tree\n")
    process.stdin.flush()
    time.sleep(1)
    
    if len(output_lines) < 2:
        print("FAIL: 'tree' command timed out or failed to return prompt!")
        return
    else:
        print("PASS: 'tree' command worked!")
        print("TREE OUTPUT:\n", output_lines[-1].encode('ascii', errors='replace').decode('ascii'))

    # Test Command 2: mkdir test_dir
    print("Testing 'mkdir test_dir'...")
    process.stdin.write(b"mkdir test_dir\n")
    process.stdin.flush()
    time.sleep(1)

    if len(output_lines) < 3:
        print("FAIL: 'mkdir' command timed out!")
        return
    else:
        print("PASS: 'mkdir' command worked!")
        print("MKDIR OUTPUT:\n", output_lines[-1].encode('ascii', errors='replace').decode('ascii'))

    # Test Command 3: tree again to verify update
    print("Testing 'tree' again...")
    process.stdin.write(b"tree\n")
    process.stdin.flush()
    time.sleep(1)
    print("TREE OUTPUT:\n", output_lines[-1].encode('ascii', errors='replace').decode('ascii'))

    # Close VFS
    print("Exiting...")
    process.stdin.write(b"exit\n")
    process.stdin.flush()
    t.join(2)
    print("VFS exit complete!")

if __name__ == "__main__":
    test_vfs()
