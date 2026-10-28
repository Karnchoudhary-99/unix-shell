#!/bin/sh
# Behaviour tests for ush: builtins, quoting, escaping, expansion, comments.
# Each case feeds a script to ./ush and checks stdout (and exit status).
set -u
cd "$(dirname "$0")/.."
fail=0

check() {
    # check <name> <expected-stdout> <expected-exit> <script...>
    name=$1; expected=$2; want_exit=$3; shift 3
    out=$(printf '%s\n' "$@" | ./ush 2>/dev/null)
    rc=$?
    if [ "$out" = "$expected" ] && [ "$rc" = "$want_exit" ]; then
        echo "ok: $name"
    else
        echo "FAIL: $name — got stdout=[$out] exit=$rc, want stdout=[$expected] exit=$want_exit" >&2
        fail=1
    fi
}

check "external echo" "hello world" 0 'echo hello world' 'exit'
check "double quotes group" "a b c" 0 'echo "a b" c' 'exit'
check "single quotes literal" 'a $HOME b' 0 "echo 'a \$HOME b'" 'exit'
check "env expansion" "$HOME" 0 'echo $HOME' 'exit'
check "braced expansion" "$HOME/x" 0 'echo ${HOME}/x' 'exit'
check "undefined expands empty" "x" 0 'echo ${USH_NO_SUCH_VAR_$$}x' 'exit'
check "escaped space" "a b" 0 'echo a\ b' 'exit'
check "empty quoted arg" " x" 0 'echo "" x' 'exit'
check "dollar-question false" "1" 0 'false' 'echo $?' 'exit'
check "dollar-question true" "0" 0 'true' 'echo $?' 'exit'
check "unknown command status" "127" 0 'ush-no-such-cmd-xyz' 'echo $?' 'exit'
check "comment stripped" "hi" 0 'echo hi # trailing comment' 'exit'
check "comment-only line" "hi" 0 '# just a comment' 'echo hi' 'exit'
check "unterminated quote recovers" "after-error" 0 'echo "oops' 'echo after-error' 'exit'
check "pwd builtin" "$PWD" 0 'pwd' 'exit'
check "cd then pwd" "/tmp" 0 'cd /tmp' 'pwd' 'exit'
check "cd tilde" "$HOME" 0 'cd ~' 'pwd' 'exit'
check "exit code passthrough" "" 3 'exit 3'
check "exit inherits last status" "" 1 'false' 'exit'

check "pipeline upper" "ONE TWO" 0 'echo one two | tr a-z A-Z' 'exit'
check "pipeline passthrough" "hello" 0 'echo hello | cat' 'exit'
check "pipeline exit is last stage" "1" 0 'true | false' 'echo $?' 'exit'
check "pipeline exit last ok" "0" 0 'false | true' 'echo $?' 'exit'
check "builtin in pipeline" "$PWD" 0 'pwd | cat' 'exit'
check "dangling pipe recovers" "after-error" 0 'echo |' 'echo after-error' 'exit'
check "missing redirect target recovers" "after-error" 0 'echo hi >' 'echo after-error' 'exit'

# Redirection to files (checked on disk, not via stdout).
tmpdir=$(mktemp -d)
trap 'rm -rf "$tmpdir"' EXIT
printf 'echo redir > %s/out.txt\nexit\n' "$tmpdir" | ./ush 2>/dev/null
if [ "$(cat "$tmpdir/out.txt")" = "redir" ]; then echo "ok: output redirect"; else echo "FAIL: output redirect" >&2; fail=1; fi
printf 'echo more >> %s/out.txt\nexit\n' "$tmpdir" | ./ush 2>/dev/null
if [ "$(cat "$tmpdir/out.txt")" = "$(printf 'redir\nmore')" ]; then echo "ok: append redirect"; else echo "FAIL: append redirect" >&2; fail=1; fi
printf 'cat < %s/out.txt\nexit\n' "$tmpdir" | ./ush 2>/dev/null > "$tmpdir/stdout.txt"
if [ "$(cat "$tmpdir/stdout.txt")" = "$(printf 'redir\nmore')" ]; then echo "ok: input redirect"; else echo "FAIL: input redirect" >&2; fail=1; fi
printf 'echo piped | cat > %s/piped.txt\nexit\n' "$tmpdir" | ./ush 2>/dev/null
if [ "$(cat "$tmpdir/piped.txt")" = "piped" ]; then echo "ok: pipeline into file"; else echo "FAIL: pipeline into file" >&2; fail=1; fi
bad_out=$(printf 'echo SHOULDNOTAPPEAR | cat < %s/nope.txt\nexit\n' "$tmpdir" | ./ush 2>/dev/null)
if [ -z "$bad_out" ]; then echo "ok: failed input redirect runs nothing"; else echo "FAIL: failed input redirect ran a stage" >&2; fail=1; fi

# help outputs several lines; verify the full listing separately.
help_out=$(printf 'help\nexit\n' | ./ush 2>/dev/null)
case "$help_out" in
  *"  cd [dir]"*"  pwd"*"  exit [n]"*) echo "ok: help full listing" ;;
  *) echo "FAIL: help full listing — got [$help_out]" >&2; fail=1 ;;
esac

exit $fail
