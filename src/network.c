#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <sys/wait.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>

#include "network.h"
#include "io_helpers.h"
#include "variables.h"

/* ===================================================================
 * Module-level state (shell process only)
 * =================================================================== */

#define MAX_CLIENTS 64

/* PID of the background server child.  Lives only in the shell process
 * because start-server / close-server are special-cased in
 * dispatch_command (like cd) and never run inside a fork()ed child. */
static pid_t server_pid = -1;

/* atexit handler – kills the server when the shell exits normally */
static void cleanup_server(void) {
    if (server_pid > 0) {
        kill(server_pid, SIGTERM);
        waitpid(server_pid, NULL, 0);
        server_pid = -1;
    }
}

/* ===================================================================
 * Internal helpers
 * =================================================================== */

/*
 * read_line_fd – read one '\n'-terminated line from fd into buf.
 * buf receives at most max_len chars (NUL-terminated, newline stripped).
 * Returns number of chars stored, or -1 on EOF / error.
 */
static ssize_t read_line_fd(int fd, char *buf, size_t max_len) {
    size_t n = 0;
    char   c;
    while (n < max_len) {
        ssize_t r = read(fd, &c, 1);
        if (r <= 0) return -1;
        if (c == '\n') break;
        buf[n++] = c;
    }
    buf[n] = '\0';
    return (ssize_t)n;
}

/*
 * tcp_connect – create a TCP socket and connect to hostname:port.
 * Returns the connected fd, or -1 on failure.
 */
static int tcp_connect(const char *hostname, int port) {
    struct hostent *he = gethostbyname(hostname);
    if (!he) return -1;

    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return -1;

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port   = htons((uint16_t)port);
    memcpy(&addr.sin_addr, he->h_addr_list[0], (size_t)he->h_length);

    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        close(fd);
        return -1;
    }
    return fd;
}

/* ===================================================================
 * Server implementation
 * =================================================================== */

typedef struct {
    int fd;      /* socket descriptor   */
    int id;      /* assigned client id  */
    int active;  /* slot in use?        */
} ClientSlot;

/*
 * server_broadcast – write msg to stdout AND every active client socket.
 */
static void server_broadcast(ClientSlot *slots, int n, const char *msg) {
    size_t len = strlen(msg);
    write(STDOUT_FILENO, msg, len);
    for (int i = 0; i < n; i++) {
        if (slots[i].active)
            write(slots[i].fd, msg, len);
    }
}

/*
 * server_loop – the event loop that runs inside the server child process.
 *
 * Protocol (first line sent by the connecting party identifies the role):
 *   "SHELL"  → a `send` command: read the next line, broadcast it, close.
 *   "CLIENT" → an interactive chat client: assign an id, relay messages.
 *
 * Special client message "\connected" → server replies with client count
 * instead of broadcasting.
 */
static void server_loop(int listen_fd) {
    ClientSlot slots[MAX_CLIENTS];
    memset(slots, 0, sizeof(slots));
    int total_clients  = 0;  /* ever connected (monotonically increasing) */
    int active_clients = 0;  /* currently connected                        */

    while (1) {
        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(listen_fd, &rfds);
        int maxfd = listen_fd;

        for (int i = 0; i < MAX_CLIENTS; i++) {
            if (slots[i].active) {
                FD_SET(slots[i].fd, &rfds);
                if (slots[i].fd > maxfd) maxfd = slots[i].fd;
            }
        }

        int ready = select(maxfd + 1, &rfds, NULL, NULL, NULL);
        if (ready < 0) {
            if (errno == EINTR) continue;
            break;
        }

        /* ------ New incoming connection ------ */
        if (FD_ISSET(listen_fd, &rfds)) {
            int cfd = accept(listen_fd, NULL, NULL);
            if (cfd >= 0) {
                char hs[16];
                if (read_line_fd(cfd, hs, sizeof(hs) - 1) < 0) {
                    close(cfd);

                } else if (strcmp(hs, "SHELL") == 0) {
                    /* `send` builtin: read message line and broadcast */
                    char msg[MAX_STR_LEN + 1];
                    if (read_line_fd(cfd, msg, MAX_STR_LEN) >= 0) {
                        char bcast[MAX_STR_LEN + 4];
                        snprintf(bcast, sizeof(bcast), "%s\n", msg);
                        server_broadcast(slots, MAX_CLIENTS, bcast);
                    }
                    close(cfd);

                } else if (strcmp(hs, "CLIENT") == 0) {
                    /* Interactive chat client */
                    int slot = -1;
                    for (int i = 0; i < MAX_CLIENTS; i++)
                        if (!slots[i].active) { slot = i; break; }

                    if (slot < 0) {
                        /* No room */
                        close(cfd);
                    } else {
                        total_clients++;
                        active_clients++;
                        slots[slot].fd     = cfd;
                        slots[slot].id     = total_clients;
                        slots[slot].active = 1;

                        /* Tell the client its assigned id */
                        char id_str[32];
                        snprintf(id_str, sizeof(id_str),
                                 "client%d\n", total_clients);
                        write(cfd, id_str, strlen(id_str));
                    }

                } else {
                    /* Unknown handshake – drop the connection */
                    close(cfd);
                }
            }
        }

        /* ------ Data from connected clients ------ */
        for (int i = 0; i < MAX_CLIENTS; i++) {
            if (!slots[i].active || !FD_ISSET(slots[i].fd, &rfds))
                continue;

            char line[MAX_STR_LEN + 1];
            ssize_t n = read_line_fd(slots[i].fd, line, MAX_STR_LEN);
            if (n < 0) {
                /* Client disconnected */
                close(slots[i].fd);
                slots[i].active = 0;
                active_clients--;
                continue;
            }

            /* Special query: report active client count back to sender only */
            if (strcmp(line, "\\connected") == 0) {
                char resp[64];
                snprintf(resp, sizeof(resp),
                         "%d clients connected\n", active_clients);
                write(slots[i].fd, resp, strlen(resp));
                continue;
            }

            /* Normal message: prepend "clientN: " and broadcast everywhere */
            char bcast[MAX_STR_LEN + 32];
            snprintf(bcast, sizeof(bcast),
                     "client%d: %s\n", slots[i].id, line);
            server_broadcast(slots, MAX_CLIENTS, bcast);
        }
    }

    close(listen_fd);
    _exit(0);
}

/* ===================================================================
 * Client implementation
 * =================================================================== */

/*
 * client_loop – interactive I/O loop inside the start-client child.
 *
 * Multiplexes stdin (user input → server) and sockfd (server → stdout)
 * using select().  Exits when the server closes the connection or the
 * user presses CTRL+D (EOF on stdin).
 *
 * Called from bn_start_client which already lives inside a child process
 * forked by execute_pipeline, so we just return when done.
 */
static void client_loop(int sockfd, const char *my_id) {
    (void)my_id;

    while (1) {
        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(STDIN_FILENO, &rfds);
        FD_SET(sockfd, &rfds);
        int maxfd = (sockfd > STDIN_FILENO) ? sockfd : STDIN_FILENO;

        int ready = select(maxfd + 1, &rfds, NULL, NULL, NULL);
        if (ready < 0) {
            if (errno == EINTR) break;  /* CTRL+C */
            break;
        }

        /* Data arriving from the server → print to stdout */
        if (FD_ISSET(sockfd, &rfds)) {
            char buf[MAX_STR_LEN + 64];
            ssize_t n = read(sockfd, buf, sizeof(buf) - 1);
            if (n <= 0) break;   /* server closed connection */
            buf[n] = '\0';
            write(STDOUT_FILENO, buf, (size_t)n);
        }

        /* User typed a line → expand variables, check length, send to server */
        if (FD_ISSET(STDIN_FILENO, &rfds)) {
            char raw[MAX_STR_LEN + 1];
            ssize_t n = 0;
            char c;
            int got_newline = 0;
            while (n < MAX_STR_LEN) {
                ssize_t r = read(STDIN_FILENO, &c, 1);
                if (r == 0) goto done;   /* EOF / CTRL+D */
                if (r < 0)  goto done;
                if (c == '\0') goto done; /* null byte = disconnect signal */
                if (c == '\n') { got_newline = 1; break; }
                raw[n++] = c;
            }
            raw[n] = '\0';

            if (!got_newline) {
                /* Line exceeded MAX_STR_LEN chars — drain remainder, then error */
                while (1) {
                    ssize_t r = read(STDIN_FILENO, &c, 1);
                    if (r <= 0 || c == '\n' || c == '\0') break;
                }
                display_error("ERROR: Message too long", "");
            } else {
                /* Expand variables; if the expanded result is >= MAX_STR_LEN,
                 * truncation occurred (original > 128 chars) — report error */
                char expanded[MAX_STR_LEN + 1];
                expand_input(expanded, raw, MAX_STR_LEN);
                if (strlen(expanded) >= (size_t)MAX_STR_LEN) {
                    display_error("ERROR: Message too long", "");
                } else {
                    size_t elen = strlen(expanded);
                    expanded[elen++] = '\n';
                    write(sockfd, expanded, elen);
                }
            }
        }
    }
done:
    close(sockfd);
}

/* ===================================================================
 * Builtin: start-server <port>
 *
 * IMPORTANT: dispatched in the shell's own process (see commands.c).
 * =================================================================== */
ssize_t bn_start_server(char **tokens) {
    if (tokens[1] == NULL) {
        display_error("ERROR: No port provided", "");
        return -1;
    }
    int port = atoi(tokens[1]);
    if (port <= 0) {
        display_error("ERROR: No port provided", "");
        return -1;
    }

    /* Register cleanup handler once */
    static int atexit_done = 0;
    if (!atexit_done) {
        atexit(cleanup_server);
        atexit_done = 1;
    }

    /* Shut down any already-running server */
    if (server_pid > 0) {
        kill(server_pid, SIGTERM);
        waitpid(server_pid, NULL, 0);
        server_pid = -1;
    }

    /* Create the listening socket in the shell process so bind/listen
     * errors are reported before we fork. */
    int lfd = socket(AF_INET, SOCK_STREAM, 0);
    if (lfd < 0) {
        display_error("ERROR: socket failed", "");
        return -1;
    }

    int opt = 1;
    setsockopt(lfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in sa;
    memset(&sa, 0, sizeof(sa));
    sa.sin_family      = AF_INET;
    sa.sin_addr.s_addr = INADDR_ANY;
    sa.sin_port        = htons((uint16_t)port);

    if (bind(lfd, (struct sockaddr *)&sa, sizeof(sa)) < 0) {
        display_error("ERROR: bind failed", "");
        close(lfd);
        return -1;
    }
    if (listen(lfd, 10) < 0) {
        display_error("ERROR: listen failed", "");
        close(lfd);
        return -1;
    }

    pid_t pid = fork();
    if (pid < 0) {
        display_error("ERROR: fork failed", "");
        close(lfd);
        return -1;
    }
    if (pid == 0) {
        /* Server child: become its own process group so CTRL+C does not
         * reach it, ignore SIGINT, then run the event loop. */
        setpgrp();
        signal(SIGINT, SIG_IGN);
        server_loop(lfd);
        _exit(0); /* unreachable */
    }

    /* Shell process: close our copy of the listen fd and remember the PID. */
    close(lfd);
    server_pid = pid;
    return 0;
}

/* ===================================================================
 * Builtin: close-server
 *
 * IMPORTANT: dispatched in the shell's own process (see commands.c).
 * =================================================================== */
ssize_t bn_close_server(char **tokens) {
    (void)tokens;
    if (server_pid <= 0) {
        display_error("ERROR: No server running", "");
        return -1;
    }
    kill(server_pid, SIGTERM);
    waitpid(server_pid, NULL, 0);
    server_pid = -1;
    return 0;
}

/* ===================================================================
 * Builtin: send <port> <hostname> <message ...>
 *
 * Runs in an execute_pipeline child (no shell-global state needed).
 * =================================================================== */
ssize_t bn_send(char **tokens) {
    if (tokens[1] == NULL) {
        display_error("ERROR: No port provided", "");
        return -1;
    }
    if (tokens[2] == NULL) {
        display_error("ERROR: No hostname provided", "");
        return -1;
    }
    if (tokens[3] == NULL) {
        display_error("ERROR: Builtin failed: ", "send");
        return -1;
    }

    int         port     = atoi(tokens[1]);
    const char *hostname = tokens[2];

    /* Join tokens[3..] into a single message string */
    char msg[MAX_STR_LEN + 1];
    msg[0] = '\0';
    for (int i = 3; tokens[i] != NULL; i++) {
        if (i > 3)
            strncat(msg, " ", sizeof(msg) - strlen(msg) - 1);
        strncat(msg, tokens[i], sizeof(msg) - strlen(msg) - 1);
    }

    /* If the assembled message hit the buffer ceiling, the original was >128 */
    if (strlen(msg) >= (size_t)MAX_STR_LEN) {
        display_error("ERROR: Message too long", "");
        return -1;
    }

    int fd = tcp_connect(hostname, port);
    if (fd < 0) {
        display_error("ERROR: Could not connect to server", "");
        return -1;
    }

    /* Protocol: "SHELL\n<message>\n" */
    char buf[MAX_STR_LEN + 16];
    snprintf(buf, sizeof(buf), "SHELL\n%s\n", msg);
    write(fd, buf, strlen(buf));
    close(fd);
    return 0;
}

/* ===================================================================
 * Builtin: start-client <port> <hostname>
 *
 * Runs in an execute_pipeline child.  Calls client_loop() directly
 * (no extra fork needed – execute_pipeline already forked us).
 * The shell blocks in waitpid() until this child returns.
 * =================================================================== */
ssize_t bn_start_client(char **tokens) {
    if (tokens[1] == NULL) {
        display_error("ERROR: No port provided", "");
        return -1;
    }
    if (tokens[2] == NULL) {
        display_error("ERROR: No hostname provided", "");
        return -1;
    }

    int         port     = atoi(tokens[1]);
    const char *hostname = tokens[2];

    int sockfd = tcp_connect(hostname, port);
    if (sockfd < 0) {
        display_error("ERROR: Could not connect to server", "");
        return -1;
    }

    /* Identify ourselves as an interactive client */
    write(sockfd, "CLIENT\n", 7);

    /* Read back the id the server assigned to us */
    char my_id[64];
    if (read_line_fd(sockfd, my_id, sizeof(my_id) - 1) < 0) {
        close(sockfd);
        display_error("ERROR: Could not get client ID from server", "");
        return -1;
    }

    /* Restore SIGINT so CTRL+C terminates the client (exec_segment sets
     * SIG_DFL before calling builtins, but be explicit). */
    signal(SIGINT, SIG_DFL);

    /* Run the interactive loop (returns on EOF / CTRL+D / CTRL+C) */
    client_loop(sockfd, my_id);
    return 0;
}
