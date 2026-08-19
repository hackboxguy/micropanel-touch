# Build, release and update — the exact commands

Every command here is meant to be pasted into a shell. Paths are the real
defaults; substitute the version and device where marked.

Throughout: `<VER>` is a version identifier such as `00.38`. **`00.23`–`00.37`
are already burned** — the update engine refuses a payload whose version equals
the running one, and reusing an identifier makes bench results ambiguous. Pick
the next unused number and never re-cut an old one.

All build commands run from the `misc-tools` checkout:

```sh
cd /path/to/misc-tools
```

---

## 0. Before building: the two test gates

Both must pass before an image build that touches what they cover. This is a
working agreement, not a suggestion — see `pi-in-system-update-plan.md` §8.

```sh
# The A/B engine (nine suites; the three loopback fixtures need root)
sudo ./packages/pi-ab-update/tests/run-tests.sh

# The application (42 tests, ~13 seconds)
cd /path/to/micropanel-touch
git submodule update --init --recursive          # once, for LVGL
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=ON
cmake --build build -j"$(nproc)"
ctest --test-dir build --output-on-failure
```

The application build needs `libgpiod` and `nlohmann-json`:

```sh
sudo pacman -S --needed libgpiod nlohmann-json     # Arch
```

The build clones the application **from its remote**, so anything you have
committed locally but not pushed will not be in the image:

```sh
cd /path/to/micropanel-touch && git push
```

---

## 1. A fresh SD-card image

Produces an image only — no update payload.

```sh
sudo ./build-image.sh \
    --board=micropanel-touch \
    --variant=luckfox-ctp \
    --version=<VER> \
    --layout=ab \
    --app-ref=main
```

Output:

```
~/pi-image-workspace/out/micropanel-touch-luckfox-ctp-ab/
    2025-10-01-raspios-trixie-arm64-lite-micropanel-touch-luckfox-ctp-ab-<VER>.img
```

Without `--release-url-template` the image points at the **production** release
location from `board.conf`:

```
https://github.com/hackboxguy/micropanel-touch/releases/latest/download/@ASSET@
```

Useful flags:

| Flag | Effect |
|---|---|
| `--dry-run` | Preflight and build plan only, changes nothing, needs no root |
| `--app-revision=<40-char sha>` | Pin an exact commit instead of a branch |
| `--flash=/dev/sdX` | Write the finished image to a card (see §4 for a safer route) |
| `--release-url-template=URL` | Point the image at a different release host (§5) |

Every A/B build now runs `ab-verify-image.sh` automatically as "Stage 4b".

---

## 2. An update payload (the `.mpupdate` bundle)

Add `--payload`. This builds the image **and** cuts the signed bundle from it.

```sh
sudo ./build-image.sh \
    --board=micropanel-touch \
    --variant=luckfox-ctp \
    --version=<VER> \
    --layout=ab \
    --app-ref=main \
    --payload
```

Output — three assets, deliberately **version-less** in name, because the
version lives inside the signed manifest:

```
~/pi-image-workspace/out/micropanel-touch-luckfox-ctp-ab/payloads/<VER>/
    micropanel-touch-luckfox-ctp.mpupdate       ~1.55 GiB
    micropanel-touch-luckfox-ctp.manifest       ~234 bytes
    micropanel-touch-luckfox-ctp.manifest.sig   64 bytes
```

Each release must go in its **own** payload directory — the names collide
otherwise, and `--payload-dir=` overrides the default if needed.

### Cutting a payload from an image you already built

No rebuild required:

```sh
IMG=~/pi-image-workspace/out/micropanel-touch-luckfox-ctp-ab/2025-10-01-raspios-trixie-arm64-lite-micropanel-touch-luckfox-ctp-ab-<VER>.img
OUT=~/pi-image-workspace/out/micropanel-touch-luckfox-ctp-ab/payloads/<VER>

sudo mkdir -p "$OUT"
sudo ./packages/pi-ab-update/ab-make-payload.sh \
    --image="$IMG" \
    --output-dir="$OUT" \
    --version=<VER> \
    --variant=luckfox-ctp \
    --boards=pi4 \
    --product=micropanel-touch \
    --signing-key=/etc/micropanel-touch/release-signing/ed25519-release.key
sudo chown -R "$USER":"$(id -gn)" "$OUT"
```

### Check the payload before shipping it

```sh
cat "$OUT"/micropanel-touch-luckfox-ctp.manifest
sudo openssl pkeyutl -verify -pubin \
    -inkey /etc/micropanel-touch/release-signing/ed25519-release.key.pub \
    -rawin -in "$OUT"/micropanel-touch-luckfox-ctp.manifest \
    -sigfile "$OUT"/micropanel-touch-luckfox-ctp.manifest.sig
```

Expect `Signature Verified Successfully`. If it fails, **do not publish** —
every device will refuse the release, which is the system working correctly.

---

## 3. Publishing a release on GitHub

The device fetches from `releases/latest/download/`, which **skips drafts and
pre-releases**. It must be a normal published release.

```sh
VER=<VER>
OUT=~/pi-image-workspace/out/micropanel-touch-luckfox-ctp-ab/payloads/$VER

gh release create "$VER" \
    --repo hackboxguy/micropanel-touch \
    --title "MicroPanel Touch $VER (luckfox-ctp, pi4)" \
    --notes "A/B update payload for Raspberry Pi 4 with the Luckfox CTP panel." \
    "$OUT/micropanel-touch-luckfox-ctp.mpupdate" \
    "$OUT/micropanel-touch-luckfox-ctp.manifest" \
    "$OUT/micropanel-touch-luckfox-ctp.manifest.sig"
```

`gh` keeps the release a draft while it uploads and publishes it at the end, so
nobody sees a half-populated release. The 1.55 GiB upload takes several minutes.

### Verify the release actually resolves

Do this before touching a device — it exercises the same chain the device will:

```sh
U=https://github.com/hackboxguy/micropanel-touch/releases/latest/download

gh release view "$VER" --repo hackboxguy/micropanel-touch \
    --json isDraft,isPrerelease,assets \
    --jq '{draft:.isDraft, prerelease:.isPrerelease, assets:[.assets[].name]}'

curl -fsSL "$U/micropanel-touch-luckfox-ctp.manifest"      -o /tmp/gh.manifest
curl -fsSL "$U/micropanel-touch-luckfox-ctp.manifest.sig"  -o /tmp/gh.manifest.sig
sudo openssl pkeyutl -verify -pubin \
    -inkey /etc/micropanel-touch/release-signing/ed25519-release.key.pub \
    -rawin -in /tmp/gh.manifest -sigfile /tmp/gh.manifest.sig
```

Expect `draft:false`, three assets, and `Signature Verified Successfully`.

### Removing a release

```sh
gh release delete "$VER" --repo hackboxguy/micropanel-touch --yes --cleanup-tag
```

> **Size ceiling.** GitHub allows **2 GiB per release asset**. The bundle is
> currently ~1.55 GiB, leaving roughly 450 MiB of headroom. The payload
> generator now enforces this itself: it **warns above 90%** of the limit and
> **fails outright at or above it**, so the ceiling is hit on the build host
> rather than discovered by a rejected upload — or, worse, by a device
> reporting a download failure as `network` when the real problem is the
> artifact. Both thresholds are overridable (`AB_ASSET_WARN_BYTES`,
> `AB_ASSET_LIMIT_BYTES`) for a channel with different limits.

---

## 4. Writing an image to an SD card

Resolve the card by what it *is*, never by the name it happened to get —
device names move between plug-ins, and a 15 GB write to the wrong disk is not
recoverable:

```sh
lsblk -dnp -P -o NAME,TYPE,MODEL,SIZE          # find the card
lsblk -nr -o MOUNTPOINT /dev/sdX               # must print nothing
```

Then:

```sh
IMG=~/pi-image-workspace/out/micropanel-touch-luckfox-ctp-ab/2025-10-01-raspios-trixie-arm64-lite-micropanel-touch-luckfox-ctp-ab-<VER>.img
sudo dd if="$IMG" of=/dev/sdX bs=4M conv=fsync status=progress
sync
```

Read the card back before trusting it — the partition suffix differs by device
class (`sdb5`, but `mmcblk0p5` / `nvme0n1p5`), so resolve slot A by label:

```sh
ROOT_A=$(lsblk -nrp -o NAME,LABEL /dev/sdX | awk '$2=="MP_ROOT_A"{print $1; exit}')
sudo mount -o ro "$ROOT_A" /mnt
grep -E '^(IMAGE_VERSION|MICROPANEL_TOUCH_REVISION)=' \
    /mnt/opt/micropanel-touch/share/micropanel-touch/image-manifest.env
grep -v '^#' /mnt/usr/lib/pi-ab-update/update-source.conf
ls /mnt/usr/local/sbin/ab-*
sudo umount /mnt
```

---

## 5. Testing an update without publishing (bench rehearsal)

Serve a payload directory from the build host and point an image at it. Plain
HTTP is faithful rather than weakened here: a release is authenticated by its
signature against a key pinned in the image, so the transport is not a trust
boundary and an unauthenticated server cannot make a device accept anything it
would otherwise refuse.

```sh
# Terminal 1 — serve the payload (Ctrl-C to stop)
./packages/pi-ab-update/ab-serve-release.sh \
    ~/pi-image-workspace/out/micropanel-touch-luckfox-ctp-ab/payloads/<VER> 8000

# Terminal 2 — build a device image that asks this host instead of GitHub
sudo ./build-image.sh \
    --board=micropanel-touch --variant=luckfox-ctp \
    --version=<OTHER_VER> --layout=ab --app-ref=main \
    --release-url-template=http://<build-host-ip>:8000/@ASSET@
```

The served version and the flashed version must **differ** — an identical
version is refused as "already running". An older served version is a
legitimate offer: downgrades stay installable so rollback remains available.

`ab-verify-image.sh` prints a notice for an `http://` source. That is expected
for a rehearsal and must never appear in a shipping image.

---

## 6. On the device

From the panel: **System → Software Update**, then *Check for updates* (network)
or *Check USB stick* (offline; needs exactly one `.mpupdate` on the stick).

### `ab-update` — the front door

One command covers almost everything below. Available from `00.38` onward.

```sh
sudo ab-update                       # status: everything on one screen
sudo ab-update check                 # ask the release source, download no payload
sudo ab-update install ota           # install from the configured release source
sudo ab-update install usb           # install from attached USB media
sudo ab-update install --file=/path/to/bundle.mpupdate
sudo ab-update watch                 # follow progress until it settles
sudo ab-update log [N]               # engine + commit journal
```

`status` reports the running version, slot and partition; **what the inactive
slot holds** — that is, what a rollback would land on, which nothing else can
tell you; the durable update state; the last check result; and the units the
commit predicate requires, with their current state.

Single values, for scripts and health checks:

```sh
ab-update --active-version           # 00.36
ab-update --active-slot              # B
ab-update --active-partition         # /dev/mmcblk0p6
sudo ab-update --inactive-version    # 00.37   (mounts the other slot read-only)
ab-update --inactive-partition       # /dev/mmcblk0p5
ab-update --state                    # committed
ab-update --check-state              # available | up-to-date | network | clock | ...
```

Queries that only read files work unprivileged; installing and
`--inactive-version` need root.

`status` and `--inactive-version` briefly take the engine's update lock, because
they mount the inactive slot and mounting one mid-write would be a real hazard.
For that ~100 ms an update started at the same moment is refused with *"another
system update is already in progress"*. It is harmless — try again — but it is
worth knowing so nobody chases it. A bench override for the release source is
`--source-config=FILE` — the URLs are otherwise pinned in the image and are
never client-supplied.

### The underlying commands

`ab-update` only ever delegates to these, so they remain the source of truth:

```sh
sudo /usr/local/sbin/ab-update-check          # metadata only, no payload fetch
cat /run/micropanel-touch-update/check        # state=available|up-to-date|...

sudo /usr/local/sbin/ab-system-update ota     # network install
sudo /usr/local/sbin/ab-system-update usb     # USB install

cat /run/micropanel-touch-update/progress     # phase= and progress=
sudo cat /data/micropanel-touch-system/update-state
/usr/local/sbin/ab-slot-selector current-slot
sudo journalctl -t ab-system-update -t ab-update-check -n 50
```

The engine journal is **empty on success** — it speaks only on failure, so
silence there is the good outcome.

Failure classes published to the UI: `source`, `signature`, `network`, `clock`,
`integrity`, `compatibility`, `payload`, `version`, `stall`, `boot`, `target`,
`selector`, `image`, `internal`. `clock` specifically means a TLS failure on a
device whose clock is unset — fix the time, or update from USB, which needs no
clock at all.

If a candidate does not come back, **remove and reapply power**: the one-shot
candidate is abandoned and the committed slot boots again.

### Status, without the GUI

```sh
grep ^IMAGE_VERSION= /opt/micropanel-touch/share/micropanel-touch/image-manifest.env
/usr/local/sbin/ab-slot-selector current-slot        # A or B
findmnt -n -o SOURCE /media/root-ro                  # mmcblk0p5 = A, p6 = B
sudo cat /data/micropanel-touch-system/update-state
```

`cat` the manifest without the `grep` for the whole picture: application
revision, LVGL commit, panel profile and firmware, and the boards this image
declares itself compatible with.

### The install works without the GUI. The commit does not.

Both engines are plain root CLIs — they never contact the application or the
broker, so `ab-system-update` and `ab-update-check` run fine with
`micropanel-touch.service` stopped.

**But `ab-update-commit` will not commit the candidate.** This board's
predicate is:

```
AB_HEALTH_UNITS=micropanel-touch.service micropanel-touch-privileged.service
AB_HEALTH_HOOK=/usr/lib/micropanel-touch/update-health   # a first frame must be rendered
AB_SETTLE_SECONDS=30
```

So an update applied on a device whose GUI does not run will install, boot into
the candidate once, fail its health window, and **fall back to the previous
slot at the next reboot**. That is the safety mechanism doing its job — it
exists to stop a broken build sticking — but it means the CLI cannot be used to
force an update onto a device whose application is genuinely broken.

After a candidate boot, check which happened:

```sh
sudo cat /data/micropanel-touch-system/update-state   # committed | candidate-armed | fallback
systemctl status ab-update-commit.service
sudo journalctl -u ab-update-commit -n 20
```

If the application is down for an unrelated reason and you still want an update
to stick, fix the application first — or, as a deliberate override rather than
routine practice, relax `AB_HEALTH_UNITS`/`AB_HEALTH_HOOK` in
`/usr/lib/pi-ab-update/ab-update.conf` **on the candidate** before its
30-second window elapses.
