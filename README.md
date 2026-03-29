# Unix Shell in C

A fully functional Unix shell built from scratch in C, implementing core OS concepts including process management, piping, I/O redirection, signal handling, and built-in commands.

## Features

- **Command execution** — fork, execvp, waitpid
- **I/O Redirection** — `>`, `>>`, `<`
- **Piping** — `cmd1 | cmd2 | cmd3`
- **Background execution** — `command &`
- **Built-in commands** — `cd`, `exit`, `history`
- **Signal handling** — Ctrl+C shows fresh prompt, Ctrl+D exits

## Build

```bash
# install readline (RHEL/AlmaLinux/Rocky)
sudo dnf install readline-devel

# compile
make

# run
./shell
```

## Usage

```bash
# basic commands
unixsh> ls -l
unixsh> grep foo file.txt

# piping
unixsh> ls /etc | grep host | sort

# I/O redirection
unixsh> echo hello > out.txt
unixsh> cat < out.txt
unixsh> ls >> out.txt

# background execution
unixsh> sleep 10 &

# built-ins
unixsh> cd /tmp
unixsh> history
unixsh> exit
```

## System Calls Used

| Call | Purpose |
|---|---|
| `fork()` | Create child process |
| `execvp()` | Replace process with command |
| `waitpid()` | Wait for child to finish |
| `pipe()` | Create communication channel between processes |
| `dup2()` | Redirect file descriptors |
| `chdir()` | Change directory (cd builtin) |
| `sigaction()` | Set up signal handlers |

## Project Structure

```
unix-shell/
  shell.c     — main source file
  Makefile    — build configuration
  README.md   — this file
```
