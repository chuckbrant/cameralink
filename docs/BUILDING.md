# Building cameralink

## 1. Get Sony's Camera Remote SDK (CrSDK)

This project talks to the camera through Sony's official **Camera Remote SDK
(CrSDK)**. It's free, but proprietary — Sony requires you to accept their own
license agreement before downloading, and that agreement doesn't clearly
grant redistribution rights. So this repo does **not** include it; you need
to fetch your own copy.

1. Go to [Sony's Camera Remote SDK download page](https://support.d-imaging.sony.co.jp/app/sdk/en/index.html)
   (or the mirror at [pro.sony](https://pro.sony/en_GB/digital-imaging/sdk-download)).
2. Accept the license agreement and download the **Linux** build matching
   your target hardware:
   - Raspberry Pi Zero 2 W running 64-bit Raspberry Pi OS → **Linux64ARMv8**
   - Older 32-bit Pi builds → **Linux32ARMv7**
3. Unzip it. Inside you'll find several sample-app bundles (`SimpleCli.zip`,
   `RemoteCli.zip`) that each carry their own copy of the same headers and
   `.so` libraries. Any one of them works — this project only needs:
   ```
   <bundle>/app/CRSDK/*.h              → third_party/CrSDK/include/CRSDK/
   <bundle>/external/crsdk/*.so        → third_party/CrSDK/lib/
   <bundle>/external/crsdk/CrAdapter/  → third_party/CrSDK/lib/CrAdapter/
   ```
4. End state should look like:
   ```
   third_party/CrSDK/
     include/CRSDK/CameraRemote_SDK.h, CrDeviceProperty.h, ...
     lib/libCr_Core.so, libmonitor_protocol.so, libmonitor_protocol_pf.so
     lib/CrAdapter/libCr_PTP_IP.so, libCr_PTP_USB.so, libusb-1.0.so, libssh2.so
   ```
   (`third_party/CrSDK/` is gitignored, so this step never touches git.)

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
running it.

## 3. Run

```
cd server/build
./cameralink_server
```

Serves the API and frontend on `http://<pi-ip>:8080`.
