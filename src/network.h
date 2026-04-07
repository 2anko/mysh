#ifndef __NETWORK_H__
#define __NETWORK_H__

#include <unistd.h>

/*
 * start-server <port>
 *   Forks a background TCP server that accepts multiple simultaneous
 *   connections.  Must be dispatched in the SHELL's own process
 *   (special-cased in dispatch_command, like cd) so that server_pid
 *   is stored in the shell's address space and close-server can reach it.
 *
 *   Reports ERROR: No port provided if the port argument is missing.
 */
ssize_t bn_start_server(char **tokens);

/*
 * close-server
 *   Terminates the currently running server.
 *   Must be dispatched in the SHELL's own process.
 */
ssize_t bn_close_server(char **tokens);

/*
 * send <port> <hostname> <message ...>
 *   Opens a short-lived connection to the server and delivers a message
 *   that the server broadcasts to stdout and all connected clients.
 *
 *   Reports ERROR: No port provided     if port is missing.
 *   Reports ERROR: No hostname provided if hostname is missing.
 */
ssize_t bn_send(char **tokens);

/*
 * start-client <port> <hostname>
 *   Connects to the server as an interactive chat client.
 *   Reads lines from stdin, sends them to the server.
 *   Prints every message received from the server to stdout.
 *   The special line "\connected" queries the number of active clients.
 *   Terminates on CTRL+D (EOF) or CTRL+C.
 *
 *   Reports ERROR: No port provided     if port is missing.
 *   Reports ERROR: No hostname provided if hostname is missing.
 */
ssize_t bn_start_client(char **tokens);

#endif /* __NETWORK_H__ */
