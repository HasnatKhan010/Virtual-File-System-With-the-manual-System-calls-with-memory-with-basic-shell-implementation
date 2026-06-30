# Virtual File System Project

An educational virtual file system written in C for an operating systems project. The repository contains a kernel-style VFS implementation, a custom shell interface, and a small Python helper UI for testing and visualization.

## Project Overview

This project simulates a file system inside a single persistent binary container. It provides common file and directory operations, path resolution, permission checks, and a command-line shell that behaves like a tiny operating system shell.

The system is designed to demonstrate how a VFS can manage metadata, file data, and directory structure without relying on the host operating system’s file APIs for the core logic.

## Main Goals

- Understand virtual file system design
- Practice low-level file and directory handling in C
- Implement inode-based storage and path resolution
- Build a shell interface for interactive use
- Keep filesystem data persistent in one binary file

## Project Files

- `vfs.h` - core data structures, constants, and function declarations
- `vfs.c` - VFS kernel logic and storage operations
- `shell.c` - command-line interface and user command dispatcher
- `vfs_ui.py` - optional Python UI/helper
- `test_vfs_logic.py` - automated test script for basic behavior checks
- `Makefile` - build instructions
- `import_demo.txt` - example input data
- `README.md` - project documentation

## Repository Layout

```text
d:\sem 5\os\project\files\
├── vfs.h
├── vfs.c
├── shell.c
├── vfs_ui.py
├── test_vfs_logic.py
├── import_demo.txt
├── Makefile
├── README.md
├── PART_1_VFS_H_FOUNDATION.md
├── PART_2_VFS_C_KERNEL.md
├── PART_3_SHELL_C_UI.md
├── extra_docs/
│   ├── DFD_DOCUMENTATION.md
│   ├── DFD_SUMMARY.md
│   └── PROJECT_COMPLETE_OVERVIEW.md
└── filesystem.bin   # generated after running the project
```

## Features

- Persistent filesystem stored in `filesystem.bin`
- Inode-based metadata management
- Support for files and directories
- Path resolution from root and relative paths
- File open, read, write, seek, and close operations
- Directory creation, listing, removal, and navigation
- Rename, copy, and permission operations
- Interactive shell for manual testing
- Optional Python UI and logic test script

## Storage Model

The filesystem uses a fixed layout inside one container file.

- Superblock stores the filesystem signature and metadata
- Inode table stores file and directory records
- Data area stores actual file contents

Typical layout:

- Offset 0: superblock
- Offset 64: inode table
- Offset 32,832: data area

The project uses fixed limits for simplicity:

- 128 inodes / files maximum
- 4 KB per file
- 32 open file descriptors

## Core Modules

### `vfs.h`

This file defines the project’s foundation. It contains:

- constants for sizes and offsets
- inode and file descriptor structures
- function declarations for the VFS API
- metadata definitions for filesystem state

### `vfs.c`

This file implements the core filesystem behavior:

- disk read/write helpers
- inode read/write helpers
- path lookup and child lookup
- file creation and open logic
- reading, writing, seeking, and closing
- directory creation and deletion
- rename, copy, chmod, ls, and cd support

### `shell.c`

This file provides the interactive shell layer:

- parses user commands
- dispatches commands to VFS functions
- prints user-friendly output
- supports repeated command entry until exit

### `vfs_ui.py`

This is an optional Python helper for UI/testing use. It is not required for the core C build, but it can be useful for demos or quick checks.

## Supported Shell Commands

Navigation:

- `pwd` - print current directory
- `cd <path>` - change directory
- `ls [path]` - list files and folders

File operations:

- `touch <file>` - create an empty file
- `cat <file>` - print file contents
- `write [-a] <file> <text>` - write or append text
- `rm <file>` - delete a file
- `cp <src> <dst>` - copy a file
- `mv <src> <dst>` - move or rename a file

Directory operations:

- `mkdir <name>` - create a directory
- `rmdir <path>` - remove an empty directory
- `tree [path]` - show directory tree

Text and utility commands:

- `nano <file>` - simple editor
- `wc <file>` - count words, lines, and bytes
- `grep <pattern> <file>` - search text
- `echo <text>` - print text

Permissions and status:

- `chmod <0-7> <file>` - change permissions
- `stat <file>` - show inode details
- `debug` - show internal state

Scripting and exit:

- `sh <script.sh>` - run a batch script
- `help` - show available commands
- `exit` - quit the shell

## Build Requirements

You need:

- GCC or another C11-compatible compiler
- Make or `mingw32-make` on Windows
- Standard C library

## Build Instructions

From the project folder:

```bash
make
```

On Windows, if `make` is not available:

```bash
mingw32-make
```

The build generates the executable and object files in the same folder.

## Run Instructions

Linux, macOS, or similar:

```bash
./vfs
```

Windows:

```bash
vfs.exe
```

After starting, use shell commands like `help`, `tree`, `mkdir`, `touch`, and `cat`.

## Example Workflow

```bash
mkdir docs
cd docs
touch notes.txt
write notes.txt Hello from VFS
cat notes.txt
ls
```

Example expected flow:

- create a directory
- move inside it
- create a file
- write text into the file
- display the file
- confirm the directory contents

## Testing

The project includes a Python test script:

```bash
python test_vfs_logic.py
```

This script checks basic shell behavior such as:

- startup banner and prompt
- `tree` command
- `mkdir` command
- updated tree output after changes

## Generated Files

These files are created while building or running the project and should not be pushed to GitHub:

- `vfs.o`
- `shell.o`
- `vfs.exe`
- `filesystem.bin`


## Notes

- The project stores the filesystem state in a single binary container.
- The shell is meant for learning and demonstration, not as a production filesystem.
- The Python helper and test script are optional, but useful for validation.

