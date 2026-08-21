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

# --- iPerf: bounded, validated, and never unlimited ------------------------
# These carry their own messages - a port range and a duration cap are worth
# saying precisely - so only the refusal itself is asserted here.
check_rejected() {
    set +e
    output=$("$handler" "$@" 2>&1)
    status=$?
    set -e
    [ "$status" -eq 64 ] || {
        echo "the handler accepted [$*] with status $status" >&2
        exit 1
    }
    printf '%s\n' "$output" | grep -q '^\[ERROR\]' || {
        echo "the handler refused [$*] without saying why: $output" >&2
        exit 1
    }
}
check_rejected iperf-server eth0 80          # a privileged port
check_rejected iperf-server eth0 notaport
check_rejected iperf-client eth0             # no server
check_rejected iperf-client eth0 1.2.3.4 5201 sctp
check_rejected iperf-client eth0 1.2.3.4 5201 udp 999      # duration cap
check_rejected iperf-client eth0 1.2.3.4 5201 udp 0
check_rejected iperf-client eth0 1.2.3.4 5201 udp 10 '1M;reboot'
check_rejected iperf-client eth0 1.2.3.4 5201 tcp 10 10M maybe
# A flood has to end. iperf3's own -t is what bounds it, and the handler caps
# what may be asked for: an unbounded flood is a denial of service on whatever
# shares the segment.
grep -Fq -e '-t "$duration"' "$handler" || {
    echo 'the iperf client runs without a duration' >&2
    exit 1
}
grep -Fq '[ "$duration" -ge 1 ] && [ "$duration" -le 60 ]' "$handler" || {
    echo 'the iperf client does not cap its duration' >&2
    exit 1
}
# Discovery's output is parsed, not just printed: the panel builds a tappable
# row per server from a fixed "SERVER <address> <port> <name>" prefix. Reword
# that line and the list silently comes back empty, so it is asserted here
# rather than left to the screen to discover.
grep -Fq 'printf "SERVER %s %s %s\n", $8, $9, $7' "$handler" || {
    echo 'discovery no longer emits the SERVER line the panel parses' >&2
    exit 1
}
# A panel running its own server hears its own announcement back - on
# loopback, and on every LAN address it holds. Dialling any of them never
# leaves the machine, so the throughput figure measures nothing. The bench
# panel really did offer itself as two servers before this filter existed.
grep -Fq '$8 !~ /^127\./' "$handler" || {
    echo 'discovery no longer filters its own loopback announcement' >&2
    exit 1
}
grep -Fq '!($8 in mine)' "$handler" || {
    echo 'discovery no longer filters this panel own addresses' >&2
    exit 1
}

# The announcement must name the address iperf3 is bound to. A bare service
# publish points at the panel host name, which a two-link panel resolves
# differently on each interface - so a peer that heard the announcement over
# the other link dials an address nothing is listening on. This is the
# published-address record the service points at with -H.
grep -Fq -e '-R -a "$announce_host" "$source_address"' "$handler" || {
    echo 'the iperf server does not advertise the address it listens on' >&2
    exit 1
}
grep -Fq -e '-s -H "$announce_host"' "$handler" || {
    echo 'the iperf server announcement is not tied to its address record' >&2
    exit 1
}
# ...and it falls back to a plain publish rather than pointing the service at
# a name nothing resolves: an invisible server is worse than a findable one
# advertising an address that can be corrected by hand.
grep -Fq 'address not advertised' "$handler" || {
    echo 'the iperf server has no fallback when the address record fails' >&2
    exit 1
}

# The server must stop advertising when it stops serving: a stale mDNS record
# sends a client somewhere that will time out.
grep -Fq 'trap' "$handler" || {
    echo 'the iperf server does not withdraw its advertisement' >&2
    exit 1
}

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
# iperf3 has no -I; it binds by source address, which the handler takes from
# the chosen interface rather than letting the route table decide.
grep -Fq -e '-B "$source_address"' "$handler" || {
    echo 'iperf3 is not bound to the interface address' >&2
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
