# VFS Project - DFD Summary Report

## Executive Summary
This document provides a complete Data Flow Diagram (DFD) analysis of the Virtual Filesystem (VFS) kernel at three levels of detail:
- **Level 0**: Context diagram showing system boundaries
- **Level 1**: System decomposition into functional processes
- **Level 2**: Detailed sub-processes for file operations

---

## Level 0: Context Diagram

### System Boundary
```
┌─────────────┐
│    User     │──Commands──→ ┌─────────────┐ ─→ ┌──────────────┐
│             │              │ VFS System  │     │ filesystem.  │
│             │←──Results─── │    (P0)     │ ←── │    bin       │
└─────────────┘              └─────────────┘     └──────────────┘
 (External)                  (Process)            (Data Store D1)
```

**Key Interactions:**
- User sends filesystem commands (open, read, write, mkdir, etc.)
- VFS processes commands and updates persistent storage
- VFS returns file data and status messages to user

---

## Level 1: Major System Components

### Six Core Processes

| Process | Responsibility | Key Functions |
|---------|-----------------|---------------|
| **P1.1: CLI Interface** | Parse & route commands | Command tokenization, syntax validation |
| **P1.2: Path Resolution** | Navigate filesystem hierarchy | find_child(), inode lookup, path traversal |
| **P1.3: File Operations** | Read/write file data | vfs_open(), vfs_read(), vfs_write(), vfs_close() |
| **P1.4: Directory Ops** | Manage directories | vfs_mkdir(), vfs_unlink(), vfs_rmdir() |
| **P1.5: Disk I/O** | Persistent storage access | vfs_read_inode(), vfs_write_inode(), disk I/O |
| **P1.6: Memory Mgmt** | Resource allocation | alloc_inode(), fd_table management, buffers |

### Storage Structure
- **filesystem.bin** (D1) contains:
  - Superblock (64 bytes): Magic number, version, metadata
  - Inode Table (32,768 bytes): 128 inodes × 256 bytes each
  - Data Area (524,288 bytes): 128 files × 4096 bytes each

---

## Level 2: Detailed File Write Operation

### 7-Step Write Process

```
Step 1: FD Lookup      → Validate file descriptor exists
        ↓
Step 2: Inode Fetch    → Read inode metadata from disk
        ↓
Step 3: Permission     → Verify WRITE flag and permissions
        ↓
Step 4: Buffer Mgmt    → Prepare user data in kernel buffer
        ↓
Step 5: Disk Write     → Persist data to storage
        ↓
Step 6: Inode Update   → Increase file size, mark modified
        ↓
Step 7: FD State       → Advance file cursor, return result
```

### Data Stores Involved
- **D1.1 Inode Table**: Metadata updates
- **D1.2 Data Area**: File content storage
- **D1.3 FD Table**: File descriptor state tracking

---

## Storage Layout

### Total Filesystem Size: 557,120 bytes (~544 KB)

```
Offset      Size            Content
──────────────────────────────────────────────────────
0           64 B            Superblock
64          32,768 B        Inode Table (offset: 64)
32,832      524,288 B       Data Area (offset: 32,832)
557,120     Total
```

### Inode Calculation
```
Inode N Location = 64 + (N × 256) bytes
Inode 0:  offset 64
Inode 1:  offset 320
Inode 2:  offset 576
...
Inode 127: offset 32,768
```

### File Data Calculation
```
File N Location = 32,832 + (N × 4,096) + cursor offset bytes
File 0:  offset 32,832 to 36,927
File 1:  offset 36,928 to 41,023
...
File 127: offset 555,008 to 559,103
```

---

## Data Dictionary

### Constants & Limits
```
MAX_INODES       = 128         // Maximum files/directories
MAX_FILE_SIZE    = 4,096       // Max file content (4 KB)
MAX_NAME         = 124         // Max filename length
MAX_FD           = 32          // Max open files simultaneously
MAX_PATH         = 512         // Max path string length
```

### Inode Type Codes
```
INODE_FREE  = 0    // Unused inode slot
INODE_FILE  = 1    // Regular file
INODE_DIR   = 2    // Directory
```

### Access Flags (vfs_open)
```
VFS_O_RDONLY  = 0x01   // Read-only
VFS_O_WRONLY  = 0x02   // Write-only
VFS_O_RDWR    = 0x03   // Read and write
VFS_O_APPEND  = 0x04   // Append mode
VFS_O_TRUNC   = 0x10   // Truncate file on open
```

### Permission Bits (rwx bitmask)
```
PERM_READ  = 4   // Can read file (4 = 100 in binary)
PERM_WRITE = 2   // Can write file (2 = 010 in binary)
PERM_EXEC  = 1   // Can execute file (1 = 001 in binary)
```

### Seek Modes
```
VFS_SEEK_SET = 0   // Absolute position from start
VFS_SEEK_CUR = 1   // Relative to current position
VFS_SEEK_END = 2   // Relative to end of file
```

---

## System Interfaces

### Core Syscalls
```c
int vfs_init(void)                              // Initialize VFS
void vfs_shutdown(void)                         // Shutdown VFS

int vfs_creat(const char *path, uint8_t mode)  // Create file
int vfs_open(const char *path, int flags)      // Open file
int vfs_read(int fd, char *buf, int nbytes)    // Read data
int vfs_write(int fd, const char *buf, int n)  // Write data
int vfs_lseek(int fd, int offset, int whence)  // Seek position
int vfs_close(int fd)                          // Close file

int vfs_mkdir(const char *path, uint8_t mode)  // Create directory
int vfs_unlink(const char *path)               // Delete file/directory
```

### Return Values
```
0 or positive  = Success (typically bytes read/written or 0 for ops)
-1             = Error (detailed error message on stderr)
```

---

## Process Flow Examples

### Example 1: File Write Operation
```
User Command: vfs_write(fd=3, "hello", 5)

Execution Flow:
1. P1.3.1 validates fd=3 in fd_table
   → inode_idx=7, flags=0x02 (WRONLY)
2. P1.3.2 fetches inode 7 from disk
   → type=INODE_FILE, permissions=0x6 (rw-), size=0
3. P1.3.3 checks PERM_WRITE flag
   → ✓ Allowed (0x6 & PERM_WRITE = true)
4. P1.3.4 allocates 5-byte buffer, copies "hello"
5. P1.3.5 writes to D1.2 at offset 32832 + (7×4096) + 0
6. P1.3.6 updates inode size: 0 → 5, writes inode
7. P1.3.7 updates fd_table[3].cursor: 0 → 5
   
Result: Return 5 (bytes written)
```

### Example 2: Path Resolution
```
User Command: vfs_open("/home/user/file.txt", VFS_O_RDONLY)

Execution Flow:
1. P1.2 splits path: root("/") → "home" → "user" → "file.txt"
2. Start at cwd_inode (root = 0)
3. find_child(0, "home")
   → Scan inode table for inode with parent=0 and name="home"
   → Returns inode 5
4. find_child(5, "user")
   → Scan inode table for inode with parent=5 and name="user"
   → Returns inode 12
5. find_child(12, "file.txt")
   → Scan inode table for inode with parent=12 and name="file.txt"
   → Returns inode 23
6. P1.3 validates fd allocation, opens inode 23
   
Result: Return file descriptor (e.g., fd=2)
```

---

## Data Flow Summary

### Command Processing Pipeline
```
User Input → CLI Parser (P1.1) → Path Resolver (P1.2)
            ↓
    ┌───────┴───────┬─────────────┐
    ↓               ↓             ↓
File Ops (P1.3)  Dir Ops (P1.4)  Memory Mgmt (P1.6)
    ↓               ↓             ↓
    └───────┬───────┴─────────────┘
            ↓
    Disk I/O Manager (P1.5) ← Memory Mgmt (P1.6)
            ↓
    ┌───────┴────────────┬──────────┐
    ↓                    ↓          ↓
Inode Table (D1.1)  Data Area (D1.2)  FD Table (D1.3)
    ↓                    ↓          ↓
    └───────┬────────────┴──────────┘
            ↓
    Disk I/O Manager (P1.5)
            ↓
    User Output (Results/Error)
```

---

## Key Design Features

1. **Three-Level Decomposition**: Context → Components → Details
2. **Single Binary Container**: All data persists in filesystem.bin
3. **Inode-Based Structure**: Each file/directory is an inode (256 bytes)
4. **Fixed File Size**: All files limited to 4 KB maximum
5. **File Descriptor Table**: Tracks open files and cursor positions
6. **Hierarchical Directory Structure**: Parent pointers enable tree navigation
7. **Permission Control**: Read, write, execute bits per file
8. **Disk I/O Isolation**: All storage access goes through P1.5

---

## Complexity Analysis

### Operations
| Operation | Complexity | Steps |
|-----------|-----------|-------|
| vfs_init | O(1) | Create/verify container |
| vfs_open | O(n) | Path resolution scans inodes |
| vfs_read | O(1) | Direct disk I/O |
| vfs_write | O(1) | Direct disk I/O |
| vfs_mkdir | O(n) | Path resolution + inode creation |
| vfs_unlink | O(n) | Path resolution + inode deletion |
| find_child | O(n) | Linear scan of inode table |

Where n = MAX_INODES (128)

---

## Limitations & Constraints

1. **Fixed Inode Count**: Maximum 128 files/directories
2. **Fixed File Size**: Maximum 4 KB per file
3. **No Subdirectory Depth Limit**: But limited by inode count
4. **Single-Process Access**: No concurrency control
5. **Simple Linear Search**: O(n) path resolution
6. **No Journaling**: Direct writes (crash-unsafe)
7. **No Sparse Files**: Full 4 KB allocated per inode

---

## Future Improvements

1. **Dynamic Inode Allocation**: Remove MAX_INODES limit
2. **Variable File Sizes**: Support larger files
3. **Journaling/WAL**: Ensure crash consistency
4. **B-Tree Directory Index**: O(log n) lookups
5. **Concurrency Control**: Multi-process support
6. **File Permissions**: Per-user access control
7. **Symbolic Links**: Soft and hard links
8. **Timestamps**: Access/modification time tracking

---

## Document Version
- **Date**: May 2026
- **Project**: VFS Kernel
- **Status**: Complete Design Documentation

For detailed process descriptions, data structures, and code references, see DFD_DOCUMENTATION.md.
