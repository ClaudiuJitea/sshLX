#!/usr/bin/env bash
# Verify that the vendored PuTTY source tree is byte-for-byte identical to the
# manifest recorded before any sshLX work began. Fails on modified, missing or
# extra files (including build output or stray .orig/.rej files).
#
# Usage: verify-putty-untouched.sh [PUTTY_DIR] [MANIFEST]
set -euo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
putty_dir="${1:-$here/../../putty-0.85}"
manifest="${2:-$here/putty-0.85.sha256}"

if [[ ! -d "$putty_dir" ]]; then
    echo "verify-putty-untouched: PuTTY dir not found: $putty_dir" >&2
    exit 2
fi
if [[ ! -f "$manifest" ]]; then
    echo "verify-putty-untouched: manifest not found: $manifest" >&2
    exit 2
fi
putty_dir="$(cd "$putty_dir" && pwd)"
manifest="$(cd "$(dirname "$manifest")" && pwd)/$(basename "$manifest")"

status=0
cd "$putty_dir"

# 1. Content check for every file listed in the manifest.
if ! sha256sum --quiet --strict -c "$manifest"; then
    echo "verify-putty-untouched: content mismatch or missing files" >&2
    status=1
fi

# 2. Extra-file / extra-dir / symlink check: compare the set of entries.
expected="$(sed -E 's/^[0-9a-f]{64}  //' "$manifest" | LC_ALL=C sort)"
actual="$(find . \( -type f -o -type l \) -printf '%P\n' | LC_ALL=C sort)"
extra="$(LC_ALL=C comm -13 <(printf '%s\n' "$expected") <(printf '%s\n' "$actual"))"
if [[ -n "$extra" ]]; then
    echo "verify-putty-untouched: unexpected files in PuTTY tree:" >&2
    printf '  %s\n' $extra >&2
    status=1
fi
# Empty directories created by stray builds would not show up above.
empty_dirs="$(find . -mindepth 1 -type d -empty -printf '%P\n')"
if [[ -n "$empty_dirs" ]]; then
    echo "verify-putty-untouched: unexpected empty directories:" >&2
    printf '  %s\n' $empty_dirs >&2
    status=1
fi

if [[ $status -eq 0 ]]; then
    echo "verify-putty-untouched: OK ($(wc -l < "$manifest") files match)"
fi
exit $status
