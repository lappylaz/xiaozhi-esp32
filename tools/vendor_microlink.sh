#!/usr/bin/env bash
#
# Re-vendor MicroLink (Tailscale ts2021 client) at a specific tag.
#
# The ESP-IDF Component Manager mishandles "two paths from the same git
# URL" — when we declare components/microlink AND components/wireguard_lwip
# as separate managed deps from the same repo, the manager caches the
# first fetch and fails to re-extract the second path. So we vendor both
# directly into <project>/components/.
#
# Run this when bumping the MicroLink version. The vendored sources end up
# at:
#   components/microlink/        (main component)
#   components/wireguard_lwip/   (PRIV_REQUIRES of microlink)
#
# Both are auto-discovered by ESP-IDF's project-root component search.
#
# Usage:
#   ./tools/vendor_microlink.sh                # default: v2.1.0
#   ./tools/vendor_microlink.sh v2.2.0         # bump to a new tag
#   ./tools/vendor_microlink.sh main           # bleeding edge

set -euo pipefail

TAG="${1:-v2.1.0}"
REPO="https://github.com/CamM2325/microlink.git"
PROJECT_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
DEST="$PROJECT_ROOT/components"

echo "→ vendoring MicroLink @ $TAG into $DEST/"

TMPDIR="$(mktemp -d)"
trap 'rm -rf "$TMPDIR"' EXIT

git clone --depth 1 --branch "$TAG" "$REPO" "$TMPDIR/ml" 2>&1 | tail -2

if [ ! -d "$TMPDIR/ml/components/microlink" ]; then
    echo "ERROR: expected components/microlink at $TAG" >&2
    exit 1
fi
# In the upstream layout, components/wireguard_lwip is a SYMLINK pointing
# at components/microlink/components/wireguard_lwip — the symlink target
# is where the real source lives. We want a flat layout with two
# independent dirs at <project>/components/, so we copy with -L to follow
# the symlink and produce a real directory.
if [ ! -L "$TMPDIR/ml/components/wireguard_lwip" ] && [ ! -d "$TMPDIR/ml/components/wireguard_lwip" ]; then
    echo "ERROR: expected components/wireguard_lwip (symlink or dir) at $TAG" >&2
    exit 1
fi

# Remove any prior vendored copy to avoid stale files.
rm -rf "$DEST/microlink" "$DEST/wireguard_lwip"

# Copy with -L (dereference symlinks). The microlink subtree itself
# contains a nested components/wireguard_lwip — that's the symlink
# target. After the next step we drop microlink's nested components/ so
# wireguard_lwip lives in only one place: $DEST/wireguard_lwip.
cp -RL "$TMPDIR/ml/components/wireguard_lwip" "$DEST/wireguard_lwip"
cp -R  "$TMPDIR/ml/components/microlink"      "$DEST/microlink"

# Strip any .git / .github / docs / examples that may have leaked in.
find "$DEST/microlink" "$DEST/wireguard_lwip" \
    -type d \( -name '.git' -o -name '.github' \) -prune -exec rm -rf {} +

# microlink ships its dep at components/microlink/components/wireguard_lwip
# as a courtesy for users who want a single path: managed dep. We've
# already copied wireguard_lwip to the top level, so drop the nested copy
# (it'd never be discovered as a component anyway — ESP-IDF's component
# search doesn't recurse into a component's own subdirs).
rm -rf "$DEST/microlink/components"

echo ""
echo "✓ vendored:"
echo "  microlink:      $(find "$DEST/microlink" -type f | wc -l | tr -d ' ') files"
echo "  wireguard_lwip: $(find "$DEST/wireguard_lwip" -type f | wc -l | tr -d ' ') files"
echo ""
echo "Run: idf.py reconfigure"
