#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/wait.h>
#include <signal.h>
#include <fcntl.h>
#include <errno.h>

#include "commands.h"
#include "builtins.h"
#include "io_helpers.h"
#include "network.h"

/* ===================================================================
 * Background-job table
 * =================================================================== */

#define MAX_BG_JOBS      64
#define MAX_PIPELINE_SEGS 32

/* Foreground child pids – written by execute_pipeline, read by SIGINT handler */
volatile pid_t fg_child_pids[MAX_PIPELINE_SEGS];
volatile int   fg_child_count = 0;

typedef struct {
    int    number;           /* job number shown to user          */
    pid_t  pid;              /* pid of last process in pipeline   */
    char   name[MAX_STR_LEN + 1]; /* first token  (for ps output) */
    char   full[MAX_STR_LEN + 1]; /* full command (for Done msg)  */
    int    active;
} BgJob;

static BgJob bg_jobs[MAX_BG_JOBS];
static int   next_job_num = 1;

/* ------------------------------------------------------------------
 * check_and_display_done_jobs
 *   Reap any finished background children and print Done messages.
 *   Called from the main loop just before the prompt is displayed.
 * ------------------------------------------------------------------ */
void check_and_display_done_jobs(void) {
    for (int i = 0; i < MAX_BG_JOBS; i++) {
        if (!bg_jobs[i].active) continue;
        int status;
        pid_t r = waitpid(bg_jobs[i].pid, &status, WNOHANG);
        if (r > 0) {
            char msg[MAX_STR_LEN + 64];
            snprintf(msg, sizeof(msg), "[%d]+ Done %s\n",
                     bg_jobs[i].number, bg_jobs[i].full);
            display_message(msg);
            bg_jobs[i].active = 0;
        }
    }
}

/* ------------------------------------------------------------------
 * list_bg_jobs  (called by bn_ps)
 *   Print active jobs, most-recently-launched first.
 *   Format: "<name> <pid>"
 * ------------------------------------------------------------------ */
void list_bg_jobs(void) {
    /* find highest active job number */
    int max_num = 0;
    for (int i = 0; i < MAX_BG_JOBS; i++) {
        if (bg_jobs[i].active && bg_jobs[i].number > max_num)
            max_num = bg_jobs[i].number;
    }
    for (int n = max_num; n >= 1; n--) {
        for (int i = 0; i < MAX_BG_JOBS; i++) {
            if (bg_jobs[i].active && bg_jobs[i].number == n) {
                char msg[MAX_STR_LEN + 32];
                snprintf(msg, sizeof(msg), "%s %d\n",
                         bg_jobs[i].name, (int)bg_jobs[i].pid);
                display_message(msg);
                break;
            }
        }
    }
}

/* ===================================================================
 * Internal helpers
 * =================================================================== */

/* Reconstruct a space-separated string from a NULL-terminated token array. */
static void tokens_to_str(char **tokens, char *buf, size_t buflen) {
    buf[0] = '\0';
    for (int i = 0; tokens[i] != NULL; i++) {
        if (i > 0)
            strncat(buf, " ", buflen - strlen(buf) - 1);
        strncat(buf, tokens[i], buflen - strlen(buf) - 1);
    }
}

/*
 * parse_pipeline
 *   Splits tokens[] on "|" tokens.  Each "|" is replaced with NULL to
 *   null-terminate the preceding segment.
 *   seg_starts[i] is set to point to the beginning of segment i.
 *   Returns number of segments (>= 1 if tokens[0] != NULL).
 */
static int parse_pipeline(char **tokens, char **seg_starts[], int max_segs) {
    if (tokens[0] == NULL) return 0;

    int nseg = 0;
    seg_starts[nseg++] = tokens;

    for (int i = 0; tokens[i] != NULL; i++) {
        if (strcmp(tokens[i], "|") == 0) {
            tokens[i] = NULL;          /* null-terminate previous segment */
            if (nseg < max_segs)
                seg_starts[nseg++] = &tokens[i + 1];
        }
    }
    return nseg;
}

/*
 * exec_segment
 *   Runs a single command in the current process.
 *   First tries builtins; if not found, tries /bin/<name> then /usr/bin/<name>.
 *   Called only from child processes – does not return (calls _exit).
 */
static void exec_segment(char **tokens) {
    if (tokens == NULL || tokens[0] == NULL) _exit(0);

    /* Try builtin */
    bn_ptr fn = check_builtin(tokens[0]);
    if (fn != NULL) {
        ssize_t r = fn(tokens);
        _exit(r >= 0 ? 0 : 1);
    }

    /* Try /bin/<name> */
    char path[256];
    snprintf(path, sizeof(path), "/bin/%s", tokens[0]);
    execv(path, tokens);

    /* Try /usr/bin/<name> */
    snprintf(path, sizeof(path), "/usr/bin/%s", tokens[0]);
    execv(path, tokens);

    /* Not found */
    display_error("ERROR: Unknown command: ", tokens[0]);
    _exit(1);
}

/*
 * execute_pipeline
 *   Forks one child per pipeline segment, wires pipes between them,
 *   and either waits (foreground) or records the job (background).
 */
static cmd_status_t execute_pipeline(char **seg_starts[], int nseg,
                                     int is_bg,
                                     const char *cmd_name,
                                     const char *full_cmd) {
    if (nseg == 0) return CMD_STATUS_OK;

    /* Create nseg-1 pipes */
    int pipe_fds[MAX_PIPELINE_SEGS][2];
    for (int i = 0; i < nseg - 1; i++) {
        if (pipe(pipe_fds[i]) < 0) {
            display_error("ERROR: pipe failed", "");
            return CMD_STATUS_ERROR;
        }
    }

    pid_t child_pids[MAX_PIPELINE_SEGS];

    for (int i = 0; i < nseg; i++) {
        pid_t pid = fork();
        if (pid < 0) {
            display_error("ERROR: fork failed", "");
            /* close remaining unopened pipes */
            for (int j = i; j < nseg - 1; j++) {
                close(pipe_fds[j][0]);
                close(pipe_fds[j][1]);
            }
            return CMD_STATUS_ERROR;
        }

        if (pid == 0) {
            /* ---- child ---- */

            /* Restore SIGINT to default so ctrl+c kills this child */
            signal(SIGINT, SIG_DFL);

            if (is_bg) {
                /* Put in a new process group so terminal ctrl+c doesn't reach it */
                setpgrp();
                /* Redirect stdin from /dev/null (first segment only; later
                   segments get stdin from the pipe below) */
                if (i == 0) {
                    int devnull = open("/dev/null", O_RDONLY);
                    if (devnull >= 0) {
                        dup2(devnull, STDIN_FILENO);
                        close(devnull);
                    }
                }
            }

            /* Wire up stdin from previous pipe output (overrides /dev/null
               for non-first background segments too) */
            if (i > 0) {
                dup2(pipe_fds[i - 1][0], STDIN_FILENO);
            }

            /* Wire up stdout to next pipe input */
            if (i < nseg - 1) {
                dup2(pipe_fds[i][1], STDOUT_FILENO);
            }

            /* Close all pipe descriptors in child */
            for (int j = 0; j < nseg - 1; j++) {
                close(pipe_fds[j][0]);
                close(pipe_fds[j][1]);
            }

            exec_segment(seg_starts[i]);
            _exit(1); /* unreachable */
        }

        /* parent: record child pid */
        child_pids[i] = pid;
    }

    /* Parent: close all pipe fds */
    for (int i = 0; i < nseg - 1; i++) {
        close(pipe_fds[i][0]);
        close(pipe_fds[i][1]);
    }

    if (is_bg) {
        /* Find a free slot */
        /* Reset job counter if no active jobs exist (matches bash behaviour) */
        int any_active = 0;
        for (int i = 0; i < MAX_BG_JOBS; i++) {
            if (bg_jobs[i].active) { any_active = 1; break; }
        }
        if (!any_active) next_job_num = 1;

        int slot = -1;
        for (int i = 0; i < MAX_BG_JOBS; i++) {
            if (!bg_jobs[i].active) { slot = i; break; }
        }
        if (slot >= 0) {
            bg_jobs[slot].active = 1;
            bg_jobs[slot].number = next_job_num++;
            bg_jobs[slot].pid    = child_pids[nseg - 1]; /* last process in pipeline */
            strncpy(bg_jobs[slot].name, cmd_name, MAX_STR_LEN);
            bg_jobs[slot].name[MAX_STR_LEN] = '\0';
            strncpy(bg_jobs[slot].full, full_cmd, MAX_STR_LEN);
            bg_jobs[slot].full[MAX_STR_LEN] = '\0';

            char msg[32];
            snprintf(msg, sizeof(msg), "[%d] %d\n",
                     bg_jobs[slot].number, (int)child_pids[nseg - 1]);
            display_message(msg);
        }
        return CMD_STATUS_OK;
    }

    /* Foreground: register pids so SIGINT handler can kill them */
    fg_child_count = nseg;
    for (int i = 0; i < nseg; i++) fg_child_pids[i] = child_pids[i];

    for (int i = 0; i < nseg; i++) {
        int status;
        while (waitpid(child_pids[i], &status, 0) == -1 && errno == EINTR)
            ; /* retry if interrupted by signal */
    }

    fg_child_count = 0;

    return CMD_STATUS_OK;
}

/* ===================================================================
 * dispatch_command – public entry point
 * =================================================================== */
cmd_status_t dispatch_command(char **tokens) {
    if (tokens == NULL || tokens[0] == NULL)
        return CMD_STATUS_OK;

    if (strcmp(tokens[0], "exit") == 0)
        return CMD_STATUS_EXIT;

    /* ---- Detect trailing & (background) ---- */
    int is_bg = 0;
    int ntok  = 0;
    while (tokens[ntok] != NULL) ntok++;

    if (ntok > 0 && strcmp(tokens[ntok - 1], "&") == 0) {
        is_bg = 1;
        tokens[ntok - 1] = NULL;
        ntok--;
    }

    if (ntok == 0) return CMD_STATUS_OK;

    /* ---- Detect pipes ---- */
    int has_pipe = 0;
    for (int i = 0; tokens[i] != NULL; i++) {
        if (strcmp(tokens[i], "|") == 0) { has_pipe = 1; break; }
    }

    /* ---- Builtins that must run in the shell's own process ---- */
    /* cd, start-server, close-server all mutate shell-process state  */
    /* (cwd or server_pid) and must never run inside a forked child.  */
    if (!is_bg && !has_pipe) {
        if (strcmp(tokens[0], "cd") == 0) {
            ssize_t r = bn_cd(tokens);
            return r >= 0 ? CMD_STATUS_OK : CMD_STATUS_ERROR;
        }
        if (strcmp(tokens[0], "start-server") == 0) {
            ssize_t r = bn_start_server(tokens);
            return r >= 0 ? CMD_STATUS_OK : CMD_STATUS_ERROR;
        }
        if (strcmp(tokens[0], "close-server") == 0) {
            ssize_t r = bn_close_server(tokens);
            return r >= 0 ? CMD_STATUS_OK : CMD_STATUS_ERROR;
        }
    }

    /*
     * Build full command string BEFORE parse_pipeline() modifies tokens
     * (it replaces "|" tokens with NULL).
     */
    char full_cmd[MAX_STR_LEN + 1];
    tokens_to_str(tokens, full_cmd, sizeof(full_cmd));

    /* ---- Parse pipeline ---- */
    char **seg_starts[MAX_PIPELINE_SEGS];
    int nseg = parse_pipeline(tokens, seg_starts, MAX_PIPELINE_SEGS);

    if (nseg == 0) return CMD_STATUS_OK;

    return execute_pipeline(seg_starts, nseg, is_bg, tokens[0], full_cmd);
}