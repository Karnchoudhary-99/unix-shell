# Custom Unix Shell

A small Unix shell in C, built from scratch — no readline, no parser
generator. A prompt, builtins, and external commands first; pipelines,
redirection, job control and history next.

## Roadmap

- [x] Project scaffold (Makefile, REPL skeleton, smoke test, CI, README)
- [x] Prompt loop + builtins (exit, cd, pwd, help)
- [x] Argument parsing (quoting, escaping, env expansion)
- [x] Pipelines + I/O redirection
- [x] Job control (background jobs, fg, jobs list)
- [x] Command history
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

## Job control

A trailing `&` runs a command or pipeline in the background under one
supervisor process; the shell prints `[n] pid` and returns at once.
`jobs` lists running jobs and any that finished since the last check
(finished jobs are cleared after being reported), `fg %n` waits for a
job and makes its exit status the shell's `$?`, and `kill %n` sends
SIGTERM to a job. This is simplified job control: there is no terminal
handover and no Ctrl-Z stopping, and a background builtin runs in its
supervisor process, so `cd /tmp &` does not change the shell's directory.

```sh
ush> sleep 30 &
[1] 12345
ush> jobs
[1] Running sleep 30
ush> kill %1
ush> jobs
[1] Done sleep 30
```

## Command history

Every non-blank line is recorded (up to 1000 entries) and persisted
to `~/.ush_history` on exit, then reloaded at the next startup.
`history` prints the numbered list, `history -c` clears it, and a line
starting with `!` is expanded before parsing: `!!` re-runs the previous
command, `!n` re-runs entry `n`, `!-n` re-runs the nth most recent
entry, and `!prefix` re-runs the most recent entry starting with
`prefix`. Extra words after the reference are appended to the expanded
command, so `!! > out.txt` re-runs the previous command into a file.
An unknown reference prints `event not found`, is not recorded, and
the shell keeps running. There is no line editing or arrow-key recall:
ush has no readline dependency, by design.

```sh
ush> echo hello
hello
ush> !!
hello
ush> !1
hello
```

## License

MIT — see [LICENSE](LICENSE).
