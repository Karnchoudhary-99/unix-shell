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

# help outputs several lines; verify the full listing separately.
help_out=$(printf 'help\nexit\n' | ./ush 2>/dev/null)
case "$help_out" in
  *"  cd [dir]"*"  pwd"*"  exit [n]"*) echo "ok: help full listing" ;;
  *) echo "FAIL: help full listing — got [$help_out]" >&2; fail=1 ;;
esac

exit $fail
