#include "ptpip_client.h"

#include <libssh2.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cstring>
#include <cstdio>
#include <ctime>
#include <map>
#include <random>

namespace {

constexpr int kPtpIpPort = 15740;

enum PtpIpPacketType : uint32_t {
    kInitCommandRequest = 1,
    kInitCommandAck = 2,
    kInitEventRequest = 3,
    kInitEventAck = 4,
    kInitFail = 5,
    kCmdRequest = 6,
    kCmdResponse = 7,
    kEvent = 8,
    kStartDataPacket = 9,
    kDataPacket = 10,
    kCancelTransaction = 11,
    kEndDataPacket = 12,
};

// One entry per Recipe field cameralink actually reads/writes. width is
// the property's wire value size in bytes; signed applies only to width 1
// (the int8 Creative Look sub-parameters -- everything else is unsigned
// on this camera). See ../../../cameralink-research/PTPIP_PROTOCOL_NOTES.md
// (kept outside this repo) for how each of these was derived and verified.
struct KnownProperty {
    const char* name;
    uint16_t code;
    int width;
    bool signedValue;
};

const KnownProperty kKnownProperties[] = {
    {"preset",        0xD0FA, 2, false},
    {"contrast",      0xD0FB, 1, true},
    {"highlights",    0xD0FC, 1, true},
    {"shadows",       0xD0FD, 1, true},
    {"fade",          0xD0FE, 1, true},
    {"saturation",    0xD0FF, 1, true},
    {"sharpness",     0xD100, 1, true},
    {"sharpnessRange",0xD101, 1, true},
    {"clarity",       0xD102, 1, true},
    {"whiteBalance",  0x5005, 2, false},
    {"colorTempK",    0xD20F, 2, false},
    {"colorFilterAB", 0xD21C, 1, false},
    {"colorFilterGM", 0xD210, 1, false},
    {"aspectRatio",   0xD211, 1, false},
    {"fileType",      0xD253, 1, false},
    {"iso",           0xD21E, 4, false},
};

uint32_t ReadU32LE(const uint8_t* p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
uint16_t ReadU16LE(const uint8_t* p) { return (uint16_t)p[0] | ((uint16_t)p[1] << 8); }

void AppendU32LE(std::vector<uint8_t>* out, uint32_t v) {
    out->push_back(v & 0xff); out->push_back((v >> 8) & 0xff);
    out->push_back((v >> 16) & 0xff); out->push_back((v >> 24) & 0xff);
}
void AppendU16LE(std::vector<uint8_t>* out, uint16_t v) {
    out->push_back(v & 0xff); out->push_back((v >> 8) & 0xff);
}

// The camera's embedded SSH server only offers "keyboard-interactive"
// auth (confirmed via libssh2_userauth_list -- plain "password" auth is
// rejected outright, error -18). libssh2's keyboard-interactive callback
// has no user-data parameter, only the session's abstract pointer, so the
// password is stashed here for the duration of one Connect() call.
thread_local const char* g_kbdInteractivePassword = nullptr;

void KeyboardInteractiveCallback(const char*, int, const char*, int, int numPrompts,
                                  const LIBSSH2_USERAUTH_KBDINT_PROMPT*,
                                  LIBSSH2_USERAUTH_KBDINT_RESPONSE* responses, void**) {
    for (int i = 0; i < numPrompts; i++) {
        const char* pw = g_kbdInteractivePassword ? g_kbdInteractivePassword : "";
        size_t len = strlen(pw);
        responses[i].text = (char*)malloc(len);
        memcpy(responses[i].text, pw, len);
        responses[i].length = (unsigned int)len;
    }
}

std::string RandomAlnum(int n) {
    static const char chars[] = "abcdefghijklmnopqrstuvwxyz0123456789";
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dist(0, (int)sizeof(chars) - 2);
    std::string s;
    for (int i = 0; i < n; i++) s += chars[dist(gen)];
    return s;
}

}  // namespace

struct PtpIpClient::Impl {
    int sock = -1;
    LIBSSH2_SESSION* session = nullptr;
    LIBSSH2_CHANNEL* cmdChannel = nullptr;
    LIBSSH2_CHANNEL* eventChannel = nullptr;
    uint32_t nextTransId = 2;  // 1 is implicitly used by the Init handshake itself
    std::vector<uint8_t> cmdBuf;    // leftover bytes not yet consumed as a full frame

    void Reset() {
        if (cmdChannel) { libssh2_channel_free(cmdChannel); cmdChannel = nullptr; }
        if (eventChannel) { libssh2_channel_free(eventChannel); eventChannel = nullptr; }
        if (session) { libssh2_session_disconnect(session, "bye"); libssh2_session_free(session); session = nullptr; }
        if (sock >= 0) { close(sock); sock = -1; }
        cmdBuf.clear();
        nextTransId = 2;
    }

    // Blocks until at least one full PTP/IP frame is available on
    // cmdChannel, then returns it (header included) and removes it from
    // cmdBuf. Session must be in blocking mode.
    bool ReadFrame(std::vector<uint8_t>* frame, std::string* error) {
        char tmp[16384];
        while (true) {
            if (cmdBuf.size() >= 8) {
                uint32_t len = ReadU32LE(cmdBuf.data());
                if (len < 8) { if (error) *error = "invalid PTP/IP frame length"; return false; }
                if (cmdBuf.size() >= len) {
                    frame->assign(cmdBuf.begin(), cmdBuf.begin() + len);
                    cmdBuf.erase(cmdBuf.begin(), cmdBuf.begin() + len);
                    return true;
                }
            }
            ssize_t n = libssh2_channel_read(cmdChannel, tmp, sizeof(tmp));
            if (n < 0) {
                if (n == LIBSSH2_ERROR_EAGAIN) continue;
                if (error) *error = "channel read failed";
                return false;
            }
            if (n == 0) {
                if (libssh2_channel_eof(cmdChannel)) { if (error) *error = "channel closed"; return false; }
                continue;
            }
            cmdBuf.insert(cmdBuf.end(), tmp, tmp + n);
        }
    }

    bool WriteAll(LIBSSH2_CHANNEL* ch, const uint8_t* data, size_t len, std::string* error) {
        size_t sent = 0;
        while (sent < len) {
            ssize_t n = libssh2_channel_write(ch, (const char*)data + sent, len - sent);
            if (n < 0) {
                if (n == LIBSSH2_ERROR_EAGAIN) continue;
                if (error) *error = "channel write failed";
                return false;
            }
            sent += n;
        }
        return true;
    }

    // Sends a Cmd Request (PTP/IP type 6): DataPhaseInfo, OpCode,
    // TransactionID, then each of params as a 4-byte parameter.
    uint32_t SendCmdRequest(uint32_t dataphase, uint16_t opcode, const std::vector<uint32_t>& params, std::string* error, bool* ok) {
        uint32_t transid = nextTransId++;
        std::vector<uint8_t> body;
        AppendU32LE(&body, dataphase);
        AppendU16LE(&body, opcode);
        AppendU32LE(&body, transid);
        for (uint32_t p : params) AppendU32LE(&body, p);

        std::vector<uint8_t> frame;
        AppendU32LE(&frame, (uint32_t)(8 + body.size()));
        AppendU32LE(&frame, kCmdRequest);
        frame.insert(frame.end(), body.begin(), body.end());
        *ok = WriteAll(cmdChannel, frame.data(), frame.size(), error);
        return transid;
    }

    bool SendDataPhase(uint32_t transid, const std::vector<uint8_t>& data, std::string* error) {
        std::vector<uint8_t> start;
        AppendU32LE(&start, 20);
        AppendU32LE(&start, kStartDataPacket);
        AppendU32LE(&start, transid);
        // TotalDataLength is an 8-byte field.
        AppendU32LE(&start, (uint32_t)data.size());
        AppendU32LE(&start, 0);
        if (!WriteAll(cmdChannel, start.data(), start.size(), error)) return false;

        std::vector<uint8_t> dataPkt;
        AppendU32LE(&dataPkt, (uint32_t)(12 + data.size()));
        AppendU32LE(&dataPkt, kDataPacket);
        AppendU32LE(&dataPkt, transid);
        dataPkt.insert(dataPkt.end(), data.begin(), data.end());
        if (!WriteAll(cmdChannel, dataPkt.data(), dataPkt.size(), error)) return false;

        std::vector<uint8_t> end;
        AppendU32LE(&end, 12);
        AppendU32LE(&end, kEndDataPacket);
        AppendU32LE(&end, transid);
        return WriteAll(cmdChannel, end.data(), end.size(), error);
    }

    // Drives one full PTP/IP transaction to completion: reads frames until
    // this transid's CmdResponse arrives, accumulating any Data packets
    // along the way. Matches the standard shape confirmed in the protocol
    // notes: CmdRequest -> [StartData/Data.../EndData] -> CmdResponse.
    bool ReadTransaction(uint32_t transid, std::vector<uint8_t>* outData, uint16_t* responseCode, std::string* error) {
        outData->clear();
        for (int guard = 0; guard < 100000; guard++) {
            std::vector<uint8_t> frame;
            if (!ReadFrame(&frame, error)) return false;
            if (frame.size() < 8) continue;
            uint32_t type = ReadU32LE(&frame[4]);
            if (type == kDataPacket && frame.size() >= 12) {
                uint32_t tid = ReadU32LE(&frame[8]);
                if (tid == transid) outData->insert(outData->end(), frame.begin() + 12, frame.end());
            } else if (type == kCmdResponse && frame.size() >= 14) {
                uint16_t code = ReadU16LE(&frame[8]);
                uint32_t tid = ReadU32LE(&frame[10]);
                if (tid == transid) {
                    if (responseCode) *responseCode = code;
                    return true;
                }
            }
            // StartData/EndData/other transactions' frames: nothing to do,
            // keep reading.
        }
        if (error) *error = "transaction did not complete (guard limit hit)";
        return false;
    }
};

PtpIpClient::PtpIpClient() : impl_(new Impl()) {}
PtpIpClient::~PtpIpClient() { Disconnect(); delete impl_; }

void PtpIpClient::Disconnect() {
    if (impl_) impl_->Reset();
    connected_ = false;
}

bool PtpIpClient::Connect(const std::string& ip, const std::string& userId,
                           const std::string& password, std::string* error) {
    Disconnect();

    impl_->sock = socket(AF_INET, SOCK_STREAM, 0);
    if (impl_->sock < 0) { if (error) *error = "socket() failed"; return false; }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(22);
    if (inet_pton(AF_INET, ip.c_str(), &addr.sin_addr) != 1) {
        if (error) *error = "invalid camera IP";
        return false;
    }
    if (connect(impl_->sock, (sockaddr*)&addr, sizeof(addr)) != 0) {
        if (error) *error = "TCP connect to camera:22 failed";
        return false;
    }

    static bool sshInited = false;
    if (!sshInited) { libssh2_init(0); sshInited = true; }

    impl_->session = libssh2_session_init();
    if (!impl_->session) { if (error) *error = "libssh2_session_init failed"; return false; }
    libssh2_session_set_blocking(impl_->session, 1);

    if (libssh2_session_handshake(impl_->session, impl_->sock) != 0) {
        if (error) *error = "SSH handshake failed";
        return false;
    }
    g_kbdInteractivePassword = password.c_str();
    int authRc = libssh2_userauth_keyboard_interactive(impl_->session, userId.c_str(), &KeyboardInteractiveCallback);
    g_kbdInteractivePassword = nullptr;
    if (authRc != 0) {
        if (error) *error = "SSH auth failed (bad user/password)";
        return false;
    }

    impl_->cmdChannel = libssh2_channel_direct_tcpip_ex(impl_->session, "localhost", kPtpIpPort, "localhost", 51000);
    impl_->eventChannel = libssh2_channel_direct_tcpip_ex(impl_->session, "localhost", kPtpIpPort, "localhost", 51001);
    if (!impl_->cmdChannel || !impl_->eventChannel) {
        if (error) *error = "direct-tcpip channel to camera's PTP/IP port failed";
        return false;
    }

    // Init Command Request: GUID(16, opaque -- a random alnum string
    // works fine, Sony's own CrSDK does the same) + friendly name
    // (UTF16LE, null-terminated) + protocol version (4 bytes, 1.0).
    {
        std::string id = RandomAlnum(16);
        std::string name = "cameralink";
        std::vector<uint8_t> body(id.begin(), id.end());
        for (char c : name) { body.push_back((uint8_t)c); body.push_back(0); }
        body.push_back(0); body.push_back(0);  // name null terminator
        AppendU16LE(&body, 0); AppendU16LE(&body, 1);  // protocol version 0x00010000

        std::vector<uint8_t> frame;
        AppendU32LE(&frame, (uint32_t)(8 + body.size()));
        AppendU32LE(&frame, kInitCommandRequest);
        frame.insert(frame.end(), body.begin(), body.end());
        if (!impl_->WriteAll(impl_->cmdChannel, frame.data(), frame.size(), error)) return false;
    }

    std::vector<uint8_t> ack;
    if (!impl_->ReadFrame(&ack, error)) return false;
    if (ack.size() < 12 || ReadU32LE(&ack[4]) != kInitCommandAck) {
        if (error) *error = "Init Command Ack not received";
        return false;
    }
    uint32_t connectionNumber = ReadU32LE(&ack[8]);

    // Init Event Request on the second channel, using the connection
    // number the camera assigned us above.
    {
        std::vector<uint8_t> frame;
        AppendU32LE(&frame, 12);
        AppendU32LE(&frame, kInitEventRequest);
        AppendU32LE(&frame, connectionNumber);
        if (!impl_->WriteAll(impl_->eventChannel, frame.data(), frame.size(), error)) return false;
    }
    {
        char tmp[64];
        ssize_t n = libssh2_channel_read(impl_->eventChannel, tmp, sizeof(tmp));
        if (n < 8 || ReadU32LE((uint8_t*)tmp + 4) != kInitEventAck) {
            if (error) *error = "Init Event Ack not received";
            return false;
        }
    }

    // OpenSession.
    bool ok = false;
    uint32_t transid = impl_->SendCmdRequest(1, 0x1002, {1}, error, &ok);
    if (!ok) return false;
    std::vector<uint8_t> data;
    uint16_t responseCode = 0;
    if (!impl_->ReadTransaction(transid, &data, &responseCode, error)) return false;

    // Vendor session-priming sequence: the camera rejects 0x9209 (the
    // property-table read) with PTP response 0x2005 (OperationNotSupported)
    // unless this exact sequence of vendor calls runs first. Byte-for-byte
    // replica of what CrSDK itself sends before its first 0x9209 call, per
    // a real capture -- see protocol notes. The individual calls' meanings
    // aren't decoded; only that this sequence, in this order, is required.
    auto simpleCall = [&](uint16_t opcode, const std::vector<uint32_t>& params) -> bool {
        bool callOk = false;
        uint32_t t = impl_->SendCmdRequest(1, opcode, params, error, &callOk);
        if (!callOk) return false;
        std::vector<uint8_t> d;
        uint16_t rc = 0;
        return impl_->ReadTransaction(t, &d, &rc, error);
    };
    if (!simpleCall(0x9201, {1, 0, 0})) return false;
    if (!simpleCall(0x9201, {2, 0, 0})) return false;
    if (!simpleCall(0x1001, {0})) return false;  // GetDeviceInfo again, as CrSDK does
    if (!simpleCall(0x9216, {0})) return false;
    if (!simpleCall(0x9202, {0x12c, 1})) return false;
    if (!simpleCall(0x9201, {3, 0, 0})) return false;
    {
        // 0x9214 is a write (dataphase=2) with a fixed 38-byte payload
        // whose meaning isn't decoded -- replicated verbatim from capture.
        static const uint8_t kPayload9214[] = {
            0x03,0x00,0x01,0x00,0x08,0x00,0x02,0x02,0x00,0x00,0x00,0x00,0x00,0x01,0x02,0x00,
            0x08,0x00,0x03,0x0a,0x00,0x00,0x00,0x00,0x00,0x00,0x03,0x00,0x08,0x00,0x00,0x0c,
            0x0f,0xff,0x00,0x00,0x00,0x00,
        };
        bool callOk = false;
        uint32_t t = impl_->SendCmdRequest(2, 0x9214, {}, error, &callOk);
        if (!callOk) return false;
        std::vector<uint8_t> payload(kPayload9214, kPayload9214 + sizeof(kPayload9214));
        if (!impl_->SendDataPhase(t, payload, error)) return false;
        std::vector<uint8_t> d;
        uint16_t rc = 0;
        if (!impl_->ReadTransaction(t, &d, &rc, error)) return false;
    }
    if (!simpleCall(0x922f, {})) return false;
    if (!simpleCall(0x921f, {})) return false;
    if (!simpleCall(0x9231, {})) return false;
    if (!simpleCall(0x9248, {})) return false;

    connected_ = true;
    return true;
}

bool PtpIpClient::GetDeviceInfo(PtpIpDeviceInfo* out, std::string* error) {
    if (!connected_) { if (error) *error = "not connected"; return false; }
    bool ok = false;
    uint32_t transid = impl_->SendCmdRequest(1, 0x1001, {0}, error, &ok);
    if (!ok) return false;
    std::vector<uint8_t> data;
    uint16_t responseCode = 0;
    if (!impl_->ReadTransaction(transid, &data, &responseCode, error)) return false;

    // Standard PTP DeviceInfo dataset. We only need the four trailing
    // strings, so skip everything before them generically: fixed fields,
    // then three variable-length uint16 arrays (OperationsSupported,
    // EventsSupported, DevicePropertiesSupported), then two more
    // (CaptureFormats, ImageFormats), then the strings.
    size_t pos = 0;
    auto need = [&](size_t n) { return pos + n <= data.size(); };
    if (!need(2 + 4 + 2)) { if (error) *error = "GetDeviceInfo: truncated header"; return false; }
    pos += 2 + 4 + 2;  // StandardVersion, VendorExtensionID, VendorExtensionVersion
    // VendorExtensionDesc: PTP string.
    auto readPtpString = [&](std::string* s) -> bool {
        if (!need(1)) return false;
        uint8_t n = data[pos]; pos += 1;
        if (!need((size_t)n * 2)) return false;
        std::vector<uint16_t> units(n);
        for (uint8_t i = 0; i < n; i++) units[i] = ReadU16LE(&data[pos + i * 2]);
        pos += (size_t)n * 2;
        s->clear();
        for (uint16_t u : units) { if (u == 0) break; s->push_back((char)(u & 0xff)); }
        return true;
    };
    std::string vendorDesc;
    if (!readPtpString(&vendorDesc)) { if (error) *error = "GetDeviceInfo: bad vendor desc"; return false; }
    if (!need(2)) { if (error) *error = "GetDeviceInfo: truncated"; return false; }
    pos += 2;  // FunctionalMode

    auto skipU16Array = [&]() -> bool {
        if (!need(4)) return false;
        uint32_t count = ReadU32LE(&data[pos]); pos += 4;
        if (!need((size_t)count * 2)) return false;
        pos += (size_t)count * 2;
        return true;
    };
    if (!skipU16Array()) { if (error) *error = "GetDeviceInfo: bad OperationsSupported"; return false; }
    if (!skipU16Array()) { if (error) *error = "GetDeviceInfo: bad EventsSupported"; return false; }
    if (!skipU16Array()) { if (error) *error = "GetDeviceInfo: bad PropertiesSupported"; return false; }
    if (!skipU16Array()) { if (error) *error = "GetDeviceInfo: bad CaptureFormats"; return false; }
    if (!skipU16Array()) { if (error) *error = "GetDeviceInfo: bad ImageFormats"; return false; }

    if (!readPtpString(&out->manufacturer)) { if (error) *error = "GetDeviceInfo: bad manufacturer"; return false; }
    if (!readPtpString(&out->model)) { if (error) *error = "GetDeviceInfo: bad model"; return false; }
    if (!readPtpString(&out->version)) { if (error) *error = "GetDeviceInfo: bad version"; return false; }
    if (!readPtpString(&out->serialNumber)) { if (error) *error = "GetDeviceInfo: bad serial"; return false; }
    return true;
}

bool PtpIpClient::ReadKnownProperties(std::vector<std::pair<uint16_t, PtpIpPropertyValue>>* out, std::string* error, size_t* rawLenOut, uint16_t* responseCodeOut) {
    if (!connected_) { if (error) *error = "not connected"; return false; }
    out->clear();

    // params={0,1}: "give me everything" -- see protocol notes. {1,1} is
    // the much smaller "what changed since last time" variant, not useful
    // for a fresh read.
    bool ok = false;
    uint32_t transid = impl_->SendCmdRequest(1, 0x9209, {0, 1}, error, &ok);
    if (!ok) return false;
    std::vector<uint8_t> data;
    uint16_t responseCode = 0;
    if (!impl_->ReadTransaction(transid, &data, &responseCode, error)) return false;
    if (rawLenOut) *rawLenOut = data.size();
    if (responseCodeOut) *responseCodeOut = responseCode;
    if (data.size() < 4) { if (error) *error = "property table too short"; return false; }

    for (const auto& kp : kKnownProperties) {
        PtpIpPropertyValue pv;
        // Targeted scan for this property's code, matching the expected
        // width's type byte set, rather than a full generic table walk --
        // deliberate: only these ~16 properties' entry shapes are decoded
        // and verified. See protocol notes for why this is enough.
        for (size_t i = 4; i + 8 <= data.size(); i++) {
            if (ReadU16LE(&data[i]) != kp.code) continue;
            uint8_t type = data[i + 2];
            int expectedWidth = (type == 0x01 || type == 0x02) ? 1 : (type == 0x04 ? 2 : (type == 0x06 ? 4 : 0));
            if (expectedWidth != kp.width) continue;
            if (i + 7 + (size_t)kp.width > data.size()) continue;
            size_t valueOff = i + (kp.width == 1 ? 7 : (kp.width == 2 ? 8 : 10));
            if (valueOff + kp.width > data.size()) continue;
            uint64_t raw = 0;
            for (int b = kp.width - 1; b >= 0; b--) raw = (raw << 8) | data[valueOff + b];
            int64_t value = (int64_t)raw;
            if (kp.signedValue) {
                if (kp.width == 1) value = (int8_t)(uint8_t)raw;
                else if (kp.width == 2) value = (int16_t)(uint16_t)raw;
                else if (kp.width == 4) value = (int32_t)(uint32_t)raw;
            }
            pv.found = true;
            pv.getEnable = data[i + 4] != 0;
            pv.setEnable = data[i + 5] != 0;
            pv.value = value;
            break;
        }
        out->push_back({kp.code, pv});
    }
    return true;
}

bool PtpIpClient::WriteProperty(uint16_t code, int width, uint64_t value, std::string* error) {
    if (!connected_) { if (error) *error = "not connected"; return false; }
    bool ok = false;
    uint32_t transid = impl_->SendCmdRequest(2, 0x9205, {code, 1}, error, &ok);
    if (!ok) return false;

    std::vector<uint8_t> payload;
    for (int i = 0; i < width; i++) payload.push_back((uint8_t)((value >> (8 * i)) & 0xff));
    if (!impl_->SendDataPhase(transid, payload, error)) return false;

    std::vector<uint8_t> data;
    uint16_t responseCode = 0;
    return impl_->ReadTransaction(transid, &data, &responseCode, error);
}
