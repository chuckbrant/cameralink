# Where the presets and Saved Recipes come from

## Film Recipe Chart (8 presets, built in)

cameralink ships one built-in preset group: **Film Recipe Chart**, sourced
from published film-stock technical data sheets (Kodak's own Portra
400/Ektar 100/Gold 200 sheets, Fujifilm's Pro 400H and Superia X-TRA 400
bulletins, Ilford's HP5 Plus data sheet, CineStill's own specs, and Kodak
Vision3 500T's). Every field in these presets — Contrast, Highlights,
Shadows, Fade, Saturation, Sharpness, Sharpness Range, Clarity, White
Balance, ISO — is a real, directly-set camera control. No translation or
approximation involved; these are `GET /api/presets`' only entries.

## Removed: four Lightroom/CoreImage-ported preset packs

Earlier versions of this project also shipped four additional preset
groups — Film Stock Emulation, Fujifilm Simulation, JP Presets, and PS
Presets — all **ported from a companion iPad photo-editing app**
([iPadPhotoFilters](https://github.com/chuckbrant/iPadPhotoFilters), a
separate, unrelated project) that emulates film stocks and Lightroom-style
edits using Core Image filters and Lightroom/ACR presets applied to
photos *after the fact*: per-channel tone curves, split-toning, camera
calibration primaries, per-color-band HSL adjustments, vignettes, and
more.

Sony's Creative Look system has no equivalent for almost any of that —
no vignette control, no tone curves, no per-band HSL, no split-toning.
Translating those source grades down to Creative Look's eight scalar
sliders meant most of what actually defined each look's character was
simply dropped, and what survived (a scaled contrast/highlights/shadows/
saturation nudge) increasingly felt like a vague stylistic gesture rather
than a real reproduction of the named preset. That gap didn't shrink with
better math — it's fundamentally a difference in how much control each
system exposes.

**These four packs were removed entirely** rather than kept as a
disclaimed "approximate" tier. In their place: a **Saved Recipes**
library (10 user-managed slots, full create/read/update/rename/delete —
see the main [README](../README.md) and `GET/POST /api/recipes/*`) that
captures whatever you actually dial in on the Custom tab, rather than
guessing at a translation from a different app's much richer editing
model. If you want to recreate one of the old packs' presets, the Custom
tab's Tone Curve and White Balance controls are the same real camera
knobs those packs were always limited to anyway — dial it in by eye
against the actual JPEG output, and save it as a named recipe once you
like it.

## A note on Custom Look slots

Both the Film Recipe Chart presets and Saved Recipes push to whatever
preset slot they were saved with — most Film Recipe Chart presets use a
fixed look (`ST`, `NT`, etc.) since their source data sheets specify one
directly. If you build a Saved Recipe around a **Custom Look slot**
(`CS1`–`CS6`), remember that slot is the only place Creative Look's eight
numeric sub-parameters (Contrast, Highlights, etc.) are actually
writable — the camera silently accepts but ignores sub-parameter writes
against a fixed preset. See
[SDK_CAPABILITIES.md](SDK_CAPABILITIES.md) for the details.
