# Virtual File System Project

A small educational virtual file system built in C for an operating systems project. It includes a kernel-style VFS implementation, a shell-based command interface, and optional Python-based tooling for testing and visualization.

## Features

- Persistent filesystem stored in a single binary file
- File and directory operations such as create, open, read, write, mkdir, ls, rm, and rename
- Shell interface for interactive use
- Optional Python UI and automated logic tests

## Project Structure

- `vfs.h` - data structures and function declarations
- `vfs.c` - core VFS logic
- `shell.c` - command-line interface
- `vfs_ui.py` - optional Python UI
- `test_vfs_logic.py` - logic tests
- `Makefile` - build instructions
- `import_demo.txt` - sample import data
- `PART_1_VFS_H_FOUNDATION.md`, `PART_2_VFS_C_KERNEL.md`, `PART_3_SHELL_C_UI.md` - project notes
- `extra_docs/` - overview and DFD documentation

## Build

```bash
make
```

On Windows, use `mingw32-make` if needed.

## Run

```bash
./vfs
```

On Windows:

```bash
vfs.exe
```

## Files To Push

Push the source and documentation files only:

- `vfs.c`
- `vfs.h`
- `shell.c`
- `vfs_ui.py`
- `test_vfs_logic.py`
- `Makefile`
- `import_demo.txt`
- `README.md`
- `PART_1_VFS_H_FOUNDATION.md`
- `PART_2_VFS_C_KERNEL.md`
- `PART_3_SHELL_C_UI.md`
- `extra_docs/`

Do not push generated files:

- `vfs.o`
- `shell.o`
- `vfs.exe`
- `filesystem.bin`