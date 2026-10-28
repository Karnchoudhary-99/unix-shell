/* ush — a small Unix shell. Prompt loop, builtins, and external
 * commands via fork/execvp. Pipelines, redirection, job control and
 * history are later roadmap items. */
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

/* Builtins. run_builtin returns 1 if argv[0] is a builtin (its exit
 * status in *out, and *should_exit set for `exit`), 0 otherwise. */

static int builtin_cd(char **argv, int argc) {
    const char *dir = NULL;
    char *owned = NULL;
    if (argc > 2) {
        fputs("ush: cd: too many arguments\n", stderr);
        return 1;
    }
    if (argc == 1 || strcmp(argv[1], "~") == 0) {
        dir = getenv("HOME");
        if (dir == NULL) {
            fputs("ush: cd: HOME not set\n", stderr);
            return 1;
        }
    } else if (strncmp(argv[1], "~/", 2) == 0) {
        const char *home = getenv("HOME");
        if (home == NULL) {
            fputs("ush: cd: HOME not set\n", stderr);
            return 1;
        }
        size_t len = strlen(home) + strlen(argv[1]);
        owned = malloc(len + 1);
        if (owned == NULL) {
            return 1;
        }
        strcpy(owned, home);
        strcat(owned, argv[1] + 1);
        dir = owned;
    } else {
        dir = argv[1];
    }
    int rc = chdir(dir);
    if (rc != 0) {
        fprintf(stderr, "ush: cd: %s: ", dir);
        perror("");
    }
    free(owned);
    return rc == 0 ? 0 : 1;
}

static int builtin_pwd(void) {
    char buf[4096];
    if (getcwd(buf, sizeof buf) == NULL) {
        perror("ush: pwd");
        return 1;
    }
    puts(buf);
    return 0;
}

static int builtin_help(void) {
    puts("ush builtins:");
    puts("  cd [dir]    change directory (no argument: $HOME, ~ supported)");
    puts("  pwd         print the working directory");
    puts("  help        show this list");
    puts("  exit [n]    leave the shell (default: last command's status)");
    puts("Anything else runs as an external command via PATH.");
    return 0;
}

static int run_builtin(char **argv, int argc, int last_status,
                       int *out, int *should_exit) {
    *should_exit = 0;
    if (strcmp(argv[0], "cd") == 0) {
        *out = builtin_cd(argv, argc);
        return 1;
    }
    if (strcmp(argv[0], "pwd") == 0) {
        *out = builtin_pwd();
        return 1;
    }
    if (strcmp(argv[0], "help") == 0) {
        *out = builtin_help();
        return 1;
    }
    if (strcmp(argv[0], "exit") == 0) {
        if (argc > 2) {
            fputs("ush: exit: too many arguments\n", stderr);
            *out = 1;
        } else if (argc == 2) {
            *out = atoi(argv[1]);
            *should_exit = 1;
        } else {
            *out = last_status;
            *should_exit = 1;
        }
        return 1;
    }
    return 0;
}

int main(void) {
    char *line = NULL;
    size_t cap = 0;
    char *argv[MAX_ARGS];
    int last_status = 0;

    for (;;) {
        if (isatty(STDIN_FILENO)) {
            fputs("ush> ", stdout);
            fflush(stdout);
        }
        if (getline(&line, &cap, stdin) < 0) {
            break; /* EOF */
        }
        int argc = tokenize(line, argv);
        if (argc == 0) {
            continue; /* blank line */
        }
        int status, should_exit;
        if (run_builtin(argv, argc, last_status, &status, &should_exit)) {
            last_status = status;
            if (should_exit) {
                break;
            }
        } else {
            last_status = run_external(argv);
        }
    }
    free(line);
    return last_status;
}
