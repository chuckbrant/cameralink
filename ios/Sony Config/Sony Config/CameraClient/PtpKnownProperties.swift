// Shared property table + property-table-blob parser used by both
// PtpIpWireClient (SSH-tunneled) and PlainPtpIpWireClient (plain TCP) --
// the wire format returned by opcode 0x9209 is identical either way.
enum PtpKnownProperties {
    struct Entry {
        let code: UInt16
        let width: Int
        let signedValue: Bool
    }

    // Same table as server/ptpip_client.cpp's kKnownProperties / main.cpp's
    // creativeLookFields()+kProp* constants.
    static let all: [Entry] = [
        Entry(code: 0xD0FA, width: 2, signedValue: false),  // preset
        Entry(code: 0xD0FB, width: 1, signedValue: true),   // contrast
        Entry(code: 0xD0FC, width: 1, signedValue: true),   // highlights
        Entry(code: 0xD0FD, width: 1, signedValue: true),   // shadows
        Entry(code: 0xD0FE, width: 1, signedValue: true),   // fade
        Entry(code: 0xD0FF, width: 1, signedValue: true),   // saturation
        Entry(code: 0xD100, width: 1, signedValue: true),   // sharpness
        Entry(code: 0xD101, width: 1, signedValue: true),   // sharpnessRange
        Entry(code: 0xD102, width: 1, signedValue: true),   // clarity
        Entry(code: 0x5005, width: 2, signedValue: false),  // whiteBalance
        Entry(code: 0xD20F, width: 2, signedValue: false),  // colorTempK
        Entry(code: 0xD21C, width: 1, signedValue: false),  // colorFilterAB
        Entry(code: 0xD210, width: 1, signedValue: false),  // colorFilterGM
        Entry(code: 0xD211, width: 1, signedValue: false),  // aspectRatio
        Entry(code: 0xD253, width: 1, signedValue: false),  // fileType
        Entry(code: 0xD21E, width: 4, signedValue: false),  // iso
    ]

    static func parse(_ data: [UInt8]) -> [UInt16: CameraPropertyValue] {
        var out: [UInt16: CameraPropertyValue] = [:]
        for kp in all {
            var pv = CameraPropertyValue()
            var i = 4
            while i + 8 <= data.count {
                if readU16LE(data, i) == kp.code {
                    let type = data[i + 2]
                    let expectedWidth = (type == 0x01 || type == 0x02) ? 1 : (type == 0x04 ? 2 : (type == 0x06 ? 4 : 0))
                    if expectedWidth == kp.width {
                        let valueOff = i + (kp.width == 1 ? 7 : (kp.width == 2 ? 8 : 10))
                        if valueOff + kp.width <= data.count {
                            var raw: UInt64 = 0
                            for b in stride(from: kp.width - 1, through: 0, by: -1) {
                                raw = (raw << 8) | UInt64(data[valueOff + b])
                            }
                            var value = Int64(bitPattern: raw)
                            if kp.signedValue {
                                if kp.width == 1 { value = Int64(Int8(bitPattern: UInt8(truncatingIfNeeded: raw))) }
                                else if kp.width == 2 { value = Int64(Int16(bitPattern: UInt16(truncatingIfNeeded: raw))) }
                                else if kp.width == 4 { value = Int64(Int32(bitPattern: UInt32(truncatingIfNeeded: raw))) }
                            }
                            pv.found = true
                            pv.getEnable = data[i + 4] != 0
                            pv.setEnable = data[i + 5] != 0
                            pv.value = value
                            break
                        }
                    }
                }
                i += 1
            }
            out[kp.code] = pv
        }
        return out
    }

    private static func readU16LE(_ b: [UInt8], _ off: Int) -> UInt16 {
        UInt16(b[off]) | (UInt16(b[off+1]) << 8)
    }

    // MARK: - String-type properties (lens model/version) --
    // confirmed 2026-08-29 against the real a7R V via a full raw-table
    // dump: [code:2][type:2 == 0xFFFF (PTP STR)][getEnable:1][setEnable:1]
    // [pad:1][strLen:1 (UTF16 units incl. null terminator)][UTF16LE
    // chars incl. null]. lensModel matched the actually-mounted lens
    // ("FE 40mm F2.5 G"); lensVersion is inferred by proximity/format
    // match, not independently cross-checked against another source.
    static let lensModelCode: UInt16 = 0xD07B
    static let lensVersionCode: UInt16 = 0xD040

    static func parseStrings(_ data: [UInt8], codes: Set<UInt16>) -> [UInt16: String] {
        var out: [UInt16: String] = [:]
        var i = 4
        while i + 8 <= data.count {
            let code = readU16LE(data, i)
            if codes.contains(code), data[i + 2] == 0xff, data[i + 3] == 0xff {
                let strLen = Int(data[i + 7])
                let start = i + 8
                let end = start + strLen * 2
                if strLen > 0, end <= data.count {
                    var chars = [Character]()
                    var ok = true
                    var k = start
                    while k + 1 < end {
                        let lo = data[k], hi = data[k + 1]
                        if hi != 0 { ok = false; break }
                        if lo == 0 { break }
                        chars.append(Character(UnicodeScalar(lo)))
                        k += 2
                    }
                    if ok { out[code] = String(chars) }
                }
            }
            i += 1
        }
        return out
    }
}
