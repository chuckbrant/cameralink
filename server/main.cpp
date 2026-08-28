// SonyConfig web app -- minimal REST backend over CrSDK, single binary,
// serves the static frontend from ./public.
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <map>
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
#include "CrControlCode.h"
#include "CrDebugString.h"
#include "httplib.h"

namespace SDK = SCRSDK;

// ---------------------------------------------------------------------
// Built-in film recipe presets, transcribed from film_recipe_charts.pdf
// (page 3, "Film Recipe Chart -- Sony a7R V"). Every field here is
// confirmed writable and does get applied (Creative Look, WB mode/Kelvin,
// Color Filter A-B/G-M via colorFilterAB/colorFilterGM, ISO). "Base ISO"
// is shown as an informational note only (the film stock's real-world
// ISO rating, not a camera property).
//
// Other approximated preset packs (Fujifilm Simulation, JP/PS Presets,
// Micro Four Nerds -- all ported from a companion app's Lightroom/
// CoreImage grades) were removed: they were never going to reproduce a
// Lightroom edit through Sony's 8 Creative Look knobs convincingly, and
// were replaced by a user-managed Saved Recipes library instead (see
// GET/POST /api/recipes/*).
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
    "colorFilterAB": 1, "colorFilterGM": 1,
    "iso": 400,
    "baseISO": "400",
    "whiteBalanceNote": "Color Filter A+1 G+1",
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
    "colorFilterAB": 1,
    "iso": 100,
    "baseISO": "100",
    "whiteBalanceNote": "Color Filter A+1",
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
    "colorFilterAB": 3, "colorFilterGM": 1,
    "iso": 200,
    "baseISO": "200",
    "whiteBalanceNote": "Color Filter A+3 G+1",
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
    "colorFilterAB": 2, "colorFilterGM": 2,
    "iso": 400,
    "baseISO": "400 (rate 200)",
    "whiteBalanceNote": "Daylight/AWB, Color Filter A+2 G+2",
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
    "colorFilterAB": 1,
    "iso": 400,
    "baseISO": "400",
    "whiteBalanceNote": "Color Filter A+1",
    "notes": "Fujifilm's own bulletin emphasizes vibrant reds/blues/yellows and smooth skin tones, so only a mild warm push is used."
  },

  {
    "group": "Film Recipe Chart",
    "id": "filmsim-vision3-500t",
    "name": "Vision3 500T",
    "preset": "CS1",
    "contrast": -3, "highlights": -3, "shadows": 2, "fade": 3, "saturation": -2,
    "whiteBalanceMode": "ColorTemp", "whiteBalanceColorTempK": 5900,
    "notes": "Approximated from the iPadPhotoFilters CoreImage grade (tungsten cinema stock, desaturated, lifted blacks). Pushes to Custom Look CS1."
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

// CrDataType_STR properties (ModelName, BodySerialNumber, LensModelName,
// LensVersionNumber) don't use GetCurrentValue() at all -- the string
// lives in GetCurrentStr(), a null-terminated CrInt16u* (UTF-16). Values
// seen so far (model names, serial numbers) are plain ASCII, so a
// truncating per-unit cast is enough; this doesn't handle non-ASCII text.
// The very first CrInt16u unit is a length prefix (string length including
// the null terminator), not a character -- confirmed empirically: reading
// this naively produced a stray leading control character whose value
// exactly matched (real string length + 1) for both ModelName ("\n" =
// 0x0A = len("ILCE-7RM5")+1) and BodySerialNumber ("\t" = 0x09 =
// len("01384122")+1) on the real camera. Skip index 0.
std::string decodeCrStr(const SDK::CrDeviceProperty& p) {
    CrInt16u* wstr = p.GetCurrentStr();
    if (!wstr || wstr[0] == 0) return "";
    std::string result;
    for (CrInt16u* c = wstr + 1; *c != 0; c++) result += (char)(*c & 0xFF);
    return result;
}

// CrBatteryLevel is a bitfield-ish enum: a coarse level (PreEnd/1_4../3_3)
// OR'd against a USB-power-supply offset (0x10000), plus two sentinel
// values (Fake, BatteryNotInstalled). BatteryRemain (%) is the precise
// number; this is just the coarse icon-level indicator.
std::string batteryLevelName(CrInt64u raw) {
    switch (raw) {
        case SDK::CrBatteryLevel_PreEndBattery: return "Nearly Empty";
        case SDK::CrBatteryLevel_1_4: return "1/4";
        case SDK::CrBatteryLevel_2_4: return "2/4";
        case SDK::CrBatteryLevel_3_4: return "3/4";
        case SDK::CrBatteryLevel_4_4: return "4/4";
        case SDK::CrBatteryLevel_1_3: return "1/3";
        case SDK::CrBatteryLevel_2_3: return "2/3";
        case SDK::CrBatteryLevel_3_3: return "3/3";
        case SDK::CrBatteryLevel_USBPowerSupply: return "USB Power";
        case SDK::CrBatteryLevel_PreEnd_PowerSupply: return "Nearly Empty (USB Power)";
        case SDK::CrBatteryLevel_1_4_PowerSupply: return "1/4 (USB Power)";
        case SDK::CrBatteryLevel_2_4_PowerSupply: return "2/4 (USB Power)";
        case SDK::CrBatteryLevel_3_4_PowerSupply: return "3/4 (USB Power)";
        case SDK::CrBatteryLevel_4_4_PowerSupply: return "4/4 (USB Power)";
        case SDK::CrBatteryLevel_Fake: return "N/A";
        case SDK::CrBatteryLevel_BatteryNotInstalled: return "No Battery";
        default: return "Unknown";
    }
}

// ColorTuningAB/ColorTuningGM calibration -- confirmed empirically against
// the real a7R V (192=neutral, 220=A/G+7, 164=B/M-7), and matches Sony's
// own documented raw range exactly: 0x9C(156,"B9")..0xE4(228,"A9"/"G9"),
// center 192, 4 raw units per on-screen step. Positive = A (amber) / G
// (green); negative = B (blue) / M (magenta).
constexpr int kColorTuningCenter = 192;
constexpr int kColorTuningStep = 4;
constexpr int kColorTuningMinRaw = 156;
constexpr int kColorTuningMaxRaw = 228;

int colorTuningToOnscreen(CrInt64u raw) {
    return ((int)raw - kColorTuningCenter) / kColorTuningStep;
}

CrInt64u colorTuningFromOnscreen(int onscreen) {
    int raw = kColorTuningCenter + onscreen * kColorTuningStep;
    raw = std::max(kColorTuningMinRaw, std::min(kColorTuningMaxRaw, raw));
    return (CrInt64u)raw;
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
        case SDK::CrWhiteBalance_ColorTemp: return "Color Temp";
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

std::string aspectRatioName(CrInt64u raw) {
    switch (raw) {
        case SDK::CrAspectRatio_3_2: return "3:2";
        case SDK::CrAspectRatio_16_9: return "16:9";
        case SDK::CrAspectRatio_4_3: return "4:3";
        case SDK::CrAspectRatio_1_1: return "1:1";
        default: return "3:2";
    }
}
CrInt64u aspectRatioRaw(const std::string& name) {
    if (name == "3:2") return SDK::CrAspectRatio_3_2;
    if (name == "16:9") return SDK::CrAspectRatio_16_9;
    if (name == "4:3") return SDK::CrAspectRatio_4_3;
    if (name == "1:1") return SDK::CrAspectRatio_1_1;
    return SDK::CrAspectRatio_3_2;
}

// Only the three most common file types are offered here (RawHeif/Heif
// omitted). All three confirmed working live -- see writeRecipeJson.
std::string fileTypeName(CrInt64u raw) {
    switch (raw) {
        case SDK::CrFileType_Raw: return "RAW";
        case SDK::CrFileType_RawJpeg: return "RAW+JPEG";
        case SDK::CrFileType_Jpeg: return "JPEG";
        default: return "RAW";
    }
}
CrInt64u fileTypeRaw(const std::string& name) {
    if (name == "RAW") return SDK::CrFileType_Raw;
    if (name == "RAW+JPEG") return SDK::CrFileType_RawJpeg;
    if (name == "JPEG") return SDK::CrFileType_Jpeg;
    return SDK::CrFileType_Raw;
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

// Minimal parse/serialize for saved_cameras.json -- a flat array of
// {name, ip, mac, userId, password} objects. Brace-matches each object,
// then reuses jsonFindString per known key -- avoids needing a real JSON
// library for a file this simple.
std::vector<std::map<std::string, std::string>> parseSavedCameras(const std::string& content) {
    std::vector<std::map<std::string, std::string>> result;
    size_t pos = 0;
    while (true) {
        size_t start = content.find('{', pos);
        if (start == std::string::npos) break;
        int depth = 1;
        size_t i = start + 1;
        while (i < content.size() && depth > 0) {
            if (content[i] == '{') depth++;
            else if (content[i] == '}') depth--;
            i++;
        }
        std::string obj = content.substr(start, i - start);
        std::map<std::string, std::string> entry;
        std::string val;
        for (const char* key : {"name", "ip", "mac", "userId", "password"}) {
            if (jsonFindString(obj, key, val)) entry[key] = val;
        }
        if (!entry.empty()) result.push_back(entry);
        pos = i;
    }
    return result;
}

std::string serializeSavedCameras(const std::vector<std::map<std::string, std::string>>& cams) {
    std::ostringstream json;
    json << "[\n";
    for (size_t idx = 0; idx < cams.size(); idx++) {
        const auto& c = cams[idx];
        json << "  {\n";
        json << "    \"name\": \"" << jsonEscape(c.count("name") ? c.at("name") : "") << "\",\n";
        json << "    \"ip\": \"" << jsonEscape(c.count("ip") ? c.at("ip") : "") << "\",\n";
        json << "    \"mac\": \"" << jsonEscape(c.count("mac") ? c.at("mac") : "") << "\",\n";
        json << "    \"userId\": \"" << jsonEscape(c.count("userId") ? c.at("userId") : "") << "\",\n";
        json << "    \"password\": \"" << jsonEscape(c.count("password") ? c.at("password") : "") << "\"\n";
        json << "  }" << (idx + 1 < cams.size() ? "," : "") << "\n";
    }
    json << "]\n";
    return json.str();
}

// ---------------------------------------------------------------------
// Saved Recipes -- a 10-slot, user-managed library (separate from the
// built-in film-chart presets above). Recipe payloads are kept as
// opaque JSON blobs rather than parsed field-by-field: the frontend
// already knows the exact shape of a "recipe" (whatever the Custom tab
// currently supports -- Creative Look fields, WB, ISO, DRO, etc.), so
// the backend just stores/returns that JSON verbatim under a slot
// number and a name, doing text-level surgery (brace-matching) for
// create/rename/delete rather than needing a real JSON parser.
// ---------------------------------------------------------------------

// Returns the raw substring of a JSON object value for `key` (from its
// opening '{' through its matching closing '}'), or "" if not found.
std::string jsonFindRawObject(const std::string& body, const std::string& key) {
    std::string needle = "\"" + key + "\"";
    size_t pos = body.find(needle);
    if (pos == std::string::npos) return "";
    pos = body.find(':', pos);
    if (pos == std::string::npos) return "";
    pos = body.find('{', pos);
    if (pos == std::string::npos) return "";
    size_t start = pos;
    int depth = 1;
    size_t i = pos + 1;
    while (i < body.size() && depth > 0) {
        if (body[i] == '{') depth++;
        else if (body[i] == '}') depth--;
        i++;
    }
    return body.substr(start, i - start);
}

// Spans of each top-level {...} object within a JSON array's text.
std::vector<std::pair<size_t, size_t>> findTopLevelObjectSpans(const std::string& content) {
    std::vector<std::pair<size_t, size_t>> spans;
    size_t pos = 0;
    while (true) {
        size_t start = content.find('{', pos);
        if (start == std::string::npos) break;
        int depth = 1;
        size_t i = start + 1;
        while (i < content.size() && depth > 0) {
            if (content[i] == '{') depth++;
            else if (content[i] == '}') depth--;
            i++;
        }
        spans.push_back({start, i});
        pos = i;
    }
    return spans;
}

std::string readJsonArrayFile(const std::string& path) {
    std::ifstream f(path);
    if (!f) return "[]";
    std::ostringstream ss;
    ss << f.rdbuf();
    std::string content = ss.str();
    return content.empty() ? "[]" : content;
}

void writeJsonArrayFile(const std::string& path, const std::string& content) {
    std::ofstream f(path);
    f << content;
}

int extractSlotNumber(const std::string& obj) {
    double v;
    if (jsonFindNumber(obj, "slot", v)) return (int)v;
    return -1;
}

// Removes the top-level object (plus one adjacent comma, so the array
// stays valid) whose "slot" matches, if present. Returns true if found.
bool removeSavedRecipeSlot(std::string& content, int targetSlot) {
    auto spans = findTopLevelObjectSpans(content);
    for (auto it = spans.rbegin(); it != spans.rend(); ++it) {
        std::string objStr = content.substr(it->first, it->second - it->first);
        if (extractSlotNumber(objStr) == targetSlot) {
            size_t eraseStart = it->first;
            size_t eraseEnd = it->second;
            if (eraseEnd < content.size() && content[eraseEnd] == ',') {
                eraseEnd++;
            } else if (eraseStart > 0) {
                size_t back = eraseStart;
                while (back > 0 && isspace((unsigned char)content[back - 1])) back--;
                if (back > 0 && content[back - 1] == ',') eraseStart = back - 1;
            }
            content.erase(eraseStart, eraseEnd - eraseStart);
            return true;
        }
    }
    return false;
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
        // 0x8218 = CrError_Connect_SSH_GetFingerprintFailed. Despite an
        // earlier guess baked into this message, this has nothing to do
        // with USB -- it means the camera's SSH host key fingerprint
        // couldn't be retrieved, most often because Remote Shoot Function
        // was toggled off/on since the last pairing (which regenerates the
        // camera's User/Password/Fingerprint -- see docs/CAMERA_SETUP.md)
        // and the credentials being used here are now stale.
        char buf[160]; snprintf(buf, sizeof(buf), "GetFingerprint failed 0x%x (stale pairing? re-check the camera's Access Authen. Info screen -- credentials regenerate when Remote Shoot Function is toggled off/on)", fpErr);
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
        SDK::CrDeviceProperty_ColorTuningAB,
        SDK::CrDeviceProperty_ColorTuningGM,
        SDK::CrDeviceProperty_IsoSensitivity,
        SDK::CrDeviceProperty_AspectRatio,
        SDK::CrDeviceProperty_FileType,
        SDK::CrDeviceProperty_Colortemp,
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
        } else if (code == SDK::CrDeviceProperty_ColorTuningAB) {
            // Raw is CrDataType_UInt8Range, 0x9C(156,"B9")..0xE4(228,"A9"),
            // center 192, 4 raw units per on-screen step -- confirmed
            // empirically against the real camera (192=0, 220=A/G+7,
            // 164=B/M-7) after WhiteBalanceTint/RGain/BGain turned out to
            // not exist on this camera at all (see colorTuningToOnscreen).
            json << "\"colorFilterAB\":" << colorTuningToOnscreen(p.GetCurrentValue());
        } else if (code == SDK::CrDeviceProperty_ColorTuningGM) {
            json << "\"colorFilterGM\":" << colorTuningToOnscreen(p.GetCurrentValue());
        } else if (code == SDK::CrDeviceProperty_IsoSensitivity) {
            CrInt64u raw = p.GetCurrentValue();
            CrInt64u isoValue = raw & 0xFFFFFF;
            if (isoValue == SDK::CrISO_AUTO) json << "\"iso\":\"Auto\"";
            else json << "\"iso\":" << isoValue;
        } else if (code == SDK::CrDeviceProperty_AspectRatio) {
            json << "\"aspectRatio\":\"" << aspectRatioName(p.GetCurrentValue()) << "\"";
        } else if (code == SDK::CrDeviceProperty_FileType) {
            json << "\"fileType\":\"" << fileTypeName(p.GetCurrentValue()) << "\"";
        } else if (code == SDK::CrDeviceProperty_Colortemp) {
            json << "\"whiteBalanceColorTempK\":" << decodeSigned(p);
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

    // CrDeviceProperty_WhiteBalancePresetColorTemperature (tried originally,
    // by name) turns out not to exist on this camera at all -- confirmed
    // absent from the full GetDeviceProperties() dump, same dead-property
    // pattern as WhiteBalanceTint/RGain/BGain. CrDeviceProperty_Colortemp
    // is the real, live one (get/set-enabled, tracks the camera's actual
    // Kelvin value).
    double colorTempK;
    if (jsonFindNumber(body, "whiteBalanceColorTempK", colorTempK)) {
        SDK::CrDeviceProperty prop;
        prop.SetCode(SDK::CrDeviceProperty_Colortemp);
        prop.SetValueType(SDK::CrDataType_UInt16);
        prop.SetCurrentValue((CrInt64u)(int64_t)colorTempK);
        SDK::CrError err = SDK::SetDeviceProperty(g_deviceHandle, &prop);
        if (err) fprintf(stderr, "[writeRecipeJson] color temp not applied (0x%x)\n", err);
    }

    // Color Filter fine-tune (A-B / G-M axes) -- CrDeviceProperty_ColorTuningAB
    // and _ColorTuningGM, confirmed via GetDeviceProperties() to actually
    // exist and be get/set-enabled on this camera, unlike WhiteBalanceTint/
    // RGain/BGain (which are absent from the camera's full property list
    // entirely -- confirmed by dumping and inspecting all 431 properties,
    // not just assumed). Calibration (192=neutral, 4 raw units/step,
    // positive=A/G, negative=B/M) confirmed empirically against the real
    // camera screen. Both are CrDataType_UInt8Range -- the earlier dead
    // properties were being set with a plain (non-Range) type, which may
    // also have contributed to those writes silently no-oping.
    double colorFilterAB;
    if (jsonFindNumber(body, "colorFilterAB", colorFilterAB)) {
        SDK::CrDeviceProperty prop;
        prop.SetCode(SDK::CrDeviceProperty_ColorTuningAB);
        prop.SetValueType(SDK::CrDataType_UInt8Range);
        prop.SetCurrentValue(colorTuningFromOnscreen((int)std::lround(colorFilterAB)));
        SDK::CrError err = SDK::SetDeviceProperty(g_deviceHandle, &prop);
        if (err) { char buf[64]; snprintf(buf, sizeof(buf), "failed to set Color Filter A-B 0x%x", err); return buf; }
    }

    double colorFilterGM;
    if (jsonFindNumber(body, "colorFilterGM", colorFilterGM)) {
        SDK::CrDeviceProperty prop;
        prop.SetCode(SDK::CrDeviceProperty_ColorTuningGM);
        prop.SetValueType(SDK::CrDataType_UInt8Range);
        prop.SetCurrentValue(colorTuningFromOnscreen((int)std::lround(colorFilterGM)));
        SDK::CrError err = SDK::SetDeviceProperty(g_deviceHandle, &prop);
        if (err) { char buf[64]; snprintf(buf, sizeof(buf), "failed to set Color Filter G-M 0x%x", err); return buf; }
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

    std::string aspectRatioStr;
    if (jsonFindString(body, "aspectRatio", aspectRatioStr)) {
        SDK::CrDeviceProperty prop;
        prop.SetCode(SDK::CrDeviceProperty_AspectRatio);
        prop.SetValueType(SDK::CrDataType_UInt16);
        prop.SetCurrentValue(aspectRatioRaw(aspectRatioStr));
        SDK::CrError err = SDK::SetDeviceProperty(g_deviceHandle, &prop);
        if (err) { char buf[64]; snprintf(buf, sizeof(buf), "failed to set Aspect Ratio 0x%x", err); return buf; }
    }

    // Confirmed working live against the real camera for all three values
    // (RAW/JPEG/RAW+JPEG), each round-tripped by hand: set here, then
    // verified via /api/debug/allprops that the camera's own raw value
    // actually changed (1/2/3 respectively). CrDataType_UInt8, not UInt16 --
    // UInt16 silently no-oped despite CrFileType's underlying type being
    // CrInt16u.
    std::string fileTypeStr;
    if (jsonFindString(body, "fileType", fileTypeStr)) {
        SDK::CrDeviceProperty prop;
        prop.SetCode(SDK::CrDeviceProperty_FileType);
        prop.SetValueType(SDK::CrDataType_UInt8);
        prop.SetCurrentValue(fileTypeRaw(fileTypeStr));
        SDK::CrError err = SDK::SetDeviceProperty(g_deviceHandle, &prop);
        if (err) { char buf[64]; snprintf(buf, sizeof(buf), "failed to set File Type 0x%x", err); return buf; }
    }

    return "";
}

// ---------------------------------------------------------------------
// Network camera search -- looks up a camera's IP by MAC address on the
// Pi's own CameraBrdg access point, so the user doesn't have to hunt for
// it manually (previously the only way was cat'ing the dnsmasq lease
// file over SSH, per docs/CAMERA_SETUP.md).
// ---------------------------------------------------------------------

std::string normalizeMac(std::string mac) {
    std::transform(mac.begin(), mac.end(), mac.begin(), ::tolower);
    return mac;
}

std::string findIpByMac(const std::string& macNorm) {
    // Primary source: the AP's own DHCP lease file -- authoritative for
    // any camera that got its address via DHCP (the normal case). Needs
    // sudo to read; cbrant has passwordless sudo on the Pi already.
    FILE* pipe = popen("sudo cat /var/lib/NetworkManager/dnsmasq-wlan0.leases 2>/dev/null", "r");
    if (pipe) {
        char line[512];
        while (fgets(line, sizeof(line), pipe)) {
            std::istringstream iss(line);
            std::string epoch, mac, ip;
            iss >> epoch >> mac >> ip;
            if (normalizeMac(mac) == macNorm) { pclose(pipe); return ip; }
        }
        pclose(pipe);
    }

    // Fallback: the kernel's own ARP table -- catches a camera configured
    // with a static IP (never requested a DHCP lease) as long as the Pi
    // has exchanged at least one packet with it recently.
    std::ifstream arp("/proc/net/arp");
    std::string line;
    std::getline(arp, line); // header row
    while (std::getline(arp, line)) {
        std::istringstream iss(line);
        std::string ip, hwType, flags, mac;
        iss >> ip >> hwType >> flags >> mac;
        if (normalizeMac(mac) == macNorm) return ip;
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

    svr.Get("/api/camera-info", [](const httplib::Request&, httplib::Response& res) {
        std::lock_guard<std::mutex> lock(g_cameraMutex);
        if (!g_connected) { res.set_content("{\"error\":\"not connected\"}", "application/json"); return; }
        std::vector<CrInt32u> codes = {
            SDK::CrDeviceProperty_ModelName,
            SDK::CrDeviceProperty_BodySerialNumber,
            SDK::CrDeviceProperty_SoftwareVersion,
            SDK::CrDeviceProperty_LensModelName,
            SDK::CrDeviceProperty_LensVersionNumber,
            SDK::CrDeviceProperty_BatteryLevel,
            SDK::CrDeviceProperty_BatteryRemain,
            SDK::CrDeviceProperty_MediaSLOT1_RemainingNumber,
            SDK::CrDeviceProperty_MediaSLOT2_RemainingNumber,
        };
        SDK::CrDeviceProperty* props = nullptr;
        CrInt32 numProps = 0;
        SDK::CrError err = SDK::GetSelectDeviceProperties(g_deviceHandle, (CrInt32u)codes.size(), codes.data(), &props, &numProps);
        if (err) {
            char buf[96]; snprintf(buf, sizeof(buf), "{\"error\":\"read failed 0x%x\"}", err);
            res.set_content(buf, "application/json");
            return;
        }
        std::ostringstream json;
        json << "{";
        for (CrInt32 i = 0; i < numProps; i++) {
            SDK::CrDeviceProperty& p = props[i];
            CrInt32u code = p.GetCode();
            if (i > 0) json << ",";
            if (code == SDK::CrDeviceProperty_ModelName) {
                json << "\"modelName\":\"" << jsonEscape(decodeCrStr(p)) << "\"";
            } else if (code == SDK::CrDeviceProperty_BodySerialNumber) {
                json << "\"bodySerialNumber\":\"" << jsonEscape(decodeCrStr(p)) << "\"";
            } else if (code == SDK::CrDeviceProperty_SoftwareVersion) {
                json << "\"softwareVersion\":\"" << jsonEscape(decodeCrStr(p)) << "\"";
            } else if (code == SDK::CrDeviceProperty_LensModelName) {
                json << "\"lensModelName\":\"" << jsonEscape(decodeCrStr(p)) << "\"";
            } else if (code == SDK::CrDeviceProperty_LensVersionNumber) {
                json << "\"lensVersionNumber\":\"" << jsonEscape(decodeCrStr(p)) << "\"";
            } else if (code == SDK::CrDeviceProperty_BatteryLevel) {
                json << "\"batteryLevel\":\"" << batteryLevelName(p.GetCurrentValue()) << "\"";
            } else if (code == SDK::CrDeviceProperty_BatteryRemain) {
                json << "\"batteryRemain\":" << decodeSigned(p);
            } else if (code == SDK::CrDeviceProperty_MediaSLOT1_RemainingNumber) {
                json << "\"mediaSlot1Remaining\":" << decodeSigned(p);
            } else if (code == SDK::CrDeviceProperty_MediaSLOT2_RemainingNumber) {
                json << "\"mediaSlot2Remaining\":" << decodeSigned(p);
            }
        }
        json << "}";
        SDK::ReleaseDeviceProperties(g_deviceHandle, props);
        res.set_content(json.str(), "application/json");
    });

    svr.Get("/api/network/find", [](const httplib::Request& req, httplib::Response& res) {
        std::string mac = normalizeMac(req.get_param_value("mac"));
        std::string ip = findIpByMac(mac);
        std::ostringstream json;
        json << "{\"found\":" << (ip.empty() ? "false" : "true")
             << ",\"ip\":\"" << jsonEscape(ip) << "\"}";
        res.set_content(json.str(), "application/json");
    });

    // Dumps the camera's FULL, unrestricted property list (not our own
    // hardcoded GetSelectDeviceProperties subset) -- how ColorTuningAB/GM
    // were originally found and confirmed real, after WhiteBalanceTint/
    // RGain/BGain turned out to not exist on this camera at all. Kept
    // around as a standing diagnostic tool, not just a one-off.
    svr.Get("/api/debug/allprops", [](const httplib::Request&, httplib::Response& res) {
        std::lock_guard<std::mutex> lock(g_cameraMutex);
        if (!g_connected) { res.set_content("{\"error\":\"not connected\"}", "application/json"); return; }
        SDK::CrDeviceProperty* props = nullptr;
        CrInt32 numProps = 0;
        SDK::CrError err = SDK::GetDeviceProperties(g_deviceHandle, &props, &numProps);
        if (err) {
            char buf[64]; snprintf(buf, sizeof(buf), "{\"error\":\"0x%x\"}", err);
            res.set_content(buf, "application/json");
            return;
        }
        std::ostringstream json;
        json << "{\"totalProps\":" << numProps << ",\"props\":[";
        for (CrInt32 i = 0; i < numProps; i++) {
            if (i > 0) json << ",";
            CrInt32u code = props[i].GetCode();
            std::string name = CrDevicePropertyString((SDK::CrDevicePropertyCode)code);
            json << "{\"code\":\"0x" << std::hex << code << std::dec << "\""
                 << ",\"name\":\"" << jsonEscape(name) << "\""
                 << ",\"getEnable\":" << props[i].IsGetEnableCurrentValue()
                 << ",\"setEnable\":" << props[i].IsSetEnableCurrentValue()
                 << ",\"value\":" << decodeSigned(props[i]) << "}";
        }
        json << "]}";
        SDK::ReleaseDeviceProperties(g_deviceHandle, props);
        res.set_content(json.str(), "application/json");
    });

    // Saved camera profiles (name/ip/mac/userId/password) for one-tap
    // connect buttons -- lives in saved_cameras.json, gitignored, never
    // committed. Missing file just means no saved cameras, not an error.
    svr.Get("/api/network/saved", [](const httplib::Request&, httplib::Response& res) {
        std::ifstream f("saved_cameras.json");
        if (!f) { res.set_content("[]", "application/json"); return; }
        std::ostringstream ss;
        ss << f.rdbuf();
        res.set_content(ss.str(), "application/json");
    });

    // Saves (or updates, matched by name) one camera profile into
    // saved_cameras.json from the Setup tab's manual Connect (Network)
    // form -- lets the user create a one-tap Quick Connect for any
    // camera, not just a hardcoded one.
    svr.Post("/api/network/save", [](const httplib::Request& req, httplib::Response& res) {
        std::string name, ip, mac, userId, password;
        jsonFindString(req.body, "name", name);
        jsonFindString(req.body, "ip", ip);
        jsonFindString(req.body, "mac", mac);
        jsonFindString(req.body, "userId", userId);
        jsonFindString(req.body, "password", password);
        if (name.empty() || ip.empty()) {
            res.set_content("{\"success\":false,\"error\":\"name and ip are required\"}", "application/json");
            return;
        }
        std::vector<std::map<std::string, std::string>> cams;
        std::ifstream inFile("saved_cameras.json");
        if (inFile) {
            std::ostringstream ss;
            ss << inFile.rdbuf();
            cams = parseSavedCameras(ss.str());
        }
        bool replaced = false;
        for (auto& c : cams) {
            if (c["name"] == name) {
                c["ip"] = ip; c["mac"] = mac; c["userId"] = userId; c["password"] = password;
                replaced = true;
                break;
            }
        }
        if (!replaced) {
            cams.push_back({{"name", name}, {"ip", ip}, {"mac", mac}, {"userId", userId}, {"password", password}});
        }
        std::ofstream outFile("saved_cameras.json");
        outFile << serializeSavedCameras(cams);
        res.set_content("{\"success\":true}", "application/json");
    });

    // Saved Recipes CRUD -- 10 user-managed slots, see the comment above
    // parseSavedCameras/jsonFindRawObject for why recipe payloads are
    // stored opaquely rather than parsed field-by-field.
    svr.Get("/api/recipes/saved", [](const httplib::Request&, httplib::Response& res) {
        res.set_content(readJsonArrayFile("saved_recipes.json"), "application/json");
    });

    svr.Post("/api/recipes/save", [](const httplib::Request& req, httplib::Response& res) {
        std::string name = "";
        jsonFindString(req.body, "name", name);
        std::string recipeRaw = jsonFindRawObject(req.body, "recipe");
        double slotNumD;
        bool haveSlot = jsonFindNumber(req.body, "slot", slotNumD);
        if (name.empty() || recipeRaw.empty()) {
            res.set_content("{\"success\":false,\"error\":\"name and recipe are required\"}", "application/json");
            return;
        }
        std::string content = readJsonArrayFile("saved_recipes.json");
        int targetSlot = haveSlot ? (int)slotNumD : -1;
        if (targetSlot == -1) {
            auto spans = findTopLevelObjectSpans(content);
            std::vector<bool> used(10, false);
            for (auto& span : spans) {
                int s = extractSlotNumber(content.substr(span.first, span.second - span.first));
                if (s >= 0 && s < 10) used[s] = true;
            }
            for (int i = 0; i < 10; i++) { if (!used[i]) { targetSlot = i; break; } }
            if (targetSlot == -1) {
                res.set_content("{\"success\":false,\"error\":\"all 10 slots are full\"}", "application/json");
                return;
            }
        }
        if (targetSlot < 0 || targetSlot > 9) {
            res.set_content("{\"success\":false,\"error\":\"slot must be 0-9\"}", "application/json");
            return;
        }
        removeSavedRecipeSlot(content, targetSlot);
        std::ostringstream newObj;
        newObj << "{\"slot\":" << targetSlot << ",\"name\":\"" << jsonEscape(name) << "\",\"recipe\":" << recipeRaw << "}";
        size_t closeBracket = content.rfind(']');
        if (closeBracket == std::string::npos) {
            content = "[" + newObj.str() + "]";
        } else {
            bool hasContent = content.find('{') != std::string::npos;
            content.insert(closeBracket, (hasContent ? "," : "") + newObj.str());
        }
        writeJsonArrayFile("saved_recipes.json", content);
        std::ostringstream resp;
        resp << "{\"success\":true,\"slot\":" << targetSlot << "}";
        res.set_content(resp.str(), "application/json");
    });

    svr.Post("/api/recipes/rename", [](const httplib::Request& req, httplib::Response& res) {
        double slotNumD;
        std::string name;
        if (!jsonFindNumber(req.body, "slot", slotNumD) || !jsonFindString(req.body, "name", name)) {
            res.set_content("{\"success\":false,\"error\":\"slot and name are required\"}", "application/json");
            return;
        }
        int targetSlot = (int)slotNumD;
        std::string content = readJsonArrayFile("saved_recipes.json");
        auto spans = findTopLevelObjectSpans(content);
        bool found = false;
        for (auto& span : spans) {
            std::string objStr = content.substr(span.first, span.second - span.first);
            if (extractSlotNumber(objStr) != targetSlot) continue;
            size_t namePos = objStr.find("\"name\"");
            if (namePos != std::string::npos) {
                size_t colon = objStr.find(':', namePos);
                size_t q1 = objStr.find('"', colon);
                size_t q2 = objStr.find('"', q1 + 1);
                std::string newObjStr = objStr.substr(0, q1 + 1) + jsonEscape(name) + objStr.substr(q2);
                content.replace(span.first, span.second - span.first, newObjStr);
                found = true;
            }
            break;
        }
        if (!found) {
            res.set_content("{\"success\":false,\"error\":\"slot not found\"}", "application/json");
            return;
        }
        writeJsonArrayFile("saved_recipes.json", content);
        res.set_content("{\"success\":true}", "application/json");
    });

    svr.Post("/api/recipes/delete", [](const httplib::Request& req, httplib::Response& res) {
        double slotNumD;
        if (!jsonFindNumber(req.body, "slot", slotNumD)) {
            res.set_content("{\"success\":false,\"error\":\"slot is required\"}", "application/json");
            return;
        }
        std::string content = readJsonArrayFile("saved_recipes.json");
        bool found = removeSavedRecipeSlot(content, (int)slotNumD);
        if (!found) {
            res.set_content("{\"success\":false,\"error\":\"slot not found\"}", "application/json");
            return;
        }
        writeJsonArrayFile("saved_recipes.json", content);
        res.set_content("{\"success\":true}", "application/json");
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
