#!/usr/bin/env python3
"""Provision the physically gated AiPi USB endpoint; never print/store credentials."""

import argparse
import getpass
import json
import re
import sys
import time
import uuid
from urllib.parse import urlsplit

READY = b"SABRINA_PROVISION_READY v1"
RESULT = b"SABRINA_PROVISION_RESULT "


def make_request(url, stt_url, key, request_id):
    endpoints = []
    for value, scheme, path in [(url, "wss", "/ws/voice"), (stt_url, "https", "/api/stt")]:
        parsed = urlsplit(value)
        if (
            parsed.scheme != scheme or not parsed.hostname or parsed.path != path
            or parsed.username is not None or parsed.password is not None
            or parsed.query or parsed.fragment or len(value) > 256
            or any(ord(c) <= 32 or ord(c) >= 127 for c in value)
        ):
            raise ValueError("Endpoints must be secure voice/STT URLs without credentials or query parameters")
        endpoints.append(parsed.netloc)
    if endpoints[0] != endpoints[1]:
        raise ValueError("Voice and STT endpoints must use the same authority")
    if not re.fullmatch(r"sb_voice_[A-Za-z0-9_-]{32,96}", key):
        raise ValueError("A scoped voice-device credential is required")
    if not re.fullmatch(r"[a-f0-9]{32}", request_id):
        raise ValueError("Invalid request identifier")
    return {"op": "sabrina_provision", "request_id": request_id,
            "url": url, "stt_url": stt_url, "device_key": key}


def provision(port, request, timeout=65):
    """Send only after the physical boot gate; accept only this request's receipt."""
    deadline = time.monotonic() + timeout
    sent = False
    while time.monotonic() < deadline:
        line = port.read_until(b"\n", 4097)
        if not line:
            continue
        if len(line) > 4096:
            raise RuntimeError("Device console returned an oversized line")
        line = line.strip()
        if not sent:
            if line != READY:
                continue
            payload = (json.dumps(request, separators=(",", ":")) + "\n").encode()
            if len(payload) > 2048:
                raise ValueError("Provisioning request exceeds device limit")
            if port.write(payload) != len(payload):
                raise RuntimeError("USB transfer was incomplete; provisioning not verified")
            port.flush()
            sent = True
        elif line.startswith(RESULT):
            try:
                receipt = json.loads(line[len(RESULT):])
            except (ValueError, UnicodeError):
                continue
            if not isinstance(receipt, dict) or receipt.get("request_id") != request["request_id"]:
                continue
            if receipt.get("status") != "ok" or receipt.get("code") != "stored":
                raise RuntimeError("Device refused provisioning; no successful write verified")
            return {"status": "ok", "code": "stored"}
    raise TimeoutError("No matching provisioning receipt; hold the right button while powering on and retry")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", required=True)
    parser.add_argument("--url", default="wss://api.sabrinainc.ai/ws/voice")
    parser.add_argument("--stt-url", default="https://api.sabrinainc.ai/api/stt")
    parser.add_argument("--key-stdin", action="store_true", help="Read one credential line from a managed-secret pipe")
    args = parser.parse_args()
    # Do not accept a --key argument: it would expose the credential in process listings/history.
    key = sys.stdin.readline(256).rstrip("\r\n") if args.key_stdin else getpass.getpass("Scoped device credential: ")
    request = make_request(args.url, args.stt_url, key, uuid.uuid4().hex)
    try:
        import serial
        from serial.tools import list_ports
    except ImportError:
        raise RuntimeError("Run with the ESP-IDF Python environment (pyserial required)") from None
    device = next((p for p in list_ports.comports() if p.device == args.port), None)
    if device is None or (device.vid, device.pid) != (0x303A, 0x1001):
        raise RuntimeError("Port is not an enumerated Espressif USB Serial/JTAG device")
    port = serial.Serial(port=None, baudrate=115200, timeout=0.25, write_timeout=5)
    port.dtr = False
    port.rts = False
    port.port = args.port
    try:
        port.open()
        print("Waiting for physical provisioning mode; hold right button while powering on.")
        provision(port, request)
        print("Sabrina settings stored and verified; unrelated NVS preserved.")
    finally:
        port.close()
        request.clear()


if __name__ == "__main__":
    try:
        main()
    except (ValueError, RuntimeError, OSError, TimeoutError) as error:
        # Only fixed messages defined above reach this handler. Avoid raw serial/provider output.
        if type(error) in (ValueError, RuntimeError, TimeoutError):
            print(str(error), file=sys.stderr)
        else:
            print("USB access failed; provisioning not verified", file=sys.stderr)
        raise SystemExit(1) from None
