# Custom Unix Shell

A small Unix shell in C, built from scratch — no readline, no parser
generator. A prompt, builtins, and external commands first; pipelines,
redirection, job control and history next.

## Roadmap

- [x] Project scaffold (Makefile, REPL skeleton, smoke test, CI, README)
- [x] Prompt loop + builtins (exit, cd, pwd, help)
- [x] Argument parsing (quoting, escaping, env expansion)
- [ ] Pipelines + I/O redirection
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

## License

MIT — see [LICENSE](LICENSE).
