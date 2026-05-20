/*
 * shell.c — VFS Shell (Custom Bash Simulator)
 *
 * Parses user input, routes commands to VFS kernel functions.
 * Implements: ls, cd, pwd, mkdir, touch, rm, cat, write, cp, mv,
 *             chmod, nano, wc, grep, sh, stat, help, exit
 *
 * Lab coverage:
 *   Lab 01 — CLI parsing + command dispatch
 *   Lab 02 — Navigation & file handling
 *   Lab 03 — chmod / permission enforcement
 *   Lab 04 — wc, grep
 *   Lab 11 — sh (batch script execution)
 */

#include "vfs.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#ifdef _WIN32
#include <windows.h>
#endif

#define MAX_ARGS   32
#define MAX_INPUT  1024

/* ═══════════════════════════════════════════════════════════
 * Utility: build current working directory string
 * ═══════════════════════════════════════════════════════════ */

static void get_cwd_string(char *out, int max)
{
    int stack[MAX_INODES];
    int depth = 0;
    int cur   = cwd_inode;

    /* Walk up to root collecting inode indices */
    while (cur != 0 && depth < MAX_INODES) {
        stack[depth++] = cur;
        Inode n;
        vfs_read_inode(cur, &n);
        if (n.parent < 0) break;
        cur = n.parent;
    }

    if (depth == 0) { strncpy(out, "/", max); return; }

    out[0] = '\0';
    for (int i = depth - 1; i >= 0; i--) {
        Inode n;
        vfs_read_inode(stack[i], &n);
        strncat(out, "/",     max - (int)strlen(out) - 1);
        strncat(out, n.name,  max - (int)strlen(out) - 1);
    }
}

/* ═══════════════════════════════════════════════════════════
 * Utility: tokenise input into argv[]
 * Handles double-quoted strings as single tokens.
 * ═══════════════════════════════════════════════════════════ */

static int parse_args(char *input, char *argv[], int max_args)
{
    int argc = 0;
    char *p  = input;

    while (*p && argc < max_args) {
        while (*p == ' ' || *p == '\t') p++;
        if (!*p) break;

        if (*p == '"') {
            p++;
            argv[argc++] = p;
            while (*p && *p != '"') p++;
            if (*p) *p++ = '\0';
        } else {
            argv[argc++] = p;
            while (*p && *p != ' ' && *p != '\t') p++;
            if (*p) *p++ = '\0';
        }
    }
    return argc;
}

/* Forward declaration (dispatch calls run_script, run_script calls dispatch) */
static void dispatch(char *line);

/* ═══════════════════════════════════════════════════════════
 * Command implementations
 * ═══════════════════════════════════════════════════════════ */

/* pwd */
static void cmd_pwd(void)
{
    char path[MAX_PATH] = {0};
    get_cwd_string(path, MAX_PATH);
    printf("%s\n", path);
}

/* ls [path] */
static void cmd_ls(int argc, char *argv[])
{
    int dir = cwd_inode;
    if (argc > 1) {
        dir = vfs_resolve_path(argv[1], cwd_inode);
        if (dir < 0) { fprintf(stderr, "ls: '%s': not found\n", argv[1]); return; }
    }
    vfs_ls(dir);
}

/* cd [path] */
static void cmd_cd(int argc, char *argv[])
{
    if (argc < 2) { cwd_inode = 0; return; }
    vfs_cd(argv[1]);
}

/* mkdir <name> */
static void cmd_mkdir(int argc, char *argv[])
{
    if (argc < 2) { fprintf(stderr, "Usage: mkdir <name>\n"); return; }
    if (vfs_mkdir(argv[1], 0x7) == 0)
        printf("mkdir: created directory '%s'\n", argv[1]);
}

/* touch <file> */
static void cmd_touch(int argc, char *argv[])
{
    if (argc < 2) { fprintf(stderr, "Usage: touch <file>\n"); return; }
    int fd = vfs_creat(argv[1], 0x6);   /* rw- */
    if (fd >= 0) { vfs_close(fd); printf("touch: created '%s'\n", argv[1]); }
}

/* rm <file|dir> */
static void cmd_rm(int argc, char *argv[])
{
    if (argc < 2) { fprintf(stderr, "Usage: rm <file|dir>\n"); return; }

    int idx = vfs_resolve_path(argv[1], cwd_inode);
    if (idx < 0) { fprintf(stderr, "rm: '%s': not found\n", argv[1]); return; }

    Inode n; vfs_read_inode(idx, &n);
    if (n.type == INODE_DIR) vfs_rmdir(argv[1]);
    else                      vfs_unlink(argv[1]);
}

/* cat <file> */
static void cmd_cat(int argc, char *argv[])
{
    if (argc < 2) { fprintf(stderr, "Usage: cat <file>\n"); return; }

    int fd = vfs_open(argv[1], VFS_O_RDONLY);
    if (fd < 0) return;

    char buf[257];
    int  n;
    while ((n = vfs_read(fd, buf, 256)) > 0) {
        buf[n] = '\0';
        printf("%s", buf);
    }
    printf("\n");
    vfs_close(fd);
}

/*
 * write [-a] <file> <text ...>
 *   -a  append mode (default: overwrite)
 */
static void cmd_write(int argc, char *argv[])
{
    if (argc < 3) { fprintf(stderr, "Usage: write [-a] <file> <text>\n"); return; }

    int append   = 0;
    int file_arg = 1;
    if (strcmp(argv[1], "-a") == 0) { append = 1; file_arg = 2; }
    if (argc < file_arg + 2) { fprintf(stderr, "Usage: write [-a] <file> <text>\n"); return; }

    /* Auto-create file if it doesn't exist */
    if (vfs_resolve_path(argv[file_arg], cwd_inode) < 0) {
        int fd = vfs_creat(argv[file_arg], 0x6);
        if (fd >= 0) vfs_close(fd);
    }

    int flags = append ? (VFS_O_WRONLY | VFS_O_APPEND) : (VFS_O_WRONLY | VFS_O_TRUNC);
    int fd    = vfs_open(argv[file_arg], flags);
    if (fd < 0) return;

    /* Join remaining tokens as the content */
    char content[MAX_FILE_SIZE] = {0};
    for (int i = file_arg + 1; i < argc; i++) {
        if (i > file_arg + 1)
            strncat(content, " ", sizeof(content) - strlen(content) - 1);
        strncat(content, argv[i], sizeof(content) - strlen(content) - 1);
    }
    strncat(content, "\n", sizeof(content) - strlen(content) - 1);

    int written = vfs_write(fd, content, (int)strlen(content));
    vfs_close(fd);
    if (written > 0)
        printf("write: %d byte(s) written to '%s'\n", written, argv[file_arg]);
}

/* cp <src> <dst> */
static void cmd_cp(int argc, char *argv[])
{
    if (argc < 3) { fprintf(stderr, "Usage: cp <src> <dst>\n"); return; }
    if (vfs_copy(argv[1], argv[2]) == 0)
        printf("cp: '%s' -> '%s'\n", argv[1], argv[2]);
}

/* mv <src> <dst> */
static void cmd_mv(int argc, char *argv[])
{
    if (argc < 3) { fprintf(stderr, "Usage: mv <src> <dst>\n"); return; }
    if (vfs_rename(argv[1], argv[2]) == 0)
        printf("mv: '%s' -> '%s'\n", argv[1], argv[2]);
}

/* chmod <mode> <file>   e.g. chmod 6 file  (rw-)  chmod 5 file (r-x) */
static void cmd_chmod(int argc, char *argv[])
{
    if (argc < 3) { fprintf(stderr, "Usage: chmod <0-7> <file>\n"); return; }
    uint8_t mode = (uint8_t)(atoi(argv[1]) & 0x7);
    vfs_chmod(argv[2], mode);
}

/*
 * nano <file>
 * Simple line editor: shows existing content, lets user type new content.
 * End input with a lone '.' line; cancel with 'q'.
 */
static void cmd_nano(int argc, char *argv[])
{
    if (argc < 2) { fprintf(stderr, "Usage: nano <file>\n"); return; }

    /* Read existing content */
    char existing[MAX_FILE_SIZE + 1] = {0};
    int  existing_len = 0;
    {
        int fd = vfs_open(argv[1], VFS_O_RDONLY);
        if (fd >= 0) {
            existing_len = vfs_read(fd, existing, MAX_FILE_SIZE);
            if (existing_len < 0) existing_len = 0;
            existing[existing_len] = '\0';
            vfs_close(fd);
        }
    }

    printf("┌── nano: %s ─────────────────────────────────┐\n", argv[1]);
    if (existing_len > 0)
        printf("│ Current content:\n%s\n", existing);
    else
        printf("│ (new file)\n");
    printf("│ Enter new content. Finish with '.' on its own line.\n");
    printf("│ Type 'q' to cancel.\n");
    printf("└──────────────────────────────────────────────┘\n");

    char new_content[MAX_FILE_SIZE] = {0};
    char line[1024];

    while (fgets(line, sizeof(line), stdin)) {
        /* Strip trailing newline for comparison */
        char stripped[1024];
        strncpy(stripped, line, sizeof(stripped) - 1);
        stripped[strcspn(stripped, "\r\n")] = '\0';

        if (strcmp(stripped, ".") == 0) break;
        if (strcmp(stripped, "q") == 0) { printf("nano: cancelled.\n"); return; }
        strncat(new_content, line, sizeof(new_content) - strlen(new_content) - 1);
    }

    /* Overwrite file */
    int fd = vfs_creat(argv[1], 0x6);
    if (fd < 0) { fprintf(stderr, "nano: cannot write to '%s'\n", argv[1]); return; }
    if (strlen(new_content) > 0)
        vfs_write(fd, new_content, (int)strlen(new_content));
    vfs_close(fd);
    printf("nano: '%s' saved (%zu bytes).\n", argv[1], strlen(new_content));
}

/* wc <file>  — word count: lines words chars */
static void cmd_wc(int argc, char *argv[])
{
    if (argc < 2) { fprintf(stderr, "Usage: wc <file>\n"); return; }

    int fd = vfs_open(argv[1], VFS_O_RDONLY);
    if (fd < 0) return;

    char buf[MAX_FILE_SIZE + 1] = {0};
    int  n = vfs_read(fd, buf, MAX_FILE_SIZE);
    vfs_close(fd);
    if (n < 0) n = 0;
    buf[n] = '\0';

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

/* grep <pattern> <file>  — print matching lines */
static void cmd_grep(int argc, char *argv[])
{
    if (argc < 3) { fprintf(stderr, "Usage: grep <pattern> <file>\n"); return; }

    int fd = vfs_open(argv[2], VFS_O_RDONLY);
    if (fd < 0) return;

    char buf[MAX_FILE_SIZE + 1] = {0};
    int  n = vfs_read(fd, buf, MAX_FILE_SIZE);
    vfs_close(fd);
    if (n < 0) n = 0;
    buf[n] = '\0';

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

/* stat <file>  — show inode details */
static void cmd_stat(int argc, char *argv[])
{
    if (argc < 2) { fprintf(stderr, "Usage: stat <file>\n"); return; }

    int idx = vfs_resolve_path(argv[1], cwd_inode);
    if (idx < 0) { fprintf(stderr, "stat: '%s': not found\n", argv[1]); return; }

    Inode n;
    vfs_read_inode(idx, &n);

    char perm[4] = "---";
    if (n.permissions & PERM_READ)  perm[0] = 'r';
    if (n.permissions & PERM_WRITE) perm[1] = 'w';
    if (n.permissions & PERM_EXEC)  perm[2] = 'x';

    printf("  File : %s\n",    n.name);
    printf("  Type : %s\n",    n.type == INODE_DIR ? "directory" : "regular file");
    printf("  Size : %u bytes\n", n.size);
    printf("  Perm : %s  (numeric: %u)\n", perm, n.permissions);
    printf(" Inode : #%d\n",   idx);
    printf("Parent : #%d\n",   n.parent);
}

/* debug  — print VFS internal state */
static void cmd_debug(void)
{
    vfs_debug();
}

/* tree [path] — draw a directory tree */
static void cmd_tree(int argc, char *argv[])
{
    int dir = cwd_inode;
    if (argc > 1) {
        dir = vfs_resolve_path(argv[1], cwd_inode);
        if (dir < 0) { fprintf(stderr, "tree: '%s': not found\n", argv[1]); return; }
    }
    int last_flags[MAX_INODES] = {0};
    vfs_tree(dir, 0, last_flags);
}

/* hexdump [bytes] — print raw memory */
static void cmd_hexdump(int argc, char *argv[])
{
    int len = 512;
    if (argc > 1) len = atoi(argv[1]);
    vfs_hexdump(len);
}

/* echo <text ...> — print text to terminal */
static void cmd_echo(int argc, char *argv[])
{
    for (int i = 1; i < argc; i++) {
        printf("%s", argv[i]);
        if (i < argc - 1) printf(" ");
    }
    printf("\n");
}

/* sh <script.sh>  — execute batch commands from a VFS file */
static void cmd_sh(int argc, char *argv[])
{
    if (argc < 2) { fprintf(stderr, "Usage: sh <script.sh>\n"); return; }

    int fd = vfs_open(argv[1], VFS_O_RDONLY);
    if (fd < 0) { fprintf(stderr, "sh: '%s': not found\n", argv[1]); return; }

    char buf[MAX_FILE_SIZE + 1] = {0};
    int  n = vfs_read(fd, buf, MAX_FILE_SIZE);
    vfs_close(fd);
    if (n <= 0) return;
    buf[n] = '\0';

    printf("[sh] Running '%s':\n", argv[1]);

    /* Manual line iteration — avoids strtok conflict with dispatch/parse_args */
    char *p = buf;
    while (*p) {
        /* Find end of this line */
        char *end = p;
        while (*end && *end != '\n') end++;

        /* Extract line into a local buffer */
        int  len = (int)(end - p);
        char line[MAX_INPUT];
        if (len >= MAX_INPUT) len = MAX_INPUT - 1;
        memcpy(line, p, len);
        line[len] = '\0';

        /* Advance past newline */
        p = (*end == '\n') ? end + 1 : end;

        /* Skip leading whitespace, blank lines, comments */
        char *trimmed = line;
        while (*trimmed == ' ' || *trimmed == '\t') trimmed++;
        if (!*trimmed || *trimmed == '#') continue;

        printf("  >> %s\n", trimmed);
        char copy[MAX_INPUT];
        strncpy(copy, trimmed, MAX_INPUT - 1);
        copy[MAX_INPUT - 1] = '\0';
        dispatch(copy);
    }
    printf("[sh] Done.\n");
}

/* help */
static void cmd_help(void)
{
    printf("\n─── VFS Shell Commands ─────────────────────────────────────\n");
    printf("  ls   [path]           list directory contents\n");
    printf("  cd   [path]           change directory\n");
    printf("  pwd                   print working directory\n");
    printf("  mkdir <name>          create directory\n");
    printf("  touch <file>          create empty file\n");
    printf("  rm    <file|dir>      remove file or empty directory\n");
    printf("  cat   <file>          display file contents\n");
    printf("  write [-a] <f> <txt>  write (or append) text to file\n");
    printf("  cp    <src> <dst>     copy file\n");
    printf("  mv    <src> <dst>     move / rename\n");
    printf("  chmod <0-7> <file>    change permissions (4=r 2=w 1=x)\n");
    printf("  nano  <file>          simple text editor\n");
    printf("  wc    <file>          word count (lines words bytes)\n");
    printf("  grep  <pat> <file>    search for pattern\n");
    printf("  stat  <file>          inode details\n");
    printf("  debug                 print VFS internal state (inodes & FDs)\n");
    printf("  tree  [path]          visual directory tree\n");
    printf("  hexdump [bytes]       print raw binary file bytes\n");
    printf("  echo  <text>          print text to terminal\n");
    printf("  sh    <script.sh>     run a .sh script from the VFS\n");
    printf("  help                  this message\n");
    printf("  exit                  quit\n");
    printf("────────────────────────────────────────────────────────────\n\n");
}

/* ═══════════════════════════════════════════════════════════
 * Central dispatcher
 * ═══════════════════════════════════════════════════════════ */

static void dispatch(char *line)
{
    /* Strip trailing newline / CR */
    line[strcspn(line, "\r\n")] = '\0';
    if (!line[0]) return;

    char  *argv[MAX_ARGS];
    int    argc = parse_args(line, argv, MAX_ARGS);
    if (argc == 0) return;

    char *cmd = argv[0];

    if      (strcmp(cmd, "ls")    == 0) cmd_ls   (argc, argv);
    else if (strcmp(cmd, "cd")    == 0) cmd_cd   (argc, argv);
    else if (strcmp(cmd, "pwd")   == 0) cmd_pwd  ();
    else if (strcmp(cmd, "mkdir") == 0) cmd_mkdir(argc, argv);
    else if (strcmp(cmd, "touch") == 0) cmd_touch(argc, argv);
    else if (strcmp(cmd, "rm")    == 0) cmd_rm   (argc, argv);
    else if (strcmp(cmd, "cat")   == 0) cmd_cat  (argc, argv);
    else if (strcmp(cmd, "write") == 0) cmd_write(argc, argv);
    else if (strcmp(cmd, "cp")    == 0) cmd_cp   (argc, argv);
    else if (strcmp(cmd, "mv")    == 0) cmd_mv   (argc, argv);
    else if (strcmp(cmd, "chmod") == 0) cmd_chmod(argc, argv);
    else if (strcmp(cmd, "nano")  == 0) cmd_nano (argc, argv);
    else if (strcmp(cmd, "wc")    == 0) cmd_wc   (argc, argv);
    else if (strcmp(cmd, "grep")  == 0) cmd_grep (argc, argv);
    else if (strcmp(cmd, "stat")  == 0) cmd_stat (argc, argv);
    else if (strcmp(cmd, "debug") == 0) cmd_debug();
    else if (strcmp(cmd, "tree")  == 0) cmd_tree (argc, argv);
    else if (strcmp(cmd, "hexdump")== 0) cmd_hexdump(argc, argv);
    else if (strcmp(cmd, "echo")  == 0) cmd_echo (argc, argv);
    else if (strcmp(cmd, "sh")    == 0) cmd_sh   (argc, argv);
    else if (strcmp(cmd, "help")  == 0) cmd_help ();
    else if (strcmp(cmd, "exit")  == 0 || strcmp(cmd, "quit") == 0) {
        vfs_shutdown();
        printf("Goodbye.\n");
        exit(0);
    }
    else {
        fprintf(stderr, "%s: command not found  (type 'help' for commands)\n", cmd);
    }
}

/* ═══════════════════════════════════════════════════════════
 * main()
 * ═══════════════════════════════════════════════════════════ */

int main(void)
{
#ifdef _WIN32
    /* Set console to UTF-8 so box-drawing characters render correctly */
    SetConsoleOutputCP(65001);
    SetConsoleCP(65001);
#endif
    printf("╔══════════════════════════════════════════════╗\n");
    printf("║   Virtual Filesystem with System Calls       ║\n");
    printf("║   Container: %-31s ║\n", CONTAINER_FILE);
    printf("╚══════════════════════════════════════════════╝\n");
    printf("Type 'help' to list commands.\n\n");

    if (vfs_init() < 0) {
        fprintf(stderr, "Fatal: could not initialise VFS\n");
        return 1;
    }

    char input[MAX_INPUT];
    char cwd[MAX_PATH];

    while (1) {
        get_cwd_string(cwd, MAX_PATH);
        printf("vfs:%s$ ", cwd);
        fflush(stdout);

        if (!fgets(input, sizeof(input), stdin)) {
            printf("\n");
            break;
        }
        dispatch(input);
    }

    vfs_shutdown();
    return 0;
}
