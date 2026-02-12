#ifndef __VARIABLES_H__
#define __VARIABLES_H__
#include <stddef.h>

/*
 * Adds a new variable or updates an existing one.
 * Memory for key and value is allocated on the heap.
 */
void add_variable(const char *key, const char *value);

/*
 * Retrieves the value of a variable.
 * Returns the value string if found, or an empty string "" if not found.
 * The returned string should not be modified or freed by the caller.
 */
char *get_variable(const char *key);

/*
 * Expands variables in the source string and writes the result to dest.
 * - Replaces $var with its value.
 * - Handles the max length constraint (truncates if necessary).
 */
void expand_input(char *dest, const char *src, size_t max_len);

#endif