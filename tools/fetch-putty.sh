#!/usr/bin/env bash
# Download the PuTTY 0.85 source release next to this tree (../putty-0.85 by
# default) and check it against tools/putty-0.85.sha256.
#
#   tools/fetch-putty.sh [destination-parent-dir]
set -euo pipefail

SRC="$(cd "$(dirname "$0")/.." && pwd)"
PARENT="${1:-$SRC/..}"
DEST="$PARENT/putty-0.85"
URL="https://the.earth.li/~sgtatham/putty/0.85/putty-0.85.tar.gz"

if [ -e "$DEST/putty.h" ]; then
    echo "PuTTY source already present: $DEST"
else
    tmp="$(mktemp -d)"
    trap 'rm -rf "$tmp"' EXIT
    echo "downloading $URL"
    curl -fL -o "$tmp/putty.tar.gz" "$URL"
    tar -xzf "$tmp/putty.tar.gz" -C "$PARENT"
fi
"$SRC/tools/verify-putty-untouched.sh" "$DEST"
