#!/bin/sh
# The power handler's whole job is to turn an enum into a fixed command, so the
# properties worth testing are what it refuses and what it never builds.
#
# It is deliberately never invoked with an accepted action: doing so would
# reboot the machine running the test. The accepted half is asserted from the
# source and exercised on the bench.
set -eu

handler=$1

test -x "$handler"

# systemctl is called by absolute path on purpose, so a poisoned PATH cannot
# redirect a reboot into something else.
grep -Fq '/usr/bin/systemctl' "$handler" || {
    echo 'the handler must call systemctl by absolute path' >&2
    exit 1
}
if grep -Eq 'eval|\$\(|`|sh -c' "$handler"; then
    echo 'the handler must not build a command from its argument' >&2
    exit 1
fi
# The argument selects between two literal targets and is never forwarded: a
# handler that passed "$1" to systemctl would accept every unit systemd knows,
# which is a very different privilege from "reboot or shut down".
if grep -Eq 'systemctl.*\$1' "$handler"; then
    echo 'the handler must not pass its argument through to systemctl' >&2
    exit 1
fi

check_rejected() { # $@ = the argv to refuse
    set +e
    output=$("$handler" "$@" 2>&1)
    status=$?
    set -e
    if [ "$status" -eq 0 ]; then
        echo "the handler accepted [$*]" >&2
        exit 1
    fi
    [ "$status" -eq 64 ] || {
        echo "the handler rejected [$*] with status $status, expected 64" >&2
        exit 1
    }
    printf '%s\n' "$output" | grep -Fxq '[ERROR] power handler requires exactly one action' || {
        echo "the handler rejected [$*] with an unexpected message: $output" >&2
        exit 1
    }
}

# Real systemd verbs that are not in the vocabulary, a case variant, an empty
# argument, and the right word with a passenger.
check_rejected poweroff
check_rejected halt
check_rejected suspend
check_rejected rescue
check_rejected emergency
check_rejected REBOOT
check_rejected ''
check_rejected reboot extra
check_rejected 'reboot; poweroff'

# Both accepted words must reach a real systemd verb, and shutdown must map to
# poweroff rather than to systemd's "shutdown", which schedules rather than acts.
grep -Fq 'target=reboot' "$handler"
grep -Fq 'target=poweroff' "$handler"

echo 'power-handler-policy: PASS'
