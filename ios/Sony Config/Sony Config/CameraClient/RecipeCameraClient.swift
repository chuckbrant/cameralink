import Foundation

// Ports server/main.cpp's readRecipeJson()/writeRecipeJson() and their
// helper tables directly to Swift -- same property codes, same value
// encodings, same settle-time delays after preset/WB-mode switches
// (confirmed necessary against the real camera). This is the only place
// that knows how a Recipe maps onto PTP property codes; CameraTransport
// implementations just move bytes.

private struct CreativeLookField {
    let code: UInt16
    let width: Int
    let signedValue: Bool
    let minValue: Int
    let maxValue: Int
    let get: (Recipe) -> Int?
    let set: (inout Recipe, Int) -> Void
}

// Ranges confirmed from Sony's own a7R V Help Guide (Creative Look
// section) -- NOT uniformly -9..9: Sharpness/Clarity are 0..9, Sharpness
// Range is 1..5.
private let creativeLookFields: [CreativeLookField] = [
    CreativeLookField(code: 0xD0FB, width: 1, signedValue: true, minValue: -9, maxValue: 9,
                       get: { $0.contrast }, set: { $0.contrast = $1 }),
    CreativeLookField(code: 0xD0FC, width: 1, signedValue: true, minValue: -9, maxValue: 9,
                       get: { $0.highlights }, set: { $0.highlights = $1 }),
    CreativeLookField(code: 0xD0FD, width: 1, signedValue: true, minValue: -9, maxValue: 9,
                       get: { $0.shadows }, set: { $0.shadows = $1 }),
    CreativeLookField(code: 0xD0FE, width: 1, signedValue: true, minValue: 0, maxValue: 9,
                       get: { $0.fade }, set: { $0.fade = $1 }),
    CreativeLookField(code: 0xD0FF, width: 1, signedValue: true, minValue: -9, maxValue: 9,
                       get: { $0.saturation }, set: { $0.saturation = $1 }),
    CreativeLookField(code: 0xD100, width: 1, signedValue: true, minValue: 0, maxValue: 9,
                       get: { $0.sharpness }, set: { $0.sharpness = $1 }),
    CreativeLookField(code: 0xD101, width: 1, signedValue: true, minValue: 1, maxValue: 5,
                       get: { $0.sharpnessRange }, set: { $0.sharpnessRange = $1 }),
    CreativeLookField(code: 0xD102, width: 1, signedValue: true, minValue: 0, maxValue: 9,
                       get: { $0.clarity }, set: { $0.clarity = $1 }),
]

private let kPropPreset: UInt16 = 0xD0FA
private let kPropWhiteBalance: UInt16 = 0x5005
private let kPropColorTempK: UInt16 = 0xD20F
private let kPropColorTuningAB: UInt16 = 0xD21C
private let kPropColorTuningGM: UInt16 = 0xD210
private let kPropIso: UInt16 = 0xD21E
private let kPropAspectRatio: UInt16 = 0xD211
private let kPropFileType: UInt16 = 0xD253

// ColorTuningAB/ColorTuningGM calibration -- confirmed empirically
// against the real a7R V (192=neutral, 220=A/G+7, 164=B/M-7): center 192,
// 4 raw units per on-screen step. Positive = A (amber) / G (green);
// negative = B (blue) / M (magenta).
private let kColorTuningCenter = 192
private let kColorTuningStep = 4
private let kColorTuningMinRaw = 156
private let kColorTuningMaxRaw = 228

private func colorTuningToOnscreen(_ raw: Int64) -> Int { (Int(raw) - kColorTuningCenter) / kColorTuningStep }
private func colorTuningFromOnscreen(_ onscreen: Int) -> Int64 {
    let raw = kColorTuningCenter + onscreen * kColorTuningStep
    return Int64(max(kColorTuningMinRaw, min(kColorTuningMaxRaw, raw)))
}

// Wire values confirmed empirically -- presets follow a clean sequential
// ordinal matching the SDK header's declared order; Custom Looks are
// 0x100+slot.
private func presetName(_ raw: Int64) -> String {
    switch raw {
    case 1: return "ST"; case 2: return "PT"; case 3: return "NT"
    case 4: return "VV"; case 5: return "VV2"; case 6: return "FL"
    case 7: return "IN"; case 8: return "SH"; case 9: return "BW"
    case 10: return "SE"
    default:
        if raw >= 0x101 { return "CS\(raw - 0x100)" }
        return "ST"
    }
}
private func presetRaw(_ name: String) -> Int64 {
    switch name {
    case "ST": return 1; case "PT": return 2; case "NT": return 3
    case "VV": return 4; case "VV2": return 5; case "FL": return 6
    case "IN": return 7; case "SH": return 8; case "BW": return 9
    case "SE": return 10
    default:
        if name.hasPrefix("CS"), let slot = Int(name.dropFirst(2)), slot >= 1 { return Int64(0x100 + slot) }
        return 1
    }
}

// WhiteBalance base values (AWB/Daylight/Fluorescent/Tungsten/Flash)
// match the standard PTP WhiteBalance enum exactly; Sony's extras
// (Cloudy/Shade/ColorTemp) use a clean sequential vendor range
// 0x8010-0x8012.
private func whiteBalanceModeName(_ raw: Int64) -> String {
    switch raw {
    case 2: return "AWB"; case 4: return "Daylight"; case 5: return "Fluorescent"
    case 6: return "Tungsten"; case 7: return "Flash"
    case 0x8010: return "Cloudy"; case 0x8011: return "Shade"; case 0x8012: return "ColorTemp"
    default: return "Unknown (\(raw))"
    }
}
private func whiteBalanceModeRaw(_ name: String) -> Int64 {
    switch name {
    case "AWB": return 2; case "Daylight": return 4; case "Fluorescent": return 5
    case "Tungsten": return 6; case "Flash": return 7
    case "Cloudy": return 0x8010; case "Shade": return 0x8011; case "ColorTemp": return 0x8012
    default: return 2  // AWB
    }
}

private func aspectRatioName(_ raw: Int64) -> String {
    switch raw { case 1: return "3:2"; case 2: return "16:9"; case 3: return "4:3"; case 4: return "1:1"; default: return "3:2" }
}
private func aspectRatioRaw(_ name: String) -> Int64 {
    switch name { case "3:2": return 1; case "16:9": return 2; case "4:3": return 3; case "1:1": return 4; default: return 1 }
}

// RAW's wire value (1) is inferred by elimination, not independently
// captured -- see protocol notes.
private func fileTypeName(_ raw: Int64) -> String {
    switch raw { case 1: return "RAW"; case 2: return "RAW+JPEG"; case 3: return "JPEG"; default: return "RAW" }
}
private func fileTypeRaw(_ name: String) -> Int64 {
    switch name { case "RAW": return 1; case "RAW+JPEG": return 2; case "JPEG": return 3; default: return 1 }
}

enum RecipeCameraError: Error, LocalizedError {
    case notConnected
    case transport(Error)
    var errorDescription: String? {
        switch self {
        case .notConnected: return "Not connected to a camera"
        case .transport(let e): return e.localizedDescription
        }
    }
}

final class RecipeCameraClient {
    let transport: CameraTransport
    init(transport: CameraTransport) { self.transport = transport }

    var isConnected: Bool { transport.isConnected }

    func connect() async throws { try await transport.connect() }
    func disconnect() { transport.disconnect() }

    func deviceInfo() async throws -> CameraDeviceInfo {
        var info = try await transport.getDeviceInfo()
        // Best-effort -- if the property read fails for any reason, still
        // return the GetDeviceInfo fields we do have.
        if let strings = try? await transport.readStringProperties(
            [PtpKnownProperties.lensModelCode, PtpKnownProperties.lensVersionCode]
        ) {
            info.lensModel = strings[PtpKnownProperties.lensModelCode]
            info.lensVersion = strings[PtpKnownProperties.lensVersionCode]
        }
        return info
    }

    func readRecipe() async throws -> Recipe {
        guard transport.isConnected else { throw RecipeCameraError.notConnected }
        let props = try await transport.readKnownProperties()
        var recipe = Recipe()

        if let pv = props[kPropPreset], pv.found { recipe.preset = presetName(pv.value) }
        if let pv = props[kPropWhiteBalance], pv.found { recipe.whiteBalanceMode = whiteBalanceModeName(pv.value) }
        if let pv = props[kPropColorTuningAB], pv.found { recipe.colorFilterAB = colorTuningToOnscreen(pv.value) }
        if let pv = props[kPropColorTuningGM], pv.found { recipe.colorFilterGM = colorTuningToOnscreen(pv.value) }
        if let pv = props[kPropIso], pv.found {
            let isoValue = pv.value & 0xFFFFFF
            recipe.iso = isoValue == 0xFFFFFF ? .auto : .value(Int(isoValue))
        }
        if let pv = props[kPropAspectRatio], pv.found { recipe.aspectRatio = aspectRatioName(pv.value) }
        if let pv = props[kPropFileType], pv.found { recipe.fileType = fileTypeName(pv.value) }
        if let pv = props[kPropColorTempK], pv.found { recipe.whiteBalanceColorTempK = Int(pv.value) }
        for f in creativeLookFields {
            if let pv = props[f.code], pv.found { f.set(&recipe, Int(pv.value)) }
        }
        return recipe
    }

    func writeRecipe(_ recipe: Recipe) async throws {
        guard transport.isConnected else { throw RecipeCameraError.notConnected }

        if let presetStr = recipe.preset {
            try await transport.writeProperty(code: kPropPreset, width: 2, value: presetRaw(presetStr))
            // The camera needs a brief moment to finish switching Creative
            // Look presets before its sub-parameters (contrast, saturation,
            // etc.) become settable again.
            try await Task.sleep(nanoseconds: 400_000_000)
        }

        for f in creativeLookFields {
            guard let value = f.get(recipe) else { continue }
            let clamped = max(f.minValue, min(f.maxValue, value))
            try await transport.writeProperty(code: f.code, width: f.width, value: Int64(clamped))
        }

        var wroteWbMode = false
        if let wbModeStr = recipe.whiteBalanceMode {
            try await transport.writeProperty(code: kPropWhiteBalance, width: 2, value: whiteBalanceModeRaw(wbModeStr))
            wroteWbMode = true
        }

        if let colorTempK = recipe.whiteBalanceColorTempK {
            // Same settle requirement as the preset switch above -- Color
            // Temp isn't reliably settable immediately after switching WB
            // modes, and switching INTO ColorTemp mode specifically needs
            // longer than other WB mode changes.
            if wroteWbMode { try await Task.sleep(nanoseconds: 1_200_000_000) }
            try? await transport.writeProperty(code: kPropColorTempK, width: 2, value: Int64(colorTempK))
        }

        if let colorFilterAB = recipe.colorFilterAB {
            try await transport.writeProperty(code: kPropColorTuningAB, width: 1, value: colorTuningFromOnscreen(colorFilterAB))
        }
        if let colorFilterGM = recipe.colorFilterGM {
            try await transport.writeProperty(code: kPropColorTuningGM, width: 1, value: colorTuningFromOnscreen(colorFilterGM))
        }

        if let iso = recipe.iso {
            let raw: Int64
            switch iso {
            case .auto: raw = 0xFFFFFF
            case .value(let v): raw = Int64(v) & 0xFFFFFF
            }
            try await transport.writeProperty(code: kPropIso, width: 4, value: raw)
        }

        if let aspectRatioStr = recipe.aspectRatio {
            try await transport.writeProperty(code: kPropAspectRatio, width: 1, value: aspectRatioRaw(aspectRatioStr))
        }
        if let fileTypeStr = recipe.fileType {
            try await transport.writeProperty(code: kPropFileType, width: 1, value: fileTypeRaw(fileTypeStr))
        }
    }
}
