import Foundation

// Wraps PtpIpWireClient (blocking libssh2/socket calls) behind the async
// CameraTransport protocol -- every call is funneled through a single
// serial queue so the wire client is never touched from two threads at
// once.
final class WifiTransport: CameraTransport {
    private let queue = DispatchQueue(label: "com.cameralink.wifi-transport")
    private let client = PtpIpWireClient()

    let ip: String
    let userId: String
    let password: String

    init(ip: String, userId: String, password: String) {
        self.ip = ip
        self.userId = userId
        self.password = password
    }

    var isConnected: Bool { client.connected }

    func connect() async throws {
        try await run { try self.client.connect(ip: self.ip, userId: self.userId, password: self.password) }
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
