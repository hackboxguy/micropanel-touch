#!/bin/sh
set -eu

handler=$1
expected_error=$2
invocation=${3:-unexpected}
output=$(mktemp)
trap 'rm -f "$output"' EXIT

test -x "$handler"

# The appliance ships mawk, not gawk. A gawk-only function does not fail
# loudly there - it produces nothing, and the surrounding logic carries on
# with an empty value. That cost a debugging round on the panel once; assert
# it for every handler rather than remembering per handler.
gawk_only='\b(strtonum|gensub|asorti?|systime|strftime|mktime|patsplit)[[:space:]]*\('
if sed 's/#.*//' "$handler" | grep -Eq "$gawk_only"; then
    echo "handler uses a gawk-only awk function; this image ships mawk" >&2
    sed 's/#.*//' "$handler" | grep -nE "$gawk_only" >&2
    exit 1
fi
if [ "$invocation" = no-arguments ]; then
    set -- "$handler"
else
    set -- "$handler" unexpected
fi
if "$@" >"$output" 2>&1; then
    echo "handler accepted an unexpected argument" >&2
    exit 1
else
    status=$?
fi
test "$status" -eq 64
grep -Fx "$expected_error" "$output" >/dev/null
