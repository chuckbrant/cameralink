# Install guide: bare Linux machine → running cameralink service

This is written to be handed directly to an AI coding assistant (Claude,
or anyone else) with shell access to a **fresh Linux machine that has
nothing else on it** — the goal is to find out how much of this install
can be automated end-to-end, and where a human still has to step in.

If you're reading this as an AI assistant: follow the numbered steps in
order. Steps marked **⚠ HUMAN REQUIRED** cannot be completed by shell
commands alone — stop, explain clearly what's needed, and wait for the
human before continuing past that step. Everything else should be
runnable as-is; if a command fails, don't guess around it silently —
report the actual error back before improvising a workaround, since a
silent workaround here is exactly the kind of thing that produces a
"why did the Kelvin write silently do nothing" bug three weeks later (see
[SDK_CAPABILITIES.md](SDK_CAPABILITIES.md) for a real example of that
happening in this project).

## What you're building

A single C++ binary (`cameralink_server`) that serves a JSON REST API and
a bundled web frontend on port 8080, and talks to a Sony camera over
Sony's proprietary Camera Remote SDK (CrSDK). There is no database, no
separate web server, no containers — just this one process plus a handful
of `.so` files it loads at runtime.

**Two possible goals**, and this guide covers both:

- **Just the server**, to test whether the build/API layer works on this
  machine at all — useful on any Linux box, doesn't require it to be a
  Raspberry Pi or to have a camera nearby.
- **The full field rig** — a Raspberry Pi Zero 2 W configured as a USB
  gadget + Wi-Fi access point, with a real Sony camera connected to it.
  This needs Pi-specific hardware (step 6 below) and a physical camera
  (step 7).

If this machine isn't a Pi, or you don't have a camera to test against
yet, stop after step 5 — you'll have a running server you can hit with
`curl`, which is the useful signal for "does this build and run cleanly
on a fresh machine."

## Step 0: confirm what you're working with

```
uname -m          # architecture -- aarch64 (64-bit ARM) or x86_64
cat /etc/os-release  # distro -- Debian/Ubuntu-family assumed below
g++ --version || echo "no g++ yet"
git --version || echo "no git yet"
```

This project has been built and run on 64-bit Raspberry Pi OS (Bookworm,
Debian-based) on a Pi Zero 2 W (`aarch64`). Anything Debian/Ubuntu-family
on `aarch64` or `x86_64` should work the same way for the build itself;
the Pi-specific network setup in step 6 assumes NetworkManager, which is
Raspberry Pi OS Bookworm's default and may not be present on other
distros.

## Step 1: install build tools

```
sudo apt-get update
sudo apt-get install -y build-essential git
```

`build-essential` gives you `g++` (C++17 support required) and `make`
(not actually used by this project's own build, but commonly a
dependency of `build-essential` itself). `git` to clone the repo.

## Step 2: clone the repo

```
git clone https://github.com/chuckbrant/cameralink.git
cd cameralink
```

## Step 3: get Sony's Camera Remote SDK — ⚠ HUMAN REQUIRED

This is the one step that cannot be automated. Sony's CrSDK is free but
proprietary, gated behind a license-agreement click-through on their own
website — there's no direct download URL that skips that, and doing so
even if one existed would be circumventing a license agreement, which
this project won't do.

**Tell the human:**

> I need you to download Sony's Camera Remote SDK myself — I can't do
> this step. Go to
> https://support.d-imaging.sony.co.jp/app/sdk/en/index.html (or the
> mirror at https://pro.sony/en_GB/digital-imaging/sdk-download), accept
> the license agreement, and download the Linux build matching this
> machine's architecture (`<uname -m output>` → **Linux64ARMv8** for
> aarch64, **Linux32ARMv7** for 32-bit ARM). Then get the zip onto this
> machine — `scp` it over, or however you'd normally transfer a file
> here — and tell me the path once it's there.

Once the human confirms the file's location, proceed:

```
mkdir -p /tmp/crsdk-unpack
unzip <path-to-sdk-zip> -d /tmp/crsdk-unpack
find /tmp/crsdk-unpack -iname "SimpleCli*" -o -iname "RemoteCli*"
```

Unzip whichever sample bundle you find (they carry identical copies of
the headers/libraries this project needs) and lay it out flat into
`third_party/CrSDK/` exactly as [BUILDING.md](BUILDING.md) describes:

```
mkdir -p third_party/CrSDK/CrAdapter
cp <bundle>/app/CRSDK/*.h                third_party/CrSDK/CRSDK/ 2>/dev/null || mkdir -p third_party/CrSDK/CRSDK && cp <bundle>/app/CRSDK/*.h third_party/CrSDK/CRSDK/
cp <bundle>/external/crsdk/*.so           third_party/CrSDK/
cp <bundle>/external/crsdk/CrAdapter/*    third_party/CrSDK/CrAdapter/
cp <bundle>/app/CrDebugString.cpp         third_party/CrSDK/
cp <bundle>/app/CrDebugString.h           third_party/CrSDK/
```

(The exact internal paths inside the bundle can vary slightly between SDK
versions — if any of the above `cp` commands don't find their source
files, `find /tmp/crsdk-unpack -iname "CameraRemote_SDK.h"` and
`find /tmp/crsdk-unpack -iname "libCr_Core.so"` to locate the real paths
in this particular download, then adjust.)

Verify the layout is right before moving on:

```
test -f third_party/CrSDK/CRSDK/CameraRemote_SDK.h && \
test -f third_party/CrSDK/libCr_Core.so && \
test -f third_party/CrSDK/CrDebugString.cpp && \
echo "SDK layout OK" || echo "SDK layout INCOMPLETE -- see above"
```

## Step 4: build

```
./scripts/build.sh
```

This should end with `Built: <repo>/server/build/cameralink_server`. If
it fails with something like `enumerator value is outside the range of
underlying type`, that means `-fsigned-char` isn't being applied — check
you're using the unmodified `scripts/build.sh`, not a hand-rolled `g++`
command missing that flag (see [BUILDING.md](BUILDING.md) for why it's
needed).

## Step 5: smoke-test the server

```
cd server/build
./cameralink_server &
sleep 1
curl http://localhost:8080/api/status
# expect: {"connected":false,"model":""}
kill %1
```

If this works, the build and API layer are confirmed functional on this
machine — independent of whether it's a Pi or has a camera nearby. If
you're not setting this up as a permanent field rig, **you can stop
here.**

## Step 6: install as a systemd service (for an always-on deployment)

```
cd <repo-root>
sudo cp scripts/cameralink.service /etc/systemd/system/
sudo sed -i "s#__REPLACE_WITH_REPO_PATH__#$(pwd)#g; s#__REPLACE_WITH_USER__#$(whoami)#g" /etc/systemd/system/cameralink.service
sudo systemctl daemon-reload
sudo systemctl enable --now cameralink.service
sudo systemctl status cameralink.service   # should show "active (running)"
curl http://localhost:8080/api/status      # should still work, now via the service
```

## Step 7: Pi-specific field setup (USB gadget + Wi-Fi AP) — only if this is a Pi Zero 2 W being deployed standalone

Skip this step entirely if you're just testing the server on a generic
Linux box, or if this machine will reach the camera over an existing
network instead of running its own access point.

```
nano scripts/setup-pi.sh   # set AP_SSID and AP_PASSWORD near the top first
./scripts/setup-pi.sh
sudo reboot
```

After reboot, verify:

```
nmcli device status         # wlan0 should show connected to your AP_SSID
ip route show                # should show 10.42.0.0/24 on wlan0
```

Why each piece of this exists (USB gadget mode, static-IP `usb0`, the
Wi-Fi AP's DHCP range) is explained in
[ARCHITECTURE.md](ARCHITECTURE.md) — worth reading if anything here
needs adapting for different hardware.

## Step 8: connect a real camera — ⚠ HUMAN REQUIRED (physical camera menu steps)

This step needs a human at the actual camera to turn on its Remote Shoot
Function and read off connection details from its screen — full
walkthrough in [CAMERA_SETUP.md](CAMERA_SETUP.md). Once you have the
camera's IP, MAC, User ID, and Password from that doc:

```
curl -X POST http://localhost:8080/api/connect/network \
  -H "Content-Type: application/json" \
  -d '{"ip":"<camera-ip>","mac":"<camera-mac>","userId":"<user>","password":"<password>"}'

curl http://localhost:8080/api/status
# expect: {"connected":true,"model":"<camera model> (network)"}

curl http://localhost:8080/api/recipe
# expect a JSON blob of the camera's current live settings
```

If `/api/recipe` returns real values matching what's actually dialed in
on the camera, the whole chain — build, service, network, SDK connection
— is confirmed working end-to-end.

## Troubleshooting

- **`CrError_Connect_HandlePlugin`** on a network connect attempt — the
  binary isn't being run from the directory containing the SDK's `.so`
  files and `CrAdapter/` folder. `libCr_Core.so` resolves that plugin
  folder relative to the process's *current working directory*, not its
  own file location. Fix: run from `server/build/` (or via the systemd
  unit, which sets `WorkingDirectory` for exactly this reason).
- **Build fails with an enum-range error** — see step 4 above
  (`-fsigned-char`).
- **A property writes successfully (`{"success":true}`) but the camera
  doesn't visibly change** — Sony's own SDK documentation is explicit
  that `SetDeviceProperty`'s return value does not indicate whether the
  write actually took effect. Confirm via `GET /api/debug/allprops`
  (dumps every property's real raw value) rather than trusting a
  success response alone. See [SDK_CAPABILITIES.md](SDK_CAPABILITIES.md)
  for several real examples of properties that looked correct but were
  either using the wrong `CrDataType` or didn't exist on the camera at
  all.
- **Wi-Fi camera connection fails outright** — if the camera has a USB
  cable plugged in (even just for charging), it prioritizes USB and
  won't run Remote Shoot Function over Wi-Fi at the same time. Unplug it.
- **`usb0` (or any USB-gadget-mode interface) is flaky, dropping for
  a minute or more at a time** — almost certainly a DHCP-retry loop on
  a link nothing serves DHCP over. Needs a static IP on both ends
  instead; see [ARCHITECTURE.md](ARCHITECTURE.md#why-usb-is-static-not-dhcp).

## Reporting back

Once you've gotten as far as you can, summarize for the human: which
steps completed cleanly, which needed a human, and — most usefully for
improving this guide — anywhere a command's actual output didn't match
what this doc predicted. That mismatch is exactly the kind of drift this
guide exists to catch.
