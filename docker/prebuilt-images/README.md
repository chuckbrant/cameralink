# Prebuilt Docker images

Ready-to-run images for anyone who just wants to run cameralink without
building it themselves.

- **v2 (current)** — [`v2/`](v2/): SDK-free, built from
  [scripts/Dockerfile.native](../scripts/Dockerfile.native). Talks to the
  camera directly over PTP/IP-over-SSH (see
  [server/ptpip_client.h](../server/ptpip_client.h)) — no Sony CrSDK
  involved at all, nothing proprietary bundled. Network/Wi-Fi connect
  only (no USB — see [docs/DOCKER.md](../docs/DOCKER.md) for why). Also
  adds the Setup tab's Ping Test (with MAC verification against the
  device that actually answers), Find Camera (looks up a saved camera's
  current IP by MAC), and per-camera "(Ping Test)"/"(Find Camera)"
  buttons.
- **v1 (legacy)** — [`mac-arm64/`](mac-arm64/) / [`intel/`](intel/): CrSDK-based,
  built from [scripts/Dockerfile](../scripts/Dockerfile). Kept around for
  anyone already depending on it; **v2 is otherwise the one to use** —
  smaller image, no SDK license terms to think about, same feature set
  plus more.

Both versions:

- [`v2/mac-arm64/cameralink-mac-arm64.tar.gz`](v2/mac-arm64/cameralink-mac-arm64.tar.gz) /
  [`mac-arm64/cameralink-mac-arm64.tar.gz`](mac-arm64/cameralink-mac-arm64.tar.gz) —
  Apple Silicon (M-series Mac), for Docker Desktop.
- [`v2/intel/cameralink-intel.tar.gz`](v2/intel/cameralink-intel.tar.gz) /
  [`intel/cameralink-intel.tar.gz`](intel/cameralink-intel.tar.gz) —
  x86_64, for a NAS (Synology Container Manager, etc.), a home server, or
  an Intel/AMD Linux box.

## About the bundled Sony Camera Remote SDK (v1 only)

**v2 doesn't use or bundle Sony's SDK at all** — this section applies only
to the legacy v1 images.

Those images embed Sony's CrSDK library files (`libCr_Core.so` and
friends) in compiled form, built from a copy fetched directly from
[Sony's official SDK download page](https://support.d-imaging.sony.co.jp/app/sdk/en/index.html).
This is licensed, not incidental: Sony's SDK license agreement explicitly
permits "incorporat[ing] a binary form of the library file in the
SOFTWARE into the APPLICATION SOFTWARE in an inseparable way and
distribut[ing] the APPLICATION SOFTWARE to any third parties," and
grants those third parties a license to use it "solely for the purposes
to remotely control or use the DEVICE in a normal usage" — exactly this.

Two things the license asks the distributor (this project) to make sure
end-users know:

- **Using your camera this way sits outside Sony's own device warranty**
  for however Sony scopes "normal use" of the camera itself.
- **Sony didn't write, own, or support this software.** It's an
  independent project; Sony isn't involved and isn't who to ask for help.

Sony's SDK page, including the license text itself:
[support.d-imaging.sony.co.jp/app/sdk/en/](https://support.d-imaging.sony.co.jp/app/sdk/en/)

## Running one

```
docker load < v2/mac-arm64/cameralink-mac-arm64.tar.gz    # or v2/intel/cameralink-intel.tar.gz
docker run -d --name cameralink -p 8080:8080 \
  -v "$(pwd)/saved_cameras.json:/app/saved_cameras.json" \
  -v "$(pwd)/saved_recipes.json:/app/saved_recipes.json" \
  cameralink-mac-arm64:v2   # or cameralink-intel:v2, matching the tag baked into the image you loaded
```

Pre-create `saved_cameras.json`/`saved_recipes.json` as `[]` first (Docker
creates a missing bind-mount source as a directory, not a file) — see
[docs/DOCKER.md](../docs/DOCKER.md) step 2. Then open `http://<host>:8080`
and connect to your camera from the Setup tab, same as any other
cameralink deployment.

For getting the camera itself ready (turning on Remote Shoot Function,
reading its User ID/Password off the Access Authen. Info screen), see
[docs/CAMERA_SETUP.md](../docs/CAMERA_SETUP.md) -- steps 1 and 4 there
apply here; step 2 is Pi-specific (this deployment instead joins the
camera to your existing Wi-Fi network, same as [docs/DOCKER.md](../docs/DOCKER.md) describes).
You'll also need the camera's IP address once it's joined that network --
find it on the camera itself under **MENU → Network → Wi-Fi → Display
Wi-Fi Info**, or use the Setup tab's "Find Camera" button once the
camera has talked to your network at least once.

## Rebuilding these

- v2: `docker build -f scripts/Dockerfile.native -t <name> .` — no
  external SDK needed, works against a plain repo checkout.
- v1: `docker build -f scripts/Dockerfile -t <name> .` against a repo
  checkout with `third_party/CrSDK/` populated from Sony's SDK for the
  matching architecture (see [docs/BUILDING.md](../docs/BUILDING.md)).

Both are single-command builds — these tarballs exist purely to skip that
step, not because the build is otherwise hard. For the x86_64 (`intel`)
image on Apple Silicon, cross-build with buildx:
`docker buildx build --platform linux/amd64 -f <Dockerfile> -t <name> --load .`
