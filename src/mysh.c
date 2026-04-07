#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <sys/types.h>
#include <unistd.h>
#include <errno.h>

#include "builtins.h"
#include "io_helpers.h"
#include "commands.h"
#include "variables.h"

/* Explicit extern declarations so the signal handler can see these
 * even when IntelliSense doesn't resolve commands.h correctly. */
extern volatile pid_t fg_child_pids[];
extern volatile int   fg_child_count;

/* SIGINT handler: kill any foreground children then write a newline so the
 * main loop re-displays the prompt (get_input returns -1 on EINTR). */
static void sigint_handler(int sig) {
    (void)sig;
    /* Forward SIGINT to every foreground child */
    int count = fg_child_count;
    for (int i = 0; i < count; i++) {
        pid_t p = fg_child_pids[i];
        if (p > 0) kill(p, SIGINT);
    }
    /* Write a newline so the terminal looks clean and readline() in the
     * test harness receives a complete line, unblocking read_stdout(). */
    write(STDOUT_FILENO, "\n", 1);
}

int main(__attribute__((unused)) int argc,
         __attribute__((unused)) char *argv[]) {

    char *prompt = "mysh$ ";

    /* Install SIGINT handler without SA_RESTART so that read() in
     * get_input is interrupted (returns EINTR) when the signal fires.
     * The main loop then re-displays the prompt. */
    struct sigaction sa;
    sa.sa_handler = sigint_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;   /* no SA_RESTART */
    sigaction(SIGINT, &sa, NULL);

    char input_buf[MAX_STR_LEN + 1];
    input_buf[MAX_STR_LEN] = '\0';

    /* Expanded output can be much larger than the raw input (variable
     * substitution), so allocate a generous buffer.  Per-token truncation
     * at MAX_STR_LEN is enforced inside expand_input. */
#define EXPANDED_BUF_SIZE 4096
    char expanded_buf[EXPANDED_BUF_SIZE];
    expanded_buf[EXPANDED_BUF_SIZE - 1] = '\0';

    char *token_arr[MAX_STR_LEN] = {NULL};

    while (1) {
        /* Reap finished background jobs and announce them */
        check_and_display_done_jobs();

        display_message(prompt);

        int ret = get_input(input_buf);

        if (ret == 0) break;   /* EOF */
        if (ret == -1) continue;

        /* ---- Variable assignment detection (before expansion) ---- */
        char temp_buf[MAX_STR_LEN + 1];
        strncpy(temp_buf, input_buf, MAX_STR_LEN + 1);
        char *temp_tokens[MAX_STR_LEN] = {NULL};
        size_t temp_count = tokenize_input(temp_buf, temp_tokens);

        /* Only treat as assignment if no pipe in the command */
        int has_pipe_in_cmd = 0;
        for (size_t j = 0; j < temp_count; j++) {
            if (strcmp(temp_tokens[j], "|") == 0) { has_pipe_in_cmd = 1; break; }
        }
        if (!has_pipe_in_cmd && temp_count > 0) {
            char *eq_pos = strchr(temp_tokens[0], '=');
            if (eq_pos != NULL) {
                *eq_pos = '\0';
                char *key = temp_tokens[0];
                char *val = eq_pos + 1;  /* everything after first '=' is the value */
                add_variable(key, val);
                continue;
            }
        }

        /* ---- Variable expansion ---- */
        expand_input(expanded_buf, input_buf, EXPANDED_BUF_SIZE - 1);

        size_t token_count = tokenize_input(expanded_buf, token_arr);
        if (token_count == 0) continue;

        if (strcmp(token_arr[0], "exit") == 0) break;

        /* ---- Command dispatch ---- */
        cmd_status_t status = dispatch_command(token_arr);
        if (status == CMD_STATUS_EXIT) break;
    }

    return 0;
}