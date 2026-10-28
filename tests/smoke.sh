#!/bin/sh
# Smoke test: the shell runs an external command and honours `exit`.
set -e
cd "$(dirname "$0")/.."
out=$(printf 'echo hello-from-ush\nexit\n' | ./ush)
case "$out" in
  *hello-from-ush*) echo "smoke: ok" ;;
  *) echo "smoke: FAIL — output was: $out" >&2; exit 1 ;;
esac
