#!/bin/sh
set -eu

handler=$1
output=$(mktemp)
trap 'rm -f "$output"' EXIT

test -x "$handler"
if "$handler" unexpected >"$output" 2>&1; then
    echo "handler accepted an unexpected argument" >&2
    exit 1
else
    status=$?
fi
test "$status" -eq 64
grep -Fx '[ERROR] simulated flash accepts no arguments' "$output" >/dev/null
