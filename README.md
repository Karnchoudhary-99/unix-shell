# Custom Unix Shell

A small Unix shell in C, built from scratch — no readline, no parser
generator. A prompt, builtins, and external commands first; pipelines,
redirection, job control and history next.

## Roadmap

- [x] Project scaffold (Makefile, REPL skeleton, smoke test, CI, README)
- [x] Prompt loop + builtins (exit, cd, pwd, help)
- [x] Argument parsing (quoting, escaping, env expansion)
- [x] Pipelines + I/O redirection
- [ ] Job control (background jobs, fg/bg, jobs list)
- [ ] Command history
- [ ] Self-review checklist, then public

## Quick start

```sh
make            # builds ./ush
./ush           # interactive prompt
printf 'echo hello\nexit\n' | ./ush   # non-interactive smoke run
make test       # build + smoke + behaviour tests
```

## Builtins and parsing

Builtins: `cd [dir]` (no argument or `~` means `$HOME`), `pwd`, `help`,
`exit [n]` (no argument: last command's status). Anything else runs as an
external command via `PATH`; a failed command sets `$?` to 127.

Parsing is POSIX-like, simplified: single quotes are literal, double quotes
allow `$VAR`/`${VAR}`/`$?`/`$$` expansion, backslash escapes the next
character, `#` starts a comment at the start of a word, and an empty quoted
string still counts as an argument. An unterminated quote prints an error
and the shell keeps running.

```sh
ush> echo "home is $HOME"
ush> echo 'no expansion $HOME'
ush> echo a\ b        # one argument: "a b"
ush> false
ush> echo $?          # 1
```

## Pipelines and redirection

Stages are separated by `|`, with no spaces required (`echo hi|cat` works).
A stage's exit status is the last stage's. Input and output redirection
(`<`, `>`, `>>`) apply per stage and win over the pipe on that side, so
`cat < in.txt | sort > out.txt` reads from the file and writes to it.
Redirect files are opened before any stage runs, so a bad path (e.g.
`cat < missing.txt`) runs nothing and reports the error. A syntax error
(dangling pipe, missing redirect target) prints an error and the shell
keeps running.

```sh
ush> echo one two | tr a-z A-Z
ONE TWO
ush> echo hello > greeting.txt
ush> cat < greeting.txt | tr a-z A-Z
HELLO
```

## License

MIT — see [LICENSE](LICENSE).
