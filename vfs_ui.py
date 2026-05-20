import tkinter as tk
from tkinter import font as tkfont
import subprocess
import threading
import queue
import os
import sys
import datetime
import ctypes

# ── Force UTF-8 everywhere (equivalent to chcp 65001) ──────────────────────
if os.name == "nt":
    ctypes.windll.kernel32.SetConsoleOutputCP(65001)
    ctypes.windll.kernel32.SetConsoleCP(65001)
if hasattr(sys.stdout, "reconfigure"):
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")
if hasattr(sys.stderr, "reconfigure"):
    sys.stderr.reconfigure(encoding="utf-8", errors="replace")

# ── Colors ─────────────────────────────────────────────────────────────────
BG_MAIN  = "#0d1117"
BG_PANEL = "#161b22"
BG_HDR   = "#21262d"
BG_TERM  = "#0d1117"
BG_ENTRY = "#1c2128"
FG_CYAN  = "#39c5cf"
FG_PINK  = "#ff6eb4"
FG_PURP  = "#bf91f3"
FG_GREEN = "#3fb950"
FG_AMBER = "#f0a500"
FG_WHITE = "#e6edf3"
FG_DIM   = "#8b949e"
FG_RED   = "#ff4d4f"
FG_TEAL  = "#00d2d3"

# ── System Call Map ─────────────────────────────────────────────────────────
# Maps VFS commands → list of (syscall_name, description, color)
SYSCALL_MAP = {
    "mkdir":    [("sys_mkdir()",  "Create directory inode",      FG_GREEN),
                 ("sys_write()",  "Write inode to disk",          FG_AMBER)],
    "rmdir":    [("sys_unlink()", "Remove directory inode",       FG_RED),
                 ("sys_write()",  "Update parent directory",       FG_AMBER)],
    "create":   [("sys_open()",   "Allocate file descriptor",     FG_CYAN),
                 ("sys_write()",  "Create inode entry",           FG_AMBER),
                 ("sys_close()",  "Release file descriptor",       FG_DIM)],
    "touch":    [("sys_open()",   "Allocate file descriptor",     FG_CYAN),
                 ("sys_write()",  "Create inode entry",           FG_AMBER),
                 ("sys_close()",  "Release file descriptor",       FG_DIM)],
    "write":    [("sys_open()",   "Open file O_WRONLY",           FG_CYAN),
                 ("sys_write()",  "Write data to blocks",          FG_AMBER),
                 ("sys_lseek()",  "Update file offset pointer",    FG_PURP),
                 ("sys_close()",  "Flush & release descriptor",    FG_DIM)],
    "append":   [("sys_open()",   "Open file O_APPEND",           FG_CYAN),
                 ("sys_lseek()",  "Seek to end of file",           FG_PURP),
                 ("sys_write()",  "Write at end of file",          FG_AMBER),
                 ("sys_close()",  "Flush & release descriptor",    FG_DIM)],
    "cat":      [("sys_open()",   "Open file O_RDONLY",           FG_CYAN),
                 ("sys_read()",   "Read data blocks into buffer",  FG_GREEN),
                 ("sys_lseek()",  "Advance read offset",           FG_PURP),
                 ("sys_close()",  "Release file descriptor",       FG_DIM)],
    "rm":       [("sys_unlink()", "Remove inode from directory",  FG_RED),
                 ("sys_write()",  "Mark blocks free in FAT",       FG_AMBER)],
    "ls":       [("sys_open()",   "Open directory inode",         FG_CYAN),
                 ("sys_read()",   "Read directory entries",        FG_GREEN),
                 ("sys_close()",  "Release directory descriptor",  FG_DIM)],
    "cd":       [("sys_chdir()",  "Change current working dir",   FG_TEAL)],
    "stat":     [("sys_stat()",   "Query inode metadata",         FG_TEAL)],
    "cp":       [("sys_open()",   "Open source O_RDONLY",         FG_CYAN),
                 ("sys_open()",   "Open dest O_WRONLY|O_CREAT",   FG_CYAN),
                 ("sys_read()",   "Read source blocks",            FG_GREEN),
                 ("sys_write()",  "Write to destination blocks",   FG_AMBER),
                 ("sys_close()",  "Close both descriptors",        FG_DIM)],
    "mv":       [("sys_open()",   "Open source file",             FG_CYAN),
                 ("sys_unlink()", "Remove source inode entry",     FG_RED),
                 ("sys_write()",  "Link to new directory entry",   FG_AMBER),
                 ("sys_close()",  "Release descriptor",            FG_DIM)],
    "hexdump":  [("sys_open()",   "Open filesystem.bin",          FG_CYAN),
                 ("sys_lseek()",  "Seek to offset 0",              FG_PURP),
                 ("sys_read()",   "Read raw 512-byte blocks",      FG_GREEN),
                 ("sys_close()",  "Close binary container",        FG_DIM)],
    "debug":    [("sys_stat()",   "Query superblock metadata",    FG_TEAL),
                 ("sys_read()",   "Read inode table stats",        FG_GREEN)],
    "tree":     [("sys_open()",   "Open root directory",          FG_CYAN),
                 ("sys_read()",   "Traverse directory entries",    FG_GREEN),
                 ("sys_close()",  "Close each directory inode",    FG_DIM)],
    "help":     [],
    "exit":     [("sys_exit()",   "Flush state, terminate VFS",   FG_RED)],
}

class VFSApp(tk.Tk):
    def __init__(self):
        super().__init__()
        self.title("VFS OS Dashboard — System Calls Visualizer")
        self.geometry("1280x800")
        self.configure(bg=BG_MAIN)
        self.resizable(True, True)

        self.q = queue.Queue()
        self.proc = None
        self.syscall_counter = 0

        self._build_ui()
        self._start_vfs()
        self._poll_queue()

    # ── Build UI ──────────────────────────────────────────────────────────
    def _build_ui(self):
        mono = tkfont.Font(family="Consolas", size=9)
        bold = tkfont.Font(family="Consolas", size=9, weight="bold")
        big  = tkfont.Font(family="Consolas", size=10, weight="bold")
        self._mono = mono
        self._bold = bold

        self.rowconfigure(0, weight=1)
        self.rowconfigure(1, weight=1)
        self.columnconfigure(0, weight=1)
        self.columnconfigure(1, weight=1)

        # ── TOP LEFT: Directory Tree ──
        tf = tk.Frame(self, bg=BG_PANEL)
        tf.grid(row=0, column=0, sticky="nsew", padx=(10,5), pady=(10,5))
        tk.Label(tf, text="  📁  VFS DIRECTORY TREE", bg=BG_HDR, fg=FG_CYAN,
                 font=bold, anchor="w", pady=6).pack(fill=tk.X)
        self.tree_box = tk.Text(tf, bg=BG_PANEL, fg=FG_CYAN, font=mono,
                                state=tk.DISABLED, bd=0, padx=8, pady=8)
        self.tree_box.pack(fill=tk.BOTH, expand=True)

        # ── TOP RIGHT: System Calls Log ──
        sf = tk.Frame(self, bg=BG_PANEL)
        sf.grid(row=0, column=1, sticky="nsew", padx=(5,10), pady=(10,5))

        hdr_sc = tk.Frame(sf, bg=BG_HDR)
        hdr_sc.pack(fill=tk.X)
        tk.Label(hdr_sc, text="  ⚡  SYSTEM CALLS LOG", bg=BG_HDR, fg=FG_AMBER,
                 font=bold, anchor="w", pady=6).pack(side=tk.LEFT, fill=tk.X, expand=True)
        tk.Label(hdr_sc, text="LIVE ", bg=BG_HDR, fg=FG_RED,
                 font=bold).pack(side=tk.RIGHT, padx=8)

        self.syscall_box = tk.Text(sf, bg=BG_PANEL, fg=FG_WHITE, font=mono,
                                   state=tk.DISABLED, bd=0, padx=8, pady=8)
        sc_sb = tk.Scrollbar(sf, command=self.syscall_box.yview,
                             bg=BG_HDR, troughcolor=BG_PANEL)
        self.syscall_box.config(yscrollcommand=sc_sb.set)
        sc_sb.pack(side=tk.RIGHT, fill=tk.Y)
        self.syscall_box.pack(fill=tk.BOTH, expand=True)

        # Configure text tags for syscall log colors
        self.syscall_box.tag_configure("ts",      foreground=FG_DIM)
        self.syscall_box.tag_configure("cmd",     foreground=FG_WHITE,   font=bold)
        self.syscall_box.tag_configure("sc_cyan", foreground=FG_CYAN)
        self.syscall_box.tag_configure("sc_grn",  foreground=FG_GREEN)
        self.syscall_box.tag_configure("sc_amb",  foreground=FG_AMBER)
        self.syscall_box.tag_configure("sc_purp", foreground=FG_PURP)
        self.syscall_box.tag_configure("sc_red",  foreground=FG_RED)
        self.syscall_box.tag_configure("sc_teal", foreground=FG_TEAL)
        self.syscall_box.tag_configure("sc_dim",  foreground=FG_DIM)
        self.syscall_box.tag_configure("desc",    foreground=FG_DIM)
        self.syscall_box.tag_configure("divider", foreground="#2d333b")

        # ── BOTTOM LEFT: Terminal ──
        pf = tk.Frame(self, bg=BG_PANEL)
        pf.grid(row=1, column=0, sticky="nsew", padx=(10,5), pady=(5,10))
        pf.rowconfigure(1, weight=1)
        pf.columnconfigure(0, weight=1)

        tk.Label(pf, text="  💻  CORE TERMINAL", bg=BG_HDR,
                 fg=FG_PURP, font=bold, anchor="w",
                 pady=6).grid(row=0, column=0, columnspan=2, sticky="ew")

        self.term_box = tk.Text(pf, bg=BG_TERM, fg=FG_GREEN, font=mono,
                                state=tk.DISABLED, bd=0, padx=10, pady=8)
        self.term_box.grid(row=1, column=0, sticky="nsew")

        t_sb = tk.Scrollbar(pf, command=self.term_box.yview,
                            bg=BG_HDR, troughcolor=BG_PANEL)
        t_sb.grid(row=1, column=1, sticky="ns")
        self.term_box.config(yscrollcommand=t_sb.set)

        irow = tk.Frame(pf, bg=BG_HDR)
        irow.grid(row=2, column=0, columnspan=2, sticky="ew")
        irow.columnconfigure(1, weight=1)

        tk.Label(irow, text=" vfs:/$ ", bg=BG_HDR, fg=FG_GREEN,
                 font=big, padx=4).grid(row=0, column=0, pady=8)

        self.entry = tk.Entry(
            irow, bg=BG_ENTRY, fg=FG_WHITE, font=mono,
            insertbackground=FG_CYAN, relief=tk.FLAT, bd=0,
            highlightthickness=2, highlightbackground="#30363d",
            highlightcolor=FG_CYAN
        )
        self.entry.grid(row=0, column=1, sticky="ew", ipady=7, padx=(0,6), pady=8)
        self.entry.bind("<Return>", self._send)
        self.entry.focus_set()

        tk.Button(irow, text=" ↺ ", bg="#21262d", fg=FG_CYAN,
                  font=big, relief=tk.FLAT, bd=0,
                  command=self._refresh_panels
                  ).grid(row=0, column=2, padx=(0,8), pady=8)

        # ── BOTTOM RIGHT: Hexdump ──
        hf = tk.Frame(self, bg=BG_PANEL)
        hf.grid(row=1, column=1, sticky="nsew", padx=(5,10), pady=(5,10))
        tk.Label(hf, text="  🔢  RAW DISK STORAGE (HEXDUMP)", bg=BG_HDR, fg=FG_PINK,
                 font=bold, anchor="w", pady=6).pack(fill=tk.X)
        self.hex_box = tk.Text(hf, bg=BG_PANEL, fg=FG_PINK, font=mono,
                               state=tk.DISABLED, bd=0, padx=8, pady=8)
        h_sb = tk.Scrollbar(hf, command=self.hex_box.yview,
                            bg=BG_HDR, troughcolor=BG_PANEL)
        self.hex_box.config(yscrollcommand=h_sb.set)
        h_sb.pack(side=tk.RIGHT, fill=tk.Y)
        self.hex_box.pack(fill=tk.BOTH, expand=True)

    # ── Start vfs.exe ────────────────────────────────────────────────────
    def _start_vfs(self):
        if not os.path.exists("vfs.exe"):
            self._term_write("ERROR: vfs.exe not found.\n")
            return

        flags = subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0
        
        # Pass UTF-8 environment to the C subprocess
        env = os.environ.copy()
        env["PYTHONIOENCODING"] = "utf-8"
        
        self.proc = subprocess.Popen(
            ["vfs.exe"],
            stdin=subprocess.PIPE, stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT, bufsize=0,
            creationflags=flags, env=env
        )

        def _reader():
            buf = b""
            while True:
                ch = self.proc.stdout.read(1)
                if not ch:
                    # Flush any remaining bytes
                    if buf:
                        self.q.put(buf.decode("utf-8", errors="replace"))
                    self.q.put(None)
                    break
                buf += ch
                # Determine how many bytes this UTF-8 char needs
                first = buf[0]
                if first < 0x80:
                    expected = 1          # ASCII
                elif first < 0xC0:
                    buf = b""             # stray continuation byte, discard
                    continue
                elif first < 0xE0:
                    expected = 2
                elif first < 0xF0:
                    expected = 3
                else:
                    expected = 4

                if len(buf) >= expected:
                    try:
                        text = buf[:expected].decode("utf-8")
                        self.q.put(text)
                    except UnicodeDecodeError:
                        self.q.put(buf[:expected].decode("utf-8", errors="replace"))
                    buf = buf[expected:]  # keep any overflow

        threading.Thread(target=_reader, daemon=True).start()
        self.after(1000, self._refresh_panels)
        # Show welcome syscall log entry
        self.after(200, lambda: self._log_startup())

    def _log_startup(self):
        self._append_syscall_header("VFS KERNEL INIT")
        calls = [
            ("sys_open()",  "Open filesystem.bin container", FG_CYAN),
            ("sys_read()",  "Load superblock into memory",   FG_GREEN),
            ("sys_read()",  "Load inode table from disk",    FG_GREEN),
            ("sys_mmap()",  "Map FAT into virtual memory",   FG_PURP),
        ]
        for sc, desc, color in calls:
            self._append_syscall(sc, desc, color)

    # ── Send command ─────────────────────────────────────────────────────
    def _send(self, event=None):
        cmd = self.entry.get().strip()
        self.entry.delete(0, tk.END)
        if not cmd:
            return

        # Show syscalls BEFORE sending
        base_cmd = cmd.split()[0].lower()
        self._log_command_syscalls(cmd, base_cmd)

        if self.proc and self.proc.poll() is None:
            try:
                self.proc.stdin.write((cmd + "\n").encode())
                self.proc.stdin.flush()
            except OSError:
                pass

        self.after(600, self._refresh_panels)

    def _log_command_syscalls(self, full_cmd, base_cmd):
        calls = SYSCALL_MAP.get(base_cmd, [])
        self._append_syscall_header(full_cmd.upper())
        if calls:
            for sc, desc, color in calls:
                self._append_syscall(sc, desc, color)
        else:
            self._sc_write(f"  (no tracked system calls)\n", "desc")

    def _append_syscall_header(self, label):
        self.syscall_counter += 1
        ts = datetime.datetime.now().strftime("%H:%M:%S")
        self.syscall_box.config(state=tk.NORMAL)
        self.syscall_box.insert(tk.END, f"\n[{ts}] ", "ts")
        self.syscall_box.insert(tk.END, f"▶ {label}\n", "cmd")
        self.syscall_box.config(state=tk.DISABLED)
        self.syscall_box.see(tk.END)

    def _append_syscall(self, syscall, desc, color):
        # Map color to tag
        tag_map = {
            FG_CYAN:  "sc_cyan",
            FG_GREEN: "sc_grn",
            FG_AMBER: "sc_amb",
            FG_PURP:  "sc_purp",
            FG_RED:   "sc_red",
            FG_TEAL:  "sc_teal",
            FG_DIM:   "sc_dim",
        }
        tag = tag_map.get(color, "sc_cyan")
        self.syscall_box.config(state=tk.NORMAL)
        self.syscall_box.insert(tk.END, f"  ├─ ", "desc")
        self.syscall_box.insert(tk.END, f"{syscall:<18}", tag)
        self.syscall_box.insert(tk.END, f"  {desc}\n", "desc")
        self.syscall_box.config(state=tk.DISABLED)
        self.syscall_box.see(tk.END)

    def _sc_write(self, text, tag="desc"):
        self.syscall_box.config(state=tk.NORMAL)
        self.syscall_box.insert(tk.END, text, tag)
        self.syscall_box.config(state=tk.DISABLED)
        self.syscall_box.see(tk.END)

    # ── Poll terminal output ─────────────────────────────────────────────
    def _poll_queue(self):
        try:
            while True:
                item = self.q.get_nowait()
                if item is None:
                    self._term_write("\n[VFS Process Terminated]\n")
                    return
                self._term_write(item)
        except queue.Empty:
            pass
        self.after(20, self._poll_queue)

    def _term_write(self, text):
        self.term_box.config(state=tk.NORMAL)
        self.term_box.insert(tk.END, text)
        self.term_box.see(tk.END)
        self.term_box.config(state=tk.DISABLED)

    # ── Refresh tree & hex ────────────────────────────────────────────────
    def _refresh_panels(self):
        threading.Thread(target=self._do_refresh, daemon=True).start()

    def _do_refresh(self):
        flags = subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0

        # Tree
        try:
            p = subprocess.Popen(
                ["vfs.exe"], stdin=subprocess.PIPE,
                stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                creationflags=flags
            )
            out, _ = p.communicate(b"tree /\nexit\n", timeout=5)
            text = out.decode("utf-8", errors="replace").replace("\r\n", "\n")
            tree = self._extract_tree(text)
            if tree:
                self.after(0, lambda t=tree: self._set_panel(self.tree_box, t))
        except Exception as e:
            self.after(0, lambda err=str(e): self._set_panel(
                self.tree_box, f"Tree error:\n{err}"))

        # Hexdump — read filesystem.bin directly
        try:
            hex_text = self._hexdump("filesystem.bin", 512)
            if hex_text:
                self.after(0, lambda h=hex_text: self._set_panel(self.hex_box, h))
        except Exception:
            pass

    def _extract_tree(self, text):
        lines = text.split("\n")
        out = []
        capture = False
        for line in lines:
            if "vfs:/" in line and "$ " in line:
                if not capture:
                    capture = True
                    continue
                else:
                    break
            if capture:
                if line.strip() == "exit":
                    continue
                out.append(line)
        return "\n".join(out).rstrip()

    def _hexdump(self, path, max_bytes=512):
        if not os.path.exists(path):
            return ""
        with open(path, "rb") as f:
            data = f.read(max_bytes)
        lines = []
        for i in range(0, len(data), 16):
            chunk = data[i:i+16]
            hex_part = " ".join(f"{b:02x}" for b in chunk)
            asc_part = "".join(chr(b) if 32 <= b < 127 else "." for b in chunk)
            lines.append(f"{i:08x}  {hex_part:<48}  |{asc_part}|")
        return "\n".join(lines)

    def _set_panel(self, widget, text):
        widget.config(state=tk.NORMAL)
        widget.delete("1.0", tk.END)
        widget.insert(tk.END, text)
        widget.config(state=tk.DISABLED)


if __name__ == "__main__":
    os.chdir(os.path.dirname(os.path.abspath(__file__)))
    app = VFSApp()
    app.mainloop()
