# Sony Config (iOS/iPadOS app)

[View on GitHub](https://github.com/chuckbrant/cameralink/tree/master/ios/Sony%20Config)

A native iPad app that talks to the exact same REST API as
`server/public/index.html` (the bundled web frontend) — same four tabs
(Custom / Saved Recipes / Film Recipes / Setup), same endpoints, same
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

Then build and run (⌘R) targeting an iPad simulator or a physical iPad.
`project.yml` sets `CODE_SIGN_STYLE: Automatic` with a `DEVELOPMENT_TEAM`
already filled in for on-device builds; change that to your own Team ID
(found via `security find-certificate -c "Apple Development: <Your Name>"
-p | openssl x509 -noout -subject` — it's the `OU=` field, not the ID in
the certificate's display name) if it doesn't match your signing
identity.

## Configuring the server address

The Setup tab has a "cameralink server" field (persisted across launches)
— defaults to `http://10.42.0.1:8080`, the Pi's own Wi-Fi access point
address this whole project was developed against. Change it there if
your server lives elsewhere (a different IP, the Pi's USB gadget address,
a non-Pi test machine from [docs/INSTALL.md](../../docs/INSTALL.md), etc.).

App Transport Security is relaxed (`NSAllowsArbitraryLoads`) since the
server is plain HTTP by design (a local-network-only tool, no public
internet exposure) — see `project.yml`'s `Info.plist` properties.

Setup also has two one-tap **Quick Connect** buttons -- "Network"
(`http://10.42.0.1:8080`, the Pi's Wi-Fi access point) and "USB Gadget"
(`http://192.168.7.2:8080`, the Pi's USB gadget address) -- that switch
the server address and then connect using whatever camera profile is
already saved on that server. No camera credentials are ever hardcoded
in this app; they're fetched fresh from the Pi each time.

## Two connections, shown separately

The header at the top of every tab shows two independent dots: whether
*this device* can reach the Pi's server at all (`AppState.serverReachable`,
set by `GET /api/status` succeeding or failing at the transport level),
and whether the *camera* is paired to the Pi (`AppState.connected`, from
that same response's `connected` field). The web UI only has one status
dot and can't tell these apart -- a dead USB cable and a disconnected
camera look identical there. Diagnosing which one is actually down
determines the fix (reconnect the cable/Wi-Fi vs. reconnect the camera).

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

Builds clean (no warnings) and confirmed working end-to-end on **both**
the iOS Simulator and a physical iPad, connected to the cameralink server
over both transports (the Pi's Wi-Fi access point and its USB gadget
cable) and paired to a real Sony a7R V. All four tabs correctly load and
display live camera state, presets, and saved recipes over the network,
including the Tone Curve and White Balance grid widgets.

A real-world timing issue was found and fixed along the way:
`URLSession`'s default request timeout (10s) was too aggressive for
`/api/connect/network` specifically -- the camera-pairing handshake can
legitimately take longer than that right as the camera is still settling
onto Wi-Fi, and the web UI's plain `fetch()` (no timeout at all) would
succeed at exactly the moment this app gave up early. Connect calls now
get a 45s timeout instead of the normal 10s default.

Not yet exercised through every interactive control individually (e.g.
dragging the tone curve points, tapping through the WB grid on-device) —
those share source-level logic with what's been verified, but haven't
each been individually click-tested end-to-end on a physical iPad.
