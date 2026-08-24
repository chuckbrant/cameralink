// SonyConfig web app -- minimal REST backend over CrSDK, single binary,
// serves the static frontend from ./public.
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <future>
#include <iostream>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "CrDeviceProperty.h"
#include "CameraRemote_SDK.h"
#include "IDeviceCallback.h"
#include "httplib.h"

namespace SDK = SCRSDK;

// ---------------------------------------------------------------------
// Panasonic preset tables, transcribed from the G9's own cam.cgi
// allmenu.xml dump. Unlike Sony's Creative Look, cam.cgi exposes no
// continuous sub-parameters (contrast/sharpness/saturation/NR) for Photo
// Style at all -- confirmed by enumerating all 34 cmd_type values the
// protocol supports. So these are just the camera's own named presets,
// not a graded recipe.
// ---------------------------------------------------------------------
const char* kPanasonicPresetsJson = R"JSON([
  {"group": "Photo Style", "id": "ps-standard", "name": "Standard", "photoStyle": "standard",
    "notes": "The camera's default color/tone response, no styling applied."},
  {"group": "Photo Style", "id": "ps-vivid", "name": "Vivid", "photoStyle": "vivid",
    "notes": "Boosted contrast and saturation for punchy, high-impact color."},
  {"group": "Photo Style", "id": "ps-natural", "name": "Natural", "photoStyle": "natural",
    "notes": "Softer contrast than Standard, aimed at portraits and smoother gradation."},
  {"group": "Photo Style", "id": "ps-mono", "name": "Monochrome", "photoStyle": "bw",
    "notes": "Standard black & white, no color filter simulation."},
  {"group": "Photo Style", "id": "ps-lmono", "name": "L.Monochrome", "photoStyle": "l_bw",
    "notes": "Monochrome tuned for a wider dynamic range and deeper blacks."},
  {"group": "Photo Style", "id": "ps-lmonod", "name": "L.Monochrome D", "photoStyle": "l_bw_d",
    "notes": "L.Monochrome variant with a stronger contrast curve, higher-key highlights."},
  {"group": "Photo Style", "id": "ps-scenery", "name": "Scenery", "photoStyle": "scenery",
    "notes": "Vivid blues and greens tuned for landscape shooting."},
  {"group": "Photo Style", "id": "ps-portrait", "name": "Portrait", "photoStyle": "portrait",
    "notes": "Skin-tone-forward color response, softened contrast."},
  {"group": "Photo Style", "id": "ps-cinelike-d", "name": "Cinelike D2", "photoStyle": "cinelike_d",
    "notes": "Flat, wide-dynamic-range gamma curve for grading in post (dynamic range priority)."},
  {"group": "Photo Style", "id": "ps-cinelike-v", "name": "Cinelike V2", "photoStyle": "cinelike_v",
    "notes": "Cinema-style gamma with more baked-in contrast than Cinelike D2 (video-look priority)."},
  {"group": "Photo Style", "id": "ps-hlg", "name": "HLG", "photoStyle": "hlg",
    "notes": "Hybrid Log-Gamma for HDR-compatible output."},
  {"group": "Photo Style", "id": "ps-vlog", "name": "V-Log", "photoStyle": "vlog_gamma",
    "notes": "Panasonic's flattest log profile, maximum dynamic range for grading."},

  {"group": "Creative Control", "id": "cc-pop", "name": "Expressive", "filterEffect": "pop",
    "notes": "Punchy, saturated color for graphic, poster-like results."},
  {"group": "Creative Control", "id": "cc-retro", "name": "Retro", "filterEffect": "retro",
    "notes": "Faded, warm-toned vintage color grade."},
  {"group": "Creative Control", "id": "cc-old-days", "name": "Old Days", "filterEffect": "old_days",
    "notes": "Soft, hazy highlights with a nostalgic color cast."},
  {"group": "Creative Control", "id": "cc-high-key", "name": "High Key", "filterEffect": "high_key",
    "notes": "Bright, airy exposure bias with lifted shadows."},
  {"group": "Creative Control", "id": "cc-low-key", "name": "Low Key", "filterEffect": "low_key",
    "notes": "Dark, moody exposure bias with crushed shadows."},
  {"group": "Creative Control", "id": "cc-sepia", "name": "Sepia", "filterEffect": "sepia",
    "notes": "Classic monochrome sepia tone."},
  {"group": "Creative Control", "id": "cc-mono", "name": "Monochrome", "filterEffect": "monochro",
    "notes": "Straight black & white, as a Creative Control filter (independent of Photo Style)."},
  {"group": "Creative Control", "id": "cc-dyn-mono", "name": "Dynamic Monochrome", "filterEffect": "dynamic_monochro",
    "notes": "Black & white with boosted contrast for a harder-edged look."},
  {"group": "Creative Control", "id": "cc-rough-mono", "name": "Rough Monochrome", "filterEffect": "rough_monochro",
    "notes": "Gritty, grain-emphasized black & white."},
  {"group": "Creative Control", "id": "cc-silky-mono", "name": "Silky Monochrome", "filterEffect": "silky_monochro",
    "notes": "Smooth, low-grain black & white with softened tonal transitions."},
  {"group": "Creative Control", "id": "cc-impressive", "name": "Impressive Art", "filterEffect": "impressive_art",
    "notes": "Heavy contrast and saturation for a dramatic, painterly result."},
  {"group": "Creative Control", "id": "cc-hdyn", "name": "High Dynamic", "filterEffect": "high_dynamic",
    "notes": "In-camera dynamic-range expansion, flatter tonal response."},
  {"group": "Creative Control", "id": "cc-cross", "name": "Cross Process", "filterEffect": "cross_proc",
    "notes": "Color-shifted highlights/shadows mimicking cross-processed film."},
  {"group": "Creative Control", "id": "cc-toy", "name": "Toy Effect", "filterEffect": "toy_photo",
    "notes": "Vignette and warm color shift mimicking a toy/lo-fi camera."},
  {"group": "Creative Control", "id": "cc-toy-pop", "name": "Toy Pop", "filterEffect": "toy_pop",
    "notes": "Toy Effect combined with boosted saturation."},
  {"group": "Creative Control", "id": "cc-bleach", "name": "Bleach Bypass", "filterEffect": "bleach_bypass",
    "notes": "Desaturated, high-contrast look mimicking the film bleach-bypass process."},
  {"group": "Creative Control", "id": "cc-diorama", "name": "Miniature Effect", "filterEffect": "diorama",
    "notes": "Simulated tilt-shift blur to make scenes look miniature."},
  {"group": "Creative Control", "id": "cc-soft-focus", "name": "Soft Focus", "filterEffect": "soft_focus",
    "notes": "Diffused, dreamy softness across the whole frame."},
  {"group": "Creative Control", "id": "cc-fantasy", "name": "Fantasy", "filterEffect": "fantasy",
    "notes": "Bright, saturated, slightly glowing color for an idealized look."},
  {"group": "Creative Control", "id": "cc-star", "name": "Cross Filter", "filterEffect": "cross_filter",
    "notes": "Star-shaped light streaks on bright point-light sources."},
  {"group": "Creative Control", "id": "cc-one-point", "name": "One Point Color", "filterEffect": "one_point_color",
    "notes": "Keeps one chosen hue in color, converts the rest of the frame to black & white."},
  {"group": "Creative Control", "id": "cc-sunshine", "name": "Sunshine", "filterEffect": "sunshine",
    "notes": "Warm, sun-flare-emphasized color grade."}
])JSON";

// ---------------------------------------------------------------------
// Built-in film recipe presets, transcribed from film_recipe_charts.pdf
// (page 3, "Film Recipe Chart -- Sony a7R V"). Base ISO and the Color
// Filter A/G white-balance offsets are shown as reference notes only --
// not written to the camera, since those property encodings haven't been
// verified against a real camera yet (unlike Creative Look + WB mode,
// which have).
// ---------------------------------------------------------------------
const char* kPresetsJson = R"JSON([
  {
    "group": "Film Recipe Chart",
    "id": "kodak-portra-400",
    "name": "Kodak Portra 400",
    "preset": "NT",
    "contrast": -2, "highlights": -1, "shadows": -1, "fade": 1,
    "saturation": 0, "sharpness": 0, "sharpnessRange": 1, "clarity": 0,
    "whiteBalanceMode": "Daylight",
    "iso": 400,
    "baseISO": "400",
    "whiteBalanceNote": "Color Filter A+1 G+1 (not auto-applied)",
    "notes": "Low contrast with a light veil for the soft, forgiving latitude look."
  },
  {
    "group": "Film Recipe Chart",
    "id": "kodak-ektar-100",
    "name": "Kodak Ektar 100",
    "preset": "VV",
    "contrast": 2, "highlights": -1, "shadows": -1, "fade": 0,
    "saturation": 3, "sharpness": 0, "sharpnessRange": 1, "clarity": 1,
    "whiteBalanceMode": "Daylight",
    "iso": 100,
    "baseISO": "100",
    "whiteBalanceNote": "Color Filter A+1 (not auto-applied)",
    "notes": "Ultra-vivid saturation with fine, sharp detail; Clarity +1 echoes Ektar's crispness."
  },
  {
    "group": "Film Recipe Chart",
    "id": "kodak-gold-200",
    "name": "Kodak Gold 200",
    "preset": "ST",
    "contrast": 1, "highlights": -1, "shadows": -1, "fade": 0,
    "saturation": 2, "sharpness": 0, "sharpnessRange": 1, "clarity": 0,
    "whiteBalanceMode": "Daylight",
    "iso": 200,
    "baseISO": "200",
    "whiteBalanceNote": "Color Filter A+3 G+1 (not auto-applied)",
    "notes": "Low-to-medium contrast, kept modest, as on the other two Kodak stocks."
  },
  {
    "group": "Film Recipe Chart",
    "id": "fujifilm-pro-400h",
    "name": "Fujifilm Pro 400H",
    "preset": "NT",
    "contrast": -1, "highlights": -1, "shadows": -1, "fade": 1,
    "saturation": -1, "sharpness": 0, "sharpnessRange": 1, "clarity": 0,
    "whiteBalanceMode": "AWB",
    "iso": 400,
    "baseISO": "400 (rate 200)",
    "whiteBalanceNote": "Daylight/AWB, Color Filter A+2 G+2 (not auto-applied)",
    "notes": "Fujifilm's own copy stresses neutral grays and controlled shadow saturation."
  },
  {
    "group": "Film Recipe Chart",
    "id": "cinestill-800t",
    "name": "CineStill 800T",
    "preset": "ST",
    "contrast": 1, "highlights": -1, "shadows": -2, "fade": 0,
    "saturation": 0, "sharpness": 0, "sharpnessRange": 1, "clarity": 0,
    "whiteBalanceMode": "ColorTemp",
    "whiteBalanceColorTempK": 3200,
    "iso": 800,
    "baseISO": "800",
    "whiteBalanceNote": "Manual Kelvin ~3200K (Tungsten)",
    "notes": "Matches the film's native tungsten balance -- reproduces the signature blue-teal mismatch if used in daylight."
  },
  {
    "group": "Film Recipe Chart",
    "id": "ilford-hp5-plus",
    "name": "Ilford HP5 Plus",
    "preset": "BW",
    "contrast": 2, "highlights": -1, "shadows": -1, "fade": 0,
    "saturation": null, "sharpness": 0, "sharpnessRange": 1, "clarity": 1,
    "whiteBalanceMode": "Daylight",
    "iso": 400,
    "baseISO": "400",
    "whiteBalanceNote": "Daylight (n/a for filter)",
    "notes": "Sony's BW Creative Look has no built-in colored contrast filter simulation; a physical yellow filter can substitute for classic HP5 sky punch."
  },
  {
    "group": "Film Recipe Chart",
    "id": "fujicolor-superia-xtra-400",
    "name": "Fujicolor Superia X-TRA 400",
    "preset": "ST",
    "contrast": 1, "highlights": -1, "shadows": -1, "fade": 0,
    "saturation": 1, "sharpness": 0, "sharpnessRange": 1, "clarity": 0,
    "whiteBalanceMode": "Daylight",
    "iso": 400,
    "baseISO": "400",
    "whiteBalanceNote": "Color Filter A+1 (not auto-applied)",
    "notes": "Fujifilm's own bulletin emphasizes vibrant reds/blues/yellows and smooth skin tones, so only a mild warm push is used."
  },

  {
    "group": "Film Stock Emulation",
    "id": "filmsim-portra-400",
    "name": "Portra 400",
    "preset": "CS1",
    "contrast": -2, "highlights": -3, "shadows": 1, "fade": 5, "saturation": 1,
    "whiteBalanceMode": "ColorTemp", "whiteBalanceColorTempK": 6800, "whiteBalanceTint": 32,
    "notes": "Approximated from the iPadPhotoFilters CoreImage grade (warm, soft-rolloff, low contrast). Pushes to Custom Look CS1 -- see note above."
  },
  {
    "group": "Film Stock Emulation",
    "id": "filmsim-ektar-100",
    "name": "Ektar 100",
    "preset": "CS1",
    "contrast": 4, "highlights": 0, "shadows": 0, "fade": 2, "saturation": 9,
    "whiteBalanceMode": "ColorTemp", "whiteBalanceColorTempK": 6350, "whiteBalanceTint": 0,
    "notes": "Approximated from the iPadPhotoFilters CoreImage grade (punchy saturation/contrast). Pushes to Custom Look CS1."
  },
  {
    "group": "Film Stock Emulation",
    "id": "filmsim-gold-200",
    "name": "Gold 200",
    "preset": "CS1",
    "contrast": 1, "highlights": -2, "shadows": 1, "fade": 7, "saturation": 3,
    "whiteBalanceMode": "ColorTemp", "whiteBalanceColorTempK": 7000, "whiteBalanceTint": 48,
    "notes": "Approximated from the iPadPhotoFilters CoreImage grade (golden cast, lifted shadows). Pushes to Custom Look CS1."
  },
  {
    "group": "Film Stock Emulation",
    "id": "filmsim-superia-400",
    "name": "Superia 400",
    "preset": "CS1",
    "contrast": 2, "highlights": -1, "shadows": 0, "fade": 3, "saturation": 6,
    "whiteBalanceMode": "ColorTemp", "whiteBalanceColorTempK": 6350, "whiteBalanceTint": -32,
    "notes": "Approximated from the iPadPhotoFilters CoreImage grade (cool green/teal cast). Pushes to Custom Look CS1."
  },
  {
    "group": "Film Stock Emulation",
    "id": "filmsim-vision3-500t",
    "name": "Vision3 500T",
    "preset": "CS1",
    "contrast": -3, "highlights": -3, "shadows": 2, "fade": 3, "saturation": -2,
    "whiteBalanceMode": "ColorTemp", "whiteBalanceColorTempK": 5900, "whiteBalanceTint": -16,
    "notes": "Approximated from the iPadPhotoFilters CoreImage grade (tungsten cinema stock, desaturated, lifted blacks). Pushes to Custom Look CS1."
  },

  {
    "group": "Fujifilm Simulation",
    "id": "fujisim-provia",
    "name": "Provia",
    "preset": "CS1",
    "contrast": 1, "highlights": -1, "shadows": 0, "fade": 0, "saturation": 2,
    "whiteBalanceMode": "ColorTemp", "whiteBalanceColorTempK": 6500, "whiteBalanceTint": 0,
    "notes": "Approximated from the iPadPhotoFilters CoreImage grade (natural, minimal grade). Pushes to Custom Look CS1."
  },
  {
    "group": "Fujifilm Simulation",
    "id": "fujisim-velvia",
    "name": "Velvia",
    "preset": "CS1",
    "contrast": 5, "highlights": 0, "shadows": 0, "fade": 2, "saturation": 9,
    "whiteBalanceMode": "ColorTemp", "whiteBalanceColorTempK": 6400, "whiteBalanceTint": 0,
    "notes": "Approximated from the iPadPhotoFilters CoreImage grade (heavy saturation/contrast landscape stock). Pushes to Custom Look CS1."
  },
  {
    "group": "Fujifilm Simulation",
    "id": "fujisim-astia",
    "name": "Astia",
    "preset": "CS1",
    "contrast": -3, "highlights": -3, "shadows": 1, "fade": 3, "saturation": 2,
    "whiteBalanceMode": "ColorTemp", "whiteBalanceColorTempK": 6650, "whiteBalanceTint": 16,
    "notes": "Approximated from the iPadPhotoFilters CoreImage grade (soft portrait profile). Pushes to Custom Look CS1."
  },
  {
    "group": "Fujifilm Simulation",
    "id": "fujisim-classic-chrome",
    "name": "Classic Chrome",
    "preset": "CS1",
    "contrast": 4, "highlights": -2, "shadows": -1, "fade": 5, "saturation": -4,
    "whiteBalanceMode": "ColorTemp", "whiteBalanceColorTempK": 6450, "whiteBalanceTint": 0,
    "notes": "Approximated from the iPadPhotoFilters CoreImage grade (muted, documentary contrast). Pushes to Custom Look CS1."
  },
  {
    "group": "Fujifilm Simulation",
    "id": "fujisim-reala-ace",
    "name": "Reala Ace",
    "preset": "CS1",
    "contrast": 3, "highlights": -1, "shadows": -1, "fade": 1, "saturation": 2,
    "whiteBalanceMode": "ColorTemp", "whiteBalanceColorTempK": 6500, "whiteBalanceTint": 0,
    "notes": "Approximated from the iPadPhotoFilters CoreImage grade (faithful color, harder tonality). Pushes to Custom Look CS1."
  },
  {
    "group": "Fujifilm Simulation",
    "id": "fujisim-pro-neg-hi",
    "name": "Pro Neg. Hi",
    "preset": "CS1",
    "contrast": 4, "highlights": -1, "shadows": 0, "fade": 2, "saturation": 5,
    "whiteBalanceMode": "ColorTemp", "whiteBalanceColorTempK": 6600, "whiteBalanceTint": 16,
    "notes": "Approximated from the iPadPhotoFilters CoreImage grade (studio portrait, enhanced contrast). Pushes to Custom Look CS1."
  },
  {
    "group": "Fujifilm Simulation",
    "id": "fujisim-pro-neg-std",
    "name": "Pro Neg. Std",
    "preset": "CS1",
    "contrast": -1, "highlights": -2, "shadows": 1, "fade": 2, "saturation": 3,
    "whiteBalanceMode": "ColorTemp", "whiteBalanceColorTempK": 6650, "whiteBalanceTint": 24,
    "notes": "Approximated from the iPadPhotoFilters CoreImage grade (soft transitional skin tones). Pushes to Custom Look CS1."
  },
  {
    "group": "Fujifilm Simulation",
    "id": "fujisim-classic-neg",
    "name": "Classic Neg",
    "preset": "CS1",
    "contrast": 5, "highlights": -2, "shadows": -1, "fade": 4, "saturation": 3,
    "whiteBalanceMode": "ColorTemp", "whiteBalanceColorTempK": 6700, "whiteBalanceTint": -64,
    "notes": "Approximated from the iPadPhotoFilters CoreImage grade (vintage color-negative, green/brown skew). Pushes to Custom Look CS1."
  },
  {
    "group": "Fujifilm Simulation",
    "id": "fujisim-nostalgic-neg",
    "name": "Nostalgic Neg",
    "preset": "CS1",
    "contrast": 1, "highlights": -4, "shadows": 0, "fade": 6, "saturation": 3,
    "whiteBalanceMode": "ColorTemp", "whiteBalanceColorTempK": 6900, "whiteBalanceTint": 48,
    "notes": "Approximated from the iPadPhotoFilters CoreImage grade (amber highlights, 1970s print look). Pushes to Custom Look CS1."
  },

  {
    "group": "JP Presets — Daily Collection",
    "id": "jp-washed-green",
    "name": "Washed Green",
    "preset": "CS1",
    "contrast": 0, "highlights": -6, "shadows": 5, "fade": 3, "saturation": -2,
    "notes": "Approximated from a Lightroom preset's Basic-panel values only -- its HSL band and tone-curve work has no in-camera equivalent. Pushes to Custom Look CS1."
  },
  {
    "group": "JP Presets — Daily Collection",
    "id": "jp-punch",
    "name": "Punch",
    "preset": "CS1",
    "contrast": 0, "highlights": -9, "shadows": 9, "fade": 1, "saturation": -2,
    "notes": "Approximated from a Lightroom preset's Basic-panel values only -- its HSL band and tone-curve work has no in-camera equivalent. Pushes to Custom Look CS1."
  },
  {
    "group": "JP Presets — Daily Collection",
    "id": "jp-light-and-dreamy",
    "name": "Light & Dreamy",
    "preset": "CS1",
    "contrast": 0, "highlights": 0, "shadows": 4, "fade": 5, "saturation": -2,
    "notes": "Approximated from a Lightroom preset's Basic-panel values only -- its HSL band and tone-curve work has no in-camera equivalent. Pushes to Custom Look CS1."
  },
  {
    "group": "JP Presets — Daily Collection",
    "id": "jp-muted",
    "name": "Muted",
    "preset": "CS1",
    "contrast": 0, "highlights": 3, "shadows": 7, "fade": 3, "saturation": -2,
    "notes": "Approximated from a Lightroom preset's Basic-panel values only -- its HSL band and tone-curve work has no in-camera equivalent. Pushes to Custom Look CS1."
  },
  {
    "group": "JP Presets — Daily Collection",
    "id": "jp-bright-whites",
    "name": "Bright Whites",
    "preset": "CS1",
    "contrast": 0, "highlights": -4, "shadows": 9, "fade": 1, "saturation": -2,
    "notes": "Approximated from a Lightroom preset's Basic-panel values only -- its HSL band and tone-curve work has no in-camera equivalent. Pushes to Custom Look CS1."
  },
  {
    "group": "JP Presets — Daily Collection",
    "id": "jp-washed-orange",
    "name": "Washed Orange",
    "preset": "CS1",
    "contrast": 0, "highlights": -7, "shadows": 8, "fade": 3, "saturation": -2,
    "notes": "Approximated from a Lightroom preset's Basic-panel values only -- its HSL band and tone-curve work has no in-camera equivalent. Pushes to Custom Look CS1."
  },
  {
    "group": "JP Presets — Daily Collection",
    "id": "jp-dark-and-moody",
    "name": "Dark & Moody",
    "preset": "CS1",
    "contrast": 0, "highlights": -5, "shadows": 5, "fade": 1, "saturation": -2,
    "notes": "Approximated from a Lightroom preset's Basic-panel values only -- its HSL band and tone-curve work has no in-camera equivalent. Pushes to Custom Look CS1."
  },
  {
    "group": "JP Presets — Daily Collection",
    "id": "jp-high-key",
    "name": "High Key",
    "preset": "CS1",
    "contrast": 0, "highlights": -6, "shadows": 8, "fade": 4, "saturation": 0,
    "notes": "Approximated from a Lightroom preset's Basic-panel values only -- its HSL band and tone-curve work has no in-camera equivalent. Pushes to Custom Look CS1."
  },
  {
    "group": "JP Presets — Daily Collection",
    "id": "jp-neutral",
    "name": "Neutral",
    "preset": "CS1",
    "contrast": 0, "highlights": -9, "shadows": 7, "fade": 5, "saturation": -2,
    "notes": "Approximated from a Lightroom preset's Basic-panel values only -- its HSL band and tone-curve work has no in-camera equivalent. Pushes to Custom Look CS1."
  },
  {
    "group": "JP Presets — Daily Collection",
    "id": "jp-heavy-contrast",
    "name": "Heavy Contrast",
    "preset": "CS1",
    "contrast": 0, "highlights": -2, "shadows": 6, "fade": 2, "saturation": -2,
    "notes": "Approximated from a Lightroom preset's Basic-panel values only -- its HSL band and tone-curve work has no in-camera equivalent. Pushes to Custom Look CS1."
  },

  {
    "group": "PS Presets",
    "id": "ps-1c2m",
    "name": "1C2M",
    "preset": "CS1",
    "contrast": 0, "highlights": -5, "shadows": 0, "fade": 2, "saturation": 0,
    "notes": "Approximated from a Lightroom preset's Basic-panel values only -- its HSL band and split-toning work has no in-camera equivalent. Pushes to Custom Look CS1."
  },
  {
    "group": "PS Presets",
    "id": "ps-bluer-hues",
    "name": "Bluer Hues",
    "preset": "CS1",
    "contrast": 3, "highlights": -8, "shadows": 9, "fade": 3, "saturation": 1,
    "notes": "Approximated from a Lightroom preset's Basic-panel values only -- its HSL band and split-toning work has no in-camera equivalent. Pushes to Custom Look CS1."
  },
  {
    "group": "PS Presets",
    "id": "ps-classy",
    "name": "Classy",
    "preset": "CS1",
    "contrast": -2, "highlights": -3, "shadows": 1, "fade": 6, "saturation": 0,
    "notes": "Approximated from a Lightroom preset's Basic-panel values only -- its HSL band and split-toning work has no in-camera equivalent. Pushes to Custom Look CS1."
  },
  {
    "group": "PS Presets",
    "id": "ps-faded",
    "name": "Faded",
    "preset": "CS1",
    "contrast": 6, "highlights": -4, "shadows": 7, "fade": 5, "saturation": 0,
    "notes": "Approximated from a Lightroom preset's Basic-panel values only -- its HSL band and split-toning work has no in-camera equivalent. Pushes to Custom Look CS1."
  },
  {
    "group": "PS Presets",
    "id": "ps-greens",
    "name": "Greens",
    "preset": "CS1",
    "contrast": -2, "highlights": -1, "shadows": 6, "fade": 5, "saturation": 0,
    "notes": "Approximated from a Lightroom preset's Basic-panel values only -- its HSL band and split-toning work has no in-camera equivalent. Pushes to Custom Look CS1."
  },
  {
    "group": "PS Presets",
    "id": "ps-summer-vibes",
    "name": "Summer Vibes",
    "preset": "CS1",
    "contrast": -3, "highlights": -4, "shadows": 7, "fade": 5, "saturation": -1,
    "notes": "Approximated from a Lightroom preset's Basic-panel values only -- its HSL band and split-toning work has no in-camera equivalent. Pushes to Custom Look CS1."
  },
  {
    "group": "PS Presets",
    "id": "ps-everything",
    "name": "Everything",
    "preset": "CS1",
    "contrast": -5, "highlights": -5, "shadows": 3, "fade": 6, "saturation": -1,
    "notes": "Approximated from a Lightroom preset's Basic-panel values only -- its HSL band and split-toning work has no in-camera equivalent. Pushes to Custom Look CS1."
  },
  {
    "group": "PS Presets",
    "id": "ps-liverbuilding",
    "name": "Liverbuilding",
    "preset": "CS1",
    "contrast": -2, "highlights": -6, "shadows": 7, "fade": 5, "saturation": 0,
    "notes": "Approximated from a Lightroom preset's Basic-panel values only -- its HSL band and split-toning work has no in-camera equivalent. Pushes to Custom Look CS1."
  },
  {
    "group": "PS Presets",
    "id": "ps-wedding",
    "name": "Wedding",
    "preset": "CS1",
    "contrast": -5, "highlights": -6, "shadows": 3, "fade": 2, "saturation": 0,
    "notes": "Approximated from a Lightroom preset's Basic-panel values only -- its HSL band and split-toning work has no in-camera equivalent. Pushes to Custom Look CS1."
  },
  {
    "group": "PS Presets",
    "id": "ps-wedding-boost",
    "name": "Wedding Boost",
    "preset": "CS1",
    "contrast": -5, "highlights": -9, "shadows": 6, "fade": 2, "saturation": 1,
    "notes": "Approximated from a Lightroom preset's Basic-panel values only -- its HSL band and split-toning work has no in-camera equivalent. Pushes to Custom Look CS1."
  },
  {
    "group": "PS Presets",
    "id": "ps-landscapes",
    "name": "Landscapes",
    "preset": "CS1",
    "contrast": -3, "highlights": -6, "shadows": 5, "fade": 2, "saturation": 0,
    "notes": "Approximated from a Lightroom preset's Basic-panel values only -- its HSL band and split-toning work has no in-camera equivalent. Pushes to Custom Look CS1."
  }
])JSON";

// ---------------------------------------------------------------------
// Camera session state (single global session -- one camera at a time)
// ---------------------------------------------------------------------

std::mutex g_cameraMutex;
bool g_sdkInitialized = false;
bool g_connected = false;
std::string g_modelName;
int64_t g_deviceHandle = 0;

std::mutex g_eventPromiseMutex;
std::promise<void>* g_eventPromise = nullptr;
void setEventPromise(std::promise<void>* p) {
    std::lock_guard<std::mutex> lock(g_eventPromiseMutex);
    g_eventPromise = p;
}

// RAII guard: clears g_eventPromise on scope exit no matter which return
// path is taken (success, early error, or timeout). Without this, a
// timed-out wait_for() leaves g_eventPromise dangling at a destroyed
// std::promise -- a late SDK callback then calls set_value()/set_exception()
// on freed memory, which can crash the whole process.
struct EventPromiseGuard {
    explicit EventPromiseGuard(std::promise<void>* p) { setEventPromise(p); }
    ~EventPromiseGuard() { setEventPromise(nullptr); }
};

class DeviceCallback : public SDK::IDeviceCallback {
public:
    void OnConnected(SDK::DeviceConnectionVersioin) override {
        g_connected = true;
        std::lock_guard<std::mutex> lock(g_eventPromiseMutex);
        if (g_eventPromise) {
            try { g_eventPromise->set_value(); } catch (const std::future_error&) {}
            g_eventPromise = nullptr;
        }
    }
    void OnError(CrInt32u error) override {
        fprintf(stderr, "[camera] error 0x%x\n", error);
        std::lock_guard<std::mutex> lock(g_eventPromiseMutex);
        if (g_eventPromise) {
            try {
                g_eventPromise->set_exception(std::make_exception_ptr(std::runtime_error("error")));
            } catch (const std::future_error&) {}
            g_eventPromise = nullptr;
        }
    }
    void OnDisconnected(CrInt32u) override {
        g_connected = false;
    }
};

DeviceCallback g_callback;

void ensureSdkInit() {
    if (!g_sdkInitialized) {
        SDK::Init();
        g_sdkInitialized = true;
    }
}

// ---------------------------------------------------------------------
// Creative Look property field table -- shared by read and write
// ---------------------------------------------------------------------

struct Field {
    std::string key;
    CrInt32u code;
    SDK::CrDataType setType;
    int minValue;
    int maxValue;
};

// Ranges confirmed from Sony's own a7R V Help Guide (Creative Look section) --
// note these are NOT uniformly -9..9: Sharpness and Clarity are 0..9, and
// Sharpness Range is 1..5. Sending an out-of-range value (e.g. a negative
// Sharpness) is silently accepted by SetDeviceProperty (returns
// CrError_None) but never actually applied by the camera -- clamp here so
// that can't happen again.
const std::vector<Field>& creativeLookFields() {
    static const std::vector<Field> fields = {
        {"contrast",       SDK::CrDeviceProperty_CreativeLook_Contrast,       SDK::CrDataType_Int8, -9, 9},
        {"highlights",     SDK::CrDeviceProperty_CreativeLook_Highlights,     SDK::CrDataType_Int8, -9, 9},
        {"shadows",        SDK::CrDeviceProperty_CreativeLook_Shadows,        SDK::CrDataType_Int8, -9, 9},
        {"fade",           SDK::CrDeviceProperty_CreativeLook_Fade,           SDK::CrDataType_Int8, 0, 9},
        {"saturation",     SDK::CrDeviceProperty_CreativeLook_Saturation,     SDK::CrDataType_Int8, -9, 9},
        {"sharpness",      SDK::CrDeviceProperty_CreativeLook_Sharpness,      SDK::CrDataType_Int8, 0, 9},
        {"sharpnessRange", SDK::CrDeviceProperty_CreativeLook_SharpnessRange, SDK::CrDataType_Int8, 1, 5},
        {"clarity",        SDK::CrDeviceProperty_CreativeLook_Clarity,        SDK::CrDataType_Int8, 0, 9},
    };
    return fields;
}

int64_t decodeSigned(const SDK::CrDeviceProperty& p) {
    SDK::CrDataType type = p.GetValueType();
    bool isSigned = (type & 0x1000) != 0;
    uint32_t width = type & 0x000F;
    CrInt64u raw = p.GetCurrentValue();
    if (!isSigned) return (int64_t)raw;
    if (width == 1) return (int64_t)(int8_t)raw;
    if (width == 2) return (int64_t)(int16_t)raw;
    if (width == 3) return (int64_t)(int32_t)raw;
    return (int64_t)raw;
}

std::string presetName(CrInt64u raw) {
    switch (raw) {
        case SDK::CrCreativeLook_ST:  return "ST";
        case SDK::CrCreativeLook_PT:  return "PT";
        case SDK::CrCreativeLook_NT:  return "NT";
        case SDK::CrCreativeLook_VV:  return "VV";
        case SDK::CrCreativeLook_VV2: return "VV2";
        case SDK::CrCreativeLook_FL:  return "FL";
        case SDK::CrCreativeLook_IN:  return "IN";
        case SDK::CrCreativeLook_SH:  return "SH";
        case SDK::CrCreativeLook_BW:  return "BW";
        case SDK::CrCreativeLook_SE:  return "SE";
        default:
            if (raw >= SDK::CrCreativeLook_CustomLookOffset) {
                return "CS" + std::to_string(raw - SDK::CrCreativeLook_CustomLookOffset);
            }
            return "ST";
    }
}

CrInt64u presetRaw(const std::string& name) {
    if (name == "ST")  return SDK::CrCreativeLook_ST;
    if (name == "PT")  return SDK::CrCreativeLook_PT;
    if (name == "NT")  return SDK::CrCreativeLook_NT;
    if (name == "VV")  return SDK::CrCreativeLook_VV;
    if (name == "VV2") return SDK::CrCreativeLook_VV2;
    if (name == "FL")  return SDK::CrCreativeLook_FL;
    if (name == "IN")  return SDK::CrCreativeLook_IN;
    if (name == "SH")  return SDK::CrCreativeLook_SH;
    if (name == "BW")  return SDK::CrCreativeLook_BW;
    if (name == "SE")  return SDK::CrCreativeLook_SE;
    if (name.rfind("CS", 0) == 0) {
        int slot = std::stoi(name.substr(2));
        if (slot >= 1) return SDK::CrCreativeLook_CustomLookOffset + slot;
    }
    return SDK::CrCreativeLook_ST;
}

std::string pictureProfileSlotName(CrInt64u raw) {
    if (raw == SDK::CrPictureProfile_Off) return "Off";
    if (raw >= SDK::CrPictureProfile_Number1 && raw <= SDK::CrPictureProfile_Number11) {
        return "PP" + std::to_string(raw - SDK::CrPictureProfile_Number1 + 1);
    }
    return "Off";
}

std::string whiteBalanceModeName(CrInt64u raw) {
    switch (raw) {
        case SDK::CrWhiteBalance_AWB: return "AWB";
        case SDK::CrWhiteBalance_Underwater_Auto: return "Underwater Auto";
        case SDK::CrWhiteBalance_Daylight: return "Daylight";
        case SDK::CrWhiteBalance_Shadow: return "Shade";
        case SDK::CrWhiteBalance_Cloudy: return "Cloudy";
        case SDK::CrWhiteBalance_Tungsten: return "Tungsten";
        case SDK::CrWhiteBalance_Fluorescent: return "Fluorescent";
        case SDK::CrWhiteBalance_Fluorescent_WarmWhite: return "Fluorescent (Warm White)";
        case SDK::CrWhiteBalance_Fluorescent_CoolWhite: return "Fluorescent (Cool White)";
        case SDK::CrWhiteBalance_Fluorescent_DayWhite: return "Fluorescent (Day White)";
        case SDK::CrWhiteBalance_Fluorescent_Daylight: return "Fluorescent (Daylight)";
        case SDK::CrWhiteBalance_Flush: return "Flash";
        case SDK::CrWhiteBalance_ColorTemp: return "Color Temp/Filter";
        case SDK::CrWhiteBalance_Custom_1: return "Custom 1";
        case SDK::CrWhiteBalance_Custom_2: return "Custom 2";
        case SDK::CrWhiteBalance_Custom_3: return "Custom 3";
        case SDK::CrWhiteBalance_Custom: return "Custom";
        default: return "Unknown (" + std::to_string(raw) + ")";
    }
}

CrInt64u whiteBalanceModeRaw(const std::string& name) {
    if (name == "AWB") return SDK::CrWhiteBalance_AWB;
    if (name == "Daylight") return SDK::CrWhiteBalance_Daylight;
    if (name == "Shade") return SDK::CrWhiteBalance_Shadow;
    if (name == "Cloudy") return SDK::CrWhiteBalance_Cloudy;
    if (name == "Tungsten") return SDK::CrWhiteBalance_Tungsten;
    if (name == "Fluorescent") return SDK::CrWhiteBalance_Fluorescent;
    if (name == "Flash") return SDK::CrWhiteBalance_Flush;
    if (name == "ColorTemp") return SDK::CrWhiteBalance_ColorTemp;
    return SDK::CrWhiteBalance_AWB;
}

// ---------------------------------------------------------------------
// Tiny hand-rolled JSON helpers (fixed, known shape -- no dependency)
// ---------------------------------------------------------------------

std::string jsonEscape(const std::string& s) {
    std::string out;
    for (char c : s) {
        if (c == '"' || c == '\\') out += '\\';
        out += c;
    }
    return out;
}

// Extracts a numeric or string value for "key" from a flat JSON object.
// Returns false if the key isn't present. Minimal, not a general parser.
bool jsonFindNumber(const std::string& body, const std::string& key, double& out) {
    std::string needle = "\"" + key + "\"";
    size_t pos = body.find(needle);
    if (pos == std::string::npos) return false;
    pos = body.find(':', pos);
    if (pos == std::string::npos) return false;
    pos++;
    while (pos < body.size() && isspace((unsigned char)body[pos])) pos++;
    size_t end = pos;
    while (end < body.size() && (isdigit((unsigned char)body[end]) || body[end] == '-' || body[end] == '.')) end++;
    if (end == pos) return false;
    out = std::stod(body.substr(pos, end - pos));
    return true;
}

bool jsonFindString(const std::string& body, const std::string& key, std::string& out) {
    std::string needle = "\"" + key + "\"";
    size_t pos = body.find(needle);
    if (pos == std::string::npos) return false;
    pos = body.find(':', pos);
    if (pos == std::string::npos) return false;
    pos = body.find('"', pos);
    if (pos == std::string::npos) return false;
    pos++;
    size_t end = body.find('"', pos);
    if (end == std::string::npos) return false;
    out = body.substr(pos, end - pos);
    return true;
}

// ---------------------------------------------------------------------
// Camera operations
// ---------------------------------------------------------------------

std::string connectUsb() {
    std::lock_guard<std::mutex> lock(g_cameraMutex);
    ensureSdkInit();

    SDK::ICrEnumCameraObjectInfo* enumInfo = nullptr;
    SDK::CrError enumErr = SDK::EnumCameraObjects(&enumInfo, 3);
    CrInt32u count = enumInfo ? enumInfo->GetCount() : 0;
    if (enumErr || !enumInfo || count == 0) {
        if (enumInfo) enumInfo->Release();
        return "no camera found over USB";
    }

    const SDK::ICrCameraObjectInfo* info = enumInfo->GetCameraObjectInfo(0);
    g_modelName = info->GetModel() ? info->GetModel() : "Sony Camera";

    std::promise<void> eventPromise;
    std::future<void> eventFuture = eventPromise.get_future();
    EventPromiseGuard guard(&eventPromise);

    SDK::CrError connectErr = SDK::Connect(
        const_cast<SDK::ICrCameraObjectInfo*>(info), &g_callback, &g_deviceHandle,
        SDK::CrSdkControlMode_Remote, SDK::CrReconnecting_ON);
    enumInfo->Release();
    if (connectErr) {
        char buf[64]; snprintf(buf, sizeof(buf), "Connect() failed 0x%x", connectErr);
        return buf;
    }

    auto status = eventFuture.wait_for(std::chrono::seconds(15));
    if (status != std::future_status::ready) return "timed out waiting for connection";
    try { eventFuture.get(); } catch (...) { return "connection error"; }

    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    return "";
}

std::string connectNetwork(const std::string& ip, const std::string& mac, const std::string& userId, const std::string& password) {
    std::lock_guard<std::mutex> lock(g_cameraMutex);
    ensureSdkInit();

    std::vector<int> octets;
    size_t pos = 0, next;
    while ((next = ip.find('.', pos)) != std::string::npos) {
        octets.push_back(std::stoi(ip.substr(pos, next - pos)));
        pos = next + 1;
    }
    octets.push_back(std::stoi(ip.substr(pos)));
    if (octets.size() != 4) return "invalid IP address";
    CrInt32u packedIP = 0;
    for (int i = 3; i >= 0; i--) packedIP = (packedIP << 8) | (CrInt32u)(octets[i] & 0xFF);

    CrInt8u macBytes[6] = {0};
    unsigned int b[6];
    if (sscanf(mac.c_str(), "%x:%x:%x:%x:%x:%x", &b[0], &b[1], &b[2], &b[3], &b[4], &b[5]) != 6) {
        return "invalid MAC address";
    }
    for (int i = 0; i < 6; i++) macBytes[i] = (CrInt8u)b[i];

    fprintf(stderr, "[connectNetwork] ip=%s packedIP=0x%x mac=%s macBytes=%02x:%02x:%02x:%02x:%02x:%02x userId=%s\n",
        ip.c_str(), packedIP, mac.c_str(),
        macBytes[0], macBytes[1], macBytes[2], macBytes[3], macBytes[4], macBytes[5], userId.c_str());
    fflush(stderr);

    SDK::ICrCameraObjectInfo* cameraInfo = nullptr;
    SDK::CrError createErr = SDK::CreateCameraObjectInfoEthernetConnection(
        &cameraInfo, SDK::CrCameraDeviceModel_ILCE_7RM5, packedIP, macBytes, 1);
    if (createErr || !cameraInfo) {
        char buf[96]; snprintf(buf, sizeof(buf), "CreateCameraObjectInfoEthernetConnection failed 0x%x", createErr);
        return buf;
    }

    char fpBuf[512] = {0};
    CrInt32u fpSize = sizeof(fpBuf);
    SDK::CrError fpErr = SDK::GetFingerprint(cameraInfo, fpBuf, &fpSize);
    if (fpErr) {
        cameraInfo->Release();
        char buf[96]; snprintf(buf, sizeof(buf), "GetFingerprint failed 0x%x (is USB still plugged in?)", fpErr);
        return buf;
    }

    g_modelName = "a7R V (network)";

    std::promise<void> eventPromise;
    std::future<void> eventFuture = eventPromise.get_future();
    EventPromiseGuard guard(&eventPromise);

    SDK::CrError connectErr = SDK::Connect(
        cameraInfo, &g_callback, &g_deviceHandle,
        SDK::CrSdkControlMode_Remote, SDK::CrReconnecting_ON,
        userId.c_str(), password.c_str(), fpBuf, fpSize);
    cameraInfo->Release();
    if (connectErr) {
        char buf[64]; snprintf(buf, sizeof(buf), "Connect() failed 0x%x", connectErr);
        return buf;
    }

    auto status = eventFuture.wait_for(std::chrono::seconds(20));
    if (status != std::future_status::ready) return "timed out waiting for connection";
    try { eventFuture.get(); } catch (...) { return "connection error"; }

    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    return "";
}

std::string readRecipeJson() {
    std::lock_guard<std::mutex> lock(g_cameraMutex);
    if (!g_connected) return "{\"error\":\"not connected\"}";

    std::vector<CrInt32u> codes = {
        SDK::CrDeviceProperty_CreativeLook,
        SDK::CrDeviceProperty_PictureProfile,
        SDK::CrDeviceProperty_WhiteBalance,
        SDK::CrDeviceProperty_WhiteBalanceTint,
        SDK::CrDeviceProperty_WhiteBalancePresetColorTemperature,
        SDK::CrDeviceProperty_WhiteBalanceRGain,
        SDK::CrDeviceProperty_WhiteBalanceBGain,
        SDK::CrDeviceProperty_IsoSensitivity,
    };
    for (auto& f : creativeLookFields()) codes.push_back(f.code);

    SDK::CrDeviceProperty* props = nullptr;
    CrInt32 numProps = 0;
    SDK::CrError err = SDK::GetSelectDeviceProperties(g_deviceHandle, (CrInt32u)codes.size(), codes.data(), &props, &numProps);
    if (err) {
        char buf[96]; snprintf(buf, sizeof(buf), "{\"error\":\"read failed 0x%x\"}", err);
        return buf;
    }

    std::ostringstream json;
    json << "{";
    for (CrInt32 i = 0; i < numProps; i++) {
        SDK::CrDeviceProperty& p = props[i];
        CrInt32u code = p.GetCode();
        if (i > 0) json << ",";
        if (code == SDK::CrDeviceProperty_CreativeLook) {
            json << "\"preset\":\"" << presetName(p.GetCurrentValue()) << "\"";
        } else if (code == SDK::CrDeviceProperty_PictureProfile) {
            json << "\"pictureProfileSlot\":\"" << pictureProfileSlotName(p.GetCurrentValue()) << "\"";
        } else if (code == SDK::CrDeviceProperty_WhiteBalance) {
            json << "\"whiteBalanceMode\":\"" << whiteBalanceModeName(p.GetCurrentValue()) << "\"";
        } else if (code == SDK::CrDeviceProperty_WhiteBalanceTint) {
            json << "\"whiteBalanceTint\":" << decodeSigned(p);
        } else if (code == SDK::CrDeviceProperty_WhiteBalancePresetColorTemperature) {
            json << "\"whiteBalanceColorTempK\":" << decodeSigned(p);
        } else if (code == SDK::CrDeviceProperty_WhiteBalanceRGain) {
            json << "\"whiteBalanceRGain\":" << (decodeSigned(p) / 10.0);
        } else if (code == SDK::CrDeviceProperty_WhiteBalanceBGain) {
            json << "\"whiteBalanceBGain\":" << (decodeSigned(p) / 10.0);
        } else if (code == SDK::CrDeviceProperty_IsoSensitivity) {
            CrInt64u raw = p.GetCurrentValue();
            CrInt64u isoValue = raw & 0xFFFFFF;
            if (isoValue == SDK::CrISO_AUTO) json << "\"iso\":\"Auto\"";
            else json << "\"iso\":" << isoValue;
        } else {
            for (auto& f : creativeLookFields()) {
                if (f.code == code) { json << "\"" << f.key << "\":" << decodeSigned(p); break; }
            }
        }
    }
    json << "}";
    SDK::ReleaseDeviceProperties(g_deviceHandle, props);
    return json.str();
}

std::string writeRecipeJson(const std::string& body) {
    std::lock_guard<std::mutex> lock(g_cameraMutex);
    if (!g_connected) return "not connected";

    std::string presetStr;
    if (jsonFindString(body, "preset", presetStr)) {
        SDK::CrDeviceProperty prop;
        prop.SetCode(SDK::CrDeviceProperty_CreativeLook);
        prop.SetValueType(SDK::CrDataType_UInt16);
        prop.SetCurrentValue(presetRaw(presetStr));
        SDK::CrError err = SDK::SetDeviceProperty(g_deviceHandle, &prop);
        if (err) { char buf[64]; snprintf(buf, sizeof(buf), "failed to set preset 0x%x", err); return buf; }
        // The camera needs a brief moment to finish switching Creative Look
        // presets before its sub-parameters (contrast, saturation, etc.)
        // become settable again -- setting them immediately after can fail
        // with CrError_Api_InvalidCalled (0x8402).
        std::this_thread::sleep_for(std::chrono::milliseconds(400));
    }

    for (auto& f : creativeLookFields()) {
        double value;
        if (!jsonFindNumber(body, f.key, value)) continue;
        int clamped = std::max(f.minValue, std::min(f.maxValue, (int)std::lround(value)));
        SDK::CrDeviceProperty prop;
        prop.SetCode(f.code);
        prop.SetValueType(f.setType);
        prop.SetCurrentValue((CrInt64u)(int64_t)clamped);
        SDK::CrError err = SDK::SetDeviceProperty(g_deviceHandle, &prop);
        if (err) { char buf[64]; snprintf(buf, sizeof(buf), "failed to set %s 0x%x", f.key.c_str(), err); return buf; }
    }

    std::string wbModeStr;
    if (jsonFindString(body, "whiteBalanceMode", wbModeStr)) {
        SDK::CrDeviceProperty prop;
        prop.SetCode(SDK::CrDeviceProperty_WhiteBalance);
        prop.SetValueType(SDK::CrDataType_UInt16);
        prop.SetCurrentValue(whiteBalanceModeRaw(wbModeStr));
        SDK::CrError err = SDK::SetDeviceProperty(g_deviceHandle, &prop);
        if (err) { char buf[64]; snprintf(buf, sizeof(buf), "failed to set white balance mode 0x%x", err); return buf; }
        // Same settle requirement as the preset switch above -- WhiteBalancePresetColorTemperature
        // (and other WB sub-params) aren't reliably settable immediately after switching modes.
        // Switching INTO ColorTemp mode specifically seems to need longer than other WB mode
        // changes (staying within Daylight/Shade/etc settles faster).
        std::this_thread::sleep_for(std::chrono::milliseconds(1200));
    }

    double colorTempK;
    if (jsonFindNumber(body, "whiteBalanceColorTempK", colorTempK)) {
        SDK::CrDeviceProperty prop;
        prop.SetCode(SDK::CrDeviceProperty_WhiteBalancePresetColorTemperature);
        prop.SetValueType(SDK::CrDataType_UInt16);
        prop.SetCurrentValue((CrInt64u)(int64_t)colorTempK);
        SDK::CrError err = SDK::SetDeviceProperty(g_deviceHandle, &prop);
        // Non-fatal: manual Kelvin color temp isn't reliably settable over
        // this connection path on this camera (unresolved -- see project
        // memory). Log it, but don't abort the rest of the recipe write
        // (e.g. ISO, which comes after this in write order).
        if (err) fprintf(stderr, "[writeRecipeJson] color temp not applied (0x%x)\n", err);
    }

    // WB Tint/R-Gain/B-Gain: non-fatal, same reasoning as color temp above --
    // these can fail with CrError_Api_InvalidCalled (0x8402) right after a
    // WB mode switch even after the mode change itself is confirmed applied.
    // Log and continue rather than aborting fields that come after them
    // (e.g. ISO) in write order.
    double tint;
    if (jsonFindNumber(body, "whiteBalanceTint", tint)) {
        SDK::CrDeviceProperty prop;
        prop.SetCode(SDK::CrDeviceProperty_WhiteBalanceTint);
        prop.SetValueType(SDK::CrDataType_Int8);
        prop.SetCurrentValue((CrInt64u)(int64_t)tint);
        SDK::CrError err = SDK::SetDeviceProperty(g_deviceHandle, &prop);
        if (err) fprintf(stderr, "[writeRecipeJson] WB tint(A/B) not applied (0x%x)\n", err);
    }

    double rGain;
    if (jsonFindNumber(body, "whiteBalanceRGain", rGain)) {
        SDK::CrDeviceProperty prop;
        prop.SetCode(SDK::CrDeviceProperty_WhiteBalanceRGain);
        prop.SetValueType(SDK::CrDataType_Int16);
        prop.SetCurrentValue((CrInt64u)(int64_t)std::lround(rGain * 10));
        SDK::CrError err = SDK::SetDeviceProperty(g_deviceHandle, &prop);
        if (err) fprintf(stderr, "[writeRecipeJson] WB R-Gain not applied (0x%x)\n", err);
    }

    double bGain;
    if (jsonFindNumber(body, "whiteBalanceBGain", bGain)) {
        SDK::CrDeviceProperty prop;
        prop.SetCode(SDK::CrDeviceProperty_WhiteBalanceBGain);
        prop.SetValueType(SDK::CrDataType_Int16);
        prop.SetCurrentValue((CrInt64u)(int64_t)std::lround(bGain * 10));
        SDK::CrError err = SDK::SetDeviceProperty(g_deviceHandle, &prop);
        if (err) fprintf(stderr, "[writeRecipeJson] WB B-Gain not applied (0x%x)\n", err);
    }

    std::string isoStr;
    double isoValue;
    bool haveIso = false;
    CrInt64u isoRawValue = 0;
    if (jsonFindString(body, "iso", isoStr) && isoStr == "Auto") {
        isoRawValue = SDK::CrISO_AUTO;
        haveIso = true;
    } else if (jsonFindNumber(body, "iso", isoValue)) {
        isoRawValue = (CrInt64u)(int64_t)isoValue & 0xFFFFFF;
        haveIso = true;
    }
    if (haveIso) {
        SDK::CrDeviceProperty prop;
        prop.SetCode(SDK::CrDeviceProperty_IsoSensitivity);
        prop.SetValueType(SDK::CrDataType_UInt32);
        CrInt64u raw = ((CrInt64u)SDK::CrISO_Normal << 24) | isoRawValue;
        prop.SetCurrentValue(raw);
        SDK::CrError err = SDK::SetDeviceProperty(g_deviceHandle, &prop);
        if (err) { char buf[64]; snprintf(buf, sizeof(buf), "failed to set ISO 0x%x", err); return buf; }
    }

    return "";
}

// ---------------------------------------------------------------------
// Panasonic cam.cgi client -- unlike the Sony CrSDK path, this is plain
// unencrypted HTTP/CGI running on the camera's own IP (reverse-engineered
// protocol, no official SDK exists for Linux). No persistent session or
// handshake is needed for the calls used here -- each request is
// independent, so "connect" just means "confirm the camera answers".
// ---------------------------------------------------------------------

std::mutex g_panasonicMutex;
bool g_panasonicConnected = false;
std::string g_panasonicIp;
std::string g_panasonicModel;

// Extracts the value of a single XML tag, e.g. <modelname>G9</modelname>.
std::string xmlFindTag(const std::string& xml, const std::string& tag) {
    std::string open = "<" + tag + ">";
    std::string close = "</" + tag + ">";
    size_t pos = xml.find(open);
    if (pos == std::string::npos) return "";
    pos += open.size();
    size_t end = xml.find(close, pos);
    if (end == std::string::npos) return "";
    return xml.substr(pos, end - pos);
}

// Extracts the live value= of a menu item from a curmenu/allmenu dump,
// e.g. id="menu_item_id_ph_sty" ... value="standard".
std::string curmenuFindValue(const std::string& xml, const std::string& itemId) {
    std::string needle = "id=\"" + itemId + "\"";
    size_t pos = xml.find(needle);
    if (pos == std::string::npos) return "";
    size_t lineEnd = xml.find('>', pos);
    if (lineEnd == std::string::npos) return "";
    std::string segment = xml.substr(pos, lineEnd - pos);
    size_t vpos = segment.find("value=\"");
    if (vpos == std::string::npos) return "";
    vpos += 7;
    size_t vend = segment.find('"', vpos);
    if (vend == std::string::npos) return "";
    return segment.substr(vpos, vend - vpos);
}

// GET http://<camIp>/cam.cgi?<query>, returns the response body (empty on
// any failure -- the camera goes idle/unreachable often, this is normal).
std::string panasonicGet(const std::string& camIp, const std::string& query) {
    httplib::Client client(camIp, 80);
    client.set_connection_timeout(3, 0);
    client.set_read_timeout(5, 0);
    auto res = client.Get("/cam.cgi?" + query);
    if (!res || res->status != 200) return "";
    return res->body;
}

std::string panasonicConnect(const std::string& ip) {
    std::lock_guard<std::mutex> lock(g_panasonicMutex);
    std::string body = panasonicGet(ip, "mode=getinfo&type=capability");
    if (body.empty()) return "camera did not respond at " + ip;
    std::string model = xmlFindTag(body, "modelname");
    g_panasonicConnected = true;
    g_panasonicIp = ip;
    g_panasonicModel = model.empty() ? "Panasonic Camera" : ("Panasonic " + model);
    return "";
}

std::string panasonicReadRecipeJson() {
    std::string ip;
    {
        std::lock_guard<std::mutex> lock(g_panasonicMutex);
        if (!g_panasonicConnected) return "{\"error\":\"not connected\"}";
        ip = g_panasonicIp;
    }
    std::string xml = panasonicGet(ip, "mode=getinfo&type=curmenu");
    if (xml.empty()) return "{\"error\":\"camera did not respond\"}";

    std::string photoStyle = curmenuFindValue(xml, "menu_item_id_ph_sty");
    std::string whiteBalance = curmenuFindValue(xml, "menu_item_id_whitebalance");
    std::string iso = curmenuFindValue(xml, "menu_item_id_sensitivity");
    std::string filterEffect = curmenuFindValue(xml, "menu_item_id_filter_set");

    std::ostringstream json;
    json << "{";
    json << "\"photoStyle\":\"" << jsonEscape(photoStyle) << "\",";
    json << "\"whiteBalance\":\"" << jsonEscape(whiteBalance) << "\",";
    json << "\"iso\":\"" << jsonEscape(iso) << "\",";
    json << "\"filterEffect\":\"" << jsonEscape(filterEffect) << "\"";
    json << "}";
    return json.str();
}

std::string panasonicWriteRecipeJson(const std::string& body) {
    std::string ip;
    {
        std::lock_guard<std::mutex> lock(g_panasonicMutex);
        if (!g_panasonicConnected) return "not connected";
        ip = g_panasonicIp;
    }

    // cmd_type -> JSON key, straight from allmenu.xml's own command shapes.
    static const std::vector<std::pair<std::string, std::string>> fields = {
        {"photoStyle", "colormode"},
        {"whiteBalance", "whitebalance"},
        {"iso", "iso"},
        {"filterEffect", "filter_setting"},
    };
    for (auto& [jsonKey, cmdType] : fields) {
        std::string value;
        if (!jsonFindString(body, jsonKey, value)) continue;
        std::string reply = panasonicGet(ip, "mode=setsetting&type=" + cmdType + "&value=" + value);
        if (reply.find("<result>ok</result>") == std::string::npos) {
            return "failed to set " + jsonKey + " (camera replied: " + reply + ")";
        }
    }
    return "";
}

// ---------------------------------------------------------------------
// HTTP server
// ---------------------------------------------------------------------

int main() {
    ensureSdkInit(); // must happen on the main thread before any request threads spin up

    httplib::Server svr;

    svr.set_mount_point("/", "./public");

    svr.Get("/api/status", [](const httplib::Request&, httplib::Response& res) {
        std::lock_guard<std::mutex> lock(g_cameraMutex);
        std::ostringstream json;
        json << "{\"connected\":" << (g_connected ? "true" : "false")
             << ",\"model\":\"" << jsonEscape(g_modelName) << "\"}";
        res.set_content(json.str(), "application/json");
    });

    svr.Post("/api/connect/usb", [](const httplib::Request&, httplib::Response& res) {
        std::string error = connectUsb();
        std::ostringstream json;
        json << "{\"success\":" << (error.empty() ? "true" : "false")
             << ",\"error\":\"" << jsonEscape(error) << "\"}";
        res.set_content(json.str(), "application/json");
    });

    svr.Post("/api/connect/network", [](const httplib::Request& req, httplib::Response& res) {
        std::string ip, mac, userId, password;
        jsonFindString(req.body, "ip", ip);
        jsonFindString(req.body, "mac", mac);
        jsonFindString(req.body, "userId", userId);
        jsonFindString(req.body, "password", password);
        std::string error = connectNetwork(ip, mac, userId, password);
        std::ostringstream json;
        json << "{\"success\":" << (error.empty() ? "true" : "false")
             << ",\"error\":\"" << jsonEscape(error) << "\"}";
        res.set_content(json.str(), "application/json");
    });

    svr.Get("/api/presets", [](const httplib::Request&, httplib::Response& res) {
        res.set_content(kPresetsJson, "application/json");
    });

    svr.Post("/api/disconnect", [](const httplib::Request&, httplib::Response& res) {
        std::lock_guard<std::mutex> lock(g_cameraMutex);
        if (g_deviceHandle) {
            SDK::Disconnect(g_deviceHandle);
            SDK::ReleaseDevice(g_deviceHandle);
            g_deviceHandle = 0;
        }
        g_connected = false;
        res.set_content("{\"success\":true}", "application/json");
    });

    svr.Get("/api/recipe", [](const httplib::Request&, httplib::Response& res) {
        res.set_content(readRecipeJson(), "application/json");
    });

    svr.Post("/api/recipe", [](const httplib::Request& req, httplib::Response& res) {
        std::string error = writeRecipeJson(req.body);
        std::ostringstream json;
        json << "{\"success\":" << (error.empty() ? "true" : "false")
             << ",\"error\":\"" << jsonEscape(error) << "\"}";
        res.set_content(json.str(), "application/json");
    });

    svr.Get("/api/panasonic/status", [](const httplib::Request&, httplib::Response& res) {
        std::lock_guard<std::mutex> lock(g_panasonicMutex);
        std::ostringstream json;
        json << "{\"connected\":" << (g_panasonicConnected ? "true" : "false")
             << ",\"model\":\"" << jsonEscape(g_panasonicModel) << "\""
             << ",\"ip\":\"" << jsonEscape(g_panasonicIp) << "\"}";
        res.set_content(json.str(), "application/json");
    });

    svr.Post("/api/panasonic/connect", [](const httplib::Request& req, httplib::Response& res) {
        std::string ip;
        jsonFindString(req.body, "ip", ip);
        std::string error = panasonicConnect(ip);
        std::ostringstream json;
        json << "{\"success\":" << (error.empty() ? "true" : "false")
             << ",\"error\":\"" << jsonEscape(error) << "\"}";
        res.set_content(json.str(), "application/json");
    });

    svr.Post("/api/panasonic/disconnect", [](const httplib::Request&, httplib::Response& res) {
        std::lock_guard<std::mutex> lock(g_panasonicMutex);
        g_panasonicConnected = false;
        res.set_content("{\"success\":true}", "application/json");
    });

    svr.Get("/api/panasonic/presets", [](const httplib::Request&, httplib::Response& res) {
        res.set_content(kPanasonicPresetsJson, "application/json");
    });

    svr.Get("/api/panasonic/recipe", [](const httplib::Request&, httplib::Response& res) {
        res.set_content(panasonicReadRecipeJson(), "application/json");
    });

    svr.Post("/api/panasonic/recipe", [](const httplib::Request& req, httplib::Response& res) {
        std::string error = panasonicWriteRecipeJson(req.body);
        std::ostringstream json;
        json << "{\"success\":" << (error.empty() ? "true" : "false")
             << ",\"error\":\"" << jsonEscape(error) << "\"}";
        res.set_content(json.str(), "application/json");
    });

    svr.Post("/api/system/shutdown", [](const httplib::Request&, httplib::Response& res) {
        res.set_content("{\"success\":true}", "application/json");
        // Give httplib a moment to actually flush this response to the
        // client before the OS starts going down -- shutdown -h now takes
        // a few seconds anyway, but detaching means we don't race it.
        std::thread([]() {
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
            system("sudo shutdown -h now");
        }).detach();
    });

    std::cout << "SonyConfig web app listening on http://0.0.0.0:8080\n";
    svr.listen("0.0.0.0", 8080);
    return 0;
}
