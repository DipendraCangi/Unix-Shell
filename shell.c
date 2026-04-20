/*
 * shell.c — A fully functional Unix shell
 *
 * Features:
 *   - Command execution (fork + execvp + waitpid)
 *   - Piping          (ls | grep foo | sort)
 *   - I/O Redirection (>, >>, <)
 *   - Background execution (sleep 10 &)
 *   - Built-ins: cd, exit, history
 *   - Signal handling (Ctrl+C, Ctrl+D)
 *   - Zombie prevention (SIGCHLD → SIG_IGN)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <signal.h>
#include <setjmp.h>
#include <readline/readline.h>
#include <readline/history.h>

/* ─────────────────────────────────────────
 * Constants
 * ───────────────────────────────────────── */
#define MAX_ARGS     64
#define MAX_COMMANDS  8

/* ─────────────────────────────────────────
 * Data Structures
 * ───────────────────────────────────────── */

/*
 * Command — one command in a pipeline
 *
 * Example: "grep foo < in.txt > out.txt"
 *   args        = ["grep", "foo", NULL]
 *   input_file  = "in.txt"
 *   output_file = "out.txt"
 *   append      = 0
 *   background  = 0
 */
typedef struct {
    char *args[MAX_ARGS];
    char *input_file;
    char *output_file;
    int   append;
    int   background;
} Command;

/*
 * Pipeline — one or more piped commands
 *
 * Example: "ls | grep foo | sort"
 *   commands[0] = {args: ["ls",NULL]}
 *   commands[1] = {args: ["grep","foo",NULL]}
 *   commands[2] = {args: ["sort",NULL]}
 *   num_commands = 3
 */
typedef struct {
    Command commands[MAX_COMMANDS];
    int     num_commands;
} Pipeline;

/* ─────────────────────────────────────────
 * Signal handling globals
 * ───────────────────────────────────────── */
static sigjmp_buf              env;
static volatile sig_atomic_t   jump_active = 0;

/* ─────────────────────────────────────────
 * Function prototypes
 * ───────────────────────────────────────── */
Pipeline parse_input(char *input);
void     setup_redirection(Command *cmd);
void     execute_single(Command *cmd, int *stat_loc);
void     execute_pipeline(Pipeline *pipeline, int *stat_loc);
void     sigint_handler(int signo);

/* ═══════════════════════════════════════════
 * PARSER
 * Splits raw input string into a Pipeline
 * struct, detecting |, >, >>, <, &
 * ═══════════════════════════════════════════ */
Pipeline parse_input(char *input) {
    Pipeline pipeline;
    memset(&pipeline, 0, sizeof(Pipeline));
    pipeline.num_commands = 1;

    int cmd_idx = 0;
    int arg_idx = 0;

    char *token = strtok(input, " \t");  /* split by space or tab */
    while (token != NULL) {

        if (strcmp(token, "|") == 0) {
            /* ── pipe: terminate current command, start next ── */
            pipeline.commands[cmd_idx].args[arg_idx] = NULL;
            cmd_idx++;
            arg_idx = 0;
            pipeline.num_commands++;

        } else if (strcmp(token, ">") == 0) {
            /* ── output redirect (overwrite) ── */
            token = strtok(NULL, " \t");
            if (token == NULL) {
                fprintf(stderr, "shell: expected filename after >\n");
                pipeline.num_commands = 0;
                return pipeline;
            }
            pipeline.commands[cmd_idx].output_file = token;
            pipeline.commands[cmd_idx].append = 0;

        } else if (strcmp(token, ">>") == 0) {
            /* ── output redirect (append) ── */
            token = strtok(NULL, " \t");
            if (token == NULL) {
                fprintf(stderr, "shell: expected filename after >>\n");
                pipeline.num_commands = 0;
                return pipeline;
            }
            pipeline.commands[cmd_idx].output_file = token;
            pipeline.commands[cmd_idx].append = 1;

        } else if (strcmp(token, "<") == 0) {
            /* ── input redirect ── */
            token = strtok(NULL, " \t");
            if (token == NULL) {
                fprintf(stderr, "shell: expected filename after <\n");
                pipeline.num_commands = 0;
                return pipeline;
            }
            pipeline.commands[cmd_idx].input_file = token;

        } else if (strcmp(token, "&") == 0) {
            /* ── background flag ── */
            pipeline.commands[cmd_idx].background = 1;

        } else {
            /* ── regular argument ── */
            if (arg_idx >= MAX_ARGS - 1) {
                fprintf(stderr, "shell: too many arguments\n");
                pipeline.num_commands = 0;
                return pipeline;
            }
            pipeline.commands[cmd_idx].args[arg_idx] = token;
            arg_idx++;
        }

        token = strtok(NULL, " \t");
    }

    /* terminate last command's args */
    pipeline.commands[cmd_idx].args[arg_idx] = NULL;
    return pipeline;
}

/* ═══════════════════════════════════════════
 * REDIRECTION SETUP
 * Called inside child process before execvp.
 * Bends stdin/stdout to files using dup2.
 * ═══════════════════════════════════════════ */
void setup_redirection(Command *cmd) {

    /* ── input redirection: < filename ── */
    if (cmd->input_file != NULL) {
        int fd = open(cmd->input_file, O_RDONLY);
        if (fd < 0) {
            perror(cmd->input_file);
            exit(1);
        }
        dup2(fd, STDIN_FILENO);
        close(fd);
    }

    /* ── output redirection: > or >> filename ── */
    if (cmd->output_file != NULL) {
        int flags = O_WRONLY | O_CREAT;
        flags |= cmd->append ? O_APPEND : O_TRUNC;

        int fd = open(cmd->output_file, flags, 0644);
        if (fd < 0) {
            perror(cmd->output_file);
            exit(1);
        }
        dup2(fd, STDOUT_FILENO);
        close(fd);
    }
}

/* ═══════════════════════════════════════════
 * EXECUTE SINGLE COMMAND
 * Handles one command with no pipes.
 * Supports redirection and background.
 * ═══════════════════════════════════════════ */
void execute_single(Command *cmd, int *stat_loc) {
    pid_t child_pid = fork();
    if (child_pid < 0) {
        perror("fork");
        exit(1);
    }

    if (child_pid == 0) {
        /* ── child process ── */

        /* reset SIGINT to default in child
         * so Ctrl+C kills the child normally */
        signal(SIGINT, SIG_DFL);

        setup_redirection(cmd);

        execvp(cmd->args[0], cmd->args);
        perror(cmd->args[0]);   /* only reached if execvp fails */
        exit(1);

    } else {
        /* ── parent process ── */
        if (cmd->background) {
            printf("[bg] PID: %d\n", child_pid);
            /* no waitpid — shell continues immediately */
        } else {
            waitpid(child_pid, stat_loc, WUNTRACED);
        }
    }
}

/* ═══════════════════════════════════════════
 * EXECUTE PIPELINE
 * Handles N commands connected by N-1 pipes.
 * Also supports redirection on first/last cmd.
 *
 * Example: cat < in.txt | grep foo | sort > out.txt
 * ═══════════════════════════════════════════ */
void execute_pipeline(Pipeline *pipeline, int *stat_loc) {
    int num_cmds = pipeline->num_commands;

    /* create all pipes upfront — N commands need N-1 pipes */
    int pipes[MAX_COMMANDS - 1][2];
    for (int i = 0; i < num_cmds - 1; i++) {
        if (pipe(pipes[i]) < 0) {
            perror("pipe");
            exit(1);
        }
    }

    pid_t pids[MAX_COMMANDS];

    for (int i = 0; i < num_cmds; i++) {
        Command *cmd = &pipeline->commands[i];

        pids[i] = fork();
        if (pids[i] < 0) {
            perror("fork");
            exit(1);
        }

        if (pids[i] == 0) {
            /* ── child process ── */
            signal(SIGINT, SIG_DFL);

            /* connect stdin to previous pipe's read end
             * (skip for first command) */
            if (i > 0) {
                dup2(pipes[i-1][0], STDIN_FILENO);
            }

            /* connect stdout to current pipe's write end
             * (skip for last command) */
            if (i < num_cmds - 1) {
                dup2(pipes[i][1], STDOUT_FILENO);
            }

            /* close ALL pipe ends in child —
             * only needs the two it just dup2'd */
            for (int j = 0; j < num_cmds - 1; j++) {
                close(pipes[j][0]);
                close(pipes[j][1]);
            }

            /* apply file redirection if any
             * first command may have <
             * last command may have > */
            setup_redirection(cmd);

            execvp(cmd->args[0], cmd->args);
            perror(cmd->args[0]);
            exit(1);
        }
    }

    /* parent closes ALL pipe ends —
     * critical: otherwise read end never gets EOF */
    for (int i = 0; i < num_cmds - 1; i++) {
        close(pipes[i][0]);
        close(pipes[i][1]);
    }

    /* parent waits for ALL children */
    for (int i = 0; i < num_cmds; i++) {
        waitpid(pids[i], stat_loc, WUNTRACED);
    }
}

/* ═══════════════════════════════════════════
 * SIGNAL HANDLER
 * Ctrl+C jumps back to sigsetjmp checkpoint
 * in main loop — shows fresh prompt
 * ═══════════════════════════════════════════ */
void sigint_handler(int signo) {
    (void)signo;    /* suppress unused parameter warning */
    if (!jump_active)
        return;
    siglongjmp(env, 42);
}

/* ═══════════════════════════════════════════
 * MAIN
 * ═══════════════════════════════════════════ */
int main() {
    int stat_loc;

    /* ── Signal setup ── */

    /* SIGCHLD → SIG_IGN
     * kernel auto-cleans background children
     * prevents zombie processes */
    signal(SIGCHLD, SIG_IGN);

    /* SIGINT → sigint_handler
     * Ctrl+C shows fresh prompt instead of killing shell */
    struct sigaction sa;
    sa.sa_handler = sigint_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    sigaction(SIGINT, &sa, NULL);

    /* ── Main loop ── */
    while (1) {

        /* Ctrl+C lands here — print newline, loop again */
        if (sigsetjmp(env, 1) == 42) {
            printf("\n");
            continue;
        }
        jump_active = 1;

        /* read input */
        char *input = readline("unixsh> ");

        /* Ctrl+D → exit cleanly */
        if (input == NULL) {
            printf("\n");
            exit(0);
        }

        /* skip empty input */
        if (*input == '\0') {
            free(input);
            continue;
        }

        /* save to readline history
         * enables arrow keys + Ctrl+R search */
        add_history(input);

        /* parse into Pipeline struct */
        Pipeline pipeline = parse_input(input);

        /* parse error — message already printed */
        if (pipeline.num_commands == 0) {
            free(input);
            continue;
        }

        Command *cmd = &pipeline.commands[0];

        /* skip if no args (e.g. only spaces typed) */
        if (cmd->args[0] == NULL) {
            free(input);
            continue;
        }

        /* ── Built-in: exit ── */
        if (strcmp(cmd->args[0], "exit") == 0) {
            int code = cmd->args[1] ? atoi(cmd->args[1]) : 0;
            free(input);
            exit(code);
        }

        /* ── Built-in: cd ── */
        if (strcmp(cmd->args[0], "cd") == 0) {
            char *path = cmd->args[1];
            if (path == NULL)
                path = getenv("HOME");   /* cd with no args → $HOME */
            if (chdir(path) < 0)
                perror(path);
            free(input);
            continue;
        }

        /* ── Built-in: history ── */
        if (strcmp(cmd->args[0], "history") == 0) {
            HIST_ENTRY **hist = history_list();
            if (hist)
                for (int i = 0; hist[i] != NULL; i++)
                    printf("%d  %s\n", i + 1, hist[i]->line);
            free(input);
            continue;
        }

        /* ── External commands ── */
        if (pipeline.num_commands == 1) {
            execute_single(cmd, &stat_loc);
        } else {
            execute_pipeline(&pipeline, &stat_loc);
        }

        free(input);
    }

    return 0;
}
