# VFS PROJECT - COMPLETE UNDERSTANDING GUIDE
## Start from Base Level

---

# FILE 1: vfs.h - THE FOUNDATION (Header Definitions)

## What is vfs.h?
This is the **blueprint** of your entire project. It defines:
- Memory layout of the filesystem
- Data structures (Inode, FileDescriptor, Superblock)
- All function declarations
- All constants and limits

## 1. CONTAINER FILE

```c
#define CONTAINER_FILE  "filesystem.bin"
```
**Meaning**: All filesystem data lives in a **single binary file** called `filesystem.bin`
- This file is persistent storage
- Everything your VFS manages is stored here
- When you restart the program, it reads from this file

---

## 2. SYSTEM LIMITS

```c
#define MAX_INODES      128        // Maximum 128 files/directories total
#define MAX_NAME        124        // Filename can be 124 chars max
#define MAX_FILE_SIZE   4096       // Each file: max 4 KB
#define MAX_FD          32         // Can open 32 files simultaneously
#define MAX_PATH        512        // Path strings: max 512 chars
```

**Why these numbers?**
- MAX_INODES = 128: Fixed number of files possible
- MAX_NAME = 124: Chosen so Inode struct = exactly 256 bytes (perfect fit)
- MAX_FILE_SIZE = 4096: Files limited to 4 KB each
- MAX_FD = 32: Maximum open file descriptors at once
- MAX_PATH = 512: Reasonable path length limit

---

## 3. INODE TYPES

```c
#define INODE_FREE  0              // Empty slot (can be reused)
#define INODE_FILE  1              // Regular file (stores data)
#define INODE_DIR   2              // Directory (stores child references)
```

**Example:**
```
Inode 0: type = INODE_DIR   (root directory "/")
Inode 1: type = INODE_FILE  (a regular file)
Inode 2: type = INODE_FREE  (unused - available for allocation)
```

---

## 4. FILE ACCESS FLAGS

```c
#define VFS_O_RDONLY  0x01         // Read-only access
#define VFS_O_WRONLY  0x02         // Write-only access
#define VFS_O_RDWR    0x03         // Read+Write (0x01 | 0x02)
#define VFS_O_APPEND  0x04         // Append mode (add to end)
#define VFS_O_TRUNC   0x10         // Truncate (clear file on open)
```

**When you open a file:**
```c
fd = vfs_open("file.txt", VFS_O_WRONLY | VFS_O_TRUNC);
```
This means: Open file.txt for writing, and clear it if it exists.

---

## 5. SEEK MODES

```c
#define VFS_SEEK_SET  0            // Absolute: from start of file
#define VFS_SEEK_CUR  1            // Relative: from current position
#define VFS_SEEK_END  2            // Relative: from end of file
```

**Examples:**
```c
vfs_lseek(fd, 0, VFS_SEEK_SET);    // Go to beginning
vfs_lseek(fd, 10, VFS_SEEK_CUR);   // Move forward 10 bytes
vfs_lseek(fd, -5, VFS_SEEK_END);   // Go to 5 bytes before end
```

---

## 6. PERMISSION BITS (rwx)

```c
#define PERM_READ   4              // Binary: 100 (read permission)
#define PERM_WRITE  2              // Binary: 010 (write permission)
#define PERM_EXEC   1              // Binary: 001 (execute permission)
```

**How permissions work:**
```
7 = 111 = read(4) + write(2) + exec(1) = rwx
6 = 110 = read(4) + write(2) = rw-
5 = 101 = read(4) + exec(1) = r-x
4 = 100 = read(4) = r--
2 = 010 = write(2) = -w-
1 = 001 = exec(1) = --x
0 = 000 = no permissions
```

---

## 7. BINARY LAYOUT (How filesystem.bin is Organized)

```
filesystem.bin structure:

┌─────────────────────────────────────────────────────┐
│  Offset: 0                                          │
│  Superblock (64 bytes)                              │
│  - Magic number: 0x56465321 ("VFS!")                │
│  - Version number                                   │
│  - Inode count                                      │
└─────────────────────────────────────────────────────┘
        ↓
┌─────────────────────────────────────────────────────┐
│  Offset: 64 (INODE_TABLE_OFFSET)                    │
│  Inode Table (32,768 bytes total)                   │
│  - 128 inodes × 256 bytes each                      │
│  Inode[0]:  bytes 64-319                            │
│  Inode[1]:  bytes 320-575                           │
│  Inode[2]:  bytes 576-831                           │
│  ...                                                │
│  Inode[127]: bytes 32,576-32,831                    │
└─────────────────────────────────────────────────────┘
        ↓
┌─────────────────────────────────────────────────────┐
│  Offset: 32,832 (DATA_OFFSET)                       │
│  Data Area (524,288 bytes total)                    │
│  - 128 files × 4096 bytes each                      │
│  File[0] data:  bytes 32,832-36,927                 │
│  File[1] data:  bytes 36,928-41,023                 │
│  ...                                                │
│  File[127] data: bytes 555,008-559,103              │
└─────────────────────────────────────────────────────┘

TOTAL: 557,120 bytes (~544 KB)
```

**Key Formula:**
```
File data for inode[i] is stored at:
   Location = DATA_OFFSET + (i × MAX_FILE_SIZE) + cursor
            = 32,832 + (i × 4,096) + cursor
```

---

## 8. CORE DATA STRUCTURES

### Superblock (64 bytes)
```c
typedef struct __attribute__((packed)) {
    uint32_t magic;        // 4 bytes: Must be 0x56465321 ("VFS!")
    uint32_t version;      // 4 bytes: Filesystem version
    uint32_t inode_count;  // 4 bytes: Number of inodes used
    uint8_t  _pad[52];     // 52 bytes: Padding to reach 64 bytes total
} Superblock;
```

**Purpose:** Metadata about the entire filesystem
- Magic number verifies this is a valid VFS filesystem
- Version allows for compatibility checking
- Inode count tracks how many inodes are in use

### Inode (256 bytes)
```c
typedef struct __attribute__((packed)) {
    char     name[124];      // Filename (e.g., "hello.txt")
    uint8_t  type;           // INODE_FREE(0), INODE_FILE(1), INODE_DIR(2)
    uint8_t  permissions;    // rwx bits (0-7)
    int32_t  parent;         // Parent directory inode index (-1 for root)
    uint32_t size;           // File size in bytes (0 for directories)
    uint8_t  _pad[122];      // Padding to 256 bytes
} Inode;
```

**Example Inode Chain:**
```
Inode[0] (ROOT):
  name = "/"
  type = INODE_DIR
  parent = -1
  children: Inode[1], Inode[2]

Inode[1] (FILE):
  name = "file.txt"
  type = INODE_FILE
  parent = 0          // Parent is inode 0 (root)
  size = 1024         // File contains 1024 bytes
  data at: 32,832 + (1 × 4,096) = 36,928

Inode[2] (DIRECTORY):
  name = "documents"
  type = INODE_DIR
  parent = 0          // Parent is inode 0 (root)
  children: Inode[3], Inode[4]
```

### File Descriptor (16 bytes, in-memory only)
```c
typedef struct {
    int      inode_idx;     // Which inode does this FD point to?
    int      flags;         // VFS_O_RDONLY, VFS_O_APPEND, etc.
    uint32_t cursor;        // Current position in file (for read/write)
    int      in_use;        // Boolean: is this FD allocated?
} FileDescriptor;
```

**Example Open File:**
```
fd_table[2]:
  inode_idx = 5          // Points to inode 5
  flags = VFS_O_RDWR     // Opened for read+write
  cursor = 0             // Currently at start of file
  in_use = 1             // This slot is being used

If we vfs_read(2, buf, 10):
  - Read 10 bytes from inode[5] starting at cursor
  - cursor becomes 10
  - Next vfs_read will start from position 10
```

---

## 9. GLOBAL STATE (Lives in vfs.c)

```c
extern FILE          *container;           // File handle to filesystem.bin
extern FileDescriptor fd_table[MAX_FD];    // Array of 32 open files
extern int            cwd_inode;           // Current working directory (starts at 0 = root)
```

**What this means:**
- `container`: The open FILE* to filesystem.bin
- `fd_table`: Array tracking which files are open
- `cwd_inode`: When you do `vfs_cd("/home")`, cwd_inode changes

---

## 10. ALL FUNCTIONS DECLARED

### Lifecycle
```c
int  vfs_init(void);       // Start VFS, open/create filesystem.bin
void vfs_shutdown(void);   // Close VFS, flush all data
```

### Basic File Operations (POSIX-like)
```c
int vfs_creat(const char *path, uint8_t mode);    // Create file
int vfs_open(const char *path, int flags);        // Open file → returns fd
int vfs_read(int fd, char *buf, int nbytes);      // Read from file
int vfs_write(int fd, const char *buf, int nbytes); // Write to file
int vfs_lseek(int fd, int offset, int whence);    // Seek to position
int vfs_close(int fd);                            // Close file
```

### Directory Operations
```c
int vfs_mkdir(const char *path, uint8_t mode);    // Create directory
int vfs_unlink(const char *path);                 // Delete file
int vfs_rmdir(const char *path);                  // Delete directory
int vfs_rename(const char *old_path, const char *new_path); // Rename
int vfs_copy(const char *src, const char *dst);   // Copy file
int vfs_chmod(const char *path, uint8_t mode);    // Change permissions
int vfs_ls(int dir_inode);                        // List directory contents
int vfs_cd(const char *path);                     // Change directory
```

### Helper Functions
```c
int vfs_resolve_path(const char *path, int start_inode);  // Navigate to inode
void vfs_read_inode(int idx, Inode *out);                 // Read inode from disk
void vfs_write_inode(int idx, const Inode *in);           // Write inode to disk
int vfs_check_perm(int inode_idx, int need_write, int need_exec); // Check permissions
```

### Debugging Tools
```c
void vfs_debug(void);                              // Show internal state
void vfs_tree(int dir_inode, int depth, int *last_flags); // Print tree structure
void vfs_hexdump(int length);                      // Dump filesystem.bin contents
```

---

## SUMMARY: What vfs.h Tells Us

1. **Everything is one file**: `filesystem.bin` holds all data
2. **Fixed limits**: 128 files max, 4 KB per file, 32 open files
3. **Inode-based**: Each file is an inode (256 bytes metadata + 4 KB data area)
4. **Flat storage**: Superblock → Inode Table → Data Area
5. **In-memory state**: fd_table tracks open files, cwd_inode tracks location
6. **POSIX-like API**: Functions look like open(), read(), write(), mkdir()

**Next Step:** Read vfs.c to see HOW these functions are implemented!

---

## Key Takeaways for Understanding the Project

| Concept | Value | Meaning |
|---------|-------|---------|
| **Container** | filesystem.bin | Single file holds everything |
| **Max Files** | 128 | Limit of filesystem |
| **Max File Size** | 4 KB | Each file max 4096 bytes |
| **Max Open Files** | 32 | Simultaneously open files |
| **Inode Size** | 256 bytes | Metadata per file |
| **Data Block Size** | 4096 bytes | Per-file data area |
| **Total Storage** | 557 KB | Total filesystem.bin size |

