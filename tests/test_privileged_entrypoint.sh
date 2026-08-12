#!/bin/sh
set -eu

broker=$1

# This test verifies the executable's command-line gate before it checks for
# root. It must run as a non-root user so it cannot create a socket or start
# the broker.
if [ "$(id -u)" -eq 0 ]; then
    exit 77
fi

check_root_gate() {
    output=$("$broker" --socket "/tmp/micropanel-touch-entrypoint-$$.sock" \
        --allowed-user "$(id -un)" 2>&1 || true)
    printf '%s\n' "$output" | grep -Fqx 'micropanel-touch-privileged must run as root'
}

check_exclusive_allowlist_gate() {
    output=$("$broker" --socket "/tmp/micropanel-touch-entrypoint-$$.sock" \
        --allowed-user "$(id -un)" --allowed-uid "$(id -u)" 2>&1 || true)
    printf '%s\n' "$output" | grep -F 'Usage:' >/dev/null
}

check_root_gate
check_exclusive_allowlist_gate
