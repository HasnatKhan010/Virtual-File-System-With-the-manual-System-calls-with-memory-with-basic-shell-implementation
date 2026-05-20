#ifndef VFS_H
#define VFS_H

#include <stdint.h>
#include <stdio.h>

/* ── Container file ─────────────────────────────────── */
#define CONTAINER_FILE  "filesystem.bin"

/* ── Limits ─────────────────────────────────────────── */
#define MAX_INODES      128
#define MAX_NAME        124   /* chosen so sizeof(Inode) == 256 exactly */
#define MAX_FILE_SIZE   4096
#define MAX_FD          32
#define MAX_PATH        512

/* ── Inode types ────────────────────────────────────── */
#define INODE_FREE  0
#define INODE_FILE  1
#define INODE_DIR   2

/* ── vfs_open() flags ───────────────────────────────── */
#define VFS_O_RDONLY  0x01
#define VFS_O_WRONLY  0x02
#define VFS_O_RDWR    0x03   /* lower 2 bits = access mode */
#define VFS_O_APPEND  0x04
#define VFS_O_TRUNC   0x10

/* ── vfs_lseek() whence ─────────────────────────────── */
#define VFS_SEEK_SET  0
#define VFS_SEEK_CUR  1
#define VFS_SEEK_END  2

/* ── Permission bits (rwx bitmask) ─────────────────── */
#define PERM_READ   4
#define PERM_WRITE  2
#define PERM_EXEC   1

/* ── Binary layout ──────────────────────────────────── */
/*
 *  Offset 0             : Superblock  (64 B)
 *  Offset 64            : Inode table (128 × 256 B = 32768 B)
 *  Offset 32832         : Data area   (128 × 4096 B)
 *
 *  Data for inode[i] lives at: DATA_OFFSET + i * MAX_FILE_SIZE
 *  Directories store NO data — children found by scanning inodes.
 */
#define SUPERBLOCK_SIZE    64
#define INODE_SIZE         256
#define INODE_TABLE_OFFSET SUPERBLOCK_SIZE
#define INODE_TABLE_SIZE   (MAX_INODES * INODE_SIZE)
#define DATA_OFFSET        (INODE_TABLE_OFFSET + INODE_TABLE_SIZE)
#define VFS_MAGIC          0x56465321u   /* "VFS!" */

/* ── On-disk structures (packed = no alignment gaps) ── */

typedef struct __attribute__((packed)) {
    uint32_t magic;        /*  4 */
    uint32_t version;      /*  4 */
    uint32_t inode_count;  /*  4 */
    uint8_t  _pad[52];     /* 52 → total 64 B */
} Superblock;

typedef struct __attribute__((packed)) {
    char     name[MAX_NAME]; /* 124 */
    uint8_t  type;           /*   1  INODE_FREE / FILE / DIR */
    uint8_t  permissions;    /*   1  rwx bitmask             */
    int32_t  parent;         /*   4  parent inode idx (-1=none) */
    uint32_t size;           /*   4  bytes used (files only) */
    uint8_t  _pad[122];      /* 122 → total 256 B */
} Inode;

/* ── In-memory file descriptor ──────────────────────── */
typedef struct {
    int      inode_idx;
    int      flags;
    uint32_t cursor;
    int      in_use;
} FileDescriptor;

/* ── Global VFS state (defined in vfs.c) ────────────── */
extern FILE          *container;
extern FileDescriptor fd_table[MAX_FD];
extern int            cwd_inode;

/* ── Lifecycle ──────────────────────────────────────── */
int  vfs_init    (void);
void vfs_shutdown(void);

/* ── Syscall interface ──────────────────────────────── */
int  vfs_creat(const char *path, uint8_t mode);
int  vfs_open (const char *path, int flags);
int  vfs_read (int fd, char *buf, int nbytes);
int  vfs_write(int fd, const char *buf, int nbytes);
int  vfs_lseek(int fd, int offset, int whence);
int  vfs_close(int fd);

/* ── Filesystem operations ──────────────────────────── */
int  vfs_mkdir (const char *path, uint8_t mode);
int  vfs_unlink(const char *path);
int  vfs_rmdir (const char *path);
int  vfs_rename(const char *old_path, const char *new_path);
int  vfs_copy  (const char *src,  const char *dst);
int  vfs_chmod (const char *path, uint8_t mode);
int  vfs_ls    (int dir_inode);
int  vfs_cd    (const char *path);

/* ── Helpers used by shell.c ────────────────────────── */
int  vfs_resolve_path(const char *path, int start_inode);
void vfs_read_inode  (int idx, Inode *out);
void vfs_write_inode (int idx, const Inode *in);
int  vfs_check_perm  (int inode_idx, int need_write, int need_exec);

/* ── Presentation / Debugging Tools ─────────────────── */
void vfs_debug(void);
void vfs_tree(int dir_inode, int depth, int *last_flags);
void vfs_hexdump(int length);

#endif /* VFS_H */
