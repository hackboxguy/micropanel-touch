#!/bin/sh
# The Wi-Fi handlers are where a secret is written to disk, so the properties
# worth testing are about where the secret does *not* go, and about what a
# malformed network name cannot make the handler do.
set -eu

join_handler=$1
forget_handler=$2

work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

profiles=$work/system-connections
mkdir -p "$profiles"
profile=$profiles/micropanel-touch-wifi.nmconnection

# A stand-in nmcli that records its own argv, so the test can assert what the
# handler asked NetworkManager to do - and what it did not pass along.
nmcli=$work/nmcli
cat > "$nmcli" <<'STUB'
#!/bin/sh
printf '%s\n' "$*" >> "$NMCLI_CALLS"
exit "${NMCLI_EXIT:-0}"
STUB
chmod +x "$nmcli"
NMCLI_CALLS=$work/nmcli-calls
export NMCLI_CALLS

run_join() { # $1=ssid, stdin=passphrase
    : > "$NMCLI_CALLS"
    MICROPANEL_TOUCH_ALLOW_UNPRIVILEGED_TEST=1 \
    MICROPANEL_TOUCH_WIFI_PROFILE_DIR="$profiles" \
    MICROPANEL_TOUCH_NMCLI="$nmcli" \
        "$join_handler" "$1"
}

run_forget() {
    : > "$NMCLI_CALLS"
    MICROPANEL_TOUCH_ALLOW_UNPRIVILEGED_TEST=1 \
    MICROPANEL_TOUCH_WIFI_PROFILE_DIR="$profiles" \
    MICROPANEL_TOUCH_NMCLI="$nmcli" \
        "$forget_handler" "$@"
}

# --- the secret reaches the keyfile, and nothing else ------------------------
printf '%s\n' 'correct-horse-battery' | run_join 'Bench AP' > "$work/join-output" 2>&1
grep -Fxq '[SUCCESS] Joined the network' "$work/join-output"
# The success line must not repeat the secret back to the operator.
if grep -Fq 'correct-horse-battery' "$work/join-output"; then
    echo 'the join handler printed the passphrase' >&2
    exit 1
fi
# ...nor hand it to NetworkManager on a command line.
if grep -Fq 'correct-horse-battery' "$NMCLI_CALLS"; then
    echo 'the join handler passed the passphrase to nmcli' >&2
    exit 1
fi
grep -Fxq 'psk=correct-horse-battery' "$profile"
grep -Fxq 'ssid=Bench AP' "$profile"
grep -Fxq 'key-mgmt=wpa-psk' "$profile"
# Root-only. The whole credential decision rests on this mode.
mode=$(stat -c %a "$profile")
[ "$mode" = 600 ] || { echo "keyfile mode is $mode, expected 600" >&2; exit 1; }
# No leftover half-written file under a name NetworkManager would load.
[ ! -e "$profile.new" ] || { echo 'a temporary profile survived' >&2; exit 1; }

# --- GKeyFile escaping ------------------------------------------------------
# NetworkManager parses the profile with GKeyFile, where a backslash starts an
# escape sequence and a leading space is stripped. All three of these are legal
# WPA passphrases, and all three were measured breaking against NetworkManager
# on the bench before the handler escaped them: an invalid escape made the
# whole value read back empty, "\s" became a space, and the leading space
# vanished. Assert the bytes on disk, which is what NetworkManager parses.
printf '%s\n' 'hunter\2secret' | run_join 'Bench AP' > /dev/null 2>&1
grep -Fxq 'psk=hunter\\2secret' "$profile" || {
    echo 'a backslash in the passphrase was not escaped' >&2
    exit 1
}
printf '%s\n' 'hunter\ssecret' | run_join 'Bench AP' > /dev/null 2>&1
grep -Fxq 'psk=hunter\\ssecret' "$profile" || {
    echo 'a literal backslash-s was not escaped' >&2
    exit 1
}
printf '%s\n' ' leading space pw' | run_join 'Bench AP' > /dev/null 2>&1
grep -Fxq 'psk=\sleading space pw' "$profile" || {
    echo 'a leading space in the passphrase was not escaped' >&2
    sed -n 's/^psk=/  got: /p' "$profile" >&2
    exit 1
}
# An SSID is exposed to exactly the same parser.
printf '%s\n' 'irrelevant' | run_join 'Odd\Name AP' > /dev/null 2>&1
grep -Fxq 'ssid=Odd\\Name AP' "$profile" || {
    echo 'a backslash in the network name was not escaped' >&2
    exit 1
}
printf '%s\n' 'irrelevant' | run_join ' Leading AP' > /dev/null 2>&1
grep -Fxq 'ssid=\sLeading AP' "$profile" || {
    echo 'a leading space in the network name was not escaped' >&2
    exit 1
}
# Characters that were measured to round-trip untouched must not be mangled by
# an over-eager escaper.
printf '%s\n' 'a;b#c=d[e] f ' | run_join 'Bench AP' > /dev/null 2>&1
grep -Fxq 'psk=a;b#c=d[e] f ' "$profile" || {
    echo 'the escaper altered characters that need no escaping' >&2
    exit 1
}

# --- an open network is the same path with no security section --------------
printf '' | run_join 'Open AP' > /dev/null 2>&1
grep -Fxq 'ssid=Open AP' "$profile"
if grep -q '^psk=' "$profile"; then
    echo 'an open network was written with a key' >&2
    exit 1
fi
if grep -Fq 'wifi-security' "$profile"; then
    echo 'an open network was written with a security section' >&2
    exit 1
fi

# --- joining again replaces, rather than accumulating -----------------------
printf '%s\n' 'second-secret' | run_join 'Second AP' > /dev/null 2>&1
[ "$(find "$profiles" -name '*.nmconnection' | wc -l)" -eq 1 ] || {
    echo 'joining a second network left more than one saved profile' >&2
    exit 1
}
if grep -Fq 'correct-horse-battery' "$profile"; then
    echo 'a replaced profile kept the previous passphrase' >&2
    exit 1
fi

# --- a network name can never become a path or a second file ----------------
for hostile in '../escape' '/etc/passwd' 'a/b'; do
    printf '%s\n' 'irrelevant' | run_join "$hostile" > /dev/null 2>&1
    [ "$(find "$profiles" -name '*.nmconnection' | wc -l)" -eq 1 ] || {
        echo "the network name \"$hostile\" created a second file" >&2
        exit 1
    }
    grep -Fxq "ssid=$hostile" "$profile"
done
[ ! -e "$work/escape" ] || { echo 'a network name escaped the profile directory' >&2; exit 1; }

# --- refusals ---------------------------------------------------------------
check_join_refused() {
    set +e
    output=$(printf '%s\n' 'irrelevant' | run_join "$1" 2>&1)
    status=$?
    set -e
    [ "$status" -eq 64 ] || {
        echo "the join handler accepted [$1] with status $status" >&2
        exit 1
    }
    printf '%s\n' "$output" |
        grep -Fxq '[ERROR] Wi-Fi join handler requires exactly one network name' || {
        echo "the join handler refused [$1] with an unexpected message: $output" >&2
        exit 1
    }
}
check_join_refused ''
check_join_refused 'aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa'   # 33 octets

# --- forget removes it, and is not an error when nothing is saved -----------
run_forget > "$work/forget-output" 2>&1
grep -Fxq '[SUCCESS] Forgot the saved network' "$work/forget-output"
[ ! -e "$profile" ] || { echo 'forget left the profile in place' >&2; exit 1; }
grep -Fq 'connection down micropanel-touch-wifi' "$NMCLI_CALLS" || {
    echo 'forget did not take the connection down before deleting it' >&2
    exit 1
}
run_forget > /dev/null 2>&1   # forgetting nothing is the requested end state

set +e
run_forget unexpected > "$work/forget-refusal" 2>&1
status=$?
set -e
[ "$status" -eq 64 ] || { echo 'the forget handler accepted an argument' >&2; exit 1; }
grep -Fxq '[ERROR] Wi-Fi forget handler accepts no arguments' "$work/forget-refusal"

# --- a NetworkManager failure is a failure ----------------------------------
NMCLI_EXIT=1
export NMCLI_EXIT
set +e
printf '%s\n' 'irrelevant' | run_join 'Bench AP' > "$work/failed-join" 2>&1
status=$?
set -e
[ "$status" -ne 0 ] || { echo 'a failed nmcli reported success' >&2; exit 1; }
grep -q '^\[ERROR\]' "$work/failed-join"
unset NMCLI_EXIT

# --- connect / disconnect keep the credential -------------------------------
profile_handler=${3:-}
if [ -n "$profile_handler" ]; then
    run_profile() {
        : > "$NMCLI_CALLS"
        MICROPANEL_TOUCH_ALLOW_UNPRIVILEGED_TEST=1 MICROPANEL_TOUCH_NMCLI="$nmcli" \
            "$profile_handler" "$1"
    }
    run_profile disconnect > "$work/disc" 2>&1
    grep -Fxq '[SUCCESS] Disconnected' "$work/disc"
    # Order matters and is the whole point: the profile exists so
    # NetworkManager rejoins on its own, so dropping the link without first
    # clearing autoconnect races the reconnect and the button looks broken.
    first=$(head -1 "$NMCLI_CALLS")
    case "$first" in
        *"connection.autoconnect no"*) ;;
        *) echo "disconnect did not clear autoconnect first, got: $first" >&2; exit 1 ;;
    esac
    grep -Fq 'connection down micropanel-touch-wifi' "$NMCLI_CALLS" || {
        echo 'disconnect did not bring the connection down' >&2; exit 1; }
    # It must never delete anything: the password survives a disconnect.
    if grep -Eq 'connection delete|rm ' "$profile_handler"; then
        echo 'the profile handler removes something; only forget may do that' >&2
        exit 1
    fi

    run_profile connect > "$work/conn" 2>&1
    grep -Fxq '[SUCCESS] Reconnected' "$work/conn"
    grep -Fq 'connection.autoconnect yes' "$NMCLI_CALLS" || {
        echo 'connect did not restore autoconnect' >&2; exit 1; }

    for rejected in '' up down enable 'connect extra' CONNECT; do
        set +e
        # shellcheck disable=SC2086
        output=$(MICROPANEL_TOUCH_ALLOW_UNPRIVILEGED_TEST=1 MICROPANEL_TOUCH_NMCLI="$nmcli" \
            "$profile_handler" $rejected 2>&1)
        status=$?
        set -e
        [ "$status" -eq 64 ] || {
            echo "the profile handler accepted [$rejected] with status $status" >&2
            exit 1
        }
        printf '%s\n' "$output" |
            grep -Fxq '[ERROR] Wi-Fi profile handler requires exactly one action' || {
            echo "the profile handler refused [$rejected] oddly: $output" >&2; exit 1; }
    done
fi

# --- the test-only override is guarded -------------------------------------
# Asserted from the source rather than by running the handler unguarded: doing
# that on a build host would write a real NetworkManager profile into /etc.
for handler in "$join_handler" "$forget_handler"; do
    grep -Fq 'if [ "${MICROPANEL_TOUCH_ALLOW_UNPRIVILEGED_TEST:-0}" = 1 ]; then' "$handler" || {
        echo 'the path override is not behind the test flag' >&2
        exit 1
    }
    grep -Fq "profile_dir_override=''" "$handler" || {
        echo 'the unguarded branch does not clear the override' >&2
        exit 1
    }
    # The passphrase must never be built into an argument list.
    if grep -Eq 'nmcli.*passphrase|passphrase.*nmcli' "$handler"; then
        echo 'the handler passes the passphrase to nmcli' >&2
        exit 1
    fi
    if grep -Eq 'eval|`|sh -c' "$handler"; then
        echo 'the handler builds a command from its input' >&2
        exit 1
    fi
done
# The join handler reads its secret from stdin and from nowhere else.
grep -Fq 'IFS= read -r passphrase' "$join_handler"
if grep -Eq '^\s*passphrase=\$[1-9]' "$join_handler"; then
    echo 'the join handler takes the passphrase as an argument' >&2
    exit 1
fi

echo 'wifi-handler-policy: PASS'
