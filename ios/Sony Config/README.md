# Sony Config (iOS/iPadOS app)

A native iPad app that talks to the exact same REST API as
`server/public/index.html` (the bundled web frontend) — same four tabs
(Custom / Film Recipes / Saved Recipes / Setup), same endpoints, same
recipe fields. It's a second client of the cameralink server, not a
replacement for it; the server (and the camera connection logic) is
unchanged.

## Requirements

- Xcode 15+ (developed against Xcode 26.6), targeting iOS/iPadOS 17+.
- [XcodeGen](https://github.com/yonaskolb/XcodeGen) (`brew install xcodegen`)
  — the `.xcodeproj` is generated from `project.yml`, not hand-maintained.
  Re-run `xcodegen generate` after editing `project.yml` or adding/removing
  source files.
- A cameralink server reachable from wherever you're running the app (see
  the main [README](../../README.md) and [docs/INSTALL.md](../../docs/INSTALL.md)).
  The iOS Simulator shares the host Mac's network, so if `curl
  http://<pi-ip>:8080/api/status` works from Terminal, the Simulator can
  reach it too — no extra setup needed for simulator testing.

## Building and running

```
cd "ios/Sony Config"
xcodegen generate
open "Sony Config.xcodeproj"
```

Then build and run (⌘R) targeting an iPad simulator. No code signing is
required for simulator builds (`CODE_SIGNING_ALLOWED: NO` in
`project.yml`); you'll need to set a Development Team in the project
settings before running on a physical device.

## Configuring the server address

The Setup tab has a "cameralink server" field (persisted across launches)
— defaults to `http://10.42.0.1:8080`, the Pi's own Wi-Fi access point
address this whole project was developed against. Change it there if
your server lives elsewhere (a different IP, the Pi's USB gadget address,
a non-Pi test machine from [docs/INSTALL.md](../../docs/INSTALL.md), etc.).

App Transport Security is relaxed (`NSAllowsArbitraryLoads`) since the
server is plain HTTP by design (a local-network-only tool, no public
internet exposure) — see `project.yml`'s `Info.plist` properties.

## Project structure

```
Sony Config/
  SonyConfigApp.swift       App entry point
  AppState.swift            @Observable app state + all API-calling logic
  APIClient.swift           Thin REST client -- one method per server endpoint
  Models.swift               Codable structs matching the server's JSON exactly
  Views/
    RootView.swift            Tab container + connection status header
    CustomView.swift          Preset/Tone Curve/WB/ISO/Other Settings + Load/Save/Save Recipe
    ToneCurveView.swift        Draggable tone curve -- ports the exact math from index.html
    WhiteBalanceGridView.swift 15x15 Color Filter grid -- ports the exact math from index.html
    FilmRecipesView.swift      Built-in preset cards + spec chip breakdown
    SavedRecipesView.swift     10-slot CRUD library cards
    SetupView.swift            Server address, Quick Connect, camera info, Shut Down Pi
```

`ToneCurveView` and `WhiteBalanceGridView` are direct ports of the
corresponding canvas-drawing and drag-interaction JavaScript in
`server/public/index.html` (same curve formula, same grid corner colors,
same drag-to-value math) — not a from-scratch reinterpretation. If the
web version's curve or grid behavior ever changes, these should be
updated to match.

## Status

First working version: builds clean (no warnings), and has been verified
live against the real cameralink server and a physical Sony a7R V from
the iPad simulator — all four tabs correctly load and display live camera
state, presets, and saved recipes over the network. Not yet tested on a
physical iPad (needs a Development Team ID for device code signing) or
exercised through every interactive control (e.g. dragging the tone curve
points, tapping through the WB grid) — those share source-level logic
with what's been verified, but haven't each been individually
click-tested end-to-end.
