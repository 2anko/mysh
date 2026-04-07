#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <limits.h>
#include <signal.h>
#include <errno.h>

#include "builtins.h"
#include "commands.h"
#include "io_helpers.h"


/* ====== Command lookup =====  */

/* Return function pointer for builtin name, or NULL if not found */
bn_ptr check_builtin(const char *cmd) {
    ssize_t cmd_num = 0;
    while (cmd_num < BUILTINS_COUNT &&
           strncmp(BUILTINS[cmd_num], cmd, MAX_STR_LEN) != 0) {
        cmd_num += 1;
    }
    return BUILTINS_FN[cmd_num];  /* NULL sentinel at end covers not-found */
}


/* ===== echo ===== */

ssize_t bn_echo(char **tokens) {
    ssize_t index = 1;

    while (tokens[index] != NULL) {
        display_message(tokens[index]);
        if (tokens[index + 1] != NULL)
            display_message(" ");
        index += 1;
    }
    display_message("\n");
    return 0;
}


/* ===== ls helpers ===== */

static int ls_dir(const char *path, int show_hidden, const char *filter,
                  int recursive, int max_depth, int cur_depth) {
    DIR *dir = opendir(path);
    if (dir == NULL) return -1;

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        char *name = entry->d_name;

        /* Always show . and .., but hide other dotfiles unless --a */
        int is_dot_entry = (strcmp(name, ".") == 0 || strcmp(name, "..") == 0);
        if (!is_dot_entry && !show_hidden && name[0] == '.') continue;

        int display = (filter == NULL || strstr(name, filter) != NULL);
        if (display) {
            display_message(name);
            display_message("\n");
        }

        if (recursive && !is_dot_entry && (max_depth < 0 || cur_depth < max_depth)) {
            char subpath[MAX_STR_LEN + 1];
            int written = snprintf(subpath, sizeof(subpath), "%s/%s", path, name);
            if (written < 0 || written >= (int)sizeof(subpath)) continue;

            struct stat st;
            if (stat(subpath, &st) == 0 && S_ISDIR(st.st_mode)) {
                ls_dir(subpath, show_hidden, filter, recursive,
                       max_depth, cur_depth + 1);
            }
        }
    }

    closedir(dir);
    return 0;
}

int expand_dots(const char *token, char *out_buf, size_t out_size); /* forward decl */

ssize_t bn_ls(char **tokens) {
    const char *path   = ".";
    int  show_hidden   = 0;
    const char *filter = NULL;
    int  recursive     = 0;
    int  depth_set     = 0;
    int  max_depth     = -1;
    int  path_set      = 0;
    int  i             = 1;

    while (tokens[i] != NULL) {
        if (strcmp(tokens[i], "--a") == 0) {
            show_hidden = 1;
            i++;
        } else if (strcmp(tokens[i], "--f") == 0) {
            i++;
            if (tokens[i] == NULL) {
                display_error("ERROR: Builtin failed: ", "ls");
                return -1;
            }
            filter = tokens[i];
            i++;
        } else if (strcmp(tokens[i], "--rec") == 0) {
            recursive = 1;
            i++;
        } else if (strcmp(tokens[i], "--d") == 0) {
            i++;
            if (tokens[i] == NULL) {
                display_error("ERROR: Builtin failed: ", "ls");
                return -1;
            }
            max_depth = atoi(tokens[i]);
            depth_set = 1;
            i++;
        } else {
            if (path_set) {
                display_error("ERROR: Too many arguments: ",
                              "ls takes a single path");
                return -1;
            }
            path = tokens[i];
            path_set = 1;
            i++;
        }
    }

    if (depth_set && !recursive) {
        display_error("ERROR: Builtin failed: ", "ls");
        return -1;
    }

    /* Apply dot expansion to path (same as cd) */
    char dot_expanded[MAX_STR_LEN + 1];
    if (expand_dots(path, dot_expanded, sizeof(dot_expanded)))
        path = dot_expanded;

    if (ls_dir(path, show_hidden, filter, recursive, max_depth, 0) < 0) {
        display_error("ERROR: Invalid path", "");
        return -1;
    }
    return 0;
}


/* ===== cd ===== */

int expand_dots(const char *token, char *out_buf, size_t out_size) {
    size_t len = strlen(token);
    if (len == 0) return 0;

    for (size_t k = 0; k < len; k++) {
        if (token[k] != '.') return 0;
    }

    if (len == 1) {
        strncpy(out_buf, ".", out_size);
        out_buf[out_size - 1] = '\0';
        return 1;
    }

    size_t n = len - 1;
    out_buf[0] = '\0';
    for (size_t k = 0; k < n; k++) {
        if (k > 0) strncat(out_buf, "/", out_size - strlen(out_buf) - 1);
        strncat(out_buf, "..", out_size - strlen(out_buf) - 1);
    }
    return 1;
}

ssize_t bn_cd(char **tokens) {
    const char *target = NULL;
    int argc = 0;
    while (tokens[argc + 1] != NULL) argc++;

    if (argc > 1) {
        display_error("ERROR: Too many arguments: ", "cd takes a single path");
        return -1;
    }

    if (argc == 0) {
        target = getenv("HOME");
        if (target == NULL) target = "/";
    } else {
        target = tokens[1];
    }

    char dot_expanded[MAX_STR_LEN + 1];
    if (expand_dots(target, dot_expanded, sizeof(dot_expanded)))
        target = dot_expanded;

    if (chdir(target) != 0) {
        display_error("ERROR: Invalid path", "");
        return -1;
    }
    return 0;
}


/* ===== cat ===== */

/*
 * If a filename is given, open and print it (as before).
 * If no filename and stdin is not a tty (i.e. it is a pipe or redirect),
 * read from stdin.
 * Otherwise: ERROR: No input source provided.
 */
ssize_t bn_cat(char **tokens) {
    int fd = -1;
    int close_fd = 0;

    if (tokens[1] != NULL) {
        /* Filename provided */
        if (tokens[2] != NULL) {
            display_error("ERROR: Too many arguments: ",
                          "cat takes a single file");
            return -1;
        }
        fd = open(tokens[1], O_RDONLY);
        if (fd < 0) {
            display_error("ERROR: Cannot open file", "");
            return -1;
        }
        close_fd = 1;
    } else {
        /* No filename: use stdin only if it is not a terminal */
        if (isatty(STDIN_FILENO)) {
            display_error("ERROR: No input source provided", "");
            return -1;
        }
        fd = STDIN_FILENO;
    }

    char buf[1024];
    ssize_t bytes;
    while ((bytes = read(fd, buf, sizeof(buf))) > 0) {
        write(STDOUT_FILENO, buf, (size_t)bytes);
    }

    if (close_fd) close(fd);
    return 0;
}


/* ===== wc ===== */

/*
 * Same stdin-fallback logic as cat.
 */
ssize_t bn_wc(char **tokens) {
    int fd = -1;
    int close_fd = 0;

    if (tokens[1] != NULL) {
        if (tokens[2] != NULL) {
            display_error("ERROR: Too many arguments: ",
                          "wc takes a single file");
            return -1;
        }
        fd = open(tokens[1], O_RDONLY);
        if (fd < 0) {
            display_error("ERROR: Cannot open file", "");
            return -1;
        }
        close_fd = 1;
    } else {
        if (isatty(STDIN_FILENO)) {
            display_error("ERROR: No input source provided", "");
            return -1;
        }
        fd = STDIN_FILENO;
    }

    long word_count    = 0;
    long char_count    = 0;
    long newline_count = 0;
    int  in_word       = 0;

    char c;
    ssize_t r;
    while ((r = read(fd, &c, 1)) == 1) {
        char_count++;
        if (c == '\n') newline_count++;

        int is_space = (c == ' ' || c == '\t' || c == '\n' || c == '\r');
        if (is_space) {
            if (in_word) { word_count++; in_word = 0; }
        } else {
            in_word = 1;
        }
    }
    if (in_word) word_count++;

    if (close_fd) close(fd);

    char out[64];
    int n;
    n = snprintf(out, sizeof(out), "word count %ld\n", word_count);
    write(STDOUT_FILENO, out, (size_t)n);
    n = snprintf(out, sizeof(out), "character count %ld\n", char_count);
    write(STDOUT_FILENO, out, (size_t)n);
    n = snprintf(out, sizeof(out), "newline count %ld\n", newline_count);
    write(STDOUT_FILENO, out, (size_t)n);

    return 0;
}


/* ===== kill ===== */

/*
 * kill [pid] [signum]
 *   Sends signal signum (default SIGTERM) to process pid.
 */
ssize_t bn_kill(char **tokens) {
    if (tokens[1] == NULL) {
        display_error("ERROR: Builtin failed: ", "kill");
        return -1;
    }

    /* Parse pid */
    char *pid_end;
    long pid_l = strtol(tokens[1], &pid_end, 10);
    if (*pid_end != '\0') {
        display_error("ERROR: The process does not exist", "");
        return -1;
    }
    pid_t pid = (pid_t)pid_l;

    /* Parse optional signal number */
    int signum = SIGTERM;
    if (tokens[2] != NULL) {
        char *sig_end;
        long sig_l = strtol(tokens[2], &sig_end, 10);
        if (*sig_end != '\0' || sig_l <= 0 || sig_l >= 64) {
            display_error("ERROR: Invalid signal specified", "");
            return -1;
        }
        signum = (int)sig_l;
    }

    /* Check process exists */
    if (kill(pid, 0) != 0) {
        if (errno == ESRCH) {
            display_error("ERROR: The process does not exist", "");
        } else if (errno == EINVAL) {
            display_error("ERROR: Invalid signal specified", "");
        } else {
            display_error("ERROR: The process does not exist", "");
        }
        return -1;
    }

    /* Send signal */
    if (kill(pid, signum) != 0) {
        if (errno == ESRCH) {
            display_error("ERROR: The process does not exist", "");
        } else if (errno == EINVAL) {
            display_error("ERROR: Invalid signal specified", "");
        } else {
            display_error("ERROR: Builtin failed: ", "kill");
        }
        return -1;
    }

    return 0;
}


/* ===== ps ===== */

/*
 * List processes launched by this shell that are still running.
 * Delegates to list_bg_jobs() in commands.c.
 */
ssize_t bn_ps(char **tokens) {
    (void)tokens;
    list_bg_jobs();
    return 0;
}