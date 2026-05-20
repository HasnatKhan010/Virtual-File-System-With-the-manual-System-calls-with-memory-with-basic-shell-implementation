/*
 * vfs.c — Virtual Filesystem Kernel
 *
 * Implements creat / open / read / write / lseek / close
 * and higher-level ops (mkdir, unlink, rmdir, rename, copy, chmod, ls, cd).
 * All state is persisted in a single binary file (filesystem.bin).
 */

#include "vfs.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ═══════════════════════════════════════════════════════════
 * Global state
 * ═══════════════════════════════════════════════════════════ */
FILE          *container = NULL;
FileDescriptor fd_table[MAX_FD];
int            cwd_inode = 0;   /* root = inode 0 */

/* ═══════════════════════════════════════════════════════════
 * Low-level disk helpers
 * ═══════════════════════════════════════════════════════════ */

void vfs_read_inode(int idx, Inode *out)
{
    fseek(container, INODE_TABLE_OFFSET + idx * INODE_SIZE, SEEK_SET);
    fread(out, sizeof(Inode), 1, container);
}

void vfs_write_inode(int idx, const Inode *in)
{
    fseek(container, INODE_TABLE_OFFSET + idx * INODE_SIZE, SEEK_SET);
    fwrite(in, sizeof(Inode), 1, container);
    fflush(container);
}

static void disk_read_data(int inode_idx, char *buf, uint32_t offset, uint32_t size)
{
    long pos = DATA_OFFSET + (long)inode_idx * MAX_FILE_SIZE + offset;
    fseek(container, pos, SEEK_SET);
    fread(buf, 1, size, container);
}

static void disk_write_data(int inode_idx, const char *buf, uint32_t offset, uint32_t size)
{
    long pos = DATA_OFFSET + (long)inode_idx * MAX_FILE_SIZE + offset;
    fseek(container, pos, SEEK_SET);
    fwrite(buf, 1, size, container);
    fflush(container);
}

/* ═══════════════════════════════════════════════════════════
 * Init / Shutdown
 * ═══════════════════════════════════════════════════════════ */

int vfs_init(void)
{
    /* Try to open existing container */
    container = fopen(CONTAINER_FILE, "r+b");

    if (!container) {
        /* First run: create and format the container */
        container = fopen(CONTAINER_FILE, "w+b");
        if (!container) {
            perror("vfs_init: cannot create container");
            return -1;
        }

        /* Zero-fill the entire container */
        long total = SUPERBLOCK_SIZE + INODE_TABLE_SIZE + (long)MAX_INODES * MAX_FILE_SIZE;
        char zero = 0;
        fseek(container, total - 1, SEEK_SET);
        fwrite(&zero, 1, 1, container);
        fflush(container);

        /* Write superblock */
        Superblock sb;
        memset(&sb, 0, sizeof(sb));
        sb.magic   = VFS_MAGIC;
        sb.version = 1;
        fseek(container, 0, SEEK_SET);
        fwrite(&sb, sizeof(sb), 1, container);

        /* Create root inode (index 0) */
        Inode root;
        memset(&root, 0, sizeof(root));
        strncpy(root.name, "/", MAX_NAME - 1);
        root.type        = INODE_DIR;
        root.permissions = 0x7;   /* rwx */
        root.parent      = -1;
        root.size        = 0;
        vfs_write_inode(0, &root);

        printf("[VFS] New filesystem created.\n");
    }

    /* Verify magic */
    Superblock sb;
    fseek(container, 0, SEEK_SET);
    fread(&sb, sizeof(sb), 1, container);
    if (sb.magic != VFS_MAGIC) {
        fprintf(stderr, "vfs_init: bad magic — not a VFS container\n");
        fclose(container);
        container = NULL;
        return -1;
    }

    memset(fd_table, 0, sizeof(fd_table));
    cwd_inode = 0;
    return 0;
}

void vfs_shutdown(void)
{
    if (container) {
        fflush(container);
        fclose(container);
        container = NULL;
    }
}

/* ═══════════════════════════════════════════════════════════
 * Path resolution helpers
 * ═══════════════════════════════════════════════════════════ */

static int alloc_inode(void)
{
    for (int i = 1; i < MAX_INODES; i++) {   /* 0 is reserved for root */
        Inode n;
        vfs_read_inode(i, &n);
        if (n.type == INODE_FREE) return i;
    }
    return -1;
}

/* Find a direct child of parent_inode by name. Returns index or -1. */
static int find_child(int parent_inode, const char *name)
{
    for (int i = 0; i < MAX_INODES; i++) {
        Inode n;
        vfs_read_inode(i, &n);
        if (n.type != INODE_FREE
            && n.parent == parent_inode
            && strcmp(n.name, name) == 0)
            return i;
    }
    return -1;
}

/* Walk path from start_inode. Returns inode index or -1. */
int vfs_resolve_path(const char *path, int start_inode)
{
    if (!path || !*path) return start_inode;

    char buf[MAX_PATH];
    strncpy(buf, path, MAX_PATH - 1);
    buf[MAX_PATH - 1] = '\0';

    int cur = (buf[0] == '/') ? 0 : start_inode;
    char *p = (buf[0] == '/') ? buf + 1 : buf;

    char *tok = strtok(p, "/");
    while (tok) {
        if (strcmp(tok, ".") == 0) {
            /* stay */
        } else if (strcmp(tok, "..") == 0) {
            Inode n;
            vfs_read_inode(cur, &n);
            if (n.parent >= 0) cur = n.parent;
        } else {
            int child = find_child(cur, tok);
            if (child < 0) return -1;
            cur = child;
        }
        tok = strtok(NULL, "/");
    }
    return cur;
}

/*
 * Split a path into (parent_dir_index, filename).
 * Returns parent index or -1 on error.
 */
static int split_path(const char *path, int start, char *name_out)
{
    char buf[MAX_PATH];
    strncpy(buf, path, MAX_PATH - 1);
    buf[MAX_PATH - 1] = '\0';

    char *slash = strrchr(buf, '/');
    int parent;

    if (!slash) {
        /* pure filename, relative to cwd */
        parent = start;
        strncpy(name_out, buf, MAX_NAME - 1);
        name_out[MAX_NAME - 1] = '\0';
    } else {
        strncpy(name_out, slash + 1, MAX_NAME - 1);
        name_out[MAX_NAME - 1] = '\0';
        if (slash == buf) {
            parent = 0;          /* file lives directly under root */
        } else {
            *slash = '\0';
            parent = vfs_resolve_path(buf, start);
        }
    }
    return parent;
}

/* ═══════════════════════════════════════════════════════════
 * Permission check
 * ═══════════════════════════════════════════════════════════ */

int vfs_check_perm(int inode_idx, int need_write, int need_exec)
{
    Inode n;
    vfs_read_inode(inode_idx, &n);
    if (need_write && !(n.permissions & PERM_WRITE)) return -1;
    if (need_exec  && !(n.permissions & PERM_EXEC))  return -1;
    return 0;
}

/* ═══════════════════════════════════════════════════════════
 * creat()
 * Creates file, returns fd.  Truncates if already exists.
 * ═══════════════════════════════════════════════════════════ */

int vfs_creat(const char *path, uint8_t mode)
{
    char name[MAX_NAME] = {0};
    int parent = split_path(path, cwd_inode, name);
    if (parent < 0)  { fprintf(stderr, "creat: directory not found\n"); return -1; }
    if (!name[0])    { fprintf(stderr, "creat: empty filename\n");       return -1; }

    /* Check parent is a directory with write permission */
    Inode par_node;
    vfs_read_inode(parent, &par_node);
    if (par_node.type != INODE_DIR) { fprintf(stderr, "creat: parent is not a directory\n"); return -1; }
    if (!(par_node.permissions & PERM_WRITE)) { fprintf(stderr, "creat: permission denied\n"); return -1; }

    int idx = find_child(parent, name);

    if (idx >= 0) {
        /* File exists — truncate */
        Inode n;
        vfs_read_inode(idx, &n);
        n.size = 0;
        vfs_write_inode(idx, &n);
    } else {
        /* Allocate new inode */
        idx = alloc_inode();
        if (idx < 0) { fprintf(stderr, "creat: inode table full\n"); return -1; }

        Inode n;
        memset(&n, 0, sizeof(n));
        strncpy(n.name, name, MAX_NAME - 1);
        n.type        = INODE_FILE;
        n.permissions = mode & 0x7;
        n.parent      = parent;
        n.size        = 0;
        vfs_write_inode(idx, &n);
    }

    /* Open and return fd */
    for (int fd = 0; fd < MAX_FD; fd++) {
        if (!fd_table[fd].in_use) {
            fd_table[fd].inode_idx = idx;
            fd_table[fd].flags     = VFS_O_WRONLY;
            fd_table[fd].cursor    = 0;
            fd_table[fd].in_use    = 1;
            return fd;
        }
    }
    fprintf(stderr, "creat: fd table full\n");
    return -1;
}

/* ═══════════════════════════════════════════════════════════
 * open()
 * ═══════════════════════════════════════════════════════════ */

int vfs_open(const char *path, int flags)
{
    int idx = vfs_resolve_path(path, cwd_inode);
    if (idx < 0) { fprintf(stderr, "open: '%s' not found\n", path); return -1; }

    Inode n;
    vfs_read_inode(idx, &n);
    if (n.type == INODE_FREE) { fprintf(stderr, "open: inode is free\n"); return -1; }

    /* Permission checks */
    int access = flags & 0x03;
    if ((access == VFS_O_RDONLY || access == VFS_O_RDWR)
        && !(n.permissions & PERM_READ)) {
        fprintf(stderr, "open: permission denied (read)\n"); return -1;
    }
    if ((access == VFS_O_WRONLY || access == VFS_O_RDWR)
        && !(n.permissions & PERM_WRITE)) {
        fprintf(stderr, "open: permission denied (write)\n"); return -1;
    }

    /* Truncate if requested */
    if (flags & VFS_O_TRUNC) {
        n.size = 0;
        vfs_write_inode(idx, &n);
    }

    /* Allocate fd */
    for (int fd = 0; fd < MAX_FD; fd++) {
        if (!fd_table[fd].in_use) {
            fd_table[fd].inode_idx = idx;
            fd_table[fd].flags     = flags;
            fd_table[fd].cursor    = (flags & VFS_O_APPEND) ? n.size : 0;
            fd_table[fd].in_use    = 1;
            return fd;
        }
    }
    fprintf(stderr, "open: fd table full\n");
    return -1;
}

/* ═══════════════════════════════════════════════════════════
 * read()
 * ═══════════════════════════════════════════════════════════ */

int vfs_read(int fd, char *buf, int nbytes)
{
    if (fd < 0 || fd >= MAX_FD || !fd_table[fd].in_use) {
        fprintf(stderr, "read: bad fd %d\n", fd); return -1;
    }
    FileDescriptor *fde = &fd_table[fd];
    if ((fde->flags & 0x03) == VFS_O_WRONLY) {
        fprintf(stderr, "read: fd not open for reading\n"); return -1;
    }

    Inode n;
    vfs_read_inode(fde->inode_idx, &n);

    uint32_t avail  = (n.size > fde->cursor) ? n.size - fde->cursor : 0;
    int      to_read = (nbytes < (int)avail) ? nbytes : (int)avail;
    if (to_read <= 0) return 0;

    disk_read_data(fde->inode_idx, buf, fde->cursor, (uint32_t)to_read);
    fde->cursor += (uint32_t)to_read;
    return to_read;
}

/* ═══════════════════════════════════════════════════════════
 * write()
 * ═══════════════════════════════════════════════════════════ */

int vfs_write(int fd, const char *buf, int nbytes)
{
    if (fd < 0 || fd >= MAX_FD || !fd_table[fd].in_use) {
        fprintf(stderr, "write: bad fd %d\n", fd); return -1;
    }
    FileDescriptor *fde = &fd_table[fd];
    if ((fde->flags & 0x03) == VFS_O_RDONLY) {
        fprintf(stderr, "write: fd not open for writing\n"); return -1;
    }

    /* Append mode: always write at end */
    if (fde->flags & VFS_O_APPEND) {
        Inode n;
        vfs_read_inode(fde->inode_idx, &n);
        fde->cursor = n.size;
    }

    uint32_t space    = (uint32_t)MAX_FILE_SIZE - fde->cursor;
    int      to_write = (nbytes < (int)space) ? nbytes : (int)space;
    if (to_write <= 0) { fprintf(stderr, "write: file is full\n"); return -1; }

    disk_write_data(fde->inode_idx, buf, fde->cursor, (uint32_t)to_write);
    fde->cursor += (uint32_t)to_write;

    /* Update inode size if we extended the file */
    Inode n;
    vfs_read_inode(fde->inode_idx, &n);
    if (fde->cursor > n.size) {
        n.size = fde->cursor;
        vfs_write_inode(fde->inode_idx, &n);
    }
    return to_write;
}

/* ═══════════════════════════════════════════════════════════
 * lseek()
 * ═══════════════════════════════════════════════════════════ */

int vfs_lseek(int fd, int offset, int whence)
{
    if (fd < 0 || fd >= MAX_FD || !fd_table[fd].in_use) {
        fprintf(stderr, "lseek: bad fd %d\n", fd); return -1;
    }
    FileDescriptor *fde = &fd_table[fd];
    Inode n;
    vfs_read_inode(fde->inode_idx, &n);

    int new_pos;
    switch (whence) {
        case VFS_SEEK_SET: new_pos = offset;                      break;
        case VFS_SEEK_CUR: new_pos = (int)fde->cursor + offset;   break;
        case VFS_SEEK_END: new_pos = (int)n.size + offset;        break;
        default: fprintf(stderr, "lseek: invalid whence\n"); return -1;
    }

    if (new_pos < 0)              new_pos = 0;
    if (new_pos > MAX_FILE_SIZE)  new_pos = MAX_FILE_SIZE;
    fde->cursor = (uint32_t)new_pos;
    return new_pos;
}

/* ═══════════════════════════════════════════════════════════
 * close()
 * ═══════════════════════════════════════════════════════════ */

int vfs_close(int fd)
{
    if (fd < 0 || fd >= MAX_FD || !fd_table[fd].in_use) {
        fprintf(stderr, "close: bad fd %d\n", fd); return -1;
    }
    fflush(container);
    memset(&fd_table[fd], 0, sizeof(FileDescriptor));
    return 0;
}

/* ═══════════════════════════════════════════════════════════
 * mkdir()
 * ═══════════════════════════════════════════════════════════ */

int vfs_mkdir(const char *path, uint8_t mode)
{
    char name[MAX_NAME] = {0};
    int parent = split_path(path, cwd_inode, name);
    if (parent < 0) { fprintf(stderr, "mkdir: parent not found\n"); return -1; }
    if (!name[0])   { fprintf(stderr, "mkdir: empty name\n");       return -1; }

    if (find_child(parent, name) >= 0) {
        fprintf(stderr, "mkdir: '%s' already exists\n", name); return -1;
    }

    int idx = alloc_inode();
    if (idx < 0) { fprintf(stderr, "mkdir: inode table full\n"); return -1; }

    Inode n;
    memset(&n, 0, sizeof(n));
    strncpy(n.name, name, MAX_NAME - 1);
    n.type        = INODE_DIR;
    n.permissions = mode & 0x7;
    n.parent      = parent;
    n.size        = 0;
    vfs_write_inode(idx, &n);
    return 0;
}

/* ═══════════════════════════════════════════════════════════
 * unlink()  — delete a file
 * ═══════════════════════════════════════════════════════════ */

int vfs_unlink(const char *path)
{
    int idx = vfs_resolve_path(path, cwd_inode);
    if (idx <= 0) { fprintf(stderr, "rm: '%s' not found\n", path); return -1; }

    Inode n;
    vfs_read_inode(idx, &n);
    if (n.type != INODE_FILE) { fprintf(stderr, "rm: not a file (use rmdir for directories)\n"); return -1; }
    if (vfs_check_perm(n.parent, 1, 0) < 0) { fprintf(stderr, "rm: permission denied\n"); return -1; }

    n.type = INODE_FREE;
    vfs_write_inode(idx, &n);
    return 0;
}

/* ═══════════════════════════════════════════════════════════
 * rmdir()  — delete an empty directory
 * ═══════════════════════════════════════════════════════════ */

int vfs_rmdir(const char *path)
{
    int idx = vfs_resolve_path(path, cwd_inode);
    if (idx <= 0) { fprintf(stderr, "rmdir: '%s' not found\n", path); return -1; }

    Inode n;
    vfs_read_inode(idx, &n);
    if (n.type != INODE_DIR) { fprintf(stderr, "rmdir: not a directory\n"); return -1; }

    /* Must be empty */
    for (int i = 0; i < MAX_INODES; i++) {
        Inode child;
        vfs_read_inode(i, &child);
        if (child.type != INODE_FREE && child.parent == idx) {
            fprintf(stderr, "rmdir: directory not empty\n"); return -1;
        }
    }
    n.type = INODE_FREE;
    vfs_write_inode(idx, &n);
    return 0;
}

/* ═══════════════════════════════════════════════════════════
 * rename()  — move / rename
 * ═══════════════════════════════════════════════════════════ */

int vfs_rename(const char *old_path, const char *new_path)
{
    int idx = vfs_resolve_path(old_path, cwd_inode);
    if (idx < 0) { fprintf(stderr, "mv: source '%s' not found\n", old_path); return -1; }

    char new_name[MAX_NAME] = {0};
    int  new_parent = split_path(new_path, cwd_inode, new_name);
    if (new_parent < 0) { fprintf(stderr, "mv: destination directory not found\n"); return -1; }

    Inode n;
    vfs_read_inode(idx, &n);
    strncpy(n.name, new_name, MAX_NAME - 1);
    n.parent = new_parent;
    vfs_write_inode(idx, &n);
    return 0;
}

/* ═══════════════════════════════════════════════════════════
 * copy()
 * ═══════════════════════════════════════════════════════════ */

int vfs_copy(const char *src, const char *dst)
{
    int src_idx = vfs_resolve_path(src, cwd_inode);
    if (src_idx < 0) { fprintf(stderr, "cp: source '%s' not found\n", src); return -1; }

    Inode src_node;
    vfs_read_inode(src_idx, &src_node);
    if (src_node.type != INODE_FILE) { fprintf(stderr, "cp: source is not a file\n"); return -1; }

    char *buf = malloc(src_node.size + 1);
    if (!buf) return -1;

    if (src_node.size > 0)
        disk_read_data(src_idx, buf, 0, src_node.size);

    int fd = vfs_creat(dst, src_node.permissions);
    if (fd < 0) { free(buf); return -1; }
    if (src_node.size > 0)
        vfs_write(fd, buf, (int)src_node.size);
    vfs_close(fd);
    free(buf);
    return 0;
}

/* ═══════════════════════════════════════════════════════════
 * chmod()
 * ═══════════════════════════════════════════════════════════ */

int vfs_chmod(const char *path, uint8_t mode)
{
    int idx = vfs_resolve_path(path, cwd_inode);
    if (idx < 0) { fprintf(stderr, "chmod: '%s' not found\n", path); return -1; }

    Inode n;
    vfs_read_inode(idx, &n);
    n.permissions = mode & 0x7;
    vfs_write_inode(idx, &n);
    printf("chmod: '%s' permissions set to %u (%c%c%c)\n",
        n.name, mode,
        (mode & PERM_READ)  ? 'r' : '-',
        (mode & PERM_WRITE) ? 'w' : '-',
        (mode & PERM_EXEC)  ? 'x' : '-');
    return 0;
}

/* ═══════════════════════════════════════════════════════════
 * ls()  — list directory contents
 * ═══════════════════════════════════════════════════════════ */

int vfs_ls(int dir_inode)
{
    Inode dir;
    vfs_read_inode(dir_inode, &dir);
    if (dir.type != INODE_DIR) { fprintf(stderr, "ls: not a directory\n"); return -1; }

    int found = 0;
    printf("%-22s  %-6s  %-5s  %s\n", "Name", "Type", "Perms", "Size");
    printf("%-22s  %-6s  %-5s  %s\n", "----", "----", "-----", "----");

    for (int i = 0; i < MAX_INODES; i++) {
        Inode n;
        vfs_read_inode(i, &n);
        if (n.type != INODE_FREE && n.parent == dir_inode) {
            char perm[4] = "---";
            if (n.permissions & PERM_READ)  perm[0] = 'r';
            if (n.permissions & PERM_WRITE) perm[1] = 'w';
            if (n.permissions & PERM_EXEC)  perm[2] = 'x';

            printf("%-22s  %-6s  %-5s  %u B\n",
                n.name,
                n.type == INODE_DIR ? "[dir]" : "[file]",
                perm,
                n.size);
            found++;
        }
    }
    if (found == 0) printf("(empty)\n");
    return 0;
}

/* ═══════════════════════════════════════════════════════════
 * cd()
 * ═══════════════════════════════════════════════════════════ */

int vfs_cd(const char *path)
{
    if (!path || strcmp(path, "/") == 0) { cwd_inode = 0; return 0; }

    int idx = vfs_resolve_path(path, cwd_inode);
    if (idx < 0) { fprintf(stderr, "cd: '%s': no such directory\n", path); return -1; }

    Inode n;
    vfs_read_inode(idx, &n);
    if (n.type != INODE_DIR)   { fprintf(stderr, "cd: not a directory\n");  return -1; }
    if (vfs_check_perm(idx, 0, 1) < 0) { fprintf(stderr, "cd: permission denied\n"); return -1; }

    cwd_inode = idx;
    return 0;
}

/* ═══════════════════════════════════════════════════════════
 * Presentation / Debug Tools
 * ═══════════════════════════════════════════════════════════ */

void vfs_debug(void)
{
    printf("\n=== VFS INTERNAL STATE ===\n");
    int used_inodes = 0;
    int dir_count = 0;
    int file_count = 0;
    uint32_t total_size = 0;
    for (int i = 0; i < MAX_INODES; i++) {
        Inode n;
        vfs_read_inode(i, &n);
        if (n.type != INODE_FREE) {
            used_inodes++;
            if (n.type == INODE_DIR) dir_count++;
            else if (n.type == INODE_FILE) {
                file_count++;
                total_size += n.size;
            }
        }
    }
    printf("Inodes Used : %d / %d\n", used_inodes, MAX_INODES);
    printf("Directories : %d\n", dir_count);
    printf("Files       : %d\n", file_count);
    printf("Data Size   : %u bytes\n", total_size);
    printf("Total Disk  : %lu bytes\n", SUPERBLOCK_SIZE + INODE_TABLE_SIZE + (long)MAX_INODES * MAX_FILE_SIZE);
    
    printf("\n=== ACTIVE FILE DESCRIPTORS ===\n");
    int active_fds = 0;
    for (int i = 0; i < MAX_FD; i++) {
        if (fd_table[i].in_use) {
            active_fds++;
            Inode n;
            vfs_read_inode(fd_table[i].inode_idx, &n);
            printf("FD %2d : Inode %2d (%s) | Cursor: %4u | Mode: %s%s\n",
                   i, fd_table[i].inode_idx, n.name, fd_table[i].cursor,
                   (fd_table[i].flags & VFS_O_RDWR) == VFS_O_RDWR ? "RDWR" :
                   (fd_table[i].flags & VFS_O_WRONLY) ? "WRONLY" : "RDONLY",
                   (fd_table[i].flags & VFS_O_APPEND) ? " (APPEND)" : "");
        }
    }
    if (active_fds == 0) printf("(No files currently open)\n");
    printf("==========================\n\n");
}

void vfs_tree(int dir_inode, int depth, int *last_flags)
{
    if (depth == 0) {
        Inode n;
        vfs_read_inode(dir_inode, &n);
        printf("%s\n", n.name);
    }
    
    /* Count children first to know which is the last one */
    int children[MAX_INODES];
    int child_count = 0;
    for (int i = 0; i < MAX_INODES; i++) {
        Inode n;
        vfs_read_inode(i, &n);
        if (n.type != INODE_FREE && n.parent == dir_inode) {
            children[child_count++] = i;
        }
    }
    
    for (int i = 0; i < child_count; i++) {
        for (int d = 0; d < depth; d++) {
            if (last_flags[d]) printf("    ");
            else printf("|   ");
        }
        
        int is_last = (i == child_count - 1);
        if (is_last) printf("`-- ");
        else printf("|-- ");
        
        Inode child;
        vfs_read_inode(children[i], &child);
        printf("%s\n", child.name);
        
        if (child.type == INODE_DIR) {
            last_flags[depth] = is_last;
            vfs_tree(children[i], depth + 1, last_flags);
        }
    }
}

void vfs_hexdump(int length)
{
    if (!container) return;
    if (length <= 0) length = 512;
    if (length > 4096) length = 4096;
    
    unsigned char *buf = malloc(length);
    if (!buf) return;
    
    fseek(container, 0, SEEK_SET);
    int n = fread(buf, 1, length, container);
    
    printf("\n=== HEXDUMP (filesystem.bin, first %d bytes) ===\n", n);
    for (int i = 0; i < n; i += 16) {
        printf("%08X  ", i);
        
        /* Hex part */
        for (int j = 0; j < 16; j++) {
            if (i + j < n) printf("%02X ", buf[i + j]);
            else printf("   ");
            if (j == 7) printf(" ");
        }
        printf(" |");
        
        /* ASCII part */
        for (int j = 0; j < 16; j++) {
            if (i + j < n) {
                unsigned char c = buf[i + j];
                if (c >= 32 && c <= 126) printf("%c", c);
                else printf(".");
            }
        }
        printf("|\n");
    }
    printf("================================================\n\n");
    free(buf);
}
