# Prebuilt Docker images

Ready-to-run images for anyone who just wants to run cameralink without
building it themselves — same code as [scripts/Dockerfile](../scripts/Dockerfile),
CrSDK-based, network/Wi-Fi connect only (no USB — see
[docs/DOCKER.md](../docs/DOCKER.md) for why).

- `mac-arm64/cameralink-mac-arm64.tar.gz` — Apple Silicon (M-series Mac),
  for Docker Desktop.
- `intel/cameralink-intel.tar.gz` — x86_64, for a NAS (Synology Container
  Manager, etc.), a home server, or an Intel/AMD Linux box.

## About the bundled Sony Camera Remote SDK

These images embed Sony's CrSDK library files (`libCr_Core.so` and
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

## Running one

```
docker load < mac-arm64/cameralink-mac-arm64.tar.gz    # or intel/cameralink-intel.tar.gz
docker run -d --name cameralink -p 8080:8080 \
  -v "$(pwd)/saved_cameras.json:/app/saved_cameras.json" \
  -v "$(pwd)/saved_recipes.json:/app/saved_recipes.json" \
  cameralink-local   # or cameralink-intel, matching the tag baked into the image you loaded
```

Pre-create `saved_cameras.json`/`saved_recipes.json` as `[]` first (Docker
creates a missing bind-mount source as a directory, not a file) — see
[docs/DOCKER.md](../docs/DOCKER.md) step 2. Then open `http://<host>:8080`
and connect to your camera from the Setup tab, same as any other
cameralink deployment.

## Rebuilding these

Each is just `docker build -f scripts/Dockerfile -t <name> .` against a
repo checkout with `third_party/CrSDK/` populated from Sony's SDK for the
matching architecture (see [docs/BUILDING.md](../docs/BUILDING.md)) —
these tarballs exist purely to skip that step, not because the build is
otherwise hard.
