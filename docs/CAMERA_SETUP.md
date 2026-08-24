# Camera setup

cameralink talks to the camera over Sony's **Remote Shoot Function** —
a Wi-Fi + SSH-based remote-control channel built into recent Sony bodies
(confirmed working on the a7R V / ILCE-7RM5; likely works on other
CrSDK-supported bodies with the same feature).

## 1. Turn on Remote Shoot Function

On the camera: **MENU → Network → Cnct./Remote Sht. → Remote Shoot
Function** → On.

The camera will display an **Access Authen. Info** screen showing:
- **MAC Address** — the camera's own Wi-Fi MAC
- **User** and **Password** — a randomly generated pairing credential
- **Fingerprint** — a base64-encoded SSH host key fingerprint

Write all four down (or photograph the screen — the text is small and easy
to mistranscribe). These stay stable across reconnect attempts and camera
reboots, but do regenerate if you turn Remote Shoot Function off and back
on, or update the camera's firmware.

## 2. Join the camera to the Pi's access point

On the camera: **MENU → Wireless → Access Point Set.** → select the
cameralink Pi's SSID (whatever you set `AP_SSID` to in
`scripts/setup-pi.sh`) → enter the AP password.

## 3. Give the camera a static IP

cameralink's access point deliberately runs **without a DHCP server** (see
[ARCHITECTURE.md](ARCHITECTURE.md#no-dhcp-on-the-access-point) for why), so
the camera needs a manually-configured address rather than Auto:

When prompted for **IP Address Setting**, choose **Manual** and enter:

| Field | Value |
|---|---|
| IP Address | `10.42.0.50` (anything in `10.42.0.2`–`10.42.0.254` works — just avoid `10.42.0.1`, that's the Pi itself) |
| Subnet Mask | `255.255.255.0` |
| Default Gateway | `10.42.0.1` |

## 4. Connect from cameralink

Use the camera's MAC address, User, Password, and Fingerprint from step 1,
plus the static IP from step 3, either through the web UI's "Connect
(Network)" form or directly against the API:

```
curl -X POST http://<pi-ip>:8080/api/connect/network \
  -H "Content-Type: application/json" \
  -d '{"ip":"10.42.0.50","mac":"AA:BB:CC:DD:EE:FF","userId":"...","password":"..."}'
```

## Known limitation: USB and Wi-Fi are mutually exclusive

If the camera has a USB cable plugged in (even just for charging) while
also trying to use Remote Shoot Function, the network connection will fail
outright — the camera prioritizes USB and won't run both remote-control
paths at once. Unplug USB from the camera before connecting over Wi-Fi.
