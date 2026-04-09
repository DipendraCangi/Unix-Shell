
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <signal.h>
#include <readline/readline.h>
#include <readline/history.h>

#define MAX_ARGS     64
#define MAX_COMMANDS  8

typedef struct {
    char *args[MAX_ARGS];
    char *input_file;
    char *output_file;
    int   append;
    int   background;       
} Command;

typedef struct {
    Command commands[MAX_COMMANDS];
    int     num_commands;
} Pipeline;

Pipeline parse_input(char *input) {
    Pipeline pipeline;
    memset(&pipeline, 0, sizeof(Pipeline));
    pipeline.num_commands = 1;

    int cmd_idx = 0;
    int arg_idx = 0;

    char *token = strtok(input, " \t");
    while (token != NULL) {
        if (strcmp(token, "|") == 0) {
            pipeline.commands[cmd_idx].args[arg_idx] = NULL;
            cmd_idx++; arg_idx = 0;
            pipeline.num_commands++;
        } else if (strcmp(token, ">") == 0) {
            token = strtok(NULL, " \t");
            pipeline.commands[cmd_idx].output_file = token;
            pipeline.commands[cmd_idx].append = 0;
        } else if (strcmp(token, ">>") == 0) {
            token = strtok(NULL, " \t");
            pipeline.commands[cmd_idx].output_file = token;
            pipeline.commands[cmd_idx].append = 1;
        } else if (strcmp(token, "<") == 0) {
            token = strtok(NULL, " \t");
            pipeline.commands[cmd_idx].input_file = token;
        } else if (strcmp(token, "&") == 0) {
            pipeline.commands[cmd_idx].background = 1; 
        } else {
            pipeline.commands[cmd_idx].args[arg_idx++] = token;
        }
        token = strtok(NULL, " \t");
    }
    pipeline.commands[cmd_idx].args[arg_idx] = NULL;
    return pipeline;
}

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

void execute_pipeline(Pipeline *pipeline, int *stat_loc) {
    int num_cmds = pipeline->num_commands;
    int pipes[MAX_COMMANDS - 1][2];
    pid_t pids[MAX_COMMANDS];

    for (int i = 0; i < num_cmds - 1; i++)
        if (pipe(pipes[i]) < 0) { perror("pipe"); exit(1); }

    for (int i = 0; i < num_cmds; i++) {
        Command *cmd = &pipeline->commands[i];
        pids[i] = fork();
        if (pids[i] < 0) { perror("fork"); exit(1); }

        if (pids[i] == 0) {
            signal(SIGINT, SIG_DFL);
            if (i > 0) dup2(pipes[i-1][0], STDIN_FILENO);
            if (i < num_cmds - 1) dup2(pipes[i][1], STDOUT_FILENO);
            for (int j = 0; j < num_cmds - 1; j++) {
                close(pipes[j][0]);
                close(pipes[j][1]);
            }
            setup_redirection(cmd);
            execvp(cmd->args[0], cmd->args);
            perror(cmd->args[0]);
            exit(1);
        }
    }

    for (int i = 0; i < num_cmds - 1; i++) {
        close(pipes[i][0]);
        close(pipes[i][1]);
    }
    for (int i = 0; i < num_cmds; i++)
        waitpid(pids[i], stat_loc, WUNTRACED);
}

int main() {
    char  *input;
    int    stat_loc;

    /* prevent zombie processes from background children */
    signal(SIGCHLD, SIG_IGN);             

    while (1) {

        input = readline("unixsh> ");
        if (input == NULL) { printf("\n"); exit(0); }
        if (*input == '\0') { free(input); continue; }

        Pipeline pipeline = parse_input(input);
        Command *cmd = &pipeline.commands[0];
        if (cmd->args[0] == NULL) { free(input); continue; }

        if (pipeline.num_commands == 1) {
            pid_t child_pid = fork();
            if (child_pid < 0) { perror("fork"); exit(1); }

            if (child_pid == 0) {
                signal(SIGINT, SIG_DFL);
                setup_redirection(cmd);
                execvp(cmd->args[0], cmd->args);
                perror(cmd->args[0]);
                exit(1);
            } else {
                if (cmd->background) {
                    printf("[bg] PID: %d\n", child_pid);
                } else {
                    waitpid(child_pid, &stat_loc, WUNTRACED);
                }
            }
        } else {
            execute_pipeline(&pipeline, &stat_loc);
        }

        free(input);
    }

    return 0;
}
