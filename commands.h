#ifndef __COMMANDS_H__
#define __COMMANDS_H__

#include <sys/types.h>

typedef enum {
    CMD_STATUS_OK = 0,
    CMD_STATUS_EXIT = 1,
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

#endif
