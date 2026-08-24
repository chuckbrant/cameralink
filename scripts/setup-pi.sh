#!/usr/bin/env bash
# One-time setup for a fresh Raspberry Pi OS (Bookworm+, NetworkManager)
# install on a Pi Zero 2 W. Run this ON THE PI over SSH, as the pi user
# (needs sudo). See docs/ARCHITECTURE.md for why each piece exists.
#
# What this does:
#   1. Enables USB gadget mode (dwc2 + g_ether) so the Pi presents itself
#      as a USB-Ethernet device to whatever it's plugged into (Mac, iPad).
#   2. Gives the USB gadget interface (usb0) a static IP instead of DHCP
#      -- DHCP over a link nothing serves DHCP on causes exactly the kind
#      of intermittent USB flakiness this project hit early on.
#   3. Creates a WiFi access point (with its own DHCP server, so cameras
#      that can't do manual/static IP entry over WiFi still work -- see
#      docs/ARCHITECTURE.md) that survives reboots.
#
# Placeholders to fill in below before running:
#   AP_SSID, AP_PASSWORD  -- your own choice, WPA2, keep AP_PASSWORD 8+ chars
set -euo pipefail

AP_SSID="YOUR_AP_SSID"
AP_PASSWORD="YOUR_AP_PASSWORD"
AP_CON_NAME="CameraBridgeAP"
USB_STATIC_IP="192.168.7.2/24"

if [ "$AP_SSID" = "YOUR_AP_SSID" ]; then
  echo "error: edit this script and set AP_SSID / AP_PASSWORD first." >&2
  exit 1
fi

echo "==> Enabling USB gadget mode (dwc2 + g_ether)"
CONFIG_TXT=/boot/firmware/config.txt
CMDLINE_TXT=/boot/firmware/cmdline.txt

if ! grep -q 'dtoverlay=dwc2,dr_mode=peripheral' "$CONFIG_TXT"; then
  sudo cp "$CONFIG_TXT" "$CONFIG_TXT.orig-backup"
  {
    echo ""
    echo "# USB Gadget mode (cameralink) -- presents this Pi as a USB"
    echo "# Ethernet device when its USB data port is plugged into a host."
    echo "dtoverlay=dwc2,dr_mode=peripheral"
  } | sudo tee -a "$CONFIG_TXT" > /dev/null
fi

if ! grep -q 'modules-load=dwc2,g_ether' "$CMDLINE_TXT"; then
  sudo cp "$CMDLINE_TXT" "$CMDLINE_TXT.orig-backup"
  sudo sed -i 's/rootwait /rootwait modules-load=dwc2,g_ether /' "$CMDLINE_TXT"
fi

echo "==> Configuring usb0 with a static IP (no DHCP -- see docs/ARCHITECTURE.md)"
sudo nmcli connection modify netplan-eth0 \
  ipv4.method manual ipv4.addresses "$USB_STATIC_IP" ipv6.method disabled || \
  echo "note: connection 'netplan-eth0' not found yet -- it's created on first boot with gadget mode active. Re-run this script after that first reboot."

echo "==> Creating WiFi access point '$AP_SSID' (with DHCP)"
sudo nmcli connection delete "$AP_CON_NAME" 2>/dev/null || true
sudo nmcli device wifi hotspot ifname wlan0 con-name "$AP_CON_NAME" \
  ssid "$AP_SSID" band bg password "$AP_PASSWORD"
# 'shared' is NetworkManager's built-in AP mode: keeps the gateway address
# fixed at 10.42.0.1 but also runs dnsmasq (DHCP range 10.42.0.10-.254) --
# needed for cameras that can't be configured with a manual/static IP over
# WiFi. A camera that *can* still works fine with a static address in that
# same subnet; DHCP here is unrelated to (and doesn't reintroduce) the
# usb0 stability bug this script's step 2 works around -- see
# docs/ARCHITECTURE.md.
sudo nmcli connection modify "$AP_CON_NAME" \
  ipv4.method shared ipv4.addresses 10.42.0.1/24 \
  connection.autoconnect yes connection.autoconnect-priority 100

echo "==> Disabling autoconnect on any other WiFi profile so the AP always wins on boot"
for profile in $(nmcli -t -f NAME,TYPE connection show | awk -F: '$2=="802-11-wireless" && $1!="'"$AP_CON_NAME"'" {print $1}'); do
  sudo nmcli connection modify "$profile" connection.autoconnect no
  echo "   disabled autoconnect on: $profile"
done

echo "==> Activating the access point now"
sudo nmcli connection up "$AP_CON_NAME"

echo ""
echo "Done. Reboot to verify everything comes up automatically:"
echo "  sudo reboot"
echo ""
echo "After reboot, 'nmcli device status' should show wlan0 connected to"
echo "'$AP_CON_NAME' with no manual steps, and 'ip route show' should show"
echo "10.42.0.0/24 on wlan0."
