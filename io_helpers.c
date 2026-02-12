#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdio.h>

#include "io_helpers.h"


// ===== Output helpers =====

/* Prereq: str is a NULL terminated string
 */
void display_message(char *str) {
    write(STDOUT_FILENO, str, strnlen(str, MAX_STR_LEN));
}


/* Prereq: pre_str, str are NULL terminated string
 */
void display_error(char *pre_str, char *str) {
    write(STDERR_FILENO, pre_str, strnlen(pre_str, MAX_STR_LEN));
    write(STDERR_FILENO, str, strnlen(str, MAX_STR_LEN));
    write(STDERR_FILENO, "\n", 1);
}


// ===== Input tokenizing =====

/* Prereq: in_ptr points to a character buffer of size > MAX_STR_LEN
 * Return: number of bytes read
 */
ssize_t get_input(char *in_ptr) {
    ssize_t total = 0;
    char c;

    while (1) {
        ssize_t r = read(STDIN_FILENO, &c, 1);

        if (r == 0) { // EOF
            if (total == 0) {
                in_ptr[0] = '\0';
                return 0;
            }
            in_ptr[total] = '\0';
            return total;
        }

        if (r < 0) { // read error
            in_ptr[0] = '\0';
            return -1;
        }

        if (c == '\n') { // end of line
            in_ptr[total] = '\0';
            return total + 1;
        }

        if (total >= MAX_STR_LEN) {
            write(STDERR_FILENO, "ERROR: input line too long\n",
                  strlen("ERROR: input line too long\n"));

            // discard rest of the line
            while (c != '\n' && (r = read(STDIN_FILENO, &c, 1)) > 0) {
                if (c == '\n') break;
            }

            in_ptr[0] = '\0';
            return -1;
        }

        in_ptr[total++] = c;
    }
}

/* Prereq: in_ptr is a string, tokens is of size >= len(in_ptr)
 * Warning: in_ptr is modified
 * Return: number of tokens.
 */
size_t tokenize_input(char *in_ptr, char **tokens) {
    char *curr_ptr = strtok (in_ptr, DELIMITERS);
    size_t token_count = 0;

    while (curr_ptr != NULL) {
        tokens[token_count++] = curr_ptr;
        curr_ptr = strtok(NULL, DELIMITERS);
    }
    tokens[token_count] = NULL;
    return token_count;
}
