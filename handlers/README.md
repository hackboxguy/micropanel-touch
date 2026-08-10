# Tier-1 handlers

This directory contains the portable, dependency-light command handlers that
ship with MicroPanel Touch. CMake installs executable handlers to
`$PREFIX/usr/bin`; development creates an explicit execution context rooted at
this source directory instead. Domain-pack tools, bitstreams, and board-only
scripts do not belong here.

Handler requirements:

- Accept only the fixed argv schema assigned by `ActionCompiler`; reject extra
  or malformed arguments before performing work. Do not parse an untrusted
  shell string or evaluate user input.
- Use standard Linux interfaces only: coreutils, `/sys`, iproute2, and
  NetworkManager where applicable. Hardware-specific work stays behind a
  platform/broker capability.
- Emit `Progress: N%` for determinate work, `[SUCCESS]` on success, and an
  `[ERROR]` line plus a non-zero exit status on failure. These are the
  ActionRunner result markers defined in `action-execution-contract.md`.
- Never print credentials, tokens, or Wi-Fi passwords. Sensitive values are
  omitted from output, logs, diagnostics, and test expectations.

`micropanel-touch-simulated-flash` is a development/demo action. It takes no
arguments and exists to exercise the same fixed-argv progress and cancellation
path future core handlers use.

`micropanel-touch-network-static-ip` is different: only the root-owned
privileged broker may invoke it, with exactly interface/address/prefix/gateway
arguments after independently validating the typed request and its peer UID.
It resolves the active NetworkManager profile itself, then passes that profile
only as a quoted data argument to fixed `nmcli connection modify` and
`connection up` commands. It must never be started directly by the UI.
