import Foundation

// Wraps PlainPtpIpWireClient (plain TCP, no SSH, no auth) behind the
// async CameraTransport protocol -- for when the camera is hosting its
// own Wi-Fi access point (the same mode used for phone pairing), as
// opposed to WifiTransport's SSH-tunneled path for when the camera joins
// someone else's network as a station.
final class DirectWifiTransport: CameraTransport {
    private let queue = DispatchQueue(label: "com.cameralink.direct-wifi-transport")
    private let client = PlainPtpIpWireClient()
    let ip: String

    init(ip: String) {
        self.ip = ip
    }

    var isConnected: Bool { client.connected }

    func connect() async throws {
        try await run { try self.client.connect(ip: self.ip) }
    }

    func disconnect() {
        queue.sync { client.disconnect() }
    }

    func getDeviceInfo() async throws -> CameraDeviceInfo {
        try await run { try self.client.getDeviceInfo() }
    }

    func readKnownProperties() async throws -> [UInt16: CameraPropertyValue] {
        try await run { try self.client.readKnownProperties() }
    }

    func readStringProperties(_ codes: Set<UInt16>) async throws -> [UInt16: String] {
        try await run { try self.client.readStringProperties(codes) }
    }

    func writeProperty(code: UInt16, width: Int, value: Int64) async throws {
        try await run { try self.client.writeProperty(code: code, width: width, value: UInt64(bitPattern: value)) }
    }

    private func run<T>(_ body: @escaping () throws -> T) async throws -> T {
        try await withCheckedThrowingContinuation { cont in
            queue.async {
                do { cont.resume(returning: try body()) }
                catch { cont.resume(throwing: error) }
            }
        }
    }
}
