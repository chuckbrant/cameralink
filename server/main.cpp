// SonyConfig web app -- minimal REST backend over a native PTP/IP client
// (server/ptpip_client.h), single binary, serves the static frontend from
// ./public. This branch has ZERO Sony CrSDK dependency -- see
// ptpip_client.h for the protocol notes summary and the (private,
// not-in-this-repo) research doc it points to for the full writeup.
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <map>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "ptpip_client.h"
#include "httplib.h"

// ---------------------------------------------------------------------
// Built-in film recipe presets, transcribed from film_recipe_charts.pdf
// (page 3, "Film Recipe Chart -- Sony a7R V"). Every field here is
// confirmed writable and does get applied (Creative Look, WB mode/Kelvin,
// Color Filter A-B/G-M via colorFilterAB/colorFilterGM, ISO). "Base ISO"
// is shown as an informational note only (the film stock's real-world
// ISO rating, not a camera property).
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
bool g_connected = false;
std::string g_modelName;
PtpIpClient g_client;

// ---------------------------------------------------------------------
// Creative Look property field table -- shared by read and write.
// Wire property codes + encodings from the protocol notes (session 1/2).
// ---------------------------------------------------------------------

struct Field {
    std::string key;
    uint16_t code;
    int width;
    bool signedValue;
    int minValue;
    int maxValue;
};

// Ranges confirmed from Sony's own a7R V Help Guide (Creative Look section) --
// note these are NOT uniformly -9..9: Sharpness and Clarity are 0..9, and
// Sharpness Range is 1..5.
const std::vector<Field>& creativeLookFields() {
    static const std::vector<Field> fields = {
        {"contrast",       0xD0FB, 1, true, -9, 9},
        {"highlights",     0xD0FC, 1, true, -9, 9},
        {"shadows",        0xD0FD, 1, true, -9, 9},
        {"fade",           0xD0FE, 1, true, 0, 9},
        {"saturation",     0xD0FF, 1, true, -9, 9},
        {"sharpness",      0xD100, 1, true, 0, 9},
        {"sharpnessRange", 0xD101, 1, true, 1, 5},
        {"clarity",        0xD102, 1, true, 0, 9},
    };
    return fields;
}

constexpr uint16_t kPropPreset = 0xD0FA;
constexpr uint16_t kPropWhiteBalance = 0x5005;
constexpr uint16_t kPropColorTempK = 0xD20F;
constexpr uint16_t kPropColorTuningAB = 0xD21C;
constexpr uint16_t kPropColorTuningGM = 0xD210;
constexpr uint16_t kPropIso = 0xD21E;
constexpr uint16_t kPropAspectRatio = 0xD211;
constexpr uint16_t kPropFileType = 0xD253;

// ColorTuningAB/ColorTuningGM calibration -- confirmed empirically against
// the real a7R V (192=neutral, 220=A/G+7, 164=B/M-7): center 192, 4 raw
// units per on-screen step. Positive = A (amber) / G (green); negative =
// B (blue) / M (magenta).
constexpr int kColorTuningCenter = 192;
constexpr int kColorTuningStep = 4;
constexpr int kColorTuningMinRaw = 156;
constexpr int kColorTuningMaxRaw = 228;

int colorTuningToOnscreen(int64_t raw) {
    return ((int)raw - kColorTuningCenter) / kColorTuningStep;
}
uint64_t colorTuningFromOnscreen(int onscreen) {
    int raw = kColorTuningCenter + onscreen * kColorTuningStep;
    raw = std::max(kColorTuningMinRaw, std::min(kColorTuningMaxRaw, raw));
    return (uint64_t)raw;
}

// Wire values confirmed empirically -- see protocol notes ("Value enum
// tables"). Presets follow a clean sequential ordinal matching the SDK
// header's declared order; Custom Looks are 0x100+slot.
std::string presetName(int64_t raw) {
    switch (raw) {
        case 1: return "ST"; case 2: return "PT"; case 3: return "NT";
        case 4: return "VV"; case 5: return "VV2"; case 6: return "FL";
        case 7: return "IN"; case 8: return "SH"; case 9: return "BW";
        case 10: return "SE";
        default:
            if (raw >= 0x101) return "CS" + std::to_string(raw - 0x100);
            return "ST";
    }
}
uint64_t presetRaw(const std::string& name) {
    if (name == "ST") return 1; if (name == "PT") return 2; if (name == "NT") return 3;
    if (name == "VV") return 4; if (name == "VV2") return 5; if (name == "FL") return 6;
    if (name == "IN") return 7; if (name == "SH") return 8; if (name == "BW") return 9;
    if (name == "SE") return 10;
    if (name.rfind("CS", 0) == 0) {
        int slot = std::stoi(name.substr(2));
        if (slot >= 1) return 0x100 + slot;
    }
    return 1;
}

// WhiteBalance base values (AWB/Daylight/Fluorescent/Tungsten/Flash) match
// the standard PTP WhiteBalance enum exactly; Sony's extras (Cloudy/Shade/
// ColorTemp) use a clean sequential vendor range 0x8010-0x8012.
std::string whiteBalanceModeName(int64_t raw) {
    switch (raw) {
        case 2: return "AWB";
        case 4: return "Daylight";
        case 5: return "Fluorescent";
        case 6: return "Tungsten";
        case 7: return "Flash";
        case 0x8010: return "Cloudy";
        case 0x8011: return "Shade";
        case 0x8012: return "Color Temp";
        default: return "Unknown (" + std::to_string(raw) + ")";
    }
}
uint64_t whiteBalanceModeRaw(const std::string& name) {
    if (name == "AWB") return 2;
    if (name == "Daylight") return 4;
    if (name == "Fluorescent") return 5;
    if (name == "Tungsten") return 6;
    if (name == "Flash") return 7;
    if (name == "Cloudy") return 0x8010;
    if (name == "Shade") return 0x8011;
    if (name == "ColorTemp") return 0x8012;
    return 2;  // AWB
}

std::string aspectRatioName(int64_t raw) {
    switch (raw) {
        case 1: return "3:2"; case 2: return "16:9"; case 3: return "4:3"; case 4: return "1:1";
        default: return "3:2";
    }
}
uint64_t aspectRatioRaw(const std::string& name) {
    if (name == "3:2") return 1; if (name == "16:9") return 2;
    if (name == "4:3") return 3; if (name == "1:1") return 4;
    return 1;
}

// RAW's wire value (1) is inferred by elimination, not independently
// captured -- see protocol notes.
std::string fileTypeName(int64_t raw) {
    switch (raw) {
        case 1: return "RAW"; case 2: return "RAW+JPEG"; case 3: return "JPEG";
        default: return "RAW";
    }
}
uint64_t fileTypeRaw(const std::string& name) {
    if (name == "RAW") return 1; if (name == "RAW+JPEG") return 2; if (name == "JPEG") return 3;
    return 1;
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
// Saved Recipes -- a 10-slot, user-managed library. Recipe payloads are
// kept as opaque JSON blobs (see cameralink's original design notes).
// ---------------------------------------------------------------------

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

std::string connectNetwork(const std::string& ip, const std::string& mac, const std::string& userId, const std::string& password) {
    std::lock_guard<std::mutex> lock(g_cameraMutex);
    (void)mac;  // not needed by the native client -- SSH connects by IP directly
    std::string error;
    if (!g_client.Connect(ip, userId, password, &error)) {
        g_connected = false;
        return error;
    }
    g_modelName = "a7R V (network)";
    g_connected = true;
    return "";
}

std::string readRecipeJson() {
    std::lock_guard<std::mutex> lock(g_cameraMutex);
    if (!g_connected) return "{\"error\":\"not connected\"}";

    std::vector<std::pair<uint16_t, PtpIpPropertyValue>> props;
    std::string error;
    if (!g_client.ReadKnownProperties(&props, &error)) {
        char buf[128]; snprintf(buf, sizeof(buf), "{\"error\":\"read failed: %s\"}", error.c_str());
        return buf;
    }

    std::map<uint16_t, PtpIpPropertyValue> byCode;
    for (auto& [code, pv] : props) byCode[code] = pv;

    std::ostringstream json;
    json << "{";
    bool first = true;
    auto comma = [&]() { if (!first) json << ","; first = false; };

    if (byCode.count(kPropPreset) && byCode[kPropPreset].found) {
        comma(); json << "\"preset\":\"" << presetName(byCode[kPropPreset].value) << "\"";
    }
    if (byCode.count(kPropWhiteBalance) && byCode[kPropWhiteBalance].found) {
        comma(); json << "\"whiteBalanceMode\":\"" << whiteBalanceModeName(byCode[kPropWhiteBalance].value) << "\"";
    }
    if (byCode.count(kPropColorTuningAB) && byCode[kPropColorTuningAB].found) {
        comma(); json << "\"colorFilterAB\":" << colorTuningToOnscreen(byCode[kPropColorTuningAB].value);
    }
    if (byCode.count(kPropColorTuningGM) && byCode[kPropColorTuningGM].found) {
        comma(); json << "\"colorFilterGM\":" << colorTuningToOnscreen(byCode[kPropColorTuningGM].value);
    }
    if (byCode.count(kPropIso) && byCode[kPropIso].found) {
        comma();
        int64_t isoValue = byCode[kPropIso].value & 0xFFFFFF;
        if (isoValue == 0xFFFFFF) json << "\"iso\":\"Auto\"";
        else json << "\"iso\":" << isoValue;
    }
    if (byCode.count(kPropAspectRatio) && byCode[kPropAspectRatio].found) {
        comma(); json << "\"aspectRatio\":\"" << aspectRatioName(byCode[kPropAspectRatio].value) << "\"";
    }
    if (byCode.count(kPropFileType) && byCode[kPropFileType].found) {
        comma(); json << "\"fileType\":\"" << fileTypeName(byCode[kPropFileType].value) << "\"";
    }
    if (byCode.count(kPropColorTempK) && byCode[kPropColorTempK].found) {
        comma(); json << "\"whiteBalanceColorTempK\":" << byCode[kPropColorTempK].value;
    }
    for (auto& f : creativeLookFields()) {
        if (byCode.count(f.code) && byCode[f.code].found) {
            comma(); json << "\"" << f.key << "\":" << byCode[f.code].value;
        }
    }
    json << "}";
    return json.str();
}

std::string writeRecipeJson(const std::string& body) {
    std::lock_guard<std::mutex> lock(g_cameraMutex);
    if (!g_connected) return "not connected";
    std::string error;

    std::string presetStr;
    if (jsonFindString(body, "preset", presetStr)) {
        if (!g_client.WriteProperty(kPropPreset, 2, presetRaw(presetStr), &error)) {
            return "failed to set preset: " + error;
        }
        // The camera needs a brief moment to finish switching Creative Look
        // presets before its sub-parameters (contrast, saturation, etc.)
        // become settable again.
        std::this_thread::sleep_for(std::chrono::milliseconds(400));
    }

    for (auto& f : creativeLookFields()) {
        double value;
        if (!jsonFindNumber(body, f.key, value)) continue;
        int clamped = std::max(f.minValue, std::min(f.maxValue, (int)std::lround(value)));
        uint64_t wire = f.signedValue ? (uint64_t)(uint8_t)(int8_t)clamped : (uint64_t)clamped;
        if (!g_client.WriteProperty(f.code, f.width, wire, &error)) {
            return "failed to set " + f.key + ": " + error;
        }
    }

    std::string wbModeStr;
    bool wroteWbMode = false;
    if (jsonFindString(body, "whiteBalanceMode", wbModeStr)) {
        if (!g_client.WriteProperty(kPropWhiteBalance, 2, whiteBalanceModeRaw(wbModeStr), &error)) {
            return "failed to set white balance mode: " + error;
        }
        wroteWbMode = true;
    }

    double colorTempK;
    bool haveColorTempK = jsonFindNumber(body, "whiteBalanceColorTempK", colorTempK);
    if (haveColorTempK) {
        // Same settle requirement as the preset switch above -- Colortemp
        // isn't reliably settable immediately after switching WB modes, and
        // switching INTO ColorTemp mode specifically needs longer than other
        // WB mode changes.
        std::this_thread::sleep_for(std::chrono::milliseconds(wroteWbMode ? 1200 : 0));
        if (!g_client.WriteProperty(kPropColorTempK, 2, (uint64_t)(int64_t)colorTempK, &error)) {
            fprintf(stderr, "[writeRecipeJson] color temp not applied: %s\n", error.c_str());
        }
    }

    double colorFilterAB;
    if (jsonFindNumber(body, "colorFilterAB", colorFilterAB)) {
        if (!g_client.WriteProperty(kPropColorTuningAB, 1, colorTuningFromOnscreen((int)std::lround(colorFilterAB)), &error)) {
            return "failed to set Color Filter A-B: " + error;
        }
    }
    double colorFilterGM;
    if (jsonFindNumber(body, "colorFilterGM", colorFilterGM)) {
        if (!g_client.WriteProperty(kPropColorTuningGM, 1, colorTuningFromOnscreen((int)std::lround(colorFilterGM)), &error)) {
            return "failed to set Color Filter G-M: " + error;
        }
    }

    std::string isoStr;
    double isoValue;
    bool haveIso = false;
    uint64_t isoRawValue = 0;
    if (jsonFindString(body, "iso", isoStr) && isoStr == "Auto") {
        isoRawValue = 0xFFFFFF;
        haveIso = true;
    } else if (jsonFindNumber(body, "iso", isoValue)) {
        isoRawValue = (uint64_t)(int64_t)isoValue & 0xFFFFFF;
        haveIso = true;
    }
    if (haveIso) {
        if (!g_client.WriteProperty(kPropIso, 4, isoRawValue, &error)) {
            return "failed to set ISO: " + error;
        }
    }

    std::string aspectRatioStr;
    if (jsonFindString(body, "aspectRatio", aspectRatioStr)) {
        if (!g_client.WriteProperty(kPropAspectRatio, 1, aspectRatioRaw(aspectRatioStr), &error)) {
            return "failed to set Aspect Ratio: " + error;
        }
    }

    std::string fileTypeStr;
    if (jsonFindString(body, "fileType", fileTypeStr)) {
        if (!g_client.WriteProperty(kPropFileType, 1, fileTypeRaw(fileTypeStr), &error)) {
            return "failed to set File Type: " + error;
        }
    }

    return "";
}

// ---------------------------------------------------------------------
// Network camera search -- looks up a camera's IP by MAC address on the
// Pi's own CameraBrdg access point. Not applicable on the Docker/NAS
// deployment (no dnsmasq/ARP-relevant local AP there), kept for parity
// with the Pi build.
// ---------------------------------------------------------------------

std::string normalizeMac(std::string mac) {
    std::transform(mac.begin(), mac.end(), mac.begin(), ::tolower);
    return mac;
}

std::string findIpByMac(const std::string& macNorm) {
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
    httplib::Server svr;

    svr.set_mount_point("/", "./public");

    svr.Get("/api/status", [](const httplib::Request&, httplib::Response& res) {
        std::lock_guard<std::mutex> lock(g_cameraMutex);
        std::ostringstream json;
        json << "{\"connected\":" << (g_connected ? "true" : "false")
             << ",\"model\":\"" << jsonEscape(g_modelName) << "\"}";
        res.set_content(json.str(), "application/json");
    });

    // Model/serial/firmware come from the standard PTP GetDeviceInfo call
    // (no property-table lookup needed). Lens and battery/media info are
    // NOT part of the known-property set this branch decoded -- omitted
    // rather than guessed. See protocol notes for what's confirmed.
    svr.Get("/api/camera-info", [](const httplib::Request&, httplib::Response& res) {
        std::lock_guard<std::mutex> lock(g_cameraMutex);
        if (!g_connected) { res.set_content("{\"error\":\"not connected\"}", "application/json"); return; }
        PtpIpDeviceInfo info;
        std::string error;
        if (!g_client.GetDeviceInfo(&info, &error)) {
            char buf[160]; snprintf(buf, sizeof(buf), "{\"error\":\"%s\"}", error.c_str());
            res.set_content(buf, "application/json");
            return;
        }
        std::ostringstream json;
        json << "{\"modelName\":\"" << jsonEscape(info.model) << "\""
             << ",\"bodySerialNumber\":\"" << jsonEscape(info.serialNumber) << "\""
             << ",\"softwareVersion\":\"" << jsonEscape(info.version) << "\"}";
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

    // Scoped-down replacement for the CrSDK build's /api/debug/allprops --
    // that endpoint dumped the camera's full ~430-property list with
    // human-readable names via CrDevicePropertyString(), which needs
    // CrSDK. This just reports the properties this branch actually knows
    // about (see kKnownProperties in ptpip_client.cpp).
    svr.Get("/api/debug/allprops", [](const httplib::Request&, httplib::Response& res) {
        std::lock_guard<std::mutex> lock(g_cameraMutex);
        if (!g_connected) { res.set_content("{\"error\":\"not connected\"}", "application/json"); return; }
        std::vector<std::pair<uint16_t, PtpIpPropertyValue>> props;
        std::string error;
        if (!g_client.ReadKnownProperties(&props, &error)) {
            char buf[128]; snprintf(buf, sizeof(buf), "{\"error\":\"%s\"}", error.c_str());
            res.set_content(buf, "application/json");
            return;
        }
        std::ostringstream json;
        json << "{\"totalProps\":" << props.size() << ",\"props\":[";
        for (size_t i = 0; i < props.size(); i++) {
            if (i > 0) json << ",";
            auto& [code, pv] = props[i];
            char codeBuf[16]; snprintf(codeBuf, sizeof(codeBuf), "0x%x", code);
            json << "{\"code\":\"" << codeBuf << "\""
                 << ",\"getEnable\":" << (pv.getEnable ? 1 : 0)
                 << ",\"setEnable\":" << (pv.setEnable ? 1 : 0)
                 << ",\"value\":" << pv.value << "}";
        }
        json << "]}";
        res.set_content(json.str(), "application/json");
    });

    svr.Get("/api/network/saved", [](const httplib::Request&, httplib::Response& res) {
        std::ifstream f("saved_cameras.json");
        if (!f) { res.set_content("[]", "application/json"); return; }
        std::ostringstream ss;
        ss << f.rdbuf();
        res.set_content(ss.str(), "application/json");
    });

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

    // USB is not supported by this branch -- the native client only
    // speaks PTP/IP-over-SSH, which requires a network connection. USB
    // gadget mode (the Pi field kit) isn't used by the NAS/Docker
    // deployment this branch targets.
    svr.Post("/api/connect/usb", [](const httplib::Request&, httplib::Response& res) {
        res.set_content("{\"success\":false,\"error\":\"USB connect is not supported in this SDK-free build\"}", "application/json");
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
        g_client.Disconnect();
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
        std::thread([]() {
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
            system("sudo shutdown -h now");
        }).detach();
    });

    std::cout << "SonyConfig web app (SDK-free / native PTP/IP) listening on http://0.0.0.0:8080\n";
    svr.listen("0.0.0.0", 8080);
    return 0;
}
