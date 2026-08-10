#!/bin/sh
set -eu

handler=$1
expected_error=$2
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
grep -Fx "$expected_error" "$output" >/dev/null
