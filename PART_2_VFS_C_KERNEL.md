# VFS PROJECT - PART 2: vfs.c (THE KERNEL IMPLEMENTATION)
## How Everything Actually Works

---

# FILE 2: vfs.c - The VFS Kernel Implementation

## Overview
vfs.c contains ALL the low-level and high-level implementation logic for the VFS system.
It implements the function declarations from vfs.h and handles actual disk I/O.

---

## 1. GLOBAL STATE (Memory)

```c
FILE          *container = NULL;           // File handle to filesystem.bin
FileDescriptor fd_table[MAX_FD];           // Array of 32 open file descriptors
int            cwd_inode = 0;              // Current working directory (starts at root=0)
```

**What this means:**
- `container`: Once opened, this FILE* stays open during the entire session
- `fd_table`: Tracks which files are currently open (32 slots)
- `cwd_inode`: When you type `cd /home`, cwd_inode changes to 5 (example)

---

## 2. LOW-LEVEL DISK HELPERS

These functions handle all direct disk I/O:

### vfs_read_inode(idx, out)
```c
void vfs_read_inode(int idx, Inode *out)
{
    fseek(container, INODE_TABLE_OFFSET + idx * INODE_SIZE, SEEK_SET);
    fread(out, sizeof(Inode), 1, container);
}
```

**What it does:**
1. Calculate position: `64 + (idx × 256)` bytes from start
2. Seek to that position in filesystem.bin
3. Read 256 bytes into Inode structure

**Example:**
```
Reading Inode[5]:
  Position = 64 + (5 × 256) = 64 + 1280 = 1344
  Read 256 bytes at offset 1344 → entire inode[5]
```

### vfs_write_inode(idx, in)
```c
void vfs_write_inode(int idx, const Inode *in)
{
    fseek(container, INODE_TABLE_OFFSET + idx * INODE_SIZE, SEEK_SET);
    fwrite(in, sizeof(Inode), 1, container);
    fflush(container);    // Force to disk immediately
}
```

**What it does:**
1. Calculate same position as read
2. Write 256 bytes from Inode structure
3. **CRITICAL**: fflush() ensures data actually hits disk

### disk_read_data(inode_idx, buf, offset, size)
```c
static void disk_read_data(int inode_idx, char *buf, uint32_t offset, uint32_t size)
{
    long pos = DATA_OFFSET + (long)inode_idx * MAX_FILE_SIZE + offset;
    fseek(container, pos, SEEK_SET);
    fread(buf, 1, size, container);
}
```

**Position Calculation:**
```
Data location = 32,832 + (inode_idx × 4,096) + offset
```

**Example:**
```
Reading 1024 bytes from inode[7] starting at offset 512:
  Position = 32,832 + (7 × 4,096) + 512
           = 32,832 + 28,672 + 512
           = 62,016
  Read 1024 bytes from offset 62,016
```

### disk_write_data(inode_idx, buf, offset, size)
```c
static void disk_write_data(int inode_idx, const char *buf, uint32_t offset, uint32_t size)
{
    long pos = DATA_OFFSET + (long)inode_idx * MAX_FILE_SIZE + offset;
    fseek(container, pos, SEEK_SET);
    fwrite(buf, 1, size, container);
    fflush(container);
}
```

Same logic as read, but writes data and flushes to disk.

---

## 3. INITIALIZATION & SHUTDOWN

### vfs_init() - Start the VFS

```c
int vfs_init(void)
{
    /* Try to open existing container */
    container = fopen(CONTAINER_FILE, "r+b");

    if (!container) {
        /* FIRST RUN: Create new filesystem */
        container = fopen(CONTAINER_FILE, "w+b");
        
        /* Zero-fill entire container */
        long total = 557120;  // Total size
        fseek(container, total - 1, SEEK_SET);
        fwrite(&zero, 1, 1, container);  // Creates sparse file of correct size
        
        /* Write superblock */
        Superblock sb;
        sb.magic   = 0x56465321;  // "VFS!"
        sb.version = 1;
        fseek(container, 0, SEEK_SET);
        fwrite(&sb, sizeof(sb), 1, container);
        
        /* Create root inode (index 0) */
        Inode root;
        root.name = "/";
        root.type = INODE_DIR;
        root.permissions = 0x7;   // rwx
        root.parent = -1;         // No parent (it's the root!)
        root.size = 0;            // Directories don't store data
        vfs_write_inode(0, &root);
        
        printf("[VFS] New filesystem created.\n");
    }
    
    /* Verify magic number */
    Superblock sb;
    fread(&sb, sizeof(sb), 1, container);
    if (sb.magic != 0x56465321) {
        fprintf(stderr, "Error: Not a valid VFS filesystem!\n");
        return -1;
    }
    
    /* Initialize runtime state */
    memset(fd_table, 0, sizeof(fd_table));
    cwd_inode = 0;
    return 0;
}
```

**What happens:**
1. **First run**: Create filesystem.bin, write empty superblock, create root directory
2. **Subsequent runs**: Open existing filesystem.bin and verify magic number
3. **Initialize**: Clear fd_table, set current directory to root (0)

### vfs_shutdown() - Close the VFS

```c
void vfs_shutdown(void)
{
    if (container) {
        fflush(container);    // Ensure all pending writes hit disk
        fclose(container);    // Close the file
        container = NULL;
    }
}
```

**What it does:**
- Flushes all buffered data to disk
- Closes filesystem.bin
- Resets global state

---

## 4. PATH RESOLUTION (Navigation)

### alloc_inode() - Find Empty Slot

```c
static int alloc_inode(void)
{
    for (int i = 1; i < MAX_INODES; i++) {   // Start from 1 (0 is root)
        Inode n;
        vfs_read_inode(i, &n);
        if (n.type == INODE_FREE) return i;   // Found empty slot
    }
    return -1;  // Table full!
}
```

**Usage:**
```
When creating new file or directory, call alloc_inode()
to find next available inode slot (0-127)
```

### find_child(parent_inode, name) - Locate Child

```c
static int find_child(int parent_inode, const char *name)
{
    for (int i = 0; i < MAX_INODES; i++) {
        Inode n;
        vfs_read_inode(i, &n);
        if (n.type != INODE_FREE
            && n.parent == parent_inode      // Has correct parent
            && strcmp(n.name, name) == 0)    // Has correct name
            return i;
    }
    return -1;  // Not found
}
```

**How directory structure works:**
```
Inode[0] (ROOT "/"):
  - has parent = -1
  - children found by scanning: find_child(0, "home") → Inode[3]

Inode[3] ("home"):
  - has parent = 0
  - children found by scanning: find_child(3, "user") → Inode[5]

Inode[5] ("user"):
  - has parent = 3
  - children found by scanning: find_child(5, "file.txt") → Inode[7]
```

### vfs_resolve_path(path, start_inode) - Navigate Path

```c
int vfs_resolve_path(const char *path, int start_inode)
{
    // Example: path = "/home/user/file.txt"
    
    // Split by "/"
    char *tok = strtok(path, "/");  // "home"
    int cur = (path[0] == '/') ? 0 : start_inode;  // Start from root if absolute path
    
    while (tok) {
        if (strcmp(tok, ".") == 0) {
            // Stay in current directory
        } else if (strcmp(tok, "..") == 0) {
            // Go to parent
            Inode n;
            vfs_read_inode(cur, &n);
            if (n.parent >= 0) cur = n.parent;
        } else {
            // Find child with this name
            int child = find_child(cur, tok);
            if (child < 0) return -1;  // Not found
            cur = child;
        }
        tok = strtok(NULL, "/");
    }
    
    return cur;  // Final inode index
}
```

**Example Walk:**
```
Input: path = "/home/user/file.txt"

Step 1: Split into tokens: "home", "user", "file.txt"
Step 2: Start at inode[0] (root, because path starts with "/")
Step 3: Find "home" in children of inode[0] → inode[3]
Step 4: Find "user" in children of inode[3] → inode[5]
Step 5: Find "file.txt" in children of inode[5] → inode[7]
Step 6: Return 7

So "/home/user/file.txt" maps to inode[7]
```

### split_path(path, start, name_out) - Separate Parent & Filename

```c
static int split_path(const char *path, int start, char *name_out)
{
    // Input:  path = "/home/user/file.txt"
    // Output: name_out = "file.txt", returns parent inode index
    
    char *slash = strrchr(path, '/');  // Find last "/"
    
    if (!slash) {
        // Pure filename, no directory: "file.txt"
        parent = start;
        name = "file.txt";
    } else {
        // Has directory: "/home/user/file.txt"
        name = "file.txt";
        
        if (slash == path) {
            // Starts with "/" like "/file.txt"
            parent = 0;  // Direct child of root
        } else {
            // Regular path
            *slash = '\0';
            parent = vfs_resolve_path(path, start);  // Resolve "/home/user" → inode[5]
        }
    }
    
    return parent;
}
```

---

## 5. PERMISSION CHECKING

### vfs_check_perm(inode_idx, need_write, need_exec)

```c
int vfs_check_perm(int inode_idx, int need_write, int need_exec)
{
    Inode n;
    vfs_read_inode(inode_idx, &n);
    
    if (need_write && !(n.permissions & PERM_WRITE)) return -1;
    if (need_exec  && !(n.permissions & PERM_EXEC))  return -1;
    
    return 0;  // Permission OK
}
```

**Usage:**
```c
if (vfs_check_perm(inode_idx, 1, 0) < 0) {
    // Permission denied (need write but don't have it)
    return -1;
}
```

---

## 6. CORE FILE OPERATIONS

### vfs_creat(path, mode) - Create or Truncate File

```c
int vfs_creat(const char *path, uint8_t mode)
{
    // 1. Split path into parent directory and filename
    char name[MAX_NAME] = {0};
    int parent = split_path(path, cwd_inode, name);
    
    // 2. Validate parent exists and is directory with write permission
    Inode par;
    vfs_read_inode(parent, &par);
    if (par.type != INODE_DIR) return -1;
    if (!(par.permissions & PERM_WRITE)) return -1;
    
    // 3. Check if file already exists
    int idx = find_child(parent, name);
    
    if (idx >= 0) {
        // File EXISTS: truncate it (clear contents)
        Inode n;
        vfs_read_inode(idx, &n);
        n.size = 0;
        vfs_write_inode(idx, &n);
    } else {
        // File DOESN'T EXIST: create new inode
        idx = alloc_inode();
        Inode n;
        memset(&n, 0, sizeof(n));
        strncpy(n.name, name, MAX_NAME - 1);
        n.type = INODE_FILE;
        n.permissions = mode & 0x7;
        n.parent = parent;
        n.size = 0;
        vfs_write_inode(idx, &n);
    }
    
    // 4. Allocate file descriptor and return
    for (int fd = 0; fd < MAX_FD; fd++) {
        if (!fd_table[fd].in_use) {
            fd_table[fd].inode_idx = idx;
            fd_table[fd].flags = VFS_O_WRONLY;
            fd_table[fd].cursor = 0;
            fd_table[fd].in_use = 1;
            return fd;  // Return FD number
        }
    }
    return -1;  // FD table full
}
```

**Example:**
```c
int fd = vfs_creat("/home/user/test.txt", 6);  // rw-
// If test.txt exists: truncate it
// If doesn't exist: create it
// Return file descriptor 2 (example)
```

### vfs_open(path, flags) - Open Existing File

```c
int vfs_open(const char *path, int flags)
{
    // 1. Resolve path to inode
    int idx = vfs_resolve_path(path, cwd_inode);
    if (idx < 0) return -1;  // File not found
    
    // 2. Read inode and check it's not free
    Inode n;
    vfs_read_inode(idx, &n);
    if (n.type == INODE_FREE) return -1;
    
    // 3. Check read/write permissions
    int access = flags & 0x03;
    if ((access == VFS_O_RDONLY || access == VFS_O_RDWR)
        && !(n.permissions & PERM_READ))
        return -1;  // Can't read
    
    if ((access == VFS_O_WRONLY || access == VFS_O_RDWR)
        && !(n.permissions & PERM_WRITE))
        return -1;  // Can't write
    
    // 4. Handle truncate flag
    if (flags & VFS_O_TRUNC) {
        n.size = 0;
        vfs_write_inode(idx, &n);
    }
    
    // 5. Allocate FD
    for (int fd = 0; fd < MAX_FD; fd++) {
        if (!fd_table[fd].in_use) {
            fd_table[fd].inode_idx = idx;
            fd_table[fd].flags = flags;
            fd_table[fd].cursor = (flags & VFS_O_APPEND) ? n.size : 0;
            fd_table[fd].in_use = 1;
            return fd;
        }
    }
    return -1;
}
```

**Example:**
```c
int fd = vfs_open("/home/user/test.txt", VFS_O_RDWR);
// Opens file read+write mode
// cursor = 0 (start of file)
// Return FD 3 (example)
```

### vfs_read(fd, buf, nbytes) - Read Data

```c
int vfs_read(int fd, char *buf, int nbytes)
{
    // 1. Validate FD
    if (fd < 0 || fd >= MAX_FD || !fd_table[fd].in_use) return -1;
    FileDescriptor *fde = &fd_table[fd];
    
    // 2. Check permission (file opened for reading)
    if ((fde->flags & 0x03) == VFS_O_WRONLY) return -1;
    
    // 3. Read inode to get file size
    Inode n;
    vfs_read_inode(fde->inode_idx, &n);
    
    // 4. Calculate how much we can read
    uint32_t avail = (n.size > fde->cursor) ? n.size - fde->cursor : 0;
    int to_read = (nbytes < (int)avail) ? nbytes : (int)avail;
    if (to_read <= 0) return 0;  // Nothing to read
    
    // 5. Read from disk
    disk_read_data(fde->inode_idx, buf, fde->cursor, (uint32_t)to_read);
    
    // 6. Update cursor
    fde->cursor += (uint32_t)to_read;
    
    return to_read;  // Return bytes actually read
}
```

**Example:**
```c
char buffer[1024];
int bytes = vfs_read(fd, buffer, 512);
// Read up to 512 bytes into buffer
// If file has 200 bytes left, returns 200 and advances cursor
```

### vfs_write(fd, buf, nbytes) - Write Data

```c
int vfs_write(int fd, const char *buf, int nbytes)
{
    // 1. Validate FD
    if (fd < 0 || fd >= MAX_FD || !fd_table[fd].in_use) return -1;
    FileDescriptor *fde = &fd_table[fd];
    
    // 2. Check permission (file opened for writing)
    if ((fde->flags & 0x03) == VFS_O_RDONLY) return -1;
    
    // 3. Handle APPEND mode
    if (fde->flags & VFS_O_APPEND) {
        Inode n;
        vfs_read_inode(fde->inode_idx, &n);
        fde->cursor = n.size;  // Always write at end
    }
    
    // 4. Check space available
    uint32_t space = (uint32_t)MAX_FILE_SIZE - fde->cursor;
    int to_write = (nbytes < (int)space) ? nbytes : (int)space;
    if (to_write <= 0) return -1;  // File full!
    
    // 5. Write to disk
    disk_write_data(fde->inode_idx, buf, fde->cursor, (uint32_t)to_write);
    
    // 6. Advance cursor
    fde->cursor += (uint32_t)to_write;
    
    // 7. Update file size if we extended it
    Inode n;
    vfs_read_inode(fde->inode_idx, &n);
    if (fde->cursor > n.size) {
        n.size = fde->cursor;
        vfs_write_inode(fde->inode_idx, &n);
    }
    
    return to_write;
}
```

**Example:**
```c
const char *data = "Hello World";
int bytes = vfs_write(fd, data, 11);
// Write 11 bytes to file
// Advances cursor by 11
// Updates file size if needed
// Returns 11
```

### vfs_lseek(fd, offset, whence) - Change Position

```c
int vfs_lseek(int fd, int offset, int whence)
{
    FileDescriptor *fde = &fd_table[fd];
    Inode n;
    vfs_read_inode(fde->inode_idx, &n);
    
    int new_pos;
    
    switch (whence) {
        case VFS_SEEK_SET:    // Absolute from start
            new_pos = offset;
            break;
        case VFS_SEEK_CUR:    // Relative from current
            new_pos = (int)fde->cursor + offset;
            break;
        case VFS_SEEK_END:    // Relative from end
            new_pos = (int)n.size + offset;
            break;
        default:
            return -1;
    }
    
    // Clamp to valid range
    if (new_pos < 0) new_pos = 0;
    if (new_pos > MAX_FILE_SIZE) new_pos = MAX_FILE_SIZE;
    
    fde->cursor = (uint32_t)new_pos;
    return new_pos;
}
```

**Example:**
```c
vfs_lseek(fd, 0, VFS_SEEK_SET);        // Go to start
vfs_lseek(fd, -10, VFS_SEEK_END);      // Go 10 bytes before end
vfs_lseek(fd, 100, VFS_SEEK_CUR);      // Move forward 100 bytes
```

### vfs_close(fd) - Close File

```c
int vfs_close(int fd)
{
    if (fd < 0 || fd >= MAX_FD || !fd_table[fd].in_use) return -1;
    
    fflush(container);  // Ensure data is on disk
    memset(&fd_table[fd], 0, sizeof(FileDescriptor));  // Clear FD entry
    return 0;
}
```

---

## 7. DIRECTORY OPERATIONS

### vfs_mkdir(path, mode) - Create Directory

```c
int vfs_mkdir(const char *path, uint8_t mode)
{
    // 1. Split path
    char name[MAX_NAME] = {0};
    int parent = split_path(path, cwd_inode, name);
    
    // 2. Check parent exists and is directory
    if (parent < 0 || !name[0]) return -1;
    
    // 3. Check directory doesn't already exist
    if (find_child(parent, name) >= 0) return -1;
    
    // 4. Allocate new inode
    int idx = alloc_inode();
    Inode n;
    memset(&n, 0, sizeof(n));
    strncpy(n.name, name, MAX_NAME - 1);
    n.type = INODE_DIR;           // Mark as directory
    n.permissions = mode & 0x7;
    n.parent = parent;
    n.size = 0;                   // Directories don't store data
    vfs_write_inode(idx, &n);
    return 0;
}
```

### vfs_unlink(path) - Delete File

```c
int vfs_unlink(const char *path)
{
    // 1. Resolve path to inode
    int idx = vfs_resolve_path(path, cwd_inode);
    if (idx <= 0) return -1;  // Can't delete root
    
    // 2. Read inode
    Inode n;
    vfs_read_inode(idx, &n);
    
    // 3. Must be a file, not directory
    if (n.type != INODE_FILE) return -1;
    
    // 4. Mark as free
    n.type = INODE_FREE;
    vfs_write_inode(idx, &n);
    return 0;
}
```

### vfs_rmdir(path) - Delete Empty Directory

```c
int vfs_rmdir(const char *path)
{
    // 1. Resolve to inode
    int idx = vfs_resolve_path(path, cwd_inode);
    
    // 2. Read inode
    Inode n;
    vfs_read_inode(idx, &n);
    if (n.type != INODE_DIR) return -1;
    
    // 3. Must be empty (no children)
    for (int i = 0; i < MAX_INODES; i++) {
        Inode child;
        vfs_read_inode(i, &child);
        if (child.type != INODE_FREE && child.parent == idx) {
            return -1;  // Directory not empty!
        }
    }
    
    // 4. Mark as free
    n.type = INODE_FREE;
    vfs_write_inode(idx, &n);
    return 0;
}
```

### vfs_cd(path) - Change Directory

```c
int vfs_cd(const char *path)
{
    // 1. Resolve path
    int idx = vfs_resolve_path(path, cwd_inode);
    if (idx < 0) return -1;
    
    // 2. Must be a directory
    Inode n;
    vfs_read_inode(idx, &n);
    if (n.type != INODE_DIR) return -1;
    
    // 3. Must have execute permission (can enter)
    if (vfs_check_perm(idx, 0, 1) < 0) return -1;
    
    // 4. Update global current directory
    cwd_inode = idx;
    return 0;
}
```

### vfs_ls(dir_inode) - List Directory

```c
int vfs_ls(int dir_inode)
{
    // 1. Verify it's a directory
    Inode dir;
    vfs_read_inode(dir_inode, &dir);
    if (dir.type != INODE_DIR) return -1;
    
    // 2. Scan all inodes for children
    printf("Name | Type | Perms | Size\n");
    printf("-----|------|-------|------\n");
    
    for (int i = 0; i < MAX_INODES; i++) {
        Inode n;
        vfs_read_inode(i, &n);
        
        if (n.type != INODE_FREE && n.parent == dir_inode) {
            // Found a child!
            char type[10] = (n.type == INODE_DIR) ? "[dir]" : "[file]";
            printf("%s | %s | %u | %u B\n",
                n.name, type, n.permissions, n.size);
        }
    }
    return 0;
}
```

---

## KEY CONCEPTS SUMMARY

1. **Everything is an inode**: Every file and directory is metadata + data
2. **Disk I/O is explicit**: Must call disk_read_data/disk_write_data
3. **Path resolution** scans inodes to find children
4. **File descriptors** track open files and cursor position
5. **Permissions** are simple bits (read, write, execute)
6. **No fragmentation**: Each file gets fixed 4 KB data block

---

## Execution Flow Example: Create and Write File

```
User: vfs_creat("/home/test.txt", 6)
│
├─ split_path("/home/test.txt", cwd, name)
│  └─ Resolve "/home" → Inode[3]
│  └─ name = "test.txt"
│
├─ Check parent is directory with write permission ✓
│
├─ find_child(3, "test.txt") → -1 (doesn't exist)
│
├─ alloc_inode() → find first INODE_FREE → Inode[8]
│
├─ Create new inode:
│  └─ name = "test.txt"
│  └─ type = INODE_FILE
│  └─ parent = 3
│  └─ size = 0
│  └─ vfs_write_inode(8, &n)
│
├─ Allocate FD:
│  └─ fd_table[2]:
│     ├─ inode_idx = 8
│     ├─ flags = VFS_O_WRONLY
│     ├─ cursor = 0
│     └─ in_use = 1
│
└─ Return fd = 2

User: vfs_write(2, "Hello!", 6)
│
├─ Validate fd_table[2] ✓ (in_use=1, inode=8)
│
├─ Check VFS_O_WRONLY ✓
│
├─ Calculate position: 32,832 + (8 × 4,096) + 0 = 65,408
│
├─ Write 6 bytes "Hello!" at offset 65,408
│
├─ Update cursor: 0 + 6 = 6
│
├─ Read Inode[8] (size was 0)
│
├─ Update size: 0 → 6
│
├─ vfs_write_inode(8, &n)
│
└─ Return 6 (bytes written)
```

This is how the VFS actually works! Every operation boils down to:
1. Path resolution (find inode)
2. Permission checking
3. Disk I/O (read or write)
4. Update metadata (inode)

Next: Read shell.c to see how the CLI commands call these functions!

