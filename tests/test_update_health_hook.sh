#!/bin/sh
# The health hook is this application's whole contribution to the engine's
# candidate-health predicate, so it has to be exactly as strict as the check it
# replaced: a rendered frame, proven by a regular file that is not a symlink.
set -eu

hook=$1
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT HUP INT TERM
failures=0

expect() { # $1=label $2=expected exit (0/1)
    if MICROPANEL_FIRST_FRAME_MARKER="$work/marker" /bin/sh "$hook" >/dev/null 2>&1; then
        actual=0
    else
        actual=1
    fi
    if [ "$actual" != "$2" ]; then
        echo "FAIL: $1 (exit $actual, expected $2)" >&2
        failures=$((failures + 1))
    else
        printf '  ok  %-42s -> exit %s\n' "$1" "$actual"
    fi
}

rm -f "$work/marker"
expect 'no first-frame marker' 1

: > "$work/marker"
expect 'first-frame marker present' 0

# A symlink is refused deliberately: the marker lives in a runtime directory and
# must be evidence the HMI itself wrote, not a pointer something else planted.
rm -f "$work/marker"; ln -s /etc/hostname "$work/marker"
expect 'marker is a symlink' 1

rm -f "$work/marker"; mkdir "$work/marker"
expect 'marker is a directory' 1

[ "$failures" -eq 0 ] || { echo "update-health-hook: $failures FAILURES" >&2; exit 1; }
printf '%s\n' 'update-health-hook: PASS'
