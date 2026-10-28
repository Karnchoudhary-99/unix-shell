# Custom Unix Shell

A small Unix shell in C, built from scratch — no readline, no parser
generator. A prompt, builtins, and external commands first; pipelines,
redirection, job control and history next.

## Roadmap

- [x] Project scaffold (Makefile, REPL skeleton, smoke test, CI, README)
- [ ] Prompt loop + builtins (exit, cd, pwd, help)
- [ ] Argument parsing (quoting, escaping, env expansion)
- [ ] Pipelines + I/O redirection
- [ ] Job control (background jobs, fg/bg, jobs list)
- [ ] Command history
- [ ] Self-review checklist, then public

## Quick start

```sh
make            # builds ./ush
./ush           # interactive prompt
printf 'echo hello\nexit\n' | ./ush   # non-interactive smoke run
make test       # build + smoke test
```

## License

MIT — see [LICENSE](LICENSE).
