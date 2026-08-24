# Where the preset packs come from, and how they were translated

cameralink ships 42 presets across five groups. Only the first group maps
directly onto real camera controls; the other four are **approximate
translations** from a different tool's much richer editing model, and
this doc is upfront about exactly how approximate.

## Film Recipe Chart (7 presets)

Sourced from published film-stock technical data sheets (Kodak's own
Portra 400/Ektar 100/Gold 200 sheets, Fujifilm's Pro 400H and Superia
X-TRA 400 bulletins, Ilford's HP5 Plus data sheet, CineStill's own specs).
Every field in these presets — Contrast, Highlights, Shadows, Fade,
Saturation, Sharpness, Sharpness Range, Clarity, White Balance, ISO — is a
real, directly-set camera control. No translation involved.

## Film Stock Emulation (5) and Fujifilm Simulation (9)

Ported from a companion iPad photo-editing app
([iPadPhotoFilters](https://github.com/chuckbrant/iPadPhotoFilters) — a
separate, unrelated project) that emulates these same film stocks and
Fujifilm's in-camera digital
simulations using Core Image filters applied to photos *after* the fact:
`CITemperatureAndTint`, `CIVibrance`, `CIColorControls`,
`CIHighlightShadowAdjust`, and `CIVignette`.

Sony's Creative Look system doesn't have equivalents for most of that —
no vignette control, no separate vibrance-vs-saturation split, no
standalone brightness knob independent of exposure. The translation used:

| Source (Core Image) | → | Camera (Creative Look) |
|---|---|---|
| `contrast` (~0.85–1.35 multiplier around 1.0) | | Contrast, scaled: `(contrast − 1) × 25`, clamped ±9 |
| `saturation` + `vibrance` combined | | Saturation, scaled: `(saturation − 1) × 20 + vibrance × 10`, clamped ±9 |
| `highlightAmount` (0–1, lower = softer rolloff) | | Highlights, scaled: `(highlightAmount − 1) × 18`, clamped ±9 |
| `shadowAmount` (−1…1, positive lifts) | | Shadows, scaled: `shadowAmount × 9`, clamped ±9 |
| `vignette` + lifted `shadowAmount`, as a "washed/matte" proxy | | Fade, scaled: `vignette × 40 + max(shadowAmount, 0) × 5`, clamped 0–9 |
| `temperature` (Kelvin delta from a 6500K neutral) | | White Balance Kelvin: `6500 + temperature`, clamped to the camera's real range |
| `tint` (green/magenta shift) | | White Balance Tint: `tint × 8`, clamped ±99 |

`brightness` and the vignette's spatial falloff have no camera-side
target at all and are dropped entirely. **Sharpness, Sharpness Range, and
Clarity are omitted from these presets** rather than guessed — the source
data has nothing resembling them, and pushing a wrong guess would be
worse than leaving whatever's already dialed in untouched.

These presets push to **Custom Look slot CS1** rather than a fixed preset
(`ST`, `VV`, etc.), because Creative Look's numeric sub-parameters are
only actually writable on a Custom Look slot — the camera silently
accepts (but ignores) sub-parameter writes against a fixed preset. If you
want to keep one of these as a permanent memory-dial position, push it
and then use the camera's own menu to save CS1's current state to a
`C1`/`C2`/`C3` slot (see the main [README](../README.md) for why that step
can't be automated).

## JP Presets — Daily Collection (10) and PS Presets (11)

Ported from the same companion app's Lightroom/ACR-preset emulations —
full-featured Lightroom-style edits: exposure, per-region tone
(Highlights/Shadows/Whites/Blacks), vibrance/saturation, camera
calibration primaries, per-channel RGB tone curves, split-toning, and
per-color-band (Red/Orange/Yellow/Green/Aqua/Blue/Purple/Magenta)
hue/saturation/luminance adjustments.

The camera has **no equivalent at all** for tone curves, split-toning,
camera calibration, or per-band HSL — that's most of what actually
defines these presets' look. Only the "Basic panel"-equivalent sliders
survive:

| Source (Lightroom, roughly −100…100) | → | Camera (Creative Look) |
|---|---|---|
| `contrast` | | Contrast, scaled: `value ÷ 11`, clamped ±9 |
| `highlights` | | Highlights, scaled: `value ÷ 11`, clamped ±9 |
| `shadows` | | Shadows, scaled: `value ÷ 11`, clamped ±9 |
| `saturation` (weighted 70%) + `vibrance` (weighted 30%) | | Saturation, scaled: `(sat × 0.7 + vib × 0.3) ÷ 11`, clamped ±9 |
| `blacks`, as a crushed-vs-lifted proxy for "fade" | | Fade, scaled: `(blacks + 100) ÷ 22`, clamped 0–9 |

`exposure`, `whites`, every HSL band, every tone curve, split-toning, and
camera calibration are dropped. These 21 presets **don't set White
Balance or ISO at all** (the source data has no equivalent), and — same
as the Film Stock/Fujifilm groups above — omit Sharpness/Sharpness
Range/Clarity and push to Custom Look **CS1**.

**Bottom line**: treat these last two groups as loose stylistic
starting points inspired by the named preset, not a faithful
reproduction — the gap between "adjust 5 scalar sliders" and "full
Lightroom grade" is real and can't be closed by better math, only by the
camera exposing controls it doesn't have.
