// Native PTP/IP-over-SSH client for Sony's Remote Shoot Function -- ZERO
// CrSDK dependency. Ported from cameralink-research/swift-ptpip-poc
// (itself a Swift port of server/ptpip_client.h/.cpp), which was compiled
// and validated end-to-end against a real a7R V. See
// PTPIP_PROTOCOL_NOTES.md (private, not in this repo) for how every wire
// detail here was derived and verified.
//
// Uses CSSH2 (github.com/Lakr233/libssh2-spm), a real source build of
// libssh2, NOT swift-nio-ssh/Citadel -- confirmed by reading
// swift-nio-ssh's own source that it has zero keyboard-interactive
// support, and this camera's SSH server rejects plain password auth
// outright (only keyboard-interactive is offered).
//
// This class does blocking socket/SSH I/O and is NOT thread-safe on its
// own -- WifiTransport is the only caller, and it funnels every call
// through a single serial queue.

import CSSH2
import Foundation
#if canImport(Darwin)
import Darwin
#else
import Glibc
#endif

enum PtpIpError: Error, CustomStringConvertible, LocalizedError {
    case message(String)
    var description: String {
        switch self { case .message(let m): return m }
    }
    // Without this, SwiftUI's error alert bridges the enum to a generic
    // NSError ("Sony_Config.PtpIpError error 0") instead of the real
    // message -- LocalizedError.errorDescription is what AppState.report()
    // and the alert actually read.
    var errorDescription: String? { description }
}

// The keyboard-interactive callback has no user-data parameter in
// libssh2's C API (only the session's `abstract` pointer, a void**) --
// stash the password here for the duration of one connect() call.
// WifiTransport's serial queue guarantees only one connect() runs at a
// time, so a plain global is safe (matches the C++ client's thread_local
// approach).
private var gKeyboardInteractivePassword: String = ""

private func keyboardInteractiveCallback(
    name: UnsafePointer<CChar>?, nameLen: CInt,
    instruction: UnsafePointer<CChar>?, instructionLen: CInt,
    numPrompts: CInt,
    prompts: UnsafePointer<LIBSSH2_USERAUTH_KBDINT_PROMPT>?,
    responses: UnsafeMutablePointer<LIBSSH2_USERAUTH_KBDINT_RESPONSE>?,
    abstract: UnsafeMutablePointer<UnsafeMutableRawPointer?>?
) {
    guard let responses else { return }
    let pw = gKeyboardInteractivePassword
    let pwBytes = Array(pw.utf8)
    for i in 0..<Int(numPrompts) {
        let buf = UnsafeMutablePointer<CChar>.allocate(capacity: pwBytes.count)
        for (j, b) in pwBytes.enumerated() { buf[j] = CChar(bitPattern: b) }
        responses[i].text = buf
        responses[i].length = UInt32(pwBytes.count)
    }
}

final class PtpIpWireClient {
    private var socketFd: Int32 = -1
    private var session: OpaquePointer?
    private var cmdChannel: OpaquePointer?
    private var eventChannel: OpaquePointer?
    private var nextTransId: UInt32 = 2
    private var cmdBuf = [UInt8]()
    private(set) var connected = false

    private static var sshInited = false

    func disconnect() {
        if let ch = cmdChannel { libssh2_channel_free(ch) }
        if let ch = eventChannel { libssh2_channel_free(ch) }
        if let s = session {
            libssh2_session_disconnect_ex(s, SSH_DISCONNECT_BY_APPLICATION, "bye", "")
            libssh2_session_free(s)
        }
        if socketFd >= 0 { close(socketFd) }
        cmdChannel = nil
        eventChannel = nil
        session = nil
        socketFd = -1
        cmdBuf.removeAll()
        nextTransId = 2
        connected = false
    }

    // MARK: - Low-level framing

    private func writeAll(_ channel: OpaquePointer, _ data: [UInt8]) throws {
        var sent = 0
        try data.withUnsafeBufferPointer { buf in
            while sent < data.count {
                let n = buf.baseAddress!.withMemoryRebound(to: CChar.self, capacity: data.count) { p in
                    libssh2_channel_write_ex(channel, 0, p + sent, data.count - sent)
                }
                if n < 0 {
                    if n == Int(LIBSSH2_ERROR_EAGAIN) { continue }
                    throw PtpIpError.message("channel write failed (\(n))")
                }
                sent += n
            }
        }
    }

    private func readFrame() throws -> [UInt8] {
        var tmp = [UInt8](repeating: 0, count: 16384)
        while true {
            if cmdBuf.count >= 8 {
                let len = readU32LE(cmdBuf, 0)
                if len < 8 { throw PtpIpError.message("invalid PTP/IP frame length") }
                if cmdBuf.count >= Int(len) {
                    let frame = Array(cmdBuf[0..<Int(len)])
                    cmdBuf.removeFirst(Int(len))
                    return frame
                }
            }
            let n = tmp.withUnsafeMutableBufferPointer { buf -> Int in
                buf.baseAddress!.withMemoryRebound(to: CChar.self, capacity: buf.count) { p in
                    libssh2_channel_read_ex(cmdChannel, 0, p, buf.count)
                }
            }
            if n < 0 {
                if n == Int(LIBSSH2_ERROR_EAGAIN) { continue }
                throw PtpIpError.message("channel read failed (\(n))")
            }
            if n == 0 {
                if libssh2_channel_eof(cmdChannel) != 0 { throw PtpIpError.message("channel closed") }
                continue
            }
            cmdBuf.append(contentsOf: tmp[0..<n])
        }
    }

    private func sendCmdRequest(dataphase: UInt32, opcode: UInt16, params: [UInt32]) throws -> UInt32 {
        let transid = nextTransId
        nextTransId += 1
        var body = [UInt8]()
        appendU32LE(&body, dataphase)
        appendU16LE(&body, opcode)
        appendU32LE(&body, transid)
        for p in params { appendU32LE(&body, p) }
        var frame = [UInt8]()
        appendU32LE(&frame, UInt32(8 + body.count))
        appendU32LE(&frame, 6)  // kCmdRequest
        frame.append(contentsOf: body)
        try writeAll(cmdChannel!, frame)
        return transid
    }

    private func sendDataPhase(transid: UInt32, data: [UInt8]) throws {
        var start = [UInt8]()
        appendU32LE(&start, 20)
        appendU32LE(&start, 9)  // kStartDataPacket
        appendU32LE(&start, transid)
        appendU32LE(&start, UInt32(data.count))
        appendU32LE(&start, 0)
        try writeAll(cmdChannel!, start)

        var dataPkt = [UInt8]()
        appendU32LE(&dataPkt, UInt32(12 + data.count))
        appendU32LE(&dataPkt, 10)  // kDataPacket
        appendU32LE(&dataPkt, transid)
        dataPkt.append(contentsOf: data)
        try writeAll(cmdChannel!, dataPkt)

        var end = [UInt8]()
        appendU32LE(&end, 12)
        appendU32LE(&end, 12)  // kEndDataPacket
        appendU32LE(&end, transid)
        try writeAll(cmdChannel!, end)
    }

    @discardableResult
    private func readTransaction(transid: UInt32) throws -> [UInt8] {
        var outData = [UInt8]()
        for _ in 0..<100_000 {
            let frame = try readFrame()
            if frame.count < 8 { continue }
            let type = readU32LE(frame, 4)
            if type == 10, frame.count >= 12 {  // DataPacket
                let tid = readU32LE(frame, 8)
                if tid == transid { outData.append(contentsOf: frame[12...]) }
            } else if type == 7, frame.count >= 14 {  // CmdResponse
                let tid = readU32LE(frame, 10)
                if tid == transid {
                    let code = readU16LE(frame, 8)
                    guard code == 0x2001 else {
                        throw PtpIpError.message(String(format: "camera rejected the request (PTP response code 0x%04x)", code))
                    }
                    return outData
                }
            }
        }
        throw PtpIpError.message("transaction did not complete")
    }

    // MARK: - Connect

    func connect(ip: String, userId: String, password: String) throws {
        disconnect()

        socketFd = socket(AF_INET, SOCK_STREAM, 0)
        guard socketFd >= 0 else { throw PtpIpError.message("socket() failed") }

        var addr = sockaddr_in()
        addr.sin_family = sa_family_t(AF_INET)
        addr.sin_port = UInt16(22).bigEndian
        guard inet_pton(AF_INET, ip, &addr.sin_addr) == 1 else {
            throw PtpIpError.message("invalid camera IP")
        }
        let connectResult = withUnsafePointer(to: &addr) { p -> Int32 in
            p.withMemoryRebound(to: sockaddr.self, capacity: 1) { sp in
                Foundation.connect(socketFd, sp, socklen_t(MemoryLayout<sockaddr_in>.size))
            }
        }
        guard connectResult == 0 else { throw PtpIpError.message("TCP connect to camera:22 failed") }

        if !Self.sshInited { libssh2_init(0); Self.sshInited = true }

        guard let s = libssh2_session_init_ex(nil, nil, nil, nil) else {
            throw PtpIpError.message("libssh2_session_init failed")
        }
        session = s
        libssh2_session_set_blocking(s, 1)

        guard libssh2_session_handshake(s, socketFd) == 0 else {
            throw PtpIpError.message("SSH handshake failed")
        }

        gKeyboardInteractivePassword = password
        let authRc = userId.withCString { uid in
            libssh2_userauth_keyboard_interactive_ex(s, uid, UInt32(strlen(uid)), keyboardInteractiveCallback)
        }
        gKeyboardInteractivePassword = ""
        guard authRc == 0 else { throw PtpIpError.message("SSH auth failed (bad user/password)") }

        cmdChannel = "localhost".withCString { host in
            "localhost".withCString { shost in
                libssh2_channel_direct_tcpip_ex(s, host, 15740, shost, 51000)
            }
        }
        eventChannel = "localhost".withCString { host in
            "localhost".withCString { shost in
                libssh2_channel_direct_tcpip_ex(s, host, 15740, shost, 51001)
            }
        }
        guard cmdChannel != nil, eventChannel != nil else {
            throw PtpIpError.message("direct-tcpip channel to camera's PTP/IP port failed")
        }

        // Init Command Request: 16-char random ID + UTF16LE name + null +
        // protocol version (0x00010000).
        let id = String((0..<16).map { _ in "abcdefghijklmnopqrstuvwxyz0123456789".randomElement()! })
        let name = "cameralink"
        var body = Array(id.utf8)
        for c in name.unicodeScalars { body.append(UInt8(c.value & 0xff)); body.append(0) }
        body.append(0); body.append(0)
        appendU16LE(&body, 0); appendU16LE(&body, 1)
        var frame = [UInt8]()
        appendU32LE(&frame, UInt32(8 + body.count))
        appendU32LE(&frame, 1)  // kInitCommandRequest
        frame.append(contentsOf: body)
        try writeAll(cmdChannel!, frame)

        let ack = try readFrame()
        guard ack.count >= 12, readU32LE(ack, 4) == 2 else {
            throw PtpIpError.message("Init Command Ack not received")
        }
        let connectionNumber = readU32LE(ack, 8)

        var evtFrame = [UInt8]()
        appendU32LE(&evtFrame, 12)
        appendU32LE(&evtFrame, 3)  // kInitEventRequest
        appendU32LE(&evtFrame, connectionNumber)
        try writeAll(eventChannel!, evtFrame)

        var evtAck = [UInt8](repeating: 0, count: 64)
        let evtN = evtAck.withUnsafeMutableBufferPointer { buf -> Int in
            buf.baseAddress!.withMemoryRebound(to: CChar.self, capacity: buf.count) { p in
                libssh2_channel_read_ex(eventChannel, 0, p, buf.count)
            }
        }
        guard evtN >= 8, readU32LE(evtAck, 4) == 4 else {
            throw PtpIpError.message("Init Event Ack not received")
        }

        // OpenSession + vendor priming sequence -- see protocol notes.
        let openTransid = try sendCmdRequest(dataphase: 1, opcode: 0x1002, params: [1])
        _ = try readTransaction(transid: openTransid)

        func simpleCall(_ opcode: UInt16, _ params: [UInt32]) throws {
            let t = try sendCmdRequest(dataphase: 1, opcode: opcode, params: params)
            _ = try readTransaction(transid: t)
        }
        try simpleCall(0x9201, [1, 0, 0])
        try simpleCall(0x9201, [2, 0, 0])
        try simpleCall(0x1001, [0])
        try simpleCall(0x9216, [0])
        try simpleCall(0x9202, [0x12c, 1])
        try simpleCall(0x9201, [3, 0, 0])
        do {
            let payload: [UInt8] = [
                0x03,0x00,0x01,0x00,0x08,0x00,0x02,0x02,0x00,0x00,0x00,0x00,0x00,0x01,0x02,0x00,
                0x08,0x00,0x03,0x0a,0x00,0x00,0x00,0x00,0x00,0x00,0x03,0x00,0x08,0x00,0x00,0x0c,
                0x0f,0xff,0x00,0x00,0x00,0x00,
            ]
            let t = try sendCmdRequest(dataphase: 2, opcode: 0x9214, params: [])
            try sendDataPhase(transid: t, data: payload)
            _ = try readTransaction(transid: t)
        }
        try simpleCall(0x922f, [])
        try simpleCall(0x921f, [])
        try simpleCall(0x9231, [])
        try simpleCall(0x9248, [])

        connected = true
    }

    // MARK: - GetDeviceInfo

    func getDeviceInfo() throws -> CameraDeviceInfo {
        guard connected else { throw PtpIpError.message("not connected") }
        let transid = try sendCmdRequest(dataphase: 1, opcode: 0x1001, params: [0])
        let data = try readTransaction(transid: transid)

        var pos = 0
        func need(_ n: Int) -> Bool { pos + n <= data.count }
        guard need(2 + 4 + 2) else { throw PtpIpError.message("GetDeviceInfo: truncated header") }
        pos += 8
        func readPtpString() throws -> String {
            guard need(1) else { throw PtpIpError.message("GetDeviceInfo: bad string") }
            let n = Int(data[pos]); pos += 1
            guard need(n * 2) else { throw PtpIpError.message("GetDeviceInfo: bad string") }
            var s = ""
            for i in 0..<n {
                let u = readU16LE(data, pos + i * 2)
                if u == 0 { break }
                s.append(Character(UnicodeScalar(UInt8(u & 0xff))))
            }
            pos += n * 2
            return s
        }
        _ = try readPtpString()
        guard need(2) else { throw PtpIpError.message("GetDeviceInfo: truncated") }
        pos += 2
        func skipU16Array() throws {
            guard need(4) else { throw PtpIpError.message("GetDeviceInfo: bad array") }
            let count = Int(readU32LE(data, pos)); pos += 4
            guard need(count * 2) else { throw PtpIpError.message("GetDeviceInfo: bad array") }
            pos += count * 2
        }
        try skipU16Array(); try skipU16Array(); try skipU16Array(); try skipU16Array(); try skipU16Array()

        var info = CameraDeviceInfo()
        info.manufacturer = try readPtpString()
        info.model = try readPtpString()
        info.version = try readPtpString()
        info.serialNumber = try readPtpString()
        return info
    }

    // MARK: - Properties

    func readKnownProperties() throws -> [UInt16: CameraPropertyValue] {
        guard connected else { throw PtpIpError.message("not connected") }
        let transid = try sendCmdRequest(dataphase: 1, opcode: 0x9209, params: [0, 1])
        let data = try readTransaction(transid: transid)
        guard data.count >= 4 else { throw PtpIpError.message("property table too short") }
        return PtpKnownProperties.parse(data)
    }

    func readStringProperties(_ codes: Set<UInt16>) throws -> [UInt16: String] {
        guard connected else { throw PtpIpError.message("not connected") }
        let transid = try sendCmdRequest(dataphase: 1, opcode: 0x9209, params: [0, 1])
        let data = try readTransaction(transid: transid)
        guard data.count >= 4 else { throw PtpIpError.message("property table too short") }
        return PtpKnownProperties.parseStrings(data, codes: codes)
    }

    func writeProperty(code: UInt16, width: Int, value: UInt64) throws {
        guard connected else { throw PtpIpError.message("not connected") }
        let transid = try sendCmdRequest(dataphase: 2, opcode: 0x9205, params: [UInt32(code), 1])
        var payload = [UInt8]()
        for i in 0..<width { payload.append(UInt8((value >> (8 * i)) & 0xff)) }
        try sendDataPhase(transid: transid, data: payload)
        _ = try readTransaction(transid: transid)
    }
}

// MARK: - Byte helpers

private func readU32LE(_ b: [UInt8], _ off: Int) -> UInt32 {
    UInt32(b[off]) | (UInt32(b[off+1]) << 8) | (UInt32(b[off+2]) << 16) | (UInt32(b[off+3]) << 24)
}
private func readU16LE(_ b: [UInt8], _ off: Int) -> UInt16 {
    UInt16(b[off]) | (UInt16(b[off+1]) << 8)
}
private func appendU32LE(_ out: inout [UInt8], _ v: UInt32) {
    out.append(UInt8(v & 0xff)); out.append(UInt8((v >> 8) & 0xff))
    out.append(UInt8((v >> 16) & 0xff)); out.append(UInt8((v >> 24) & 0xff))
}
private func appendU16LE(_ out: inout [UInt8], _ v: UInt16) {
    out.append(UInt8(v & 0xff)); out.append(UInt8((v >> 8) & 0xff))
}
