/* ush — a small Unix shell. Prompt loop, builtins, and external
 * commands via fork/execvp. Pipelines, redirection, job control and
 * history are later roadmap items. */
#define _POSIX_C_SOURCE 200809L

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#define MAX_ARGS 64

/* ---- Argument parser: quoting, escaping, env expansion, comments ----
 * Rules (POSIX-like, simplified):
 *   '...'  literal, no expansion, no escapes inside
 *   "..."  $VAR/${VAR}/$?/$$ expand; backslash escapes only $, `, ", \, newline
 *   unquoted backslash escapes the next character literally
 *   # starts a comment when it begins a word
 *   an empty quoted string is still an argument (echo "" passes one arg)
 * On an unterminated quote, returns -1 and prints a diagnostic. */

struct sbuf {
    char *data;
    size_t len;
    size_t cap;
};

static int sbuf_putc(struct sbuf *b, char c) {
    if (b->len + 2 > b->cap) {
        size_t ncap = b->cap ? b->cap * 2 : 32;
        char *nd = realloc(b->data, ncap);
        if (nd == NULL) {
            return -1;
        }
        b->data = nd;
        b->cap = ncap;
    }
    b->data[b->len++] = c;
    b->data[b->len] = '\0';
    return 0;
}

static int sbuf_puts(struct sbuf *b, const char *s) {
    for (; *s; s++) {
        if (sbuf_putc(b, *s) != 0) {
            return -1;
        }
    }
    return 0;
}

static int is_var_start(char c) {
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_';
}

static int is_var_char(char c) {
    return is_var_start(c) || (c >= '0' && c <= '9');
}

/* Expand $... at *p (pointing just past the '$'); advances *p past the
 * expansion and appends the value. Returns 0, or -1 on allocation failure. */
static int expand_var(const char **p, struct sbuf *out, int last_status) {
    const char *s = *p;
    if (*s == '?') {
        char num[16];
        snprintf(num, sizeof num, "%d", last_status);
        *p = s + 1;
        return sbuf_puts(out, num);
    }
    if (*s == '$') {
        char num[32];
        snprintf(num, sizeof num, "%d", (int)getpid());
        *p = s + 1;
        return sbuf_puts(out, num);
    }
    char name[256];
    size_t n = 0;
    if (*s == '{') {
        s++;
        while (*s && *s != '}' && n < sizeof name - 1) {
            name[n++] = *s++;
        }
        if (*s == '}') {
            s++;
        } else {
            /* No closing brace: treat the whole thing literally. */
            if (sbuf_putc(out, '$') != 0 || sbuf_putc(out, '{') != 0) {
                return -1;
            }
            name[n] = '\0';
            return sbuf_puts(out, name);
        }
    } else if (is_var_start(*s)) {
        while (is_var_char(*s) && n < sizeof name - 1) {
            name[n++] = *s++;
        }
    } else {
        /* Lone '$' followed by something else: keep it literal. */
        return sbuf_putc(out, '$');
    }
    name[n] = '\0';
    const char *val = getenv(name);
    *p = s;
    return val ? sbuf_puts(out, val) : 0;
}

static void free_argv(char **argv, int argc) {
    if (argv == NULL) {
        return;
    }
    for (int i = 0; i < argc; i++) {
        free(argv[i]);
    }
    free(argv);
}

/* Parse one input line into a freshly allocated NULL-terminated argv.
 * Returns argc, or -1 on an unterminated quote (argv left NULL). */
static int parse_line(const char *line, int last_status, char ***argv_out) {
    char **argv = NULL;
    int argc = 0, argv_cap = 0;
    struct sbuf cur = {NULL, 0, 0};
    int in_token = 0;

    enum { UNQUOTED, SINGLE, DOUBLE } state = UNQUOTED;
    const char *p = line;
    int rc = 0;

    /* Helper macros: append a finished token to argv (takes ownership). */
#define PUSH_TOKEN() do { \
        if (argc + 2 > argv_cap) { \
            int ncap = argv_cap ? argv_cap * 2 : 16; \
            char **na = realloc(argv, (size_t)ncap * sizeof *na); \
            if (na == NULL) { rc = -1; goto done; } \
            argv = na; argv_cap = ncap; \
        } \
        argv[argc++] = cur.data ? cur.data : strdup(""); \
        cur.data = NULL; cur.len = 0; cur.cap = 0; \
        in_token = 0; \
    } while (0)

    for (; *p; p++) {
        char c = *p;
        if (state == SINGLE) {
            if (c == '\'') {
                state = UNQUOTED;
            } else if (sbuf_putc(&cur, c) != 0) {
                rc = -1;
                goto done;
            }
            continue;
        }
        if (state == DOUBLE) {
            if (c == '"') {
                state = UNQUOTED;
            } else if (c == '\\' && (p[1] == '$' || p[1] == '`' ||
                                     p[1] == '"' || p[1] == '\\' ||
                                     p[1] == '\n')) {
                p++;
                if (p[0] != '\n' && sbuf_putc(&cur, p[0]) != 0) {
                    rc = -1;
                    goto done;
                }
            } else if (c == '$') {
                p++;
                if (expand_var(&p, &cur, last_status) != 0) {
                    rc = -1;
                    goto done;
                }
                p--; /* loop's p++ lands on the char after the expansion */
            } else if (sbuf_putc(&cur, c) != 0) {
                rc = -1;
                goto done;
            }
            continue;
        }
        /* UNQUOTED */
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
            if (in_token) {
                PUSH_TOKEN();
            }
        } else if (c == '|' || c == '<' || c == '>') {
            /* Operators are tokens of their own, even unspaced: a|b,
             * echo hi>file, cmd < in. Quoted operators stay literal
             * (handled by the SINGLE/DOUBLE branches above). */
            if (in_token) {
                PUSH_TOKEN();
            }
            if (c == '>' && p[1] == '>') {
                cur.data = strdup(">>");
                cur.len = 2;
                cur.cap = 3;
                p++;
            } else {
                cur.data = malloc(2);
                if (cur.data != NULL) {
                    cur.data[0] = c;
                    cur.data[1] = '\0';
                }
                cur.len = 1;
                cur.cap = 2;
            }
            if (cur.data == NULL) {
                rc = -1;
                goto done;
            }
            in_token = 1;
            PUSH_TOKEN();
        } else if (c == '#' && !in_token) {
            break; /* comment runs to end of line */
        } else if (c == '\'') {
            state = SINGLE;
            in_token = 1;
        } else if (c == '"') {
            state = DOUBLE;
            in_token = 1;
        } else if (c == '\\' && p[1] != '\0') {
            p++;
            if (p[0] != '\n' && sbuf_putc(&cur, p[0]) != 0) {
                rc = -1;
                goto done;
            }
            in_token = 1;
        } else if (c == '$') {
            p++;
            if (expand_var(&p, &cur, last_status) != 0) {
                rc = -1;
                goto done;
            }
            p--;
            in_token = 1;
        } else {
            if (sbuf_putc(&cur, c) != 0) {
                rc = -1;
                goto done;
            }
            in_token = 1;
        }
    }
    if (state != UNQUOTED) {
        fputs("ush: unterminated quote\n", stderr);
        rc = -1;
        goto done;
    }
    if (in_token) {
        PUSH_TOKEN();
    }
#undef PUSH_TOKEN

done:
    free(cur.data);
    if (rc != 0) {
        free_argv(argv, argc);
        *argv_out = NULL;
        return -1;
    }
    if (argv == NULL) {
        argv = calloc(1, sizeof *argv);
        if (argv == NULL) {
            *argv_out = NULL;
            return -1;
        }
    }
    argv[argc] = NULL;
    *argv_out = argv;
    return argc;
}

/* ---- Pipeline segments ----
 * A parsed line is a pipeline of segments separated by '|'. Each
 * segment owns an argv array (pointing at the line's token strings,
 * which it must NOT free) plus optional redirections. The last
 * redirection of each kind wins, as in POSIX shells. */
struct segment {
    char **argv;
    int argc;
    const char *infile;
    const char *outfile;
    int append;
};

static void free_segments(struct segment *segs, int nsegs) {
    if (segs == NULL) {
        return;
    }
    for (int i = 0; i < nsegs; i++) {
        free(segs[i].argv);
    }
    free(segs);
}

static int is_operator_token(const char *s) {
    return strcmp(s, "|") == 0 || strcmp(s, "<") == 0 ||
           strcmp(s, ">") == 0 || strcmp(s, ">>") == 0;
}

/* Split a token argv into pipeline segments. Returns 0, or -1 on a
 * syntax error (empty segment, dangling pipe, missing redirect target,
 * or an operator where a filename was expected). */
static int parse_pipeline(char **argv, int argc, struct segment **segs_out,
                          int *nsegs_out) {
    struct segment *segs = NULL;
    int nsegs = 0, segs_cap = 0;
    struct segment cur;
    memset(&cur, 0, sizeof cur);
    int cur_cap = 0;

    for (int i = 0; i < argc; i++) {
        const char *tok = argv[i];
        if (strcmp(tok, "|") == 0) {
            if (cur.argc == 0) {
                fputs("ush: syntax error near '|'\n", stderr);
                goto fail;
            }
            if (nsegs + 1 > segs_cap) {
                int ncap = segs_cap ? segs_cap * 2 : 4;
                struct segment *ns = realloc(segs, (size_t)ncap * sizeof *ns);
                if (ns == NULL) {
                    goto fail_nomsg;
                }
                segs = ns;
                segs_cap = ncap;
            }
            cur.argv[cur.argc] = NULL;
            segs[nsegs++] = cur;
            memset(&cur, 0, sizeof cur);
            cur_cap = 0;
        } else if (strcmp(tok, "<") == 0 || strcmp(tok, ">") == 0 ||
                   strcmp(tok, ">>") == 0) {
            if (i + 1 >= argc || is_operator_token(argv[i + 1])) {
                fprintf(stderr, "ush: syntax error near '%s'\n", tok);
                goto fail;
            }
            if (strcmp(tok, "<") == 0) {
                cur.infile = argv[++i];
            } else {
                cur.outfile = argv[++i];
                cur.append = strcmp(tok, ">>") == 0;
            }
        } else {
            if (cur.argc + 2 > cur_cap) {
                int ncap = cur_cap ? cur_cap * 2 : 8;
                char **na = realloc(cur.argv, (size_t)ncap * sizeof *na);
                if (na == NULL) {
                    goto fail_nomsg;
                }
                cur.argv = na;
                cur_cap = ncap;
            }
            cur.argv[cur.argc++] = argv[i];
        }
    }
    if (cur.argc == 0) {
        fputs("ush: syntax error near end of line\n", stderr);
        goto fail;
    }
    if (nsegs + 1 > segs_cap) {
        int ncap = segs_cap ? segs_cap * 2 : 4;
        struct segment *ns = realloc(segs, (size_t)ncap * sizeof *ns);
        if (ns == NULL) {
            goto fail_nomsg;
        }
        segs = ns;
        segs_cap = ncap;
    }
    cur.argv[cur.argc] = NULL;
    segs[nsegs++] = cur;
    *segs_out = segs;
    *nsegs_out = nsegs;
    return 0;

fail:
    free(cur.argv);
    free_segments(segs, nsegs);
    return -1;
fail_nomsg:
    fputs("ush: out of memory\n", stderr);
    free(cur.argv);
    free_segments(segs, nsegs);
    return -1;
}

static int run_external(char **argv) {
    pid_t pid = fork();
    if (pid < 0) {
        perror("ush: fork");
        return 1;
    }
    if (pid == 0) {
        execvp(argv[0], argv);
        fprintf(stderr, "ush: %s: ", argv[0]);
        perror("");
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

/* Run a pipeline (or a single command with redirections). Redirect
 * files are opened up front so a bad path fails before any stage
 * runs; each stage's stdin is its input redirect, else the previous
 * pipe, and its stdout is its output redirect, else the next pipe.
 * Returns the last stage's exit status. */
static int run_pipeline(struct segment *segs, int nsegs, int last_status) {
    int *in_fd = calloc((size_t)nsegs, sizeof *in_fd);
    int *out_fd = calloc((size_t)nsegs, sizeof *out_fd);
    pid_t *pids = calloc((size_t)nsegs, sizeof *pids);
    if (in_fd == NULL || out_fd == NULL || pids == NULL) {
        free(in_fd);
        free(out_fd);
        free(pids);
        return 1;
    }
    for (int i = 0; i < nsegs; i++) {
        in_fd[i] = -1;
        out_fd[i] = -1;
        if (segs[i].infile != NULL) {
            in_fd[i] = open(segs[i].infile, O_RDONLY);
            if (in_fd[i] < 0) {
                fprintf(stderr, "ush: %s: ", segs[i].infile);
                perror("");
                goto cleanup_fail;
            }
        }
        if (segs[i].outfile != NULL) {
            int flags = O_WRONLY | O_CREAT | (segs[i].append ? O_APPEND : O_TRUNC);
            out_fd[i] = open(segs[i].outfile, flags, 0644);
            if (out_fd[i] < 0) {
                fprintf(stderr, "ush: %s: ", segs[i].outfile);
                perror("");
                goto cleanup_fail;
            }
        }
    }

    int prev_read = -1;
    for (int i = 0; i < nsegs; i++) {
        int pipefd[2] = {-1, -1};
        if (i < nsegs - 1 && pipe(pipefd) != 0) {
            perror("ush: pipe");
            goto cleanup_fail;
        }
        pid_t pid = fork();
        if (pid < 0) {
            perror("ush: fork");
            goto cleanup_fail;
        }
        if (pid == 0) {
            if (in_fd[i] >= 0) {
                dup2(in_fd[i], STDIN_FILENO);
            } else if (prev_read >= 0) {
                dup2(prev_read, STDIN_FILENO);
            }
            if (out_fd[i] >= 0) {
                dup2(out_fd[i], STDOUT_FILENO);
            } else if (i < nsegs - 1) {
                dup2(pipefd[1], STDOUT_FILENO);
            }
            if (prev_read >= 0) {
                close(prev_read);
            }
            if (pipefd[0] >= 0) {
                close(pipefd[0]);
            }
            if (pipefd[1] >= 0) {
                close(pipefd[1]);
            }
            for (int j = 0; j < nsegs; j++) {
                if (in_fd[j] >= 0) {
                    close(in_fd[j]);
                }
                if (out_fd[j] >= 0) {
                    close(out_fd[j]);
                }
            }
            int status, should_exit;
            if (run_builtin(segs[i].argv, segs[i].argc, last_status,
                            &status, &should_exit)) {
                _exit(status);
            }
            execvp(segs[i].argv[0], segs[i].argv);
            fprintf(stderr, "ush: %s: ", segs[i].argv[0]);
            perror("");
            _exit(127);
        }
        pids[i] = pid;
        if (prev_read >= 0) {
            close(prev_read);
        }
        if (pipefd[1] >= 0) {
            close(pipefd[1]);
        }
        prev_read = pipefd[0];
        if (in_fd[i] >= 0) {
            close(in_fd[i]);
            in_fd[i] = -1;
        }
        if (out_fd[i] >= 0) {
            close(out_fd[i]);
            out_fd[i] = -1;
        }
    }
    if (prev_read >= 0) {
        close(prev_read);
    }
    int last = 0;
    for (int i = 0; i < nsegs; i++) {
        int status;
        while (waitpid(pids[i], &status, 0) < 0) {
            /* retry on EINTR */
        }
        if (i == nsegs - 1) {
            last = WIFEXITED(status) ? WEXITSTATUS(status) : 1;
        }
    }
    free(in_fd);
    free(out_fd);
    free(pids);
    return last;

cleanup_fail:
    for (int i = 0; i < nsegs; i++) {
        if (in_fd[i] >= 0) {
            close(in_fd[i]);
        }
        if (out_fd[i] >= 0) {
            close(out_fd[i]);
        }
    }
    free(in_fd);
    free(out_fd);
    free(pids);
    return 1;
}

int main(void) {
    char *line = NULL;
    size_t cap = 0;
    int last_status = 0;

    for (;;) {
        if (isatty(STDIN_FILENO)) {
            fputs("ush> ", stdout);
            fflush(stdout);
        }
        if (getline(&line, &cap, stdin) < 0) {
            break; /* EOF */
        }
        char **argv = NULL;
        int argc = parse_line(line, last_status, &argv);
        if (argc < 0) {
            last_status = 2; /* parse error, keep the shell alive */
            continue;
        }
        if (argc == 0) {
            free_argv(argv, argc);
            continue; /* blank line / comment only */
        }
        struct segment *segs = NULL;
        int nsegs = 0;
        if (parse_pipeline(argv, argc, &segs, &nsegs) != 0) {
            last_status = 2; /* syntax error, keep the shell alive */
            free_argv(argv, argc);
            continue;
        }
        if (nsegs == 1 && segs[0].infile == NULL && segs[0].outfile == NULL) {
            /* Plain single command: builtins run in the shell itself
             * so cd/exit affect this process. */
            int status, should_exit;
            if (run_builtin(segs[0].argv, segs[0].argc, last_status,
                            &status, &should_exit)) {
                last_status = status;
                free_segments(segs, nsegs);
                free_argv(argv, argc);
                if (should_exit) {
                    break;
                }
            } else {
                last_status = run_external(segs[0].argv);
                free_segments(segs, nsegs);
                free_argv(argv, argc);
            }
        } else {
            last_status = run_pipeline(segs, nsegs, last_status);
            free_segments(segs, nsegs);
            free_argv(argv, argc);
        }
    }
    free(line);
    return last_status;
}
