# Virtual File System Project

A small educational virtual file system built in C for an operating systems project. It includes the core VFS logic and a shell-based command interface.

## Main Files

- `vfs.h` - data structures and declarations
- `vfs.c` - core VFS implementation
- `shell.c` - command-line interface
- `Makefile` - build instructions

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

## Push These Files

For the GitHub repository, push only the main project files and this README:

- `vfs.c`
- `vfs.h`
- `shell.c`
- `Makefile`
- `README.md`

Do not push generated files such as:

- `vfs.o`
- `shell.o`
- `vfs.exe`
- `filesystem.bin`