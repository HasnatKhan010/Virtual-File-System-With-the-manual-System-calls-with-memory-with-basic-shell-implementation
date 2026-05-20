# Data Flow Diagram (DFD) Documentation
## VFS (Virtual Filesystem) Project

---

## Table of Contents
1. [DFD Level 0 - Context Diagram](#dfd-level-0---context-diagram)
2. [DFD Level 1 - System Decomposition](#dfd-level-1---system-decomposition)
3. [DFD Level 2 - Detailed Process Flow](#dfd-level-2---detailed-process-flow)
4. [Data Dictionary](#data-dictionary)
5. [Process Descriptions](#process-descriptions)

---

## DFD Level 0 - Context Diagram

### Overview
The Level 0 DFD represents the VFS system as a single process (P0) showing external entities and data stores that interact with the entire system.

### Components

#### External Entities
- **User**: Human operator or external program that sends commands and receives output
  - Input: File operation commands (create, open, read, write, mkdir, etc.)
  - Output: File content, directory listings, status messages, error codes

#### Processes
- **P0: VFS System**: Central Virtual Filesystem Kernel that manages all file operations

#### Data Stores
- **D1: filesystem.bin**: Persistent binary storage containing:
  - Superblock (64 bytes) - Metadata and magic number
  - Inode Table (32,768 bytes) - 128 inodes × 256 bytes each
  - Data Area (524,288 bytes) - File content blocks (128 × 4096 bytes)

#### Data Flows
| From | To | Data | Description |
|------|-----|------|------------|
| User | P0 | Commands | `vfs_init()`, `vfs_create()`, `vfs_open()`, `vfs_read()`, `vfs_write()`, `vfs_close()`, `vfs_mkdir()`, `vfs_unlink()` |
| P0 | User | Results | File content, directory info, status codes, error messages |
| P0 | D1 | I/O Requests | Read/write inode table and data blocks |
| D1 | P0 | Data | Inode metadata, file content, superblock info |

---

## DFD Level 1 - System Decomposition

### Overview
Level 1 decomposes the VFS kernel (P0) into six major functional subsystems, each handling specific responsibilities.

### Processes

#### P1.1: CLI Interface & Command Parser
**Function**: Parse and validate incoming user commands
- Tokenizes command input
- Validates command syntax
- Routes commands to appropriate subsystems
- Handles error conditions in command format

**Input Data**: Raw command strings from user
**Output Data**: Parsed command structures

**Key Functions**: 
- Command tokenizer
- Syntax validator
- Command router

---

#### P1.2: Path Resolution Engine
**Function**: Navigate filesystem hierarchy and resolve file/directory paths
- Splits path into components
- Performs inode lookup for each component
- Validates path existence
- Resolves relative vs. absolute paths
- Handles symbolic parent directory references

**Input Data**: Path strings, current working directory (cwd_inode)
**Output Data**: Target inode index, inode metadata

**Key Functions**:
- `find_child()` - Locate child inode by name under parent
- Path tokenizer
- Inode navigator
- CWD manager

---

#### P1.3: File Operations
**Function**: Implement syscalls for file manipulation
- Open/create files with access modes
- Read file contents with offset support
- Write data to files with append/truncate modes
- Seek to file positions
- Close file descriptors

**Input Data**: File descriptor, buffer data, offsets, flags
**Output Data**: Data from read, byte count written/read

**Key Functions**:
- `vfs_open()` - Open file with VFS_O_RDONLY, VFS_O_WRONLY, VFS_O_RDWR, VFS_O_APPEND, VFS_O_TRUNC flags
- `vfs_read()` - Read up to nbytes from fd
- `vfs_write()` - Write nbytes to fd
- `vfs_lseek()` - Seek with SEEK_SET, SEEK_CUR, SEEK_END
- `vfs_close()` - Release file descriptor

---

#### P1.4: Directory Operations
**Function**: Manage filesystem hierarchy and directory structures
- Create directories with permissions
- Delete empty directories
- List directory contents
- Remove files
- Rename files/directories
- Change directory

**Input Data**: Path, permissions, operation flags
**Output Data**: Inode list, directory structure info

**Key Functions**:
- `vfs_mkdir()` - Create directory with mode
- `vfs_rmdir()` - Remove empty directory
- `vfs_unlink()` - Remove file
- `vfs_rename()` - Rename file or directory
- Directory listing

---

#### P1.5: Disk I/O Manager
**Function**: Low-level read/write operations to persistent storage
- Manage inode table access (read/write inode structures)
- Manage data area access (read/write file content blocks)
- Handle sector/block calculations
- Manage file pointer positioning
- Flush buffers to ensure persistence

**Input Data**: Inode index, data buffer, offset, size
**Output Data**: Data from disk, write confirmation

**Key Functions**:
- `vfs_read_inode(idx, out)` - Read inode from disk
- `vfs_write_inode(idx, in)` - Write inode to disk
- `disk_read_data()` - Read data block content
- `disk_write_data()` - Write data block content
- Buffer flushing

**Storage Calculations**:
```
Inode Location = INODE_TABLE_OFFSET + (idx × INODE_SIZE)
                = 64 + (idx × 256) bytes

Data Location = DATA_OFFSET + (inode_idx × MAX_FILE_SIZE) + offset
              = 32832 + (inode_idx × 4096) + offset bytes
```

---

#### P1.6: Memory & Resource Management
**Function**: Allocate and manage in-memory resources
- Allocate free inodes for new files/directories
- Manage file descriptor table
- Track open file references
- Handle buffer allocation
- Manage cursor positions

**Input Data**: Allocation request, resource type
**Output Data**: Allocated resource ID, available resources list

**Key Functions**:
- `alloc_inode()` - Find and allocate free inode
- FD table initialization and management
- Buffer pool management
- Reference counting

---

### Data Stores (Level 1)

#### D1: filesystem.bin - Complete Persistent Storage
Location: `filesystem.bin` (binary file)

**Structure**:
```
Offset    | Size      | Content
----------|-----------|---------------------------
0         | 64 B      | Superblock
64        | 32,768 B  | Inode Table (128 inodes)
32,832    | 524,288 B | Data Area (128 × 4096 B)
```

---

### Data Flows (Level 1)

| From | To | Data | Description |
|------|------|------|-------------|
| User | P1.1 | Command strings | Raw command input |
| P1.1 | P1.2 | Parsed paths | Tokenized path for resolution |
| P1.2 | P1.3, P1.4 | Inode indices | Target inode from path resolution |
| P1.3 | P1.5 | Read/write requests | File I/O at specific offsets |
| P1.4 | P1.5 | Inode requests | Directory modification operations |
| P1.6 | P1.3, P1.4 | Allocation info | Inode IDs, FD slots, buffers |
| P1.5 | D1 | I/O operations | Read/write to filesystem.bin |
| D1 | P1.5 | Data blocks | Inode and file content from storage |
| P1.3, P1.4 | User | Results | File data, operation status |

---

## DFD Level 2 - Detailed Process Flow

### Focus: vfs_write() Operation

This Level 2 DFD decomposes **P1.3 (File Operations)** into seven detailed sub-processes showing the complete write operation flow.

### Processes

#### P1.3.1: File Descriptor Lookup & Validation
**Function**: Verify file descriptor exists and is valid
- Look up fd in fd_table array
- Check if fd is in use (in_use flag)
- Validate fd is within range [0, MAX_FD-1]
- Retrieve inode_idx and access flags

**Input**: File descriptor (fd)
**Output**: Valid inode_idx, access flags, or error code

---

#### P1.3.2: Inode Fetch from Disk
**Function**: Retrieve inode metadata from persistent storage
- Calculate inode location: `INODE_TABLE_OFFSET + (idx × 256)`
- Read inode structure from disk
- Verify inode type (INODE_FILE vs INODE_DIR)
- Load permissions and current file size

**Input**: Inode index
**Output**: Complete Inode structure (name, type, permissions, size, parent)

---

#### P1.3.3: Permission & Access Validation
**Function**: Verify write operation is allowed
- Check inode type is INODE_FILE (not directory)
- Extract permission bits from inode
- Verify PERM_WRITE bit is set
- Validate access flags from fd match write operation
- Check for conflicting flags (O_RDONLY vs O_WRONLY)

**Input**: Inode permissions, fd flags, operation (write)
**Output**: Permission valid (yes/no), or error code

---

#### P1.3.4: Buffer Management & Data Preparation
**Function**: Prepare user data for disk write
- Allocate write buffer (nbytes size)
- Copy user data into kernel buffer
- Validate buffer size doesn't exceed MAX_FILE_SIZE
- Handle O_APPEND flag (set cursor to file end)
- Handle O_TRUNC flag (clear file on open)

**Input**: User buffer, nbytes, flags, current file size
**Output**: Prepared kernel buffer, write position

---

#### P1.3.5: Write to Data Area
**Function**: Persist data to storage
- Calculate data location: `DATA_OFFSET + (inode_idx × 4096) + cursor`
- Seek to correct file position
- Write nbytes from kernel buffer
- Flush to ensure persistence (fflush)
- Confirm write success

**Input**: Data buffer, inode_idx, cursor position, nbytes
**Output**: Bytes written successfully

---

#### P1.3.6: Inode Metadata Update
**Function**: Update file size and metadata
- Add nbytes to current file size
- Update modification timestamp (if tracking time)
- Mark inode as modified
- Write updated inode back to disk
- Flush changes

**Input**: Bytes written, old inode, new size
**Output**: Updated inode on disk

---

#### P1.3.7: File Descriptor State Update
**Function**: Maintain file descriptor consistency
- Advance cursor by nbytes written
- Update fd_table[fd].cursor
- Maintain flags and inode reference
- Mark FD as having pending changes
- Return bytes written to caller

**Input**: Bytes written, fd, cursor position
**Output**: Updated fd_table, return value (bytes written or -1)

---

### Data Stores (Level 2)

#### D1.1: Inode Table (filesystem.bin offset 64)
- Stores all inode metadata
- Each inode is 256 bytes
- Contains: name, type, permissions, parent, size

#### D1.2: Data Area (filesystem.bin offset 32832)
- Stores actual file contents
- 128 × 4096 byte blocks
- One block per inode

#### D1.3: File Descriptor Table (in-memory)
- Global array `fd_table[MAX_FD]`
- Stores: inode_idx, flags, cursor, in_use status
- Currently open files

---

### Data Flows (Level 2 - Write Operation)

```
User → Command (fd=3, data="hello", nbytes=5)
  ↓
P1.3.1 Lookup FD
  ├→ D1.3 fd_table
  ├← inode_idx=7, flags=0x02 (WRONLY)
  ↓
P1.3.2 Fetch Inode
  ├→ D1.1 Inode Table
  ├← Inode data (type=FILE, perms=6, size=0)
  ↓
P1.3.3 Validate Permission
  ├ Check: INODE_FILE? ✓
  ├ Check: PERM_WRITE? ✓
  ├ Check: Flags match? ✓
  ↓
P1.3.4 Buffer Management
  ├ Allocate buffer (5 bytes)
  ├ Copy user data: "hello"
  ↓
P1.3.5 Write to Disk
  ├→ D1.2 Data Area [inode 7]
  ├ Write "hello" at cursor 0
  ├← Write confirmed
  ↓
P1.3.6 Update Inode
  ├ New size: 0 + 5 = 5 bytes
  ├→ D1.1 Inode Table
  ├ Write updated inode
  ↓
P1.3.7 Update FD
  ├→ D1.3 fd_table[3]
  ├ Set cursor: 0 + 5 = 5
  ↓
Return to User: 5 bytes written ✓
```

---

## Data Dictionary

### Inode Structure
```c
typedef struct {
    char     name[124];      // Filename (0-123 null-terminated)
    uint8_t  type;           // INODE_FREE(0), INODE_FILE(1), INODE_DIR(2)
    uint8_t  permissions;    // rwx bitmask: READ(4), WRITE(2), EXEC(1)
    int32_t  parent;         // Parent inode index (-1 for root)
    uint32_t size;           // File size in bytes (0 for directories)
    uint8_t  _pad[122];      // Padding to 256 bytes
} Inode;                     // Total: 256 bytes
```

### File Descriptor Structure
```c
typedef struct {
    int      inode_idx;      // Index into inode table (0-127)
    int      flags;          // VFS_O_RDONLY, VFS_O_WRONLY, VFS_O_RDWR, etc.
    uint32_t cursor;         // Current position in file (0 to MAX_FILE_SIZE)
    int      in_use;         // Boolean: is this FD allocated?
} FileDescriptor;
```

### Superblock Structure
```c
typedef struct {
    uint32_t magic;          // VFS_MAGIC = 0x56465321 ("VFS!")
    uint32_t version;        // Filesystem version
    uint32_t inode_count;    // Number of inodes (128)
    uint8_t  _pad[52];       // Padding to 64 bytes
} Superblock;               // Total: 64 bytes
```

### Operation Flags
```
VFS_O_RDONLY  = 0x01   // Read-only
VFS_O_WRONLY  = 0x02   // Write-only
VFS_O_RDWR    = 0x03   // Read/Write (0x01 | 0x02)
VFS_O_APPEND  = 0x04   // Append to end of file
VFS_O_TRUNC   = 0x10   // Truncate file on open

Permission Bits:
PERM_READ  = 4         // Binary: 100
PERM_WRITE = 2         // Binary: 010
PERM_EXEC  = 1         // Binary: 001
```

### Key Constants
```c
MAX_INODES       = 128       // Maximum files/directories
MAX_NAME         = 124       // Max filename length
MAX_FILE_SIZE    = 4096      // Max file size (4 KB)
MAX_FD           = 32        // Max open file descriptors
MAX_PATH         = 512       // Max path length

SUPERBLOCK_SIZE  = 64        // Bytes
INODE_SIZE       = 256       // Bytes
INODE_TABLE_SIZE = 32768     // 128 × 256
DATA_OFFSET      = 32832     // Start of data area
TOTAL_SIZE       = 557120    // Superblock + Inode Table + Data Area
```

---

## Process Descriptions

### System Lifecycle

#### Initialization (vfs_init)
1. Open or create filesystem.bin
2. If new: Initialize superblock with magic number
3. If new: Create root directory inode at index 0
4. Verify magic number matches VFS_MAGIC
5. Initialize fd_table (all entries unused)
6. Set cwd_inode to 0 (root)

#### Shutdown (vfs_shutdown)
1. Flush all buffers to disk
2. Close filesystem.bin
3. Reset global state

### File Creation (vfs_creat)
1. Parse path to extract parent directory and filename
2. Resolve parent inode via P1.2
3. Check if file already exists
4. Allocate new inode via P1.6
5. Initialize inode with INODE_FILE type
6. Set permissions and parent reference
7. Write inode to D1.1
8. Return file descriptor or -1 on error

### File Open (vfs_open)
1. Parse path to resolve target inode
2. Fetch inode from disk
3. Validate permissions against access flags
4. Check file type (must be INODE_FILE)
5. Allocate file descriptor from fd_table
6. Initialize FD: inode_idx, flags, cursor=0
7. Return fd or -1 on error

### File Read (vfs_read)
1. Validate fd (P1.3.1)
2. Fetch inode (P1.3.2)
3. Check PERM_READ and O_RDONLY/O_RDWR flags (P1.3.3)
4. Calculate read range: cursor to cursor+nbytes
5. Clamp nbytes to file size (don't read past EOF)
6. Read from D1.2 at: DATA_OFFSET + inode_idx × 4096 + cursor
7. Copy to user buffer
8. Advance cursor
9. Return bytes read

### File Write (vfs_write)
1. Validate fd (P1.3.1)
2. Fetch inode (P1.3.2)
3. Check PERM_WRITE and O_WRONLY/O_RDWR flags (P1.3.3)
4. Handle O_APPEND: set cursor to file size
5. Handle O_TRUNC: clear file (set size=0)
6. Validate cursor + nbytes ≤ MAX_FILE_SIZE
7. Write to D1.2 at: DATA_OFFSET + inode_idx × 4096 + cursor
8. Update inode.size = max(cursor + nbytes, inode.size)
9. Update inode on disk
10. Advance cursor
11. Return bytes written

### File Seek (vfs_lseek)
1. Validate fd
2. Fetch inode
3. Calculate new cursor based on whence:
   - SEEK_SET: cursor = offset
   - SEEK_CUR: cursor = cursor + offset
   - SEEK_END: cursor = inode.size + offset
4. Clamp cursor to [0, MAX_FILE_SIZE]
5. Update fd_table[fd].cursor
6. Return new cursor position

### Directory Creation (vfs_mkdir)
1. Parse path to parent and dirname
2. Check parent exists and is INODE_DIR
3. Allocate new inode
4. Initialize as INODE_DIR type
5. Set parent reference
6. Write inode to disk
7. Return 0 or -1

### File/Directory Deletion (vfs_unlink)
1. Resolve target inode path
2. Check target exists
3. If INODE_FILE: free inode, clear from parent
4. If INODE_DIR: verify empty, then free inode
5. Mark inode as INODE_FREE
6. Write freed inode to disk

---

## Summary

The VFS system decomposes into:
- **6 Major Processes (Level 1)**: CLI, Path Resolution, File Ops, Dir Ops, Disk I/O, Memory Mgmt
- **7 Sub-processes (Level 2)**: Detailed write operation showing all steps from FD validation through disk persistence
- **3 Data Stores**: Inode Table, Data Area, File Descriptor Table
- **Multiple Data Flows**: Commands → processing → results

This three-level DFD hierarchy provides complete visibility into the VFS kernel architecture, making it suitable for documentation, design review, and code maintenance.
