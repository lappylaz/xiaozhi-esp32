#!/usr/bin/env bash
#
# One-shot device provisioning for the AiPi-Lite running our xiaozhi fork.
#
# Writes Settings("sabrina") values into the device's NVS partition so
# SabrinaProtocol::LoadConfig() and Application::InitializeMicrolink()
# can read them at boot:
#   sabrina/url            — wss://… (Sabrina /ws/voice WebSocket)
#   sabrina/stt_url        — https://… (Sabrina /api/stt Whisper proxy)
#   sabrina/jwt            — long-lived JWT for /ws/voice ?token=…
#   sabrina/device_key     — DEVICE_API_KEY for /api/stt X-API-Key header
#   sabrina/tailscale_key  — Tailscale auth key for MicroLink tailnet join
#
# Approach:
#   1. Mint a fresh JWT via mint_device_jwt.py (using JWT_SECRET_KEY from
#      Doppler).
#   2. Pull DEVICE_API_KEY from Doppler.
#   3. Build an NVS partition CSV with the four values.
#   4. Use ESP-IDF's nvs_partition_gen.py to compile the CSV → 16 KB binary
#      that matches the NVS partition size from the AiPi-Lite partition
#      table (0x4000 bytes).
#   5. esptool write-flash that binary to offset 0x9000 (NVS partition
#      start) — using --before usb-reset for reliable bootloader entry.
#
# Idempotent. Safe to re-run after rotating keys or moving servers.
#
# Requires: ESP-IDF v5.5.2 sourced (so nvs_partition_gen.py is in PATH),
# Doppler CLI authenticated, mpremote NOT holding the serial port.
#
# Usage:
#   ./tools/provision_device.sh <ws_url> <stt_url> <email> [days]
#
#   e.g. ./tools/provision_device.sh \
#        wss://api.sabrinainc.ai/ws/voice \
#        https://api.sabrinainc.ai/api/stt \
#        branson@example.com \
#        365

set -euo pipefail

if [ "$#" -lt 3 ]; then
    echo "Usage: $0 <ws_url> <stt_url> <email> [days]" >&2
    exit 2
fi

WS_URL="$1"
STT_URL="$2"
EMAIL="$3"
DAYS="${4:-365}"
PORT="${ESPPORT:-/dev/cu.usbmodem2101}"

if [ -z "${IDF_PATH:-}" ]; then
    echo "ESP-IDF not sourced. Run:" >&2
    echo "  source ~/esp/esp-idf-v5.5.2/export.sh" >&2
    exit 3
fi

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
WORK_DIR="$(mktemp -d)"
trap 'rm -rf "$WORK_DIR"' EXIT

# 1. Mint JWT (writes to stdout; capture to var, never log it).
echo "→ minting JWT for $EMAIL ($DAYS days)..."
JWT="$(python3 "$SCRIPT_DIR/mint_device_jwt.py" --email "$EMAIL" --days "$DAYS")"
if [ -z "$JWT" ]; then
    echo "JWT mint failed" >&2
    exit 4
fi

# 2. Pull DEVICE_API_KEY from Doppler.
echo "→ reading DEVICE_API_KEY from Doppler..."
DEVICE_KEY="$(doppler secrets get DEVICE_API_KEY -p sabrina -c dev_personal --plain)"
if [ -z "$DEVICE_KEY" ]; then
    echo "DEVICE_API_KEY not set in Doppler sabrina/dev_personal" >&2
    exit 5
fi

# 2b. Pull TAILSCALE_AIPI_LITE_AUTH_KEY from Doppler.
#     Optional — if absent, the device just won't join the tailnet
#     (CONFIG_USE_MICROLINK_TAILSCALE will silently no-op).
echo "→ reading TAILSCALE_AIPI_LITE_AUTH_KEY from Doppler (optional)..."
TS_KEY="$(doppler secrets get TAILSCALE_AIPI_LITE_AUTH_KEY -p sabrina -c dev_personal --plain 2>/dev/null || true)"
if [ -z "$TS_KEY" ]; then
    echo "  (not set — MicroLink will skip tailnet bring-up; provision later if/when needed)"
fi

# 3. Build NVS CSV. Note: nvs_partition_gen.py uses single-line CSV with
#    a specific header. Quote the values we know contain '/' / ':' / '=' /
#    plenty of base64 padding — wrap in double quotes.
CSV="$WORK_DIR/sabrina_nvs.csv"
cat > "$CSV" <<EOF
key,type,encoding,value
sabrina,namespace,,
url,data,string,"$WS_URL"
stt_url,data,string,"$STT_URL"
jwt,data,string,"$JWT"
device_key,data,string,"$DEVICE_KEY"
tailscale_key,data,string,"$TS_KEY"
EOF

# 4. Compile to 16 KB binary (NVS partition size on AiPi-Lite is 0x4000).
NVS_BIN="$WORK_DIR/sabrina_nvs.bin"
echo "→ compiling NVS partition image..."
python3 "$IDF_PATH/components/nvs_flash/nvs_partition_generator/nvs_partition_gen.py" \
    generate "$CSV" "$NVS_BIN" 0x4000 >/dev/null

# Sanity check the binary
if [ ! -s "$NVS_BIN" ]; then
    echo "NVS binary generation failed" >&2
    exit 6
fi
echo "  $(stat -f '%z' "$NVS_BIN" 2>/dev/null || stat -c '%s' "$NVS_BIN") bytes"

# 5. Flash to NVS partition offset 0x9000.
#    --before usb-reset is the reliable bootloader-entry path on this board
#    (xiaozhi running firmware doesn't yield to default DTR/RTS reset).
echo "→ flashing NVS partition to $PORT @ 0x9000..."
esptool --chip esp32s3 --port "$PORT" -b 460800 \
    --before usb-reset --after hard-reset --connect-attempts 10 \
    write-flash 0x9000 "$NVS_BIN"

echo "✓ Device provisioned. Reboot to pick up new Settings."
echo "  (After CONFIG_USE_SABRINA_PROTOCOL=y rebuild + flash, the device"
echo "   will use these values to reach Sabrina.)"
