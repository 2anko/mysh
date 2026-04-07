#ifndef __BUILTINS_H__
#define __BUILTINS_H__

#include <unistd.h>
#include "network.h"   /* bn_send, bn_start_client declarations */


/* Type for builtin handling functions
 * Input: Array of tokens
 * Return: >=0 on success and -1 on error
 */
typedef ssize_t (*bn_ptr)(char **);
ssize_t bn_echo(char **tokens);
ssize_t bn_ls(char **tokens);
ssize_t bn_cd(char **tokens);
ssize_t bn_cat(char **tokens);
ssize_t bn_wc(char **tokens);
ssize_t bn_kill(char **tokens);
ssize_t bn_ps(char **tokens);
/* network builtins run via exec_segment (child process) */
/* bn_send and bn_start_client are declared in network.h  */


/* Return: the address of the function handling the builtin,
 * or NULL if cmd doesn't match any builtin.
 */
bn_ptr check_builtin(const char *cmd);


/* BUILTINS and BUILTINS_FN are parallel arrays of length BUILTINS_COUNT.
 *
 * NOTE: start-server and close-server are NOT listed here because they
 * must run in the shell's own process and are special-cased in
 * dispatch_command (like cd).  send and start-client run in an
 * execute_pipeline child and therefore appear here normally.
 */
static const char * const BUILTINS[] = {
    "echo", "ls", "cd", "cat", "wc", "kill", "ps",
    "send", "start-client"
};
static const bn_ptr BUILTINS_FN[] = {
    bn_echo, bn_ls, bn_cd, bn_cat, bn_wc, bn_kill, bn_ps,
    bn_send, bn_start_client,
    NULL   /* sentinel */
};
static const ssize_t BUILTINS_COUNT =
    (ssize_t)(sizeof(BUILTINS) / sizeof(BUILTINS[0]));

#endif