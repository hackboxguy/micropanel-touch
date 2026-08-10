#!/bin/sh
# Run as root after installing micropanel-touch and its systemd unit.
set -eu

unit=${1:-micropanel-touch.service}
started=false

cleanup() {
    if [ "$started" = true ]; then
        systemctl stop "$unit" || true
    fi
}
trap cleanup EXIT HUP INT TERM

systemctl daemon-reload
systemctl start "$unit"
started=true
systemctl is-active --quiet "$unit"
echo "$unit is active"
