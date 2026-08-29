# Building cameralink

## 1. Get Sony's Camera Remote SDK (CrSDK)

This project talks to the camera through Sony's official **Camera Remote SDK
(CrSDK)**. It's free, but proprietary — Sony requires you to accept their own
license agreement before downloading. That agreement's GRANT OF LICENSE
section does permit incorporating the compiled `.so` files into a built
application "in an inseparable way" and distributing that application
(see [docker/prebuilt-images/](../docker/prebuilt-images/README.md) for
one built that way) — but the raw SDK files themselves aren't
redistributable as a standalone download, so this repo does **not**
include them, and this step can't be automated (it requires a human
clicking through a license page in a browser) — you need to fetch your
own copy.

1. Go to [Sony's Camera Remote SDK download page](https://support.d-imaging.sony.co.jp/app/sdk/en/index.html)
   (or the mirror at [pro.sony](https://pro.sony/en_GB/digital-imaging/sdk-download)).
2. Accept the license agreement and download the **Linux** build matching
   your target hardware's CPU architecture:
   - Raspberry Pi (any model running 64-bit Raspberry Pi OS), or any other
     64-bit ARM Linux board → **Linux64ARMv8**
   - Older 32-bit Pi builds → **Linux32ARMv7**
   - A generic x86_64 Linux machine (for testing the server itself without
     Pi-specific hardware — see [INSTALL.md](INSTALL.md) — or for a
     Docker/NAS deployment, see [DOCKER.md](DOCKER.md)) → the **Linux64PC**
     build, if Sony offers one for your SDK version (confirmed available
     for v2.02.00); not all versions do, since CrSDK primarily targets
     Pi-class ARM hardware in the field
3. Unzip it. Inside you'll find several sample-app bundles (`SimpleCli.zip`,
   `RemoteCli.zip`) that each carry their own copy of the same headers and
   `.so` libraries. Any one of them works — copy these into
   `third_party/CrSDK/` in this repo, **flattened** into this exact layout:
   ```
   third_party/CrSDK/
     CRSDK/*.h                    (from <bundle>/app/CRSDK/*.h)
     libCr_Core.so                (from <bundle>/external/crsdk/*.so, direct siblings)
     libmonitor_protocol.so
     libmonitor_protocol_pf.so
     CrAdapter/                   (from <bundle>/external/crsdk/CrAdapter/*)
       libCr_PTP_IP.so
       libCr_PTP_USB.so
       libusb-1.0.so
       libssh2.so
       ...
     CrDebugString.cpp            (from <bundle>/app/CrDebugString.cpp)
     CrDebugString.h              (from <bundle>/app/CrDebugString.h)
   ```
   `CrDebugString.cpp`/`.h` are Sony's own sample-code property-name lookup
   table (`CrDevicePropertyString()`) — used by the `/api/debug/allprops`
   diagnostic endpoint to show human-readable property names instead of raw
   hex codes.

   (`third_party/CrSDK/` is gitignored, so none of this ever touches git.)

## 2. Build

```
./scripts/build.sh
```

This compiles `server/main.cpp` against the SDK and copies the runtime
`.so` files + `public/` (the frontend) next to the resulting binary in
`server/build/`.

**Why `-fsigned-char`:** on ARM, `char` defaults to *unsigned* — but the
SDK's headers contain enum values like `CrZoomOperation_Wide = -1` that
don't fit in an unsigned `CrInt8`/`char`, so the build fails without this
flag. x86/Apple Silicon don't need it (their default `char` is already
signed), but it's harmless to always pass it.

**Why the runtime `.so` layout matters:** `libCr_Core.so` looks for its
`CrAdapter/` plugin folder *relative to the process's current working
directory* — not relative to its own file location. If you run the binary
from anywhere other than the directory `build.sh` populated, network
(Wi-Fi/SSH) camera connections will fail with `CrError_Connect_HandlePlugin`
even though everything else works. Always `cd` into `server/build/` before
running it, or run it via the systemd unit in
[scripts/cameralink.service](../scripts/cameralink.service), which sets
`WorkingDirectory` correctly for you.

## 3. Run

```
cd server/build
./cameralink_server
```

Serves the API and frontend on `http://<host-ip>:8080`. Confirm it's alive:

```
curl http://localhost:8080/api/status
# {"connected":false,"model":""}
```

## 4. Run as a service (recommended for a Pi, or any always-on deployment)

```
sudo cp scripts/cameralink.service /etc/systemd/system/
sudo sed -i "s#__REPLACE_WITH_REPO_PATH__#$(pwd)#g; s#__REPLACE_WITH_USER__#$(whoami)#g" /etc/systemd/system/cameralink.service
sudo systemctl daemon-reload
sudo systemctl enable --now cameralink.service
sudo systemctl status cameralink.service
```

The unit is set to `Restart=on-failure`, so it survives crashes, and
`WantedBy=multi-user.target` means it starts automatically on every boot —
confirmed via real power cycles on the field Pi. Logs go to
`server/build/server.log` (see the unit file for the exact path).

**After every rebuild** (a new `./scripts/build.sh` run), restart the
service so it picks up the new binary — the SDK connection state lives
entirely in-process, so a restart also means reconnecting to the camera
afterward:

```
sudo systemctl restart cameralink.service
```
