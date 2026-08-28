# Running cameralink in Docker (NAS / home server / VM)

This is a second deployment path alongside the Pi Zero 2 W field kit
(see [ARCHITECTURE.md](ARCHITECTURE.md)) — for when you already have an
always-on x86_64 Linux box on the same network as the camera (a NAS with
Docker/Container Manager, a home server, a VM) and don't need the Pi's
field-deployable "no infrastructure at all" design. Confirmed working on
a Synology DS920+ (Celeron J4125, x86_64, DSM 7.4 Container Manager) with
a real Sony a7R V.

## How this differs from the Pi setup

The Pi runs its *own* Wi-Fi access point and the camera joins it directly
— no home network involved anywhere. This Docker setup is the opposite:
the container and the camera both join an **existing** Wi-Fi network/LAN
you already have (in the reference deployment, an SSID called
`NASVAN#3`), and reach each other over that shared network like any two
normal devices. There's no USB gadget mode, no dedicated hotspot, and no
`scripts/setup-pi.sh` step — none of that is relevant here.

The container uses `network_mode: host` (see
[scripts/docker-compose.yml](../scripts/docker-compose.yml)), so it's
reachable at the host machine's own LAN address on `:8080`, exactly like
the server binary running directly on a Pi — no port-mapping/NAT to
configure.

## 1. Get the x86_64 CrSDK build

Same process as [BUILDING.md](BUILDING.md) step 1, but download the
**Linux64PC** build instead of an ARM one (Sony's SDK page offers both;
confirmed available for CrSDK v2.02.00). Unzip it, then unzip the
`RemoteCli.zip` bundle inside it, and flatten it into
`third_party/CrSDK/` exactly as BUILDING.md describes — the internal
layout (`app/CRSDK/*.h`, `app/CrDebugString.*`, `external/crsdk/*.so`,
`external/crsdk/CrAdapter/*`) is the same shape regardless of target
architecture.

## 2. Pre-create the persisted data files

`saved_cameras.json`/`saved_recipes.json` live in the container's working
directory and are bind-mounted individually so they survive
rebuilds/recreates. Docker creates a missing bind-mount source as a
*directory*, not a file, so create these as empty JSON arrays first:

```
mkdir -p scripts/data
echo '[]' > scripts/data/saved_cameras.json
echo '[]' > scripts/data/saved_recipes.json
```

(These two files are gitignored, same as the Pi's copies — they hold
real camera IPs/MACs/credentials and are never committed.)

## 3. Build and run

```
docker compose -f scripts/docker-compose.yml up -d --build
```

Or, on a Synology NAS via Container Manager's Project feature (no local
Docker CLI needed): create a Project pointed at a share containing this
repo's `third_party/` and `server/` alongside
[scripts/Dockerfile](../scripts/Dockerfile), paste in
[scripts/docker-compose.yml](../scripts/docker-compose.yml)'s contents
(adjusting the bind-mount paths to real absolute paths, e.g.
`/volume1/docker/cameralink/data/saved_cameras.json:/app/saved_cameras.json`
— Container Manager's compose engine doesn't resolve `./data/...`
relative to the project the same way plain `docker compose` does), and
build + start the project.

Confirm it's alive:

```
curl http://<host-ip>:8080/api/status
# {"connected":false,"model":""}
```

Then pair a camera the same way as the Pi (Setup tab → Connect (Network),
or the camera-specific docs in [CAMERA_SETUP.md](CAMERA_SETUP.md)) — this
instance's `saved_cameras.json`/`saved_recipes.json` are independent of
any other cameralink instance's (the Pi's included). Both the web UI and
the [Sony Config iOS app](../ios/Sony%20Config/README.md) can point at
this instance's address as just another server URL / Quick Connect
target.

## Known no-op: "Shut Down Pi"

The web UI's shutdown button (`POST /api/system/shutdown`) shells out to
`sudo shutdown -h now`. Neither `sudo` nor a real init system exists
inside this container, so the call just fails harmlessly here — it is
**not** wired to shut down the host NAS/server. This is intentional
(nothing in this repo attempts to gate or hide the button per
deployment), not a bug to fix.

## Why libxml2 needs installing explicitly

The x86_64 build of `libCr_Core.so` links against `libxml2` — a
transitive dependency Sony's SDK brings in that the ARM/Pi build doesn't
seem to need the same way. [scripts/Dockerfile](../scripts/Dockerfile)
installs `libxml2-dev` in the build stage and `libxml2` in the runtime
stage; without it the link step fails with `undefined reference to
xmlParseFile@LIBXML2_2.4.30` and similar.
