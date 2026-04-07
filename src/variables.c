#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "variables.h"
#include "io_helpers.h"

// Linked list node structure
typedef struct Variable {
    char *key;
    char *value;
    struct Variable *next;
} Variable;

static Variable *head = NULL;

void add_variable(const char *key, const char *value) {
    // Search for existing variable to update
    Variable *curr = head;
    while (curr != NULL) {
        if (strcmp(curr->key, key) == 0) {
            // Update existing value
            free(curr->value); // Free old value
            curr->value = strdup(value);
            return;
        }
        curr = curr->next;
    }

    // Create new variable if not found
    Variable *new_node = malloc(sizeof(Variable));
    if (new_node == NULL) {
        perror("malloc failed");
        return;
    }
    new_node->key = strdup(key);
    new_node->value = strdup(value);
    new_node->next = head;
    head = new_node;
}

char *get_variable(const char *key) {
    Variable *curr = head;
    while (curr != NULL) {
        if (strcmp(curr->key, key) == 0) {
            return curr->value;
        }
        curr = curr->next;
    }
    // Spec: Referencing an undefined variable results in the empty string
    return ""; 
}

void expand_input(char *dest, const char *src, size_t max_len) {
    size_t src_idx = 0;
    size_t dest_idx = 0;
    size_t token_len = 0;  /* chars written in the current whitespace-delimited token */

    while (src[src_idx] != '\0' && dest_idx < max_len) {
        if (src[src_idx] == '$') {
            src_idx++; /* skip '$' */

            /* Extract variable name */
            char var_name[129];
            size_t name_len = 0;
            while (src[src_idx] != '\0' &&
                   src[src_idx] != ' '  &&
                   src[src_idx] != '\t' &&
                   src[src_idx] != '\n' &&
                   src[src_idx] != '$') {
                if (name_len < 128) var_name[name_len++] = src[src_idx];
                src_idx++;
            }
            var_name[name_len] = '\0';

            /* Append expanded value, enforcing per-token 128-char limit */
            char *val = get_variable(var_name);
            for (size_t k = 0; val[k] != '\0'; k++) {
                char c = val[k];
                if (c == ' ' || c == '\t' || c == '\n') {
                    token_len = 0;
                    if (dest_idx < max_len) dest[dest_idx++] = c;
                } else {
                    if (token_len < (size_t)MAX_STR_LEN) {
                        if (dest_idx < max_len) dest[dest_idx++] = c;
                        token_len++;
                    }
                    /* else: token exceeds 128 chars — truncate (skip char) */
                }
            }
        } else {
            char c = src[src_idx++];
            if (c == ' ' || c == '\t' || c == '\n') {
                token_len = 0;
                if (dest_idx < max_len) dest[dest_idx++] = c;
            } else {
                if (token_len < (size_t)MAX_STR_LEN) {
                    if (dest_idx < max_len) dest[dest_idx++] = c;
                    token_len++;
                }
            }
        }
    }

    if (dest_idx < max_len) dest[dest_idx] = '\0';
    else dest[max_len] = '\0';
}