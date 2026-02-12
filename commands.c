#include <string.h>

#include "commands.h"
#include "builtins.h"
#include "io_helpers.h"

cmd_status_t dispatch_command(char **tokens) {
    // Defensive: never crash regardless of input.
    if (tokens == NULL || tokens[0] == NULL) {
        return CMD_STATUS_OK; // empty input
    }

    // "exit" must match exactly (do not treat "exitnotreally" as exit).
    if (strcmp(tokens[0], "exit") == 0) {
        return CMD_STATUS_EXIT;
    }

    // Try builtin.
    bn_ptr builtin_fn = check_builtin(tokens[0]);
    if (builtin_fn != NULL) {
        ssize_t err = builtin_fn(tokens);
        if (err < 0) {
            display_error("ERROR: Builtin failed: ", tokens[0]);
            return CMD_STATUS_ERROR;
        }
        return CMD_STATUS_OK;
    }

    // Unknown command.
    display_error("ERROR: Unknown command: ", tokens[0]);
    return CMD_STATUS_ERROR;
}
