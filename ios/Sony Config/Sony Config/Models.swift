import Foundation

// The server's ISO field is a JSON string ("Auto") or a plain number,
// never a fixed type -- matches server/public/index.html's own handling
// (`isoVal === "Auto" ? "Auto" : parseInt(...)`).
enum ISOValue: Codable, Equatable, Hashable {
    case auto
    case value(Int)

    init(from decoder: Decoder) throws {
        let container = try decoder.singleValueContainer()
        if let intVal = try? container.decode(Int.self) {
            self = .value(intVal)
        } else if let strVal = try? container.decode(String.self), strVal == "Auto" {
            self = .auto
        } else {
            self = .auto
        }
    }

    func encode(to encoder: Encoder) throws {
        var container = encoder.singleValueContainer()
        switch self {
        case .auto: try container.encode("Auto")
        case .value(let v): try container.encode(v)
        }
    }

    var displayString: String {
        switch self {
        case .auto: return "Auto"
        case .value(let v): return "\(v)"
        }
    }

    static let selectable: [ISOValue] = [.auto] + [
        100, 125, 160, 200, 250, 320, 400, 500, 640, 800, 1000, 1250,
        1600, 2000, 2500, 3200, 4000, 5000, 6400, 8000, 10000, 12800,
        16000, 20000, 25600, 32000, 51200, 102400,
    ].map(ISOValue.value)
}

struct StatusResponse: Codable {
    var connected: Bool
    var model: String
}

struct CameraInfo: Codable {
    var modelName: String?
    var bodySerialNumber: String?
    var softwareVersion: String?
    var lensModelName: String?
    var lensVersionNumber: String?
    var batteryLevel: String?
    var batteryRemain: Int?
    var mediaSlot1Remaining: Int?
    var mediaSlot2Remaining: Int?
    var error: String?
}

// The full read/write recipe shape -- mirrors buildRecipeBody() /
// readRecipeJson() in server/public/index.html and server/main.cpp
// exactly, field for field.
struct Recipe: Codable, Equatable {
    var preset: String?
    var pictureProfileSlot: String?
    var contrast: Int?
    var highlights: Int?
    var shadows: Int?
    var fade: Int?
    var saturation: Int?
    var sharpness: Int?
    var sharpnessRange: Int?
    var clarity: Int?
    var whiteBalanceMode: String?
    var whiteBalanceColorTempK: Int?
    var colorFilterAB: Int?
    var colorFilterGM: Int?
    var iso: ISOValue?
    var aspectRatio: String?
    var fileType: String?
    var error: String?

    static let blank = Recipe()
}

struct FilmPreset: Codable, Identifiable {
    var group: String
    var id: String
    var name: String
    var preset: String?
    var contrast: Int?
    var highlights: Int?
    var shadows: Int?
    var fade: Int?
    var saturation: Int?
    var sharpness: Int?
    var sharpnessRange: Int?
    var clarity: Int?
    var whiteBalanceMode: String?
    var whiteBalanceColorTempK: Int?
    var colorFilterAB: Int?
    var colorFilterGM: Int?
    var iso: ISOValue?
    var baseISO: String?
    var whiteBalanceNote: String?
    var notes: String?

    // Only the fields this preset actually defines are included when
    // pushing -- matches pushPreset()'s "don't touch fields with no real
    // source data" behavior in the web frontend.
    var asRecipe: Recipe {
        Recipe(
            preset: preset, contrast: contrast, highlights: highlights,
            shadows: shadows, fade: fade, saturation: saturation,
            sharpness: sharpness, sharpnessRange: sharpnessRange,
            clarity: clarity, whiteBalanceMode: whiteBalanceMode,
            whiteBalanceColorTempK: whiteBalanceColorTempK,
            colorFilterAB: colorFilterAB, colorFilterGM: colorFilterGM,
            iso: iso
        )
    }
}

struct SavedRecipeEntry: Codable, Identifiable {
    var slot: Int
    var name: String
    var recipe: Recipe
    var id: Int { slot }
}

struct SavedCamera: Codable, Identifiable, Hashable {
    var name: String
    var ip: String
    var mac: String
    var userId: String
    var password: String
    var id: String { name }

    static func == (lhs: SavedCamera, rhs: SavedCamera) -> Bool { lhs.name == rhs.name }
    func hash(into hasher: inout Hasher) { hasher.combine(name) }
}

struct SimpleSuccess: Codable {
    var success: Bool
    var error: String?
    var slot: Int?
}

struct FindResponse: Codable {
    var found: Bool
    var ip: String
}

// Display-name lookups mirroring server/main.cpp's presetName()/
// presetNames map in the frontend.
let presetDisplayNames: [String: String] = [
    "ST": "Standard", "PT": "Portrait", "NT": "Neutral", "VV": "Vivid",
    "VV2": "Vivid 2", "FL": "Film", "IN": "Instant", "SH": "Soft Highkey",
    "BW": "Black & White", "SE": "Sepia",
    "CS1": "Custom 1", "CS2": "Custom 2", "CS3": "Custom 3",
    "CS4": "Custom 4", "CS5": "Custom 5", "CS6": "Custom 6",
]

let presetOptions: [(value: String, label: String)] = [
    ("ST", "Standard"), ("PT", "Portrait"), ("NT", "Neutral"), ("VV", "Vivid"),
    ("VV2", "Vivid 2"), ("FL", "Film"), ("IN", "Instant"), ("SH", "Soft Highkey"),
    ("BW", "Black & White"), ("SE", "Sepia"),
    ("CS1", "Custom 1"), ("CS2", "Custom 2"), ("CS3", "Custom 3"),
    ("CS4", "Custom 4"), ("CS5", "Custom 5"), ("CS6", "Custom 6"),
]

let wbModeOptions: [(value: String, label: String)] = [
    ("AWB", "AWB"), ("Daylight", "Daylight"), ("Shade", "Shade"),
    ("Cloudy", "Cloudy"), ("Tungsten", "Tungsten"), ("Fluorescent", "Fluorescent"),
    ("Flash", "Flash"), ("ColorTemp", "Color Temp"),
]

// Maps the API's read-side WB names onto the closest editable mode --
// same table as wbModeFromReadName in index.html.
let wbModeFromReadName: [String: String] = [
    "AWB": "AWB", "Daylight": "Daylight", "Shade": "Shade", "Cloudy": "Cloudy",
    "Tungsten": "Tungsten", "Fluorescent": "Fluorescent",
    "Fluorescent (Warm White)": "Fluorescent", "Fluorescent (Cool White)": "Fluorescent",
    "Fluorescent (Day White)": "Fluorescent", "Fluorescent (Daylight)": "Fluorescent",
    "Flash": "Flash", "Color Temp": "ColorTemp",
    "Custom 1": "AWB", "Custom 2": "AWB", "Custom 3": "AWB", "Custom": "AWB",
    "Underwater Auto": "AWB",
]

let aspectRatioOptions = ["3:2", "16:9", "4:3", "1:1"]
let fileTypeOptions = ["RAW", "RAW+JPEG", "JPEG"]
