#pragma once
// Minimal native PTP/IP-over-SSH client for Sony's Remote Shoot Function --
// replaces Sony's CrSDK entirely (no libCr_Core.so, no vendored CrAdapter
// libraries). Built from protocol research documented separately (kept out
// of this repo -- proprietary-protocol reverse engineering for personal
// use only, see project notes). Links against a standard system libssh2,
// not Sony's vendored copy.
//
// Connection shape: plain SSH to the camera's port 22 (same
// User/Password pair as CrSDK's "Remote Shoot Function"), then a
// direct-tcpip port forward to the camera's own localhost:15740 (the
// standard PTP/IP port) -- SSH is purely a secure tunnel, the actual
// protocol on the other end is ordinary PTP/IP. Two such channels are
// opened: a Command channel and an Event channel.

#include <cstdint>
#include <string>
#include <vector>

struct PtpIpDeviceInfo {
    std::string manufacturer;
    std::string model;
    std::string version;
    std::string serialNumber;
};

// One property's current state, as read from the camera's bulk property
// table (PTP/IP vendor operation 0x9209). Only the properties this app
// actually uses are looked up -- see kKnownProperties in the .cpp.
struct PtpIpPropertyValue {
    bool found = false;
    bool getEnable = false;
    bool setEnable = false;
    int64_t value = 0;
};

class PtpIpClient {
public:
    PtpIpClient();
    ~PtpIpClient();

    // Connects, authenticates, opens both channels, and completes the
    // PTP/IP Init handshake + OpenSession. On failure, *error is set and
    // the client is left disconnected.
    bool Connect(const std::string& ip, const std::string& userId,
                 const std::string& password, std::string* error);
    void Disconnect();
    bool IsConnected() const { return connected_; }

    bool GetDeviceInfo(PtpIpDeviceInfo* out, std::string* error);

    // Reads the camera's full current property table and looks up just
    // the property codes named in kKnownProperties (server/main.cpp's
    // Recipe fields) -- not a generic property-table parser, since only
    // those properties' entry shapes have been decoded and verified.
    bool ReadKnownProperties(std::vector<std::pair<uint16_t, PtpIpPropertyValue>>* out,
                              std::string* error, size_t* rawLenOut = nullptr, uint16_t* responseCodeOut = nullptr);

    // Writes one property. width must match the property's known wire
    // encoding (1, 2, or 4 bytes) -- see kKnownProperties in the .cpp.
    bool WriteProperty(uint16_t code, int width, uint64_t value, std::string* error);

private:
    struct Impl;
    Impl* impl_;
    bool connected_ = false;
};
