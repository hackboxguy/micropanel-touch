# Action execution contract

**Status:** binding for Sprint 2 engine work (2026-08-10).

This is the contract between the JSON loader, `ActionRunner`, the single
`CommandService`, Tier-1 handlers, and the privileged broker. It makes the
requirements in [the PRD](micropanel-touch-prd.md#66-privilege-and-execution-security-model)
testable before any production action is wired to the UI. A change to this
document requires the corresponding golden or lifecycle test to change in the
same commit.

It deliberately does not define the final JSON schema. Existing action strings
must load unchanged; internally, however, every action is compiled into a
validated execution request before it can run.

## Invariants

1. The UI process runs as the unprivileged `micropanel-touch` account. UI code
   never invokes `sudo`, never accepts a raw privileged command, and is never
   granted a broad writable filesystem or device capability merely for a menu
   entry.
2. `CommandService` is the only route for external commands. This applies to
   `ActionRunner`, built-ins, `textbox` refreshes, and future dynamic-list
   providers alike. `async` controls presentation, not whether a command may
   bypass the service.
3. The normal execution form is a fixed executable plus an argument vector.
   No untrusted value is concatenated into a command string.
4. Each invocation has one owner, one cancellation token, a timeout, an output
   limit, and one terminal result. Workers post immutable events through
   `UiEventQueue`; only the UI thread uses LVGL.
5. At most one action job may be running at a time in v1. A second activation
   is rejected with a visible busy result; it is not silently queued.

## Execution context and compatibility paths

The application constructs `ExecutionContext` at startup; it is not inherited
from a caller's environment. Its initial fields and production defaults are:

| Field | Production value | Purpose |
|---|---|---|
| `micropanel_home` | CMake install prefix | Expands `$MICROPANEL_HOME`; the installed handlers therefore resolve under `$MICROPANEL_HOME/usr/bin`. |
| `config_dir` | `$MICROPANEL_HOME/share/micropanel-touch/screens` | Root used to resolve config-relative assets. |
| `data_dir` | `/var/lib/micropanel-touch` (backed by the `data` partition) | Persistent state and user-owned data. |
| `log_dir` | `$MICROPANEL_DATA/logs` | Managed action logs; created with restrictive ownership and mode. |
| `runtime_dir` | `/run/micropanel-touch` | Sockets, transient state and job metadata; never persistent. |
| `handler_dir` | `$MICROPANEL_HOME/usr/bin` | Package-owned Tier-1 handler executables. Development explicitly selects the source `handlers/` directory instead. |

Development and test contexts supply these values explicitly. A process
environment variable with the same name does not override them.

Expansion is a single, deterministic pass over execution-bearing fields only:
`action`, `items_source`, `items_action`, `list_selection`, and
`depends.script_path`. The recognized context tokens are
`$MICROPANEL_HOME`, `$MICROPANEL_DATA`, and `$MICROPANEL_LOG`. Runtime values
such as `$INTERFACE` and a list selection's `$1` are separately typed bindings,
not ambient environment variables. Unknown tokens, recursive expansion, a
NUL byte, or an expansion that escapes an allowed root are validation errors.
The source JSON is read-only and is never rewritten.

Older hard-coded paths are handled by a small, versioned compatibility map
before command compilation, not by a general text rewrite or a mutable
symlink farm. The initial map is:

| Legacy path | Replacement |
|---|---|
| `/home/pi/micropanel` prefix | `micropanel_home` |
| `/home/pi/micropanel/settings.json` | `data_dir/settings.json` |
| legacy action log below `/tmp` | a validated basename under `log_dir` |

Entries are exact paths or path prefixes with segment boundaries. Every
additional mapping needs a config fixture and a golden test for the complete,
expanded command. Paths such as `/boot/firmware/config.txt` remain literal;
they are not remapped just because they are outside the app tree.

## Command forms and dynamic values

The loader compiles a config action into one of two forms:

| Form | Use | Rule |
|---|---|---|
| `Argv` | Tier-1 handlers and all new native actions | Executable and argument array are fixed by the handler/action definition; typed values are validated then appended as one argument. This is the default. |
| `LegacyShellTemplate` | A pinned legacy config that genuinely needs shell grammar | Temporary compatibility adapter only. The static template is package-owned and reviewed; it runs through `/bin/sh -c` only after its dynamic slots have been validated and POSIX-shell quoted. |

`LegacyShellTemplate` is never selected by a user input or a JSON flag alone.
The loader selects it from an allowlisted compatibility entry tied to a pinned
legacy config revision. New screens and handlers must use `Argv`.

`$1` retains its observable legacy meaning of **one replacement only**. The
adapter supports exactly one unquoted dynamic slot; it applies a type-specific
validator and substitutes a POSIX-shell-quoted value. A template with an
unsupported quote context, more than one dynamic slot, or an unrecognised
runtime token fails validation rather than guessing at shell syntax. This is a
safe intentional break from the legacy engine's unescaped replacement.

The initial binding types are `InterfaceName`, `Ipv4Address`, `PrefixLength`,
`Ssid`, `FileNameWithinApprovedDirectory`, and a pack-defined closed enum. Each
has a rejecting validator; a display title, SSID, filename, or command output
is not a command argument merely because it is text. Secrets (notably Wi-Fi
passwords) are passed in memory as a sensitive argument, omitted from events,
logs, diagnostics, and golden-output failure messages.

## Privilege boundary

The default `CommandService` may execute only unprivileged allowlisted
handlers. Operations needing system authority use a separate root-owned
`micropanel-touch-privileged` broker reached over a Unix socket in
`runtime_dir` with restrictive filesystem permissions. The UI sends a
structured request containing a semantic operation id and typed values; the
broker does not accept a shell command, executable path, or arbitrary argv.

The broker owns the following initial operation classes:

| Operation class | Examples | Broker responsibility |
|---|---|---|
| Network configuration | static IP, DHCP, Wi-Fi join | Validate interface/address input and invoke the fixed NetworkManager operation. |
| Boot-profile update | managed `piscreen` overlay orientation | Modify only the managed overlay line; never edit an arbitrary config path. |
| Controlled storage/media | mount, collect logs, flash-device preparation | Validate the block device/mount target against the capability matrix and manage writable paths. |
| Power control | reboot, shutdown | Perform the fixed system action after the UI's confirmation policy has completed. |

No generic `run as root` operation exists. Domain packs may add a broker
operation only with a named capability-matrix row, fixed argument schema,
device and writable-path declaration, and tests. Until then the broker rejects
it. The production service retains `NoNewPrivileges=true`; its later hardening
will add only the minimum `DeviceAllow`, filesystem protections and
`ReadWritePaths` proven by that matrix. `KillMode=control-group` is required
when the production systemd unit is introduced so a service restart kills the
whole UI job cgroup.

## Output, logs, and result semantics

`CommandService` captures stdout and stderr through one combined pipe. Reads
are in pipe order, bounded by the request's output limit, and the child does
not inherit the UI's stdin. `ActionRunner` retains a bounded in-memory tail for
the progress UI and, when a log is requested, writes a single managed log file.
It never opens two writers to the same file and never copies the legacy
overlapping `stdout`/`stderr` truncation behavior. A final result reports the
captured-byte count and whether output was truncated.

The terminal `ActionResult` is chosen by the following order. Earlier matching
rows always win; result-pattern extraction affects display text only, never the
status.

| Priority | Condition | Result | Compatibility disposition |
|---:|---|---|---|
| 1 | Request invalid, fork/exec fails, or the executable cannot start | `start_failed` | New explicit outcome; do not infer success. |
| 2 | Cancellation or timeout is first observed while the job is live | `cancelled` or `timed_out` | New explicit outcome. A simultaneous race is resolved by the first terminal event recorded by `CommandService`. |
| 3 | A normally running child terminates by an external signal | `killed` | New explicit outcome; its signal is retained for diagnostics. |
| 4 | Output capture limit is exceeded | `failed` | The bounded log is incomplete, so marker/exit interpretation is not trusted; the result carries an explicit diagnostic. |
| 5 | Configured log is unreadable, or contains `[ERROR]`, `Error`, `Failed`, or `failed` | `failed` | Preserved. Error wins even if the log also contains a success marker. |
| 6 | Configured log contains `[SUCCESS]`, `Flash verification successful`, or `Optionbyte verification successful`, with no recognised error | `succeeded` | Preserved, including a non-zero process exit. |
| 7 | A configured, readable log has no recognised completion marker | exit status `0` → `succeeded`; other exit → `failed` | Intentional safety improvement over the legacy marker-only result. |
| 8 | No `log_file` is configured and rows 1–4 did not apply | `assumed_succeeded` | Preserved legacy no-log rule, including a non-zero normal exit; the exit status remains diagnostic data. |

The marker strings above were byte-verified on 2026-08-10 against the legacy
`GenericListScreen.cpp` completion scan (`[SUCCESS]`, `Flash verification
successful`, `Optionbyte verification successful`; `[ERROR]`, `Error`,
`Failed`, `failed`). The ActionRunner golden fixtures must use this exact set;
they may not replace it with a case-insensitive approximation.

`result_pattern` is searched line by line in the final log. The last matching
line wins, its remaining text is trimmed, and `result_prefix` is prepended.
This preserves legacy presentation semantics without allowing it to affect the
status. `parse_progress` emits the last valid numeric percent in the log,
clamped to 0–100. `usb_blaster_duration` is time-derived, caps at 99 until a
terminal result, and is always labelled **estimated**.

## Process lifecycle

For every job, `CommandService` must:

1. Allocate a job id and record ownership before spawn. The worker creates a
   new session/process group (`setsid`) before `exec`, and the parent verifies
   the PGID race safely.
2. On cancellation, timeout, output-limit breach, or service shutdown, send
   `SIGTERM` to the whole PGID, wait the configured grace period (1.5 seconds
   by default), then send `SIGKILL` to the whole PGID if any member remains.
3. Drain/close output without allowing a noisy child to postpone escalation,
   and reap the direct child before publishing its terminal event. A process in
   uninterruptible kernel sleep is not reported as complete until the kernel
   permits reaping.
4. On ordinary app shutdown, cancel the current job and wait for the reap.
   On a crash or systemd restart, `KillMode=control-group` supplies the outer
   containment. At startup, clean up only job scopes recorded by this product;
   never kill processes by name or by an unverified PID.

The current `CommandRunner` already supplies argv execution, `/dev/null`
stdin, bounded combined output, TERM-to-KILL escalation, and direct-child
reaping. Sprint 2 promotes it behind `CommandService`, adds session ownership,
job metadata/startup cleanup, and the service-level cgroup guarantee above.

## Required tests before an action is enabled

| Area | Required proof |
|---|---|
| Expansion | Golden fully-expanded argv/shell-template tests for `$MICROPANEL_HOME`, every compatibility-path rule, and each typed dynamic binding; source JSON bytes are unchanged. |
| Injection boundary | Malicious SSID, filename and `$1` values are rejected or remain one literal argument; none can execute a second command. Unknown tokens and unsupported shell quoting are rejected. |
| Result table | Fixtures cover contradictory markers, non-zero exit with `[SUCCESS]`, markerless configured logs for exit 0/non-zero, missing log, and no-log non-zero assumed success. |
| Progress/result text | Last-percent, estimated 99%, last `result_pattern` match, and redacted sensitive values are asserted. |
| Lifecycle | Cancel, timeout, output limit, graceful TERM, TERM-ignoring child, service restart, and UI `SIGKILL` each leave no descendant process. |
| Privilege | The UI cannot issue raw privileged argv; malformed and unallowlisted broker requests are denied; every accepted request maps to one capability-matrix row. |
| UI delivery | Worker events are immutable and a stress test proves no worker invokes LVGL or updates a stale screen. |

No new action is considered operationally supported until all applicable rows
exist. The capability matrix remains the place where the command's package,
device access, writable paths, and privileged operation (if any) are recorded.
