import Foundation

// Abstraction over "how this iPad reaches the camera" -- there are two
// implementations (WifiTransport over PTP/IP-over-SSH, UsbTransport
// attempted over ExternalAccessory) so RecipeCameraClient's read/write
// logic doesn't care which one is active. No server/Pi in this picture at
// all -- this app talks to the camera directly.

struct CameraDeviceInfo {
    var manufacturer = ""
    var model = ""
    var version = ""
    var serialNumber = ""
    // Not from GetDeviceInfo -- filled in separately from the property
    // table's string-type entries (see PtpKnownProperties.lensModelCode/
    // lensVersionCode), confirmed 2026-08-29 against the real a7R V.
    // Battery level and media slot remaining-shots aren't available:
    // battery (standard PTP code 0x5001) was confirmed ABSENT from the
    // full property table dump (not just unverified -- genuinely not
    // exposed by this protocol), and media remaining-shots was never
    // reverse-engineered at all.
    var lensModel: String?
    var lensVersion: String?
}

struct CameraPropertyValue {
    var found = false
    var getEnable = false
    var setEnable = false
    var value: Int64 = 0
}

protocol CameraTransport: AnyObject {
    var isConnected: Bool { get }
    func connect() async throws
    func disconnect()
    func getDeviceInfo() async throws -> CameraDeviceInfo
    func readKnownProperties() async throws -> [UInt16: CameraPropertyValue]
    func readStringProperties(_ codes: Set<UInt16>) async throws -> [UInt16: String]
    func writeProperty(code: UInt16, width: Int, value: Int64) async throws
}
