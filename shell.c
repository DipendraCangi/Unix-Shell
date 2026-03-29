/*
 * shell.c — Unix Shell
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <readline/readline.h>
#include <readline/history.h>

#define MAX_ARGS 64

/* Parse input string into argv array */
void parse_input(char *input, char **args) {
    int i = 0;
    char *token = strtok(input, " \t");
    while (token != NULL && i < MAX_ARGS - 1) {
        args[i++] = token;
        token = strtok(NULL, " \t");
    }
    args[i] = NULL;
}

int main() {
    char  *input;
    char  *args[MAX_ARGS];
    pid_t  child_pid;
    int    stat_loc;

    while (1) {

        /* read input from user */
        input = readline("unixsh> ");

        /* Ctrl+D → exit */
        if (input == NULL) {
            printf("\n");
            exit(0);
        }

        /* skip empty input */
        if (*input == '\0') {
            free(input);
            continue;
        }

        /* parse into args array */
        parse_input(input, args);

        /* fork a child process */
        child_pid = fork();
        if (child_pid < 0) {
            perror("fork");
            exit(1);
        }

        if (child_pid == 0) {
            /* child: replace itself with the command */
            if (execvp(args[0], args) < 0) {
                perror(args[0]);
                exit(1);
            }
        } else {
            /* parent: wait for child to finish */
            waitpid(child_pid, &stat_loc, WUNTRACED);
        }

        free(input);
    }

    return 0;
}
