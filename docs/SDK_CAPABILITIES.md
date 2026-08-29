# What Sony's CrSDK actually exposes

This project connects to the camera through Sony's **Camera Remote SDK
(CrSDK)**, over Wi-Fi/SSH (the "Remote Shoot Function" — see
[CAMERA_SETUP.md](CAMERA_SETUP.md)). Everything below has been verified
directly against a physical a7R V (ILCE-7RM5), not just read from
documentation — and several properties that *look* correct in the SDK's
own headers turned out to be silently dead on this camera. The standing
diagnostic tool for this kind of investigation is `GET /api/debug/allprops`,
which dumps every property `SDK::GetDeviceProperties()` returns for the
connected camera (name, get/set-enabled flags, and current raw value) —
used repeatedly below to confirm a property actually exists before
trusting it.

For the full picture beyond what this project happens to use, see
[docs/reference/a7RV_all_properties.pdf](reference/a7RV_all_properties.pdf) —
a complete dump of all 431 properties `GetDeviceProperties()` returns for
this camera (code, name, Sony's own description text, current value,
get/set flags), generated the same way as `/api/debug/allprops` above.
Useful for anyone extending this project into properties it doesn't
touch yet.

## Every property this project reads or writes

| Property (`CrDeviceProperty_...`) | Data type | Access | Used for |
|---|---|---|---|
| `CreativeLook` | `UInt16` | Read/Write | Preset selector (`ST`/`PT`/.../`CS1`-`CS6`) |
| `CreativeLook_Contrast` | `Int8` | Read/Write | Creative Look sub-parameter |
| `CreativeLook_Highlights` | `Int8` | Read/Write | Creative Look sub-parameter |
| `CreativeLook_Shadows` | `Int8` | Read/Write | Creative Look sub-parameter |
| `CreativeLook_Fade` | `Int8` | Read/Write | Creative Look sub-parameter |
| `CreativeLook_Saturation` | `Int8` | Read/Write | Creative Look sub-parameter |
| `CreativeLook_Sharpness` | `Int8` | Read/Write | Creative Look sub-parameter |
| `CreativeLook_SharpnessRange` | `Int8` | Read/Write | Creative Look sub-parameter |
| `CreativeLook_Clarity` | `Int8` | Read/Write | Creative Look sub-parameter |
| `PictureProfile` | `UInt16` | Read-only | Slot selector (`Off`/`PP1`-`PP11`) -- see note below |
| `WhiteBalance` | `UInt16` | Read/Write | WB mode |
| `Colortemp` | `UInt16` | Read/Write | Kelvin value (when WB mode is Color Temp) |
| `ColorTuningAB` | `UInt8Range` | Read/Write | Color Filter A-B axis |
| `ColorTuningGM` | `UInt8Range` | Read/Write | Color Filter G-M axis |
| `IsoSensitivity` | `UInt32` | Read/Write | ISO (bitfield-encoded, see below) |
| `AspectRatio` | `UInt16` | Read/Write | `3:2`/`16:9`/`4:3`/`1:1` |
| `FileType` | `UInt8` | Read/Write | `RAW`/`RAW+JPEG`/`JPEG` |
| `ModelName` | `STR` | Read-only | `GET /api/camera-info` |
| `BodySerialNumber` | `STR` | Read-only | `GET /api/camera-info` |
| `SoftwareVersion` | `STR` | Read-only | `GET /api/camera-info` |
| `LensModelName` | `STR` | Read-only | `GET /api/camera-info` |
| `LensVersionNumber` | `STR` | Read-only | `GET /api/camera-info` |
| `BatteryLevel` | enum | Read-only | `GET /api/camera-info` |
| `BatteryRemain` | `Int8` | Read-only | `GET /api/camera-info` |
| `MediaSLOT1_RemainingNumber` | `UInt32` | Read-only | `GET /api/camera-info` |
| `MediaSLOT2_RemainingNumber` | `UInt32` | Read-only | `GET /api/camera-info` |
| `WhiteBalancePresetColorTemperature` | -- | Neither (dead) | Looked promising, isn't -- see below |

Every row is covered in more detail in its own section below (data type
gotchas, calibration, dead ends). `DRO`/`HighIsoNR` were used by this
project at one point and are documented further down even though they
aren't currently wired in.

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

A slot selector (`Off`, `PP1`–`PP11`) is exposed and included in
`GET /api/recipe` as `pictureProfileSlot` -- **read-only in this project's
current code**, not read/write as an earlier version of this doc claimed.
Nothing in `writeRecipeJson()` sets `CrDeviceProperty_PictureProfile`; the
SDK property itself is documented as settable, this project just doesn't
exercise that path. The much larger set of per-profile sub-parameters
(gamma, knee, color depth per channel, detail/sharpness curve) has real,
documented `CrDeviceProperty` codes in the SDK headers but isn't wired
into this project at all yet — it's a substantially bigger surface than
Creative Look and wasn't needed for the film-recipe use case this project
was built for.

## White Balance

- **Mode** — `AWB`, `Daylight`, `Shade`, `Cloudy`, `Tungsten`,
  `Fluorescent` (+ 4 sub-variants), `Flash`, `Color Temp`, and three
  `Custom` slots. Read/write confirmed.
- **Kelvin color temperature** — `CrDeviceProperty_Colortemp` (code
  `0x115`), `CrDataType_UInt16`. This is the one that actually works;
  see the dead-property note below for the one that doesn't. Settable
  when White Balance mode is `Color Temp`; confirmed round-tripping a
  written value (e.g. `6500`) back out correctly, and cross-checked
  against `/api/debug/allprops`' own raw value to make sure it was a
  real camera-side change, not just an echo of the request body.
- **Color Filter (A-B / G-M axes)** — `CrDeviceProperty_ColorTuningAB` /
  `_ColorTuningGM`, both `CrDataType_UInt8Range`, raw range
  `0x9C`(156)–`0xE4`(228), center `192`, **4 raw units per on-screen
  step** (positive = Amber/Green, negative = Blue/Magenta). This
  calibration was reverse-engineered empirically by setting known
  on-screen values (e.g. `A+7`, `B-7`) on the physical camera's own
  White Balance fine-tune grid and reading back the raw property value
  each time — not documented anywhere in the SDK headers. The frontend's
  15×15 grid picker matches the camera's own on-screen grid exactly as
  a result.

### Dead properties (present in the SDK headers, absent or non-functional on this camera)

Two W\hite Balance properties that *look* like the obvious way to do the
above turned out to be dead ends, confirmed by dumping the camera's full,
unrestricted property list (`SDK::GetDeviceProperties()`, not the
hand-picked subset `GetSelectDeviceProperties()` normally reads) and
finding they simply aren't in it at all:

- **`CrDeviceProperty_WhiteBalanceTint`** — not present in the full
  property dump. This is why Color Filter A-B/G-M (above) is the real
  mechanism instead.
- **`CrDeviceProperty_RGain` / `_BGain`** — also not present. Same
  conclusion.
- **`CrDeviceProperty_WhiteBalancePresetColorTemperature`** — also not
  present. Writes against it are silently accepted (`SetDeviceProperty`
  returns `CrError_None`) but never actually change anything, because
  Sony's own documentation is explicit that this function's return value
  doesn't indicate whether the property was actually applied. The real,
  live Kelvin property is `CrDeviceProperty_Colortemp` (above) — a
  different, unrelated property despite the very similar name.

The general lesson, worth restating for anyone extending this project:
**a property existing in the SDK's C++ headers does not mean it exists on
a given camera body.** Always confirm via `/api/debug/allprops` before
wiring a new field into the recipe read/write path.

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

## Aspect Ratio

`CrDeviceProperty_AspectRatio` (`CrAspectRatioIndex` enum), `CrDataType_UInt16`.
Four values confirmed: `3:2`, `16:9`, `4:3`, `1:1`. Read/write confirmed.

## File Type (RAW / RAW+JPEG / JPEG)

`CrDeviceProperty_FileType` (`CrFileType` enum, code `0x106`). The SDK
enum also defines `RawHeif`/`Heif`, not offered here (untested, and HEIF
introduces a whole separate compression-format property this project
doesn't otherwise touch).

**Data type matters here and is easy to get wrong**: `CrFileType`'s
underlying C++ type is `CrInt16u`, which would suggest
`CrDataType_UInt16` — but that silently no-oped on the real camera
(`SetDeviceProperty` returned success, the property never changed).
`CrDataType_UInt8` is what actually works. All three values (`RAW`=2,
`JPEG`=1, `RAW+JPEG`=3) were confirmed by hand: set via the API, then
independently verified via `/api/debug/allprops` that the camera's own
raw value actually changed, not just that the write call returned
success.

## Camera / Lens / Memory info

Read-only, exposed via `GET /api/camera-info`:

- `ModelName`, `BodySerialNumber`, `SoftwareVersion`, `LensModelName`,
  `LensVersionNumber` — all `CrDataType_STR`. **The string decoding has a
  gotcha**: the data lives in `GetCurrentStr()` (a null-terminated
  `CrInt16u*`, i.e. UTF-16), not `GetCurrentValue()`, and the *first*
  `CrInt16u` unit is a length prefix (the string's length including its
  null terminator), not the first character. Skipping that prefix was a
  real bug found and fixed here — confirmed by the leading byte matching
  `strlen(name) + 1` exactly for both `ModelName` and `BodySerialNumber`.
- `BatteryLevel` — `CrBatteryLevel` enum. Not a simple ordinal: it's a
  bitfield-ish combination of a coarse charge level (`PreEndBattery`,
  `1_4`…`3_3`, i.e. quarters and thirds) OR'd with a `+0x10000` offset
  when running on USB power, plus two sentinel values
  (`CrBatteryLevel_Fake`, `CrBatteryLevel_BatteryNotInstalled`). Decoded
  via direct case-matching rather than arithmetic, since the offset
  pattern isn't uniform enough to compute reliably.
- `BatteryRemain` — plain signed percentage.
- `MediaSLOT1_RemainingNumber` / `MediaSLOT2_RemainingNumber` — plain
  remaining-shots counts per card slot.

## Removed from this project: D-Range Optimizer, High ISO NR

Both were implemented, confirmed working against the real camera, and
later removed at the user's request to simplify the Custom tab — not
because they didn't work. If reintroducing either:

- **DRO** — `CrDeviceProperty_DRO` (`CrDRangeOptimizer` enum),
  `CrDataType_UInt16`. This camera's own menu only goes up to `Off`/
  `Auto`/Manual 1–5, even though the SDK enum defines further Manual 6–8
  and HDR variants (for a different HDR shooting mode, not applicable
  here).
- **High ISO NR** — `CrDeviceProperty_HighIsoNR` (`CrHighIsoNR` enum),
  `CrDataType_UInt8`. The SDK enum also defines `CrHighIsoNR_High`, but
  hand-testing on the physical camera (cycling the real menu through
  Off→Low→Normal and reading back via the API each time) confirmed
  `High` isn't actually a selectable option on this body at all — only
  Off/Low/Normal are real.

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

## `SetDeviceProperty`'s return value lies

Worth stating explicitly since it explains several of the dead-ends
above: Sony's own documentation says the return value from
`SetDeviceProperty` "will not indicate whether or not the property was
set successfully." A call can return `CrError_None` while doing
absolutely nothing on the camera, if the property doesn't exist, the
value is out of range, or the wrong `CrDataType` was used for
`SetValueType()`. The only reliable way to confirm a write actually took
effect is to read the value back afterward (ideally via
`/api/debug/allprops`'s raw value, not just this project's own decoded
`GET /api/recipe`, to rule out a decoding bug masking a real failure or
vice versa).
