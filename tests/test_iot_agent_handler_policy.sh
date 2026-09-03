#!/bin/sh
# The IoT agent handlers write a secret to disk and start/stop a daemon, so
# the properties worth testing are where the secret does not go, what survives
# in the file they rewrite, and what a malformed request cannot make them do.
set -eu

handler=$1
control=$2

test -x "$handler"
test -x "$control"

work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

config=$work/etc
mkdir -p "$config"
login=$config/xmpp-login.txt
marker=$config/disabled

# A stand-in systemctl that records its own argv.
systemctl=$work/systemctl
cat > "$systemctl" <<'STUB'
#!/bin/sh
printf '%s\n' "$*" >> "$SYSTEMCTL_CALLS"
exit "${SYSTEMCTL_EXIT:-0}"
STUB
chmod +x "$systemctl"
SYSTEMCTL_CALLS=$work/systemctl-calls
export SYSTEMCTL_CALLS

run() { # $@=argv, stdin=password
    : > "$SYSTEMCTL_CALLS"
    MICROPANEL_TOUCH_ALLOW_UNPRIVILEGED_TEST=1 \
    MICROPANEL_TOUCH_IOT_AGENT_CONFIG_DIR="$config" \
    MICROPANEL_TOUCH_SYSTEMCTL="$systemctl" \
        "$handler" "$@"
}
run_control() {
    : > "$SYSTEMCTL_CALLS"
    MICROPANEL_TOUCH_ALLOW_UNPRIVILEGED_TEST=1 \
    MICROPANEL_TOUCH_IOT_AGENT_CONFIG_DIR="$config" \
    MICROPANEL_TOUCH_SYSTEMCTL="$systemctl" \
        "$control" "$@"
}

# systemctl by absolute path, and no command built from an argument.
for h in "$handler" "$control"; do
    grep -Fq '/usr/bin/systemctl' "$h"
    if grep -Eq 'eval|`|sh -c' "$h"; then
        echo 'the handler must not build a command from its argument' >&2
        exit 1
    fi
    # $(id -u) is the one allowed substitution: it takes no input.
    if grep -E '\$\(' "$h" | grep -vFq '$(id -u)'; then
        echo 'the handler must not build a command from its argument' >&2
        exit 1
    fi
done
if grep -Eq 'systemctl.*\$1' "$control"; then
    echo 'the control handler must not pass its argument through to systemctl' >&2
    exit 1
fi

# --- a fresh file: the primary keys, nothing else ---------------------------
: > "$marker"
printf '%s\n' 's3cret-Pa55' | run 'bot@example.org' - - no - - - > "$work/out" 2>&1
grep -Fxq '[SUCCESS] IoT agent settings applied' "$work/out"
if grep -Fq 's3cret-Pa55' "$work/out"; then
    echo 'the handler printed the password' >&2
    exit 1
fi
if grep -Fq 's3cret-Pa55' "$SYSTEMCTL_CALLS"; then
    echo 'the handler passed the password to systemctl' >&2
    exit 1
fi
grep -Fxq 'user: bot@example.org' "$login"
grep -Fxq 'pw: s3cret-Pa55' "$login"
for absent in server port bosh boshurl boshhost adminbuddy; do
    if grep -q "^$absent:" "$login"; then
        echo "an absent $absent must not produce a line" >&2
        exit 1
    fi
done
grep -Fxq 'restart --no-block xmproxysrv.service' "$SYSTEMCTL_CALLS"
mode=$(stat -c '%a' "$login")
[ "$mode" = 640 ] || { echo "login file mode is $mode, expected 640" >&2; exit 1; }
[ ! -e "$login.new" ]
[ ! -e "$marker" ] || { echo 'applying an account must clear the disabled marker' >&2; exit 1; }

# --- a rewrite keeps every line that is not the primary account -------------
cat > "$login" <<'SEED'
user: old@example.org
pw: old-secret
server: old.example.org
port: 5223
adminbuddy: owner@example.org
# tuning
heartbeat: 60
fallbackuser: bot@backup.example.org
fallbackpw: backup-secret
SEED
printf '%s\n' 'new-secret-9' | run 'bot@example.org' 'xmpp.example.org' 5222 yes \
    'https://xmpp.example.org:5281/http-bind' 'example.org' - > "$work/out" 2>&1
grep -Fxq '[SUCCESS] IoT agent settings applied' "$work/out"
grep -Fxq 'user: bot@example.org' "$login"
grep -Fxq 'pw: new-secret-9' "$login"
grep -Fxq 'server: xmpp.example.org' "$login"
grep -Fxq 'port: 5222' "$login"
grep -Fxq 'bosh: true' "$login"
grep -Fxq 'boshurl: https://xmpp.example.org:5281/http-bind' "$login"
grep -Fxq 'boshhost: example.org' "$login"
grep -Fxq 'adminbuddy: owner@example.org' "$login"   # "-" keeps the file's admin
grep -Fxq '# tuning' "$login"
grep -Fxq 'heartbeat: 60' "$login"
grep -Fxq 'fallbackuser: bot@backup.example.org' "$login"
grep -Fxq 'fallbackpw: backup-secret' "$login"
for gone in 'user: old@example.org' 'pw: old-secret' 'server: old.example.org' 'port: 5223'; do
    if grep -Fxq "$gone" "$login"; then
        echo "the old line '$gone' survived the rewrite" >&2
        exit 1
    fi
done
[ "$(grep -c '^user:' "$login")" -eq 1 ]
[ "$(grep -c '^pw:' "$login")" -eq 1 ]

# --- turning BOSH off drops its lines; a given admin replaces the old one ---
printf '%s\n' 'new-secret-9' | run 'bot@example.org' - - no - - 'boss@example.org' > "$work/out" 2>&1
grep -Fxq '[SUCCESS] IoT agent settings applied' "$work/out"
for gone in 'bosh:' 'boshurl:' 'boshhost:' 'port:' 'server:'; do
    if grep -q "^$gone" "$login"; then
        echo "the line '$gone' survived a request without it" >&2
        exit 1
    fi
done
grep -Fxq 'adminbuddy: boss@example.org' "$login"
[ "$(grep -c '^adminbuddy:' "$login")" -eq 1 ]
grep -Fxq 'fallbackpw: backup-secret' "$login"

# --- a failed restart is reported, and the file is still written ------------
if printf '%s\n' 'another-secret' | SYSTEMCTL_EXIT=1 run 'bot@example.org' - - no - - - > "$work/out" 2>&1; then
    echo 'the handler reported success although systemctl failed' >&2
    exit 1
fi
grep -Fxq '[ERROR] The IoT agent could not be restarted' "$work/out"
grep -Fxq 'pw: another-secret' "$login"

# --- what it refuses ----------------------------------------------------------
cp "$login" "$work/before"
check_rejected() { # $1=password, $2...=argv
    password=$1
    shift
    set +e
    output=$(printf '%s\n' "$password" | run "$@" 2>&1)
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
    if [ -s "$SYSTEMCTL_CALLS" ]; then
        echo "the handler restarted the agent for a rejected request [$*]" >&2
        exit 1
    fi
}
check_rejected 'secret' '' - - no - - -
check_rejected 'secret' 'no-at-sign' - - no - - -
check_rejected 'secret' '@example.org' - - no - - -
check_rejected 'secret' 'bot@' - - no - - -
check_rejected 'secret' 'bot@example.org/resource' - - no - - -
check_rejected 'secret' 'bot@example.org
adminbuddy: attacker@example.org' - - no - - -
check_rejected 'secret' 'bot@example.org' 'xmpp.example.org'
check_rejected 'secret' 'bot@example.org' 'xmpp.example.org' - no - - - extra
check_rejected 'secret' 'bot@example.org' 'host with space' - no - - -
check_rejected 'secret' 'bot@example.org' - 0 no - - -
check_rejected 'secret' 'bot@example.org' - 70000 no - - -
check_rejected 'secret' 'bot@example.org' - 52a2 no - - -
check_rejected 'secret' 'bot@example.org' - - maybe - - -
check_rejected 'secret' 'bot@example.org' - - yes - - -
check_rejected 'secret' 'bot@example.org' - - yes 'ftp://x/y' - -
check_rejected 'secret' 'bot@example.org' - - yes 'https://x/y z' - -
check_rejected 'secret' 'bot@example.org' - - no - 'host name' -
check_rejected 'secret' 'bot@example.org' - - no - - 'not-a-jid'
check_rejected '' 'bot@example.org' - - no - - -
check_rejected 'two words' 'bot@example.org' - - no - - -
cmp -s "$login" "$work/before" || {
    echo 'a rejected request changed the login file' >&2
    exit 1
}

# --- the control handler: stop leaves a marker, start removes it ------------
run_control stop > "$work/out" 2>&1
grep -Fxq '[SUCCESS] IoT agent stopped' "$work/out"
[ -e "$marker" ] || { echo 'stop must leave the disabled marker' >&2; exit 1; }
grep -Fxq 'stop --no-block xmproxysrv.service' "$SYSTEMCTL_CALLS"
run_control start > "$work/out" 2>&1
grep -Fxq '[SUCCESS] IoT agent starting' "$work/out"
[ ! -e "$marker" ] || { echo 'start must remove the disabled marker' >&2; exit 1; }
grep -Fxq 'restart --no-block xmproxysrv.service' "$SYSTEMCTL_CALLS"
cmp -s "$login" "$work/before" || { echo 'the control handler changed the login file' >&2; exit 1; }
for bad in restart '' 'stop extra' STOP 'stop; rm -rf /'; do
    set +e
    # shellcheck disable=SC2086
    output=$(run_control $bad 2>&1)
    status=$?
    set -e
    [ "$status" -eq 64 ] || { echo "the control handler accepted [$bad] ($status)" >&2; exit 1; }
    [ ! -s "$SYSTEMCTL_CALLS" ] || { echo "the control handler ran systemctl for [$bad]" >&2; exit 1; }
done
if SYSTEMCTL_EXIT=1 run_control stop > "$work/out" 2>&1; then
    echo 'the control handler reported success although systemctl failed' >&2
    exit 1
fi
grep -Fxq '[ERROR] The IoT agent could not be stopped' "$work/out"

echo 'iot-agent-handler-policy: PASS'
