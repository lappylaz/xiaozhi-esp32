#!/usr/bin/env python3
"""
Mint a long-lived JWT for the AiPi-Lite device to use against Sabrina's
/ws/voice WebSocket.

Bypasses sabrina-core's create_access_token() because that helper enforces
24-hour (personal) or 15-minute (enterprise) TTLs — neither makes sense for
an embedded device. We sign with the same JWT_SECRET_KEY using the same
HS256 algorithm, so verify_token() in sabrina-core will accept it.

Reads JWT_SECRET_KEY from Doppler (sabrina/dev_personal). The minted token
is printed to stdout — pipe it directly into the provisioning script,
DO NOT save it in plaintext anywhere.

Required claim per backend/src/api/routes/voice.py line ~96:
    email — non-empty string, used to derive tenant_id

Optional claims with defaults applied by voice.py:
    role  — defaults to "staff"
    scope — defaults to "enterprise"

Usage:
    python tools/mint_device_jwt.py \
        --email branson@example.com \
        --days 365 \
        --role device \
        --scope personal \
      | xargs -I {} ./tools/provision_device.sh {}

Or just print:
    python tools/mint_device_jwt.py --email branson@example.com --days 365
"""

import argparse
import os
import subprocess
import sys
from datetime import UTC, datetime, timedelta

try:
    from jose import jwt
except ImportError:
    sys.stderr.write(
        "missing dependency: pip install python-jose[cryptography]\n"
    )
    sys.exit(2)


def read_secret_from_doppler() -> str:
    """Pull JWT_SECRET_KEY from sabrina/dev_personal via the doppler CLI."""
    try:
        result = subprocess.run(
            [
                "doppler",
                "secrets",
                "get",
                "JWT_SECRET_KEY",
                "-p",
                "sabrina",
                "-c",
                "dev_personal",
                "--plain",
            ],
            check=True,
            capture_output=True,
            text=True,
            timeout=10,
        )
    except (subprocess.CalledProcessError, FileNotFoundError) as exc:
        sys.stderr.write(f"could not read JWT_SECRET_KEY from Doppler: {exc}\n")
        sys.exit(3)
    secret = result.stdout.strip()
    if not secret:
        sys.stderr.write("JWT_SECRET_KEY is empty\n")
        sys.exit(3)
    return secret


def mint(email: str, days: int, role: str, scope: str, secret: str) -> str:
    now = datetime.now(UTC)
    payload = {
        "email": email,
        "role": role,
        "scope": scope,
        "iat": now,
        "exp": now + timedelta(days=days),
    }
    return jwt.encode(payload, secret, algorithm="HS256")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.strip().splitlines()[0])
    parser.add_argument("--email", required=True, help="email claim, mandatory")
    parser.add_argument(
        "--days",
        type=int,
        default=365,
        help="token lifetime in days (default: 365)",
    )
    parser.add_argument(
        "--role",
        default="device",
        help='role claim (default: "device"; voice.py defaults to "staff" if absent)',
    )
    parser.add_argument(
        "--scope",
        default="personal",
        choices=["personal", "enterprise", "device"],
        help="scope claim — voice.py reads it for context, doesn't enforce TTL here",
    )
    parser.add_argument(
        "--secret-from",
        default="doppler",
        choices=["doppler", "env"],
        help='where to read JWT_SECRET_KEY (default: doppler "sabrina/dev_personal")',
    )
    args = parser.parse_args()

    if args.secret_from == "doppler":
        secret = read_secret_from_doppler()
    else:
        secret = os.environ.get("JWT_SECRET_KEY", "")
        if not secret:
            sys.stderr.write("JWT_SECRET_KEY env var not set\n")
            return 3

    token = mint(args.email, args.days, args.role, args.scope, secret)
    sys.stdout.write(token + "\n")
    return 0


if __name__ == "__main__":
    sys.exit(main())
