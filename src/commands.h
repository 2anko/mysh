#ifndef __COMMANDS_H__
#define __COMMANDS_H__

#include <sys/types.h>

/* Foreground child pids exposed for the SIGINT handler in mysh.c */
#define MAX_PIPELINE_SEGS_H 32
extern volatile pid_t fg_child_pids[MAX_PIPELINE_SEGS_H];
extern volatile int   fg_child_count;

typedef enum {
    CMD_STATUS_OK    =  0,
    CMD_STATUS_EXIT  =  1,
    CMD_STATUS_ERROR = -1,
} cmd_status_t;

/*
 * Dispatch a tokenized input line.
 *
 * Prereq: tokens is a NULL-terminated array of C strings.
 *         tokens[0] may be NULL (empty input).
 *
 * Return:
 *   CMD_STATUS_OK   : command handled successfully (or empty input)
 *   CMD_STATUS_EXIT : caller should terminate the shell with exit code 0
 *   CMD_STATUS_ERROR: an error occurred (message already printed to stderr)
 */
cmd_status_t dispatch_command(char **tokens);

/*
 * Check for completed background jobs and print "[N]+ Done <cmd>" for each.
 * Call this before displaying the shell prompt.
 */
void check_and_display_done_jobs(void);

/*
 * Print the list of active background jobs (used by the ps builtin).
 * Format per line: "<cmd_name> <pid>"
 * Printed from most-recently-launched to oldest.
 */
void list_bg_jobs(void);

#endif