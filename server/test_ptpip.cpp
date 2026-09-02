// Standalone validation harness for ptpip_client -- not part of the
// server, just proves the native client works against the real camera
// before it's wired into main.cpp. Usage:
//   ./test_ptpip <camera-ip> <userId> <password>
#include "ptpip_client.h"
#include <cstdio>
#include <chrono>
#include <thread>

int main(int argc, char** argv) {
    if (argc < 4) {
        fprintf(stderr, "usage: %s <camera-ip> <userId> <password>\n", argv[0]);
        return 1;
    }
    PtpIpClient client;
    std::string error;
    printf("Connecting to %s...\n", argv[1]);
    if (!client.Connect(argv[1], argv[2], argv[3], &error)) {
        printf("Connect FAILED: %s\n", error.c_str());
        return 1;
    }
    printf("Connected.\n");

    PtpIpDeviceInfo info;
    if (client.GetDeviceInfo(&info, &error)) {
        printf("DeviceInfo: manufacturer=\"%s\" model=\"%s\" version=\"%s\" serial=\"%s\"\n",
               info.manufacturer.c_str(), info.model.c_str(), info.version.c_str(), info.serialNumber.c_str());
    } else {
        printf("GetDeviceInfo FAILED: %s\n", error.c_str());
    }

    std::vector<std::pair<uint16_t, PtpIpPropertyValue>> props;
    size_t rawLen = 0; uint16_t responseCode = 0;
    if (client.ReadKnownProperties(&props, &error, &rawLen, &responseCode)) {
        printf("Known properties:\n");
        for (auto& [code, pv] : props) {
            if (pv.found) {
                printf("  0x%04x: get=%d set=%d value=%lld\n", code, pv.getEnable, pv.setEnable, (long long)pv.value);
            } else {
                printf("  0x%04x: NOT FOUND\n", code);
            }
        }
    } else {
        printf("ReadKnownProperties FAILED: %s (rawLen=%zu responseCode=0x%04x)\n", error.c_str(), rawLen, responseCode);
    }

    if (argc > 4 && std::string(argv[4]) == "--write-test") {
        printf("Writing contrast=5...\n");
        if (!client.WriteProperty(0xD0FB, 1, (uint64_t)(uint8_t)5, &error)) {
            printf("WriteProperty FAILED: %s\n", error.c_str());
            return 1;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
        std::vector<std::pair<uint16_t, PtpIpPropertyValue>> props2;
        client.ReadKnownProperties(&props2, &error);
        for (auto& [code, pv] : props2) if (code == 0xD0FB) printf("  readback contrast=%lld\n", (long long)pv.value);

        printf("Reverting contrast=2...\n");
        if (!client.WriteProperty(0xD0FB, 1, (uint64_t)(uint8_t)2, &error)) {
            printf("  WriteProperty(revert) FAILED: %s\n", error.c_str());
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
        std::vector<std::pair<uint16_t, PtpIpPropertyValue>> props3;
        client.ReadKnownProperties(&props3, &error);
        for (auto& [code, pv] : props3) if (code == 0xD0FB) printf("  readback contrast=%lld\n", (long long)pv.value);
    }

    return 0;
}
