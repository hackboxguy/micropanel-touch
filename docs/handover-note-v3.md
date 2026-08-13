# MicroPanel Touch handover — v3

**Prepared:** 2026-08-13
**Supersedes:** [`handover-note-v2.md`](handover-note-v2.md) as the current
restart point. Version 2 remains the historical record of the accepted
overlay-root and boot-console work.

## Current implementation state

- The current image profile pins the app commit named in
  `misc-tools/board-configs/micropanel-touch/hooks.txt`. The app includes the
  DHCP-Client, Static-address, and DHCP-server IP Settings modes, plus the
  physical-panel dropdown and button-spacing fixes.
- DHCP server is an **eth0-only isolated provisioning mode**. A separate typed
  broker request validates a private server subnet and lease range, then calls
  a fixed-argv handler. It is never a generic root command.
- The image installs dnsmasq but masks its distribution unit. Only the
  dedicated, marker-gated MicroPanel service may run it. Its generated config
  has dynamic eth0 binding, no DNS listener, and explicitly suppresses DHCP
  router and DNS options. It does not provide forwarding or NAT.
- DHCP-server settings persist below root-owned, UI-group-readable `/data`.
  Leases intentionally remain below `/run`; connected clients rediscover after
  a panel reboot. Static-address and DHCP-client remove the marker and stop
  the dedicated service. They remain usable on existing/minimal installations
  because they stop that appliance unit only when it exists.
- DHCP-server mode currently requires a multi-address lease range. A
  single-address pool is not a supported v1 use case; add explicit validation
  only if that deployment becomes necessary.
- The current live-image baseline has been checked for active HMI/broker, no
  failed units, direct p3 `/data`, read-only boot firmware, an inactive
  DHCP-server service before enablement, and a masked generic dnsmasq service.

## Verification completed locally

- Typed broker validation, malformed-request rejection, asynchronous client,
  and real `CommandRunner` cancellation coverage pass.
- Headless UI coverage verifies the three mode labels, server defaults,
  confirmation flow, lease-start control alias, dropdown divider geometry, and
  distinct server-form button spacing.
- Handler policy coverage verifies router/DNS suppression, dynamic binding,
  volatile lease location, and the conditional appliance-service stop that
  preserves Tier-1 portability.

## Hardware acceptance still required

Do not enable DHCP Server on the normal LAN. It intentionally changes eth0's
address and terminates normal SSH/LAN access; a second DHCP authority would be
unsafe. Use a directly connected client or isolated switch/VLAN with no other
DHCP server.

1. Run static-address → reboot → verify `manual` profile → DHCP-client →
   reboot → verify `auto` profile through the UI broker.
2. Run the isolated DHCP-server procedure in the board
   `misc-tools/board-configs/micropanel-touch/BUILD.md`:
   lease within range, panel reachability, no default route/DNS option,
   reboot/re-discovery, then clean return to DHCP client.

## Remaining Sprint 2.5 work

1. Complete and record the two hardware network acceptance sequences above.
2. Implement and validate the panel-profile/capacitive-panel seam.
3. Implement display sleep/wake using a verified backlight path.
4. Continue write-path inventory and later power-cut testing. Service sandbox
   tightening (`ProtectSystem`, write-path and capability restrictions) remains
   part of the Sprint 6 release-hardening pass.
