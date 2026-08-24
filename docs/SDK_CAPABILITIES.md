# What Sony's CrSDK actually exposes

This project connects to the camera through Sony's **Camera Remote SDK
(CrSDK)**, over Wi-Fi/SSH (the "Remote Shoot Function" — see
[CAMERA_SETUP.md](CAMERA_SETUP.md)). Everything below has been verified
directly against a physical a7R V, not just read from documentation.

## Creative Look

The camera's built-in JPEG "film look" system. One preset selector, plus
eight adjustable sub-parameters. **The ranges are not uniform** — several
CrSDK/SDK reference docs list them only as "Variable"; the real numbers
below came from Sony's own per-camera Help Guide and were confirmed by
testing (a value outside these ranges is silently accepted by the SDK,
`SetDeviceProperty` returns success, but the camera never actually applies
it — there's no error surfaced for out-of-range writes):

| Parameter | Range |
|---|---|
| Preset | `ST` `PT` `NT` `VV` `VV2` `FL` `IN` `SH` `BW` `SE`, plus 6 user-programmable slots `CS1`–`CS6` |
| Contrast | −9 … 9 |
| Highlights | −9 … 9 |
| Shadows | −9 … 9 |
| Fade | 0 … 9 |
| Saturation | −9 … 9 |
| Sharpness | 0 … 9 |
| Sharpness Range | **1 … 5** |
| Clarity | 0 … 9 |

The `CS1`–`CS6` slots are the only ones where the eight sub-parameters are
actually adjustable — write attempts against a fixed preset (`ST`, `VV`,
etc.) are accepted without error but silently don't change the sub-values.

## Picture Profile

A slot selector (`Off`, `PP1`–`PP11`) is exposed and read/writable. The
much larger set of per-profile sub-parameters (gamma, knee, color depth
per channel, detail/sharpness curve) has real, documented `CrDeviceProperty`
codes in the SDK headers but isn't wired into this project yet — it's a
substantially bigger surface than Creative Look and wasn't needed for the
film-recipe use case this project was built for.

## White Balance

- **Mode** — `AWB`, `Daylight`, `Shade`, `Cloudy`, `Tungsten`,
  `Fluorescent` (+ 4 sub-variants), `Flash`, `Color Temp/Filter`, and
  three `Custom` slots. Read/write confirmed.
- **Tint** — documented as the camera's on-screen A/B axis, range
  −99…99. Wired for read/write.
- **R-Gain / B-Gain** — a separate, finer raw-gain pair, range ±32767,
  encoded at **10× the real value**. Wired for read/write; not yet
  empirically confirmed which on-screen axis (if any beyond Tint) this
  corresponds to.
- **Preset Color Temperature** (Kelvin) — settable when White Balance
  mode is `Color Temp/Filter`. In testing this write is unreliable over
  the network connection even once the mode switch is confirmed applied —
  treated as best-effort/non-fatal by the server rather than a hard
  failure, since blocking the rest of a recipe write on one flaky field
  isn't worth it.

## ISO

Exposed as a single `UInt32` property with a bitfield encoding, not a
plain integer:

```
bits 0-23:  ISO value (or 0xFFFFFF for Auto)
bits 24-27: ISO mode (Normal / Multi Frame NR / Multi Frame NR High / extended)
bits 28-31: extension
```

Read/write confirmed working, including round-tripping a value change and
reading it back correctly.

## What CrSDK does *not* expose

**No property or command saves the camera's current live state into an
in-camera memory slot** (the physical `MR`/`C1`/`C2`/`C3` dial or menu
positions). This was checked directly — no `CrDeviceProperty` or
`CrCommandId` in the headers does it, and it isn't in the documented
operation list either. If you want a recipe to survive as a dial position
for untethered shooting later, that's a manual, on-camera step: push the
recipe via cameralink, then use the camera's own menu to register the
current (now cameralink-set) state to a memory slot.

## Connection quirk: USB and Wi-Fi are mutually exclusive

Not a CrSDK limitation as such, but a camera-firmware one worth knowing:
if a USB cable is connected to the camera, Wi-Fi-based Remote Shoot
Function connections fail outright, even with everything else configured
correctly. The camera prioritizes USB and won't run both remote-control
paths simultaneously.
