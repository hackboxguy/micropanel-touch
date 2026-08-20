#!/bin/sh
# The diagnostics handler takes a test name, an interface and an optional
# target, and turns them into fixed commands. What is worth testing is what it
# refuses, and that every command it builds is bound to the interface it was
# given - an unbound test on a panel with two links measures whichever one the
# route table prefers and reports it as the answer.
set -eu

handler=$1
test -x "$handler"

check_refused() {
    set +e
    output=$("$handler" "$@" 2>&1)
    status=$?
    set -e
    [ "$status" -eq 64 ] || {
        echo "the handler accepted [$*] with status $status" >&2
        exit 1
    }
    printf '%s\n' "$output" |
        grep -Fxq '[ERROR] net test handler requires a test and an interface' || {
        echo "the handler refused [$*] with an unexpected message: $output" >&2
        exit 1
    }
}

# Too few arguments, an unknown test, and an interface name that is not one.
check_refused
check_refused ping
check_refused bogus eth0
check_refused ping ''
check_refused ping 'eth0;reboot'
check_refused ping 'eth0 -c99'
check_refused ping '../../etc/passwd'
# A target must be a host, never an option: a leading dash would let a caller
# push flags into ping or curl.
check_refused ping eth0 '-oProxyCommand=x'
check_refused ping eth0 '8.8.8.8;reboot'
check_refused ping eth0 '$(reboot)'

# Every command is interface-bound, and by absolute path.
grep -Fq 'ping=/usr/bin/ping' "$handler"
grep -Fq 'curl=/usr/bin/curl' "$handler"
grep -Fq -e '-I "$interface"' "$handler" || {
    echo 'ping is not bound to the interface' >&2
    exit 1
}
grep -Fq -e '--interface "$interface"' "$handler" || {
    echo 'curl is not bound to the interface' >&2
    exit 1
}
# Nothing may be built by the shell from its input.
if grep -Eq 'eval|`|sh -c' "$handler"; then
    echo 'the handler builds a command from its input' >&2
    exit 1
fi
# Every test is bounded: an unbounded probe would hold the screen forever.
grep -Fq -e '-c 4' "$handler" || { echo 'ping is unbounded' >&2; exit 1; }
grep -Fq -e '--max-time' "$handler" || { echo 'curl is unbounded' >&2; exit 1; }

echo 'net-test-handler-policy: PASS'
