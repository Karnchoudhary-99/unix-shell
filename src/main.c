/* ush — a small Unix shell. Scaffold: prompt loop, the `exit` builtin,
 * and external commands via fork/execvp. Pipelines, redirection, job
 * control and history are later roadmap items. */
#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#define MAX_ARGS 64

/* Split a line on whitespace in place; returns argc, argv is NULL-terminated. */
static int tokenize(char *line, char **argv) {
    int argc = 0;
    char *tok = strtok(line, " \t\r\n");
    while (tok != NULL && argc < MAX_ARGS - 1) {
        argv[argc++] = tok;
        tok = strtok(NULL, " \t\r\n");
    }
    argv[argc] = NULL;
    return argc;
}

static int run_external(char **argv) {
    pid_t pid = fork();
    if (pid < 0) {
        perror("ush: fork");
        return 1;
    }
    if (pid == 0) {
        execvp(argv[0], argv);
        perror("ush");
        _exit(127);
    }
    int status;
    while (waitpid(pid, &status, 0) < 0) {
        /* retry on EINTR */
    }
    return WIFEXITED(status) ? WEXITSTATUS(status) : 1;
}

int main(void) {
    char *line = NULL;
    size_t cap = 0;
    char *argv[MAX_ARGS];

    for (;;) {
        if (isatty(STDIN_FILENO)) {
            fputs("ush> ", stdout);
            fflush(stdout);
        }
        if (getline(&line, &cap, stdin) < 0) {
            break; /* EOF */
        }
        if (tokenize(line, argv) == 0) {
            continue; /* blank line */
        }
        if (strcmp(argv[0], "exit") == 0) {
            break;
        }
        run_external(argv);
    }
    free(line);
    return 0;
}
