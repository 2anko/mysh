# mysh — Unix Shell in C

A Unix shell implementation built incrementally across five milestones, written in C. Supports variable expansion, filesystem commands, pipes, process management, signals, and a networked chat service.

## Building

```bash
cd project_final/src
make
```

Requires GCC with `-Wall -Wextra -Werror` and AddressSanitizer (`-fsanitize=address,leak`).

## Running

```bash
./mysh
```

## Features by Milestone

### Milestone 1 — Core Shell

- Prompt: `mysh$ `
- `echo [string]` — prints arguments to stdout; extra spaces are collapsed, quotes are not processed
- `exit` — exits with code 0; also exits on EOF (Ctrl+D)
- Input line limit: 128 characters; exceeding this prints `ERROR: input line too long`
- Unknown commands print `ERROR: Unknown command: [cmd]`

### Milestone 2 — Variables

- Variable assignment: `myvar=hello`
- Variable expansion: `echo $myvar`; undefined variables expand to empty string
- Tokens are truncated at 128 characters after expansion
- Expanded text is executable (e.g. expanding to a valid builtin runs it)
- Assignments are not re-evaluated as commands
- Arbitrary number of variables supported via heap allocation

### Milestone 3 — Filesystem

Commands:

| Command | Description |
|---|---|
| `ls [path]` | List directory contents |
| `ls --a` | Include hidden files |
| `ls --f substring` | Filter entries by substring |
| `ls --rec` | Recursive traversal |
| `ls --d depth` | Limit recursion depth (0 = current dir only) |
| `cd [path]` | Change directory; no argument → home directory |
| `cat [file]` | Print file contents to stdout |
| `wc [file]` | Print word count, character count, and newline count |

Path support: `.`, `..`, `...` (two levels up), and arbitrary chains of dots (`N` dots = `N-1` levels up).

Error messages: `ERROR: Invalid path`, `ERROR: No input source provided`, `ERROR: Cannot open file`, `ERROR: Too many arguments`, `ERROR: Builtin failed`.

### Milestone 4 — Pipes, Signals, and Process Management

- Pipes (`|`) between any builtins or external commands
- `cat` and `wc` read from stdin when no file is provided and stdin is a pipe
- External commands resolved from `/bin` and `/usr/bin`
- Background processes with `&`: displays `[n] pid` on launch, `[n]+ Done [cmd]` on completion
- Background processes cannot read from the terminal
- Ctrl+C terminates the foreground builtin but not the shell itself

Commands:

| Command | Description |
|---|---|
| `kill [pid] [signum]` | Send signal to process; default signal is SIGTERM |
| `ps` | List all processes launched by this shell |

### Milestone 5 — Network / Chat

A multi-client chat service embedded in the shell.

| Command | Description |
|---|---|
| `start-server [port]` | Start a non-blocking background server |
| `close-server` | Terminate the server |
| `send [port] [hostname] [message]` | Send a one-shot message to a server |
| `start-client [port] [hostname]` | Connect an interactive client that reads from stdin |

- Each connecting client is assigned an ID (`client1:`, `client2:`, …) prepended to all its messages
- Messages are broadcast to the server console and all connected clients
- Message length is limited to fewer than 128 characters; longer messages produce an error
- `\connected` (sent by a client) reports the number of currently connected clients
- Variable expansion applies to client messages
- Ctrl+D or Ctrl+C disconnects the client

## Project Structure

```
project_final/
  src/
    mysh.c          # Main shell loop
    builtins.c/h    # Built-in commands (echo, ls, cd, cat, wc, kill, ps, …)
    commands.c/h    # Command dispatch and pipeline execution
    variables.c/h   # Variable store and $-expansion
    network.c/h     # Server, client, and send implementation
    io_helpers.c/h  # I/O utilities and constants (MAX_STR_LEN = 128)
    Makefile
  tests/
    milestone1tests/
    milestone2tests/
    milestone3tests/
    milestone4tests/
    milestone5tests/
    main_test_runner.py
```

## Running Tests

```bash
cd project_final/tests
python3 main_test_runner.py
```

Individual milestone tests can be run by invoking the relevant test file directly with Python's `unittest` runner.
