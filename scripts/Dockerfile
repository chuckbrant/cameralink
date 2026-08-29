# Builds cameralink_server for a generic x86_64 Linux host (a NAS, a home
# server, a VM) using Sony's Linux64PC CrSDK build -- an alternative to the
# Pi Zero 2 W field kit for anyone who already has an always-on Linux box
# on the same network as the camera. See docs/DOCKER.md.
#
# Build context is the repo root (see scripts/docker-compose.yml), so this
# needs third_party/CrSDK/ already populated per docs/BUILDING.md, just
# with the Linux64PC SDK build instead of an ARM one.
FROM debian:bookworm-slim AS builder

# libCr_Core.so (the x86_64 build) links against libxml2 -- not something
# this project chose, it's a transitive dependency baked into Sony's
# prebuilt .so and not needed by the ARM/Pi build.
RUN apt-get update && apt-get install -y --no-install-recommends g++ libxml2-dev \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /build
COPY third_party /build/third_party
COPY server/main.cpp /build/server/main.cpp

# Same flags as scripts/build.sh (the Pi build) -- see that script for why
# each one is needed.
RUN g++ -std=c++17 -pthread -fsigned-char -fstack-protector-all \
    -I "/build/third_party/CrSDK/CRSDK" -I "/build/third_party" -I "/build/third_party/CrSDK" \
    /build/server/main.cpp /build/third_party/CrSDK/CrDebugString.cpp \
    -L "/build/third_party/CrSDK" -lCr_Core \
    -Wl,-rpath,'$ORIGIN' \
    -o /build/cameralink_server

FROM debian:bookworm-slim
RUN apt-get update && apt-get install -y --no-install-recommends libxml2 \
    && rm -rf /var/lib/apt/lists/*
WORKDIR /app

# Same "runtime .so files must be direct siblings of the binary's working
# directory" requirement documented in scripts/build.sh -- libCr_Core.so
# resolves CrAdapter/ relative to cwd, not its own file location.
COPY --from=builder /build/cameralink_server /app/cameralink_server
COPY third_party/CrSDK/*.so /app/
COPY third_party/CrSDK/CrAdapter /app/CrAdapter
COPY server/public /app/public

# "Shut Down Pi" is a no-op here anyway (see docs/DOCKER.md), but there's
# no reason to show a button for it in a deployment with no Pi at all --
# strip the block between its markers in the served copy only, the
# source file (used by the actual Pi build) is untouched.
RUN sed -i '/<!-- PI-ONLY:START -->/,/<!-- PI-ONLY:END -->/d' /app/public/index.html

EXPOSE 8080
CMD ["./cameralink_server"]
