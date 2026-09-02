// Plain-TCP PTP/IP client -- NO SSH, NO authentication at all. Confirmed
// 2026-08-29 by a raw Python socket test against the real a7R V while it
// was hosting its own Wi-Fi access point (the same mode used for phone
// pairing): port 15740 (the standard PTP/IP port) is exposed directly on
// that interface, and the exact same vendor priming sequence, property
// read (0x9209), and property write (0x9205) used by PtpIpWireClient
// (the SSH-tunneled client, for when the camera instead joins someone
// else's Wi-Fi as a station -- e.g. the Pi's AP) work completely
// unmodified over a bare socket. This is a second, simpler Wi-Fi sub-mode,
// not a replacement for the SSH one.
//
// Same framing/property table/byte helpers as PtpIpWireClient -- only the
// transport underneath (raw socket read/write instead of libssh2 channel
// read/write) differs.

import Foundation
#if canImport(Darwin)
import Darwin
#else
import Glibc
#endif

final class PlainPtpIpWireClient {
    private var cmdFd: Int32 = -1
    private var eventFd: Int32 = -1
    private var nextTransId: UInt32 = 2
    private var cmdBuf = [UInt8]()
    private(set) var connected = false

    func disconnect() {
        if cmdFd >= 0 { close(cmdFd) }
        if eventFd >= 0 { close(eventFd) }
        cmdFd = -1
        eventFd = -1
        cmdBuf.removeAll()
        nextTransId = 2
        connected = false
    }

    // MARK: - Low-level framing

    private static func openSocket(ip: String, port: UInt16) throws -> Int32 {
        let fd = socket(AF_INET, SOCK_STREAM, 0)
        guard fd >= 0 else { throw PtpIpError.message("socket() failed") }
        var addr = sockaddr_in()
        addr.sin_family = sa_family_t(AF_INET)
        addr.sin_port = port.bigEndian
        guard inet_pton(AF_INET, ip, &addr.sin_addr) == 1 else {
            close(fd)
            throw PtpIpError.message("invalid camera IP")
        }
        let result = withUnsafePointer(to: &addr) { p -> Int32 in
            p.withMemoryRebound(to: sockaddr.self, capacity: 1) { sp in
                Foundation.connect(fd, sp, socklen_t(MemoryLayout<sockaddr_in>.size))
            }
        }
        guard result == 0 else {
            close(fd)
            throw PtpIpError.message("TCP connect to camera:\(port) failed")
        }
        return fd
    }

    private func writeAll(_ fd: Int32, _ data: [UInt8]) throws {
        var sent = 0
        try data.withUnsafeBufferPointer { buf in
            while sent < data.count {
                let n = send(fd, buf.baseAddress!.advanced(by: sent), data.count - sent, 0)
                if n < 0 {
                    if errno == EINTR { continue }
                    throw PtpIpError.message("socket write failed (errno \(errno))")
                }
                sent += n
            }
        }
    }

    private func recvSome(_ fd: Int32, into tmp: inout [UInt8]) throws -> Int {
        let n = tmp.withUnsafeMutableBufferPointer { buf in
            recv(fd, buf.baseAddress, buf.count, 0)
        }
        if n < 0 {
            if errno == EINTR { return 0 }
            throw PtpIpError.message("socket read failed (errno \(errno))")
        }
        if n == 0 { throw PtpIpError.message("socket closed") }
        return n
    }

    private func readFrame() throws -> [UInt8] {
        var tmp = [UInt8](repeating: 0, count: 16384)
        while true {
            if cmdBuf.count >= 8 {
                let len = readU32LE_(cmdBuf, 0)
                if len < 8 { throw PtpIpError.message("invalid PTP/IP frame length") }
                if cmdBuf.count >= Int(len) {
                    let frame = Array(cmdBuf[0..<Int(len)])
                    cmdBuf.removeFirst(Int(len))
                    return frame
                }
            }
            let n = try recvSome(cmdFd, into: &tmp)
            if n > 0 { cmdBuf.append(contentsOf: tmp[0..<n]) }
        }
    }

    private func sendCmdRequest(dataphase: UInt32, opcode: UInt16, params: [UInt32]) throws -> UInt32 {
        let transid = nextTransId
        nextTransId += 1
        var body = [UInt8]()
        appendU32LE_(&body, dataphase)
        appendU16LE_(&body, opcode)
        appendU32LE_(&body, transid)
        for p in params { appendU32LE_(&body, p) }
        var frame = [UInt8]()
        appendU32LE_(&frame, UInt32(8 + body.count))
        appendU32LE_(&frame, 6)  // kCmdRequest
        frame.append(contentsOf: body)
        try writeAll(cmdFd, frame)
        return transid
    }

    private func sendDataPhase(transid: UInt32, data: [UInt8]) throws {
        var start = [UInt8]()
        appendU32LE_(&start, 20)
        appendU32LE_(&start, 9)  // kStartDataPacket
        appendU32LE_(&start, transid)
        appendU32LE_(&start, UInt32(data.count))
        appendU32LE_(&start, 0)
        try writeAll(cmdFd, start)

        var dataPkt = [UInt8]()
        appendU32LE_(&dataPkt, UInt32(12 + data.count))
        appendU32LE_(&dataPkt, 10)  // kDataPacket
        appendU32LE_(&dataPkt, transid)
        dataPkt.append(contentsOf: data)
        try writeAll(cmdFd, dataPkt)

        var end = [UInt8]()
        appendU32LE_(&end, 12)
        appendU32LE_(&end, 12)  // kEndDataPacket
        appendU32LE_(&end, transid)
        try writeAll(cmdFd, end)
    }

    @discardableResult
    private func readTransaction(transid: UInt32) throws -> [UInt8] {
        var outData = [UInt8]()
        for _ in 0..<100_000 {
            let frame = try readFrame()
            if frame.count < 8 { continue }
            let type = readU32LE_(frame, 4)
            if type == 10, frame.count >= 12 {  // DataPacket
                let tid = readU32LE_(frame, 8)
                if tid == transid { outData.append(contentsOf: frame[12...]) }
            } else if type == 7, frame.count >= 14 {  // CmdResponse
                let tid = readU32LE_(frame, 10)
                if tid == transid {
                    let code = readU16LE_(frame, 8)
                    guard code == 0x2001 else {
                        throw PtpIpError.message(String(format: "camera rejected the request (PTP response code 0x%04x)", code))
                    }
                    return outData
                }
            }
        }
        throw PtpIpError.message("transaction did not complete")
    }

    // MARK: - Connect (no auth -- knowing the Wi-Fi password is the only gate)

    func connect(ip: String) throws {
        disconnect()

        cmdFd = try Self.openSocket(ip: ip, port: 15740)
        eventFd = try Self.openSocket(ip: ip, port: 15740)

        let id = String((0..<16).map { _ in "abcdefghijklmnopqrstuvwxyz0123456789".randomElement()! })
        let name = "cameralink"
        var body = Array(id.utf8)
        for c in name.unicodeScalars { body.append(UInt8(c.value & 0xff)); body.append(0) }
        body.append(0); body.append(0)
        appendU16LE_(&body, 0); appendU16LE_(&body, 1)
        var frame = [UInt8]()
        appendU32LE_(&frame, UInt32(8 + body.count))
        appendU32LE_(&frame, 1)  // kInitCommandRequest
        frame.append(contentsOf: body)
        try writeAll(cmdFd, frame)

        let ack = try readFrame()
        guard ack.count >= 12, readU32LE_(ack, 4) == 2 else {
            throw PtpIpError.message("Init Command Ack not received")
        }
        let connectionNumber = readU32LE_(ack, 8)

        var evtFrame = [UInt8]()
        appendU32LE_(&evtFrame, 12)
        appendU32LE_(&evtFrame, 3)  // kInitEventRequest
        appendU32LE_(&evtFrame, connectionNumber)
        try writeAll(eventFd, evtFrame)

        var evtAck = [UInt8](repeating: 0, count: 64)
        let evtN = try recvSome(eventFd, into: &evtAck)
        guard evtN >= 8, readU32LE_(evtAck, 4) == 4 else {
            throw PtpIpError.message("Init Event Ack not received")
        }

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
                let u = readU16LE_(data, pos + i * 2)
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
            let count = Int(readU32LE_(data, pos)); pos += 4
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

// MARK: - Byte helpers (suffixed `_` to avoid colliding with
// PtpIpWireClient.swift's private helpers of the same name in another file)

private func readU32LE_(_ b: [UInt8], _ off: Int) -> UInt32 {
    UInt32(b[off]) | (UInt32(b[off+1]) << 8) | (UInt32(b[off+2]) << 16) | (UInt32(b[off+3]) << 24)
}
private func readU16LE_(_ b: [UInt8], _ off: Int) -> UInt16 {
    UInt16(b[off]) | (UInt16(b[off+1]) << 8)
}
private func appendU32LE_(_ out: inout [UInt8], _ v: UInt32) {
    out.append(UInt8(v & 0xff)); out.append(UInt8((v >> 8) & 0xff))
    out.append(UInt8((v >> 16) & 0xff)); out.append(UInt8((v >> 24) & 0xff))
}
private func appendU16LE_(_ out: inout [UInt8], _ v: UInt16) {
    out.append(UInt8(v & 0xff)); out.append(UInt8((v >> 8) & 0xff))
}
