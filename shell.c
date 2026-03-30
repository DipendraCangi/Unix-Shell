
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <readline/readline.h>
#include <readline/history.h>

#define MAX_ARGS 64

typedef struct {
    char *args[MAX_ARGS];
    char *input_file;
    char *output_file;
    int   append;
} Command;

/* Parse input into Command struct — detects >, >>, < */
Command parse_input(char *input) {
    Command cmd;
    memset(&cmd, 0, sizeof(Command));
    int i = 0;

    char *token = strtok(input, " \t");
    while (token != NULL) {
        if (strcmp(token, ">") == 0) {
            token = strtok(NULL, " \t");
            cmd.output_file = token;
            cmd.append = 0;
        } else if (strcmp(token, ">>") == 0) {
            token = strtok(NULL, " \t");
            cmd.output_file = token;
            cmd.append = 1;
        } else if (strcmp(token, "<") == 0) {
            token = strtok(NULL, " \t");
            cmd.input_file = token;
        } else {
            cmd.args[i++] = token;
        }
        token = strtok(NULL, " \t");
    }
    cmd.args[i] = NULL;
    return cmd;
}

/*
 * Redirect stdin/stdout to files using dup2.
 * Called inside child process before execvp.
 */
void setup_redirection(Command *cmd) {
    if (cmd->input_file != NULL) {
        int fd = open(cmd->input_file, O_RDONLY);
        if (fd < 0) { perror(cmd->input_file); exit(1); }
        dup2(fd, STDIN_FILENO);
        close(fd);
    }
    if (cmd->output_file != NULL) {
        int flags = O_WRONLY | O_CREAT;
        flags |= cmd->append ? O_APPEND : O_TRUNC;
        int fd = open(cmd->output_file, flags, 0644);
        if (fd < 0) { perror(cmd->output_file); exit(1); }
        dup2(fd, STDOUT_FILENO);
        close(fd);
    }
}

int main() {
    char   *input;
    pid_t   child_pid;
    int     stat_loc;

    while (1) {

        input = readline("unixsh> ");
        if (input == NULL) { printf("\n"); exit(0); }
        if (*input == '\0') { free(input); continue; }

        Command cmd = parse_input(input);
        if (cmd.args[0] == NULL) { free(input); continue; }

        child_pid = fork();
        if (child_pid < 0) { perror("fork"); exit(1); }

        if (child_pid == 0) {
            setup_redirection(&cmd);        /* ← new */
            if (execvp(cmd.args[0], cmd.args) < 0) {
                perror(cmd.args[0]);
                exit(1);
            }
        } else {
            waitpid(child_pid, &stat_loc, WUNTRACED);
        }

        free(input);
    }

    return 0;
}
