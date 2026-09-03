#!/bin/sh
# The IoT agent handler writes a secret to disk and restarts a daemon, so the
# properties worth testing are where the secret does not go, what survives in
# the file it rewrites, and what a malformed account cannot make it do.
set -eu

handler=$1

test -x "$handler"

work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

config=$work/etc
mkdir -p "$config"
login=$config/xmpp-login.txt

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

# systemctl by absolute path, and no command built from an argument.
grep -Fq '/usr/bin/systemctl' "$handler"
if grep -Eq 'eval|\$\(|`|sh -c' "$handler" ; then
    # $(id -u) is the one allowed substitution: it takes no input.
    if [ "$(grep -Ec 'eval|\$\(|`|sh -c' "$handler")" -ne 1 ] ||
       ! grep -Fq '"$(id -u)"' "$handler"; then
        echo 'the handler must not build a command from its argument' >&2
        exit 1
    fi
fi

# --- a fresh file: the three primary keys, nothing else --------------------
printf '%s\n' 's3cret-Pa55' | run 'bot@example.org' > "$work/out" 2>&1
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
if grep -q '^server:' "$login"; then
    echo 'an absent server must not produce a server line' >&2
    exit 1
fi
grep -Fxq 'restart --no-block xmproxysrv.service' "$SYSTEMCTL_CALLS"
mode=$(stat -c '%a' "$login")
[ "$mode" = 640 ] || { echo "login file mode is $mode, expected 640" >&2; exit 1; }
[ ! -e "$login.new" ]

# --- a rewrite keeps every line that is not the primary account -------------
cat > "$login" <<'SEED'
user: old@example.org
pw: old-secret
server: old.example.org
adminbuddy: owner@example.org
# tuning
heartbeat: 60
fallbackuser: bot@backup.example.org
fallbackpw: backup-secret
SEED
printf '%s\n' 'new-secret-9' | run 'bot@example.org' 'xmpp.example.org' > "$work/out" 2>&1
grep -Fxq '[SUCCESS] IoT agent settings applied' "$work/out"
grep -Fxq 'user: bot@example.org' "$login"
grep -Fxq 'pw: new-secret-9' "$login"
grep -Fxq 'server: xmpp.example.org' "$login"
grep -Fxq 'adminbuddy: owner@example.org' "$login"
grep -Fxq '# tuning' "$login"
grep -Fxq 'heartbeat: 60' "$login"
grep -Fxq 'fallbackuser: bot@backup.example.org' "$login"
grep -Fxq 'fallbackpw: backup-secret' "$login"
for gone in 'user: old@example.org' 'pw: old-secret' 'server: old.example.org'; do
    if grep -Fxq "$gone" "$login"; then
        echo "the old line '$gone' survived the rewrite" >&2
        exit 1
    fi
done
[ "$(grep -c '^user:' "$login")" -eq 1 ]
[ "$(grep -c '^pw:' "$login")" -eq 1 ]

# --- a failed restart is reported, and the file is still written ------------
if printf '%s\n' 'another-secret' | SYSTEMCTL_EXIT=1 run 'bot@example.org' > "$work/out" 2>&1; then
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
check_rejected 'secret' ''
check_rejected 'secret' 'no-at-sign'
check_rejected 'secret' '@example.org'
check_rejected 'secret' 'bot@'
check_rejected 'secret' 'bot@example.org/resource'
check_rejected 'secret' 'bot@example.org
adminbuddy: attacker@example.org'
check_rejected 'secret' 'bot@example.org' 'xmpp.example.org' 'extra'
check_rejected 'secret' 'bot@example.org' 'host with space'
check_rejected '' 'bot@example.org'
check_rejected 'two words' 'bot@example.org'
cmp -s "$login" "$work/before" || {
    echo 'a rejected request changed the login file' >&2
    exit 1
}

echo 'iot-agent-handler-policy: PASS'
