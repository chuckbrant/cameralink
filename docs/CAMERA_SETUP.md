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

## 3. Give the camera an IP address

cameralink's access point runs a normal DHCP server (`10.42.0.10`–
`10.42.0.254`), so on most cameras **Auto** just works — join the network
and the camera gets an address automatically.

A **static** address is optional but convenient (the address won't change
between sessions, so you don't have to check it every time). If the
camera's Wi-Fi settings support **IP Address Setting: Manual**, you can
set:

| Field | Value |
|---|---|
| IP Address | `10.42.0.50` (anything in `10.42.0.2`–`10.42.0.254` works — just avoid `10.42.0.1`, that's the Pi itself) |
| Subnet Mask | `255.255.255.0` |
| Default Gateway | `10.42.0.1` |

Not every camera's Wi-Fi remote-control feature supports manual IP
entry — if yours doesn't, Auto/DHCP is the only option, and that's fine;
just check the address it was assigned before connecting in step 4.

## 4. Connect from cameralink

Use the camera's MAC address, User, Password, and Fingerprint from step 1,
plus its IP address (static from step 3, or whatever DHCP assigned —
check the camera's own network status screen, or look at the Pi's leases
with `cat /var/lib/NetworkManager/dnsmasq-wlan0.leases`), either through
the web UI's "Connect (Network)" form or directly against the API:

```
curl -X POST http://<pi-ip>:8080/api/connect/network \
  -H "Content-Type: application/json" \
  -d '{"ip":"10.42.0.50","mac":"AA:BB:CC:DD:EE:FF","userId":"...","password":"..."}'
```

## Known limitation: USB data connections and Wi-Fi are mutually exclusive

Power over USB is fine — a wall charger/power brick doesn't interfere
with Remote Shoot Function at all. The conflict is specifically a **USB
data connection to a computer**: if the camera is plugged into a computer
over USB (even just to charge, since a computer's USB port still
negotiates a data connection unless the camera's own USB mode is set to
charge-only), it defaults to that USB connection and Wi-Fi won't work —
the network connection will fail outright, since the camera won't run
both remote-control paths at once. Unplug the camera from the computer
(or set its USB connection mode to charge-only, if it has one) before
connecting over Wi-Fi.
