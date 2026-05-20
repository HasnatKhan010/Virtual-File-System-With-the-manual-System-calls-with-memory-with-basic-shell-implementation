# VFS PROJECT - PART 3: shell.c (THE USER INTERFACE)
## CLI Commands & Command Dispatching

---

# FILE 3: shell.c - The Interactive Shell

## Overview
shell.c is the **user-facing interface** to the VFS kernel. It:
1. Reads user commands from terminal
2. Parses command-line arguments
3. Routes commands to appropriate VFS functions
4. Displays results to user

---

## 1. ARCHITECTURE

### Three Layers

```
┌─────────────────────────────────────────┐
│  USER TERMINAL                          │
│  Type: ls /home                         │
└──────────┬──────────────────────────────┘
           │
┌──────────▼──────────────────────────────┐
│  SHELL.C - CLI Interface Layer          │
│  ✓ Parse arguments                      │
│  ✓ Route to command handler             │
│  ✓ Display results                      │
└──────────┬──────────────────────────────┘
           │
┌──────────▼──────────────────────────────┐
│  VFS.C - Kernel Layer                   │
│  ✓ Implement vfs_ls()                   │
│  ✓ vfs_open(), vfs_read()               │
│  ✓ Disk I/O operations                  │
└──────────┬──────────────────────────────┘
           │
┌──────────▼──────────────────────────────┐
│  FILE SYSTEM                            │
│  filesystem.bin (persistent storage)    │
└─────────────────────────────────────────┘
```

---

## 2. COMMAND LINE PARSING

### parse_args(input, argv[], max_args)

```c
static int parse_args(char *input, char *argv[], int max_args)
{
    // Input: "write -a file.txt hello world"
    // Output: argv[] = {"write", "-a", "file.txt", "hello", "world"}
    //         return argc = 5
    
    int argc = 0;
    char *p = input;
    
    while (*p && argc < max_args) {
        // Skip whitespace
        while (*p == ' ' || *p == '\t') p++;
        if (!*p) break;
        
        // Handle quoted strings
        if (*p == '"') {
            p++;
            argv[argc++] = p;
            while (*p && *p != '"') p++;
            if (*p) *p++ = '\0';
        } else {
            // Regular token
            argv[argc++] = p;
            while (*p && *p != ' ' && *p != '\t') p++;
            if (*p) *p++ = '\0';
        }
    }
    return argc;
}
```

**Features:**
- Splits by whitespace
- Handles double-quoted strings as single token
- Returns argc and fills argv array

**Example:**
```
Input:  'write -a file.txt "hello world"'
Output: argc = 4
        argv[0] = "write"
        argv[1] = "-a"
        argv[2] = "file.txt"
        argv[3] = "hello world"
```

### get_cwd_string(out, max)

```c
static void get_cwd_string(char *out, int max)
{
    // Converts cwd_inode into a path string
    // Example: cwd_inode = 5 → path = "/home/user"
    
    int stack[MAX_INODES];
    int depth = 0;
    int cur = cwd_inode;
    
    // Walk up to root, collecting inode indices
    while (cur != 0 && depth < MAX_INODES) {
        stack[depth++] = cur;
        Inode n;
        vfs_read_inode(cur, &n);
        cur = n.parent;
    }
    
    // Build path from root down
    out[0] = '\0';
    for (int i = depth - 1; i >= 0; i--) {
        Inode n;
        vfs_read_inode(stack[i], &n);
        strncat(out, "/", max - strlen(out) - 1);
        strncat(out, n.name, max - strlen(out) - 1);
    }
}
```

**Example:**
```
If cwd_inode = 5:
  stack = [5, 3, 0]     (collecting from 5 up to root)

After walking parent pointers:
  Inode[5].parent = 3 (so go to 3)
  Inode[3].parent = 0 (so go to 0, which is root)
  
Result: "/home/user"

Prompt: vfs:/home/user$
```

---

## 3. COMMAND IMPLEMENTATIONS

Each command is a function that:
1. Validates arguments
2. Calls VFS functions
3. Displays results

### pwd - Print Working Directory

```c
static void cmd_pwd(void)
{
    char path[MAX_PATH] = {0};
    get_cwd_string(path, MAX_PATH);
    printf("%s\n", path);
}
```

**Example:**
```
vfs:/home/user$ pwd
/home/user
```

### ls [path] - List Directory

```c
static void cmd_ls(int argc, char *argv[])
{
    int dir = cwd_inode;  // Default: current directory
    
    if (argc > 1) {
        // Optional path argument
        dir = vfs_resolve_path(argv[1], cwd_inode);
        if (dir < 0) { fprintf(stderr, "ls: '%s': not found\n", argv[1]); return; }
    }
    
    // Call VFS function to list directory
    vfs_ls(dir);
}
```

**Usage:**
```
vfs:/$ ls
Name                   Type    Perms  Size
----                   ----    -----  ----
home                   [dir]   rwx    0 B
etc                    [dir]   r-x    0 B

vfs:/$ ls /home
Name                   Type    Perms  Size
----                   ----    -----  ----
user                   [dir]   rwx    0 B
```

### cd [path] - Change Directory

```c
static void cmd_cd(int argc, char *argv[])
{
    if (argc < 2) {
        cwd_inode = 0;  // No arg: go to root
        return;
    }
    vfs_cd(argv[1]);  // Call VFS, which updates cwd_inode
}
```

**Usage:**
```
vfs:/$ cd /home/user
vfs:/home/user$ cd ..
vfs:/home$ cd /
vfs:/$
```

### mkdir <name> - Create Directory

```c
static void cmd_mkdir(int argc, char *argv[])
{
    if (argc < 2) {
        fprintf(stderr, "Usage: mkdir <name>\n");
        return;
    }
    if (vfs_mkdir(argv[1], 0x7) == 0)  // 0x7 = rwx
        printf("mkdir: created directory '%s'\n", argv[1]);
}
```

**Usage:**
```
vfs:/$ mkdir projects
mkdir: created directory 'projects'

vfs:/$ ls
Name                   Type    Perms  Size
----                   ----    -----  ----
projects               [dir]   rwx    0 B
```

### touch <file> - Create Empty File

```c
static void cmd_touch(int argc, char *argv[])
{
    if (argc < 2) { fprintf(stderr, "Usage: touch <file>\n"); return; }
    
    int fd = vfs_creat(argv[1], 0x6);  // 0x6 = rw-
    if (fd >= 0) {
        vfs_close(fd);
        printf("touch: created '%s'\n", argv[1]);
    }
}
```

**Usage:**
```
vfs:/$ touch note.txt
touch: created 'note.txt'

vfs:/$ cat note.txt
(empty file, nothing to display)
```

### cat <file> - Display File Contents

```c
static void cmd_cat(int argc, char *argv[])
{
    if (argc < 2) { fprintf(stderr, "Usage: cat <file>\n"); return; }
    
    int fd = vfs_open(argv[1], VFS_O_RDONLY);
    if (fd < 0) return;
    
    char buf[257];
    int n;
    while ((n = vfs_read(fd, buf, 256)) > 0) {
        buf[n] = '\0';
        printf("%s", buf);
    }
    printf("\n");
    vfs_close(fd);
}
```

**Usage:**
```
vfs:/$ cat note.txt
Hello World!
This is a test file.
```

### write [-a] <file> <text...> - Write/Append Text

```c
static void cmd_write(int argc, char *argv[])
{
    // -a flag for append mode (default: overwrite)
    int append = 0;
    int file_arg = 1;
    
    if (strcmp(argv[1], "-a") == 0) {
        append = 1;
        file_arg = 2;
    }
    
    // Auto-create file if doesn't exist
    if (vfs_resolve_path(argv[file_arg], cwd_inode) < 0) {
        int fd = vfs_creat(argv[file_arg], 0x6);
        if (fd >= 0) vfs_close(fd);
    }
    
    // Open with appropriate flags
    int flags = append ? (VFS_O_WRONLY | VFS_O_APPEND) : (VFS_O_WRONLY | VFS_O_TRUNC);
    int fd = vfs_open(argv[file_arg], flags);
    
    // Join remaining args as content
    char content[MAX_FILE_SIZE] = {0};
    for (int i = file_arg + 1; i < argc; i++) {
        if (i > file_arg + 1) strncat(content, " ", sizeof(content) - strlen(content) - 1);
        strncat(content, argv[i], sizeof(content) - strlen(content) - 1);
    }
    strncat(content, "\n", sizeof(content) - strlen(content) - 1);
    
    int written = vfs_write(fd, content, (int)strlen(content));
    vfs_close(fd);
    printf("write: %d byte(s) written to '%s'\n", written, argv[file_arg]);
}
```

**Usage:**
```
vfs:/$ write test.txt hello world
write: 12 byte(s) written to 'test.txt'

vfs:/$ cat test.txt
hello world

vfs:/$ write -a test.txt goodbye
write: 8 byte(s) written to 'test.txt'

vfs:/$ cat test.txt
hello world
goodbye
```

### cp <src> <dst> - Copy File

```c
static void cmd_cp(int argc, char *argv[])
{
    if (argc < 3) { fprintf(stderr, "Usage: cp <src> <dst>\n"); return; }
    
    if (vfs_copy(argv[1], argv[2]) == 0)
        printf("cp: '%s' -> '%s'\n", argv[1], argv[2]);
}
```

**Usage:**
```
vfs:/$ cp test.txt test2.txt
cp: 'test.txt' -> 'test2.txt'

vfs:/$ ls
test.txt  test2.txt
```

### chmod <mode> <file> - Change Permissions

```c
static void cmd_chmod(int argc, char *argv[])
{
    if (argc < 3) { fprintf(stderr, "Usage: chmod <0-7> <file>\n"); return; }
    
    uint8_t mode = (uint8_t)(atoi(argv[1]) & 0x7);
    vfs_chmod(argv[2], mode);
}
```

**Permission Bits:**
```
7 = 111 = rwx  (read, write, execute)
6 = 110 = rw-  (read, write)
5 = 101 = r-x  (read, execute)
4 = 100 = r--  (read-only)
2 = 010 = -w-  (write-only)
1 = 001 = --x  (execute-only)
0 = 000 = ---  (no permissions)
```

**Usage:**
```
vfs:/$ chmod 4 secret.txt
chmod: 'secret.txt' permissions set to 4 (r--)

vfs:/$ chmod 6 note.txt
chmod: 'note.txt' permissions set to 6 (rw-)
```

### nano <file> - Simple Text Editor

```c
static void cmd_nano(int argc, char *argv[])
{
    if (argc < 2) { fprintf(stderr, "Usage: nano <file>\n"); return; }
    
    // Read existing content
    char existing[MAX_FILE_SIZE + 1] = {0};
    int existing_len = 0;
    {
        int fd = vfs_open(argv[1], VFS_O_RDONLY);
        if (fd >= 0) {
            existing_len = vfs_read(fd, existing, MAX_FILE_SIZE);
            existing[existing_len] = '\0';
            vfs_close(fd);
        }
    }
    
    // Show editor interface
    printf("┌── nano: %s ─────────────────────────────────┐\n", argv[1]);
    printf("│ Enter new content. Finish with '.' on its own line.\n");
    printf("│ Type 'q' to cancel.\n");
    printf("└──────────────────────────────────────────────┘\n");
    
    // Read new content
    char new_content[MAX_FILE_SIZE] = {0};
    char line[1024];
    
    while (fgets(line, sizeof(line), stdin)) {
        if (strcmp(line, ".\n") == 0) break;
        if (strcmp(line, "q\n") == 0) { printf("nano: cancelled.\n"); return; }
        strncat(new_content, line, sizeof(new_content) - strlen(new_content) - 1);
    }
    
    // Overwrite file
    int fd = vfs_creat(argv[1], 0x6);
    if (strlen(new_content) > 0)
        vfs_write(fd, new_content, (int)strlen(new_content));
    vfs_close(fd);
    printf("nano: '%s' saved (%zu bytes).\n", argv[1], strlen(new_content));
}
```

**Usage:**
```
vfs:/$ nano note.txt
┌── nano: note.txt ─────────────────────────────────┐
│ Enter new content. Finish with '.' on its own line.
│ Type 'q' to cancel.
└──────────────────────────────────────────────────┘
This is my note
It has multiple lines
.
nano: 'note.txt' saved (44 bytes).
```

### wc <file> - Word Count

```c
static void cmd_wc(int argc, char *argv[])
{
    if (argc < 2) { fprintf(stderr, "Usage: wc <file>\n"); return; }
    
    int fd = vfs_open(argv[1], VFS_O_RDONLY);
    char buf[MAX_FILE_SIZE + 1] = {0};
    int n = vfs_read(fd, buf, MAX_FILE_SIZE);
    vfs_close(fd);
    buf[n] = '\0';
    
    // Count lines, words, characters
    int lines = 0, words = 0, in_word = 0;
    for (int i = 0; i < n; i++) {
        if (buf[i] == '\n') lines++;
        if (isspace((unsigned char)buf[i])) {
            in_word = 0;
        } else if (!in_word) {
            in_word = 1;
            words++;
        }
    }
    printf("%6d %6d %6d  %s\n", lines, words, n, argv[1]);
}
```

**Usage:**
```
vfs:/$ wc note.txt
     2      8     44  note.txt
     (2 lines, 8 words, 44 bytes)
```

### grep <pattern> <file> - Search Text

```c
static void cmd_grep(int argc, char *argv[])
{
    if (argc < 3) { fprintf(stderr, "Usage: grep <pattern> <file>\n"); return; }
    
    int fd = vfs_open(argv[2], VFS_O_RDONLY);
    char buf[MAX_FILE_SIZE + 1] = {0};
    int n = vfs_read(fd, buf, MAX_FILE_SIZE);
    vfs_close(fd);
    buf[n] = '\0';
    
    // Search lines
    int matches = 0;
    char *line = strtok(buf, "\n");
    while (line) {
        if (strstr(line, argv[1])) {
            printf("%s\n", line);
            matches++;
        }
        line = strtok(NULL, "\n");
    }
    if (matches == 0) printf("grep: no matches for '%s'\n", argv[1]);
}
```

**Usage:**
```
vfs:/$ grep "is" note.txt
This is my note
It is great!
```

### stat <file> - Show Inode Info

```c
static void cmd_stat(int argc, char *argv[])
{
    if (argc < 2) { fprintf(stderr, "Usage: stat <file>\n"); return; }
    
    int idx = vfs_resolve_path(argv[1], cwd_inode);
    Inode n;
    vfs_read_inode(idx, &n);
    
    char perm[4] = "---";
    if (n.permissions & PERM_READ)  perm[0] = 'r';
    if (n.permissions & PERM_WRITE) perm[1] = 'w';
    if (n.permissions & PERM_EXEC)  perm[2] = 'x';
    
    printf("  File : %s\n", n.name);
    printf("  Type : %s\n", n.type == INODE_DIR ? "directory" : "regular file");
    printf("  Size : %u bytes\n", n.size);
    printf("  Perm : %s  (numeric: %u)\n", perm, n.permissions);
    printf(" Inode : #%d\n", idx);
    printf("Parent : #%d\n", n.parent);
}
```

**Usage:**
```
vfs:/$ stat note.txt
  File : note.txt
  Type : regular file
  Size : 44 bytes
  Perm : rw-  (numeric: 6)
 Inode : #5
Parent : #0
```

### debug - Internal State

```c
static void cmd_debug(void)
{
    vfs_debug();
}
```

Shows:
- Inode usage (how many files/directories)
- Total data size
- Active file descriptors
- Cursor positions

### tree [path] - Visual Directory Tree

```c
static void cmd_tree(int argc, char *argv[])
{
    int dir = cwd_inode;
    if (argc > 1) {
        dir = vfs_resolve_path(argv[1], cwd_inode);
    }
    vfs_tree(dir, 0, ...);
}
```

**Usage:**
```
vfs:/$ tree
/
├── home
│   ├── user
│   │   └── note.txt
│   └── admin
└── etc
    └── config.txt
```

### sh <script.sh> - Run Batch Commands

```c
static void cmd_sh(int argc, char *argv[])
{
    int fd = vfs_open(argv[1], VFS_O_RDONLY);
    char buf[MAX_FILE_SIZE + 1] = {0};
    int n = vfs_read(fd, buf, MAX_FILE_SIZE);
    vfs_close(fd);
    buf[n] = '\0';
    
    // Parse script line by line
    char *p = buf;
    while (*p) {
        char *end = p;
        while (*end && *end != '\n') end++;
        
        int len = (int)(end - p);
        char line[MAX_INPUT];
        memcpy(line, p, len);
        line[len] = '\0';
        
        p = (*end == '\n') ? end + 1 : end;
        
        // Skip comments and blanks
        char *trimmed = line;
        while (*trimmed == ' ' || *trimmed == '\t') trimmed++;
        if (!*trimmed || *trimmed == '#') continue;
        
        printf("  >> %s\n", trimmed);
        dispatch(trimmed);  // Execute each line
    }
}
```

**Example script file (commands.sh):**
```
# Create a test directory
mkdir test

# Navigate into it
cd test

# Create some files
touch file1.txt
touch file2.txt

# List contents
ls

# Change directory back
cd /
```

---

## 4. COMMAND DISPATCH

### dispatch(line) - Route Commands

```c
static void dispatch(char *line)
{
    // Strip newline
    line[strcspn(line, "\r\n")] = '\0';
    if (!line[0]) return;
    
    // Parse arguments
    char *argv[MAX_ARGS];
    int argc = parse_args(line, argv, MAX_ARGS);
    if (argc == 0) return;
    
    char *cmd = argv[0];
    
    // Route to appropriate command handler
    if      (strcmp(cmd, "ls")    == 0) cmd_ls(argc, argv);
    else if (strcmp(cmd, "cd")    == 0) cmd_cd(argc, argv);
    else if (strcmp(cmd, "pwd")   == 0) cmd_pwd();
    else if (strcmp(cmd, "mkdir") == 0) cmd_mkdir(argc, argv);
    // ... more commands ...
    else if (strcmp(cmd, "exit")  == 0) {
        vfs_shutdown();
        exit(0);
    }
    else {
        fprintf(stderr, "%s: command not found\n", cmd);
    }
}
```

---

## 5. MAIN LOOP

```c
int main(void)
{
    printf("╔══════════════════════════════════════════════╗\n");
    printf("║   Virtual Filesystem with System Calls       ║\n");
    printf("║   Container: filesystem.bin                 ║\n");
    printf("╚══════════════════════════════════════════════╝\n");
    printf("Type 'help' to list commands.\n\n");
    
    // Initialize VFS (open/create filesystem.bin)
    if (vfs_init() < 0) {
        fprintf(stderr, "Fatal: could not initialise VFS\n");
        return 1;
    }
    
    char input[MAX_INPUT];
    char cwd[MAX_PATH];
    
    // Main event loop
    while (1) {
        // Build and display prompt
        get_cwd_string(cwd, MAX_PATH);
        printf("vfs:%s$ ", cwd);
        fflush(stdout);
        
        // Read command from user
        if (!fgets(input, sizeof(input), stdin)) {
            printf("\n");
            break;
        }
        
        // Execute command
        dispatch(input);
    }
    
    vfs_shutdown();
    return 0;
}
```

**Main Loop Flow:**
```
1. Show prompt: "vfs:/home/user$ "
2. Read user input: "ls /home"
3. Parse arguments: argc=2, argv={"ls", "/home"}
4. Route to handler: cmd_ls(argc, argv)
5. Handler calls VFS: vfs_ls(inode)
6. Display results
7. Repeat step 1
```

---

## COMMAND REFERENCE

| Command | Purpose | Example |
|---------|---------|---------|
| `pwd` | Print working directory | `pwd` → `/home/user` |
| `ls [path]` | List directory | `ls /home` |
| `cd [path]` | Change directory | `cd /home/user` |
| `mkdir <name>` | Create directory | `mkdir test` |
| `touch <file>` | Create empty file | `touch note.txt` |
| `rm <file/dir>` | Delete file or empty dir | `rm note.txt` |
| `cat <file>` | Display file | `cat note.txt` |
| `write [-a] <f> <txt>` | Write/append text | `write -a file.txt hello` |
| `cp <src> <dst>` | Copy file | `cp file.txt copy.txt` |
| `mv <src> <dst>` | Move/rename | `mv old.txt new.txt` |
| `chmod <0-7> <file>` | Change permissions | `chmod 4 file.txt` |
| `nano <file>` | Text editor | `nano note.txt` |
| `wc <file>` | Word/line count | `wc note.txt` |
| `grep <pat> <file>` | Search text | `grep "hello" file.txt` |
| `stat <file>` | Show inode info | `stat file.txt` |
| `debug` | Show internal state | `debug` |
| `tree [path]` | Draw directory tree | `tree` |
| `sh <script>` | Run batch commands | `sh commands.sh` |
| `echo <text>` | Print text | `echo hello` |
| `exit` | Quit VFS | `exit` |

---

## SUMMARY: How It All Works Together

```
User Types:   write -a note.txt hello world

Main Loop:
  ↓
parse_args()  → argc=5, argv={"write", "-a", "note.txt", "hello", "world"}
  ↓
dispatch()    → recognizes "write" command
  ↓
cmd_write()   → processes flags and arguments
  ↓
vfs_open()    → opens file in append mode
  ↓
vfs_write()   → writes "hello world\n" to file
  ↓
vfs_close()   → closes file descriptor
  ↓
printf()      → displays "write: 12 byte(s) written to 'note.txt'"
  ↓
Back to Main Loop
```

This three-tier architecture makes it easy to:
- Add new commands (just add cmd_xxx() function and route in dispatch)
- Test VFS independently of shell
- Handle errors gracefully

