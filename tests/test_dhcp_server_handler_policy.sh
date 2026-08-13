#!/bin/sh
set -eu

static_handler=$1
dhcp_handler=$2
server_handler=$3

for handler in "$static_handler" "$dhcp_handler"; do
    # Core DHCP-client/static handlers remain valid on existing/minimal
    # installations without the appliance-specific server unit. The persistent
    # marker removal is always harmless, but stopping the unit is conditional.
    grep -Fq 'rm -f /data/micropanel-touch-network/dhcp-server/enabled' "$handler"
    grep -Fq 'if /usr/bin/systemctl cat micropanel-touch-dhcp-server.service >/dev/null 2>&1; then' "$handler"
    grep -Fq 'if ! /usr/bin/systemctl stop micropanel-touch-dhcp-server.service; then' "$handler"
done

# An isolated provisioning link must never advertise the panel as a default
# router or DNS service. Dynamic binding avoids a boot race with NetworkManager
# assigning the saved address while retaining the single eth0 interface scope.
grep -Fxq 'bind-dynamic' "$server_handler"
if grep -Fxq 'bind-interfaces' "$server_handler"; then
    printf '%s\n' 'unexpected static dnsmasq binding mode' >&2
    exit 1
fi
grep -Fxq 'dhcp-option=option:router' "$server_handler"
grep -Fxq 'dhcp-option=option:dns-server' "$server_handler"
grep -Fxq 'dhcp-leasefile=/run/micropanel-touch-dhcp-server/dnsmasq.leases' "$server_handler"
