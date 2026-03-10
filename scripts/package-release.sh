#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DIST_DIR="${DIST_DIR:-$ROOT_DIR/dist}"
CLIENT_EXE="$ROOT_DIR/bluetodo-win16.exe"
UPDATER_EXE="$ROOT_DIR/btupdt16.exe"

extract_version() {
    python3 - <<'PY' "$ROOT_DIR/client_version.h"
import pathlib
import re
import sys

text = pathlib.Path(sys.argv[1]).read_text(encoding="utf-8", errors="replace")
match = re.search(r'CLIENT_APP_VERSION\s+"([^"]+)"', text)
if not match:
    raise SystemExit("CLIENT_APP_VERSION not found")
print(match.group(1))
PY
}

VERSION="$(extract_version)"
PACKAGE_BASENAME="bluetodo-win16-v${VERSION}"
PACKAGE_DIR="$DIST_DIR/$PACKAGE_BASENAME"
ARCHIVE_PATH="$DIST_DIR/${PACKAGE_BASENAME}.zip"
CHECKSUM_PATH="$DIST_DIR/${PACKAGE_BASENAME}.sha256"

mkdir -p "$DIST_DIR"
rm -rf "$PACKAGE_DIR" "$ARCHIVE_PATH" "$CHECKSUM_PATH"

if command -v wmake >/dev/null 2>&1 || [[ -n "${WATCOM:-}" ]] || [[ -n "${WATCOM_ENV_SCRIPT:-}" ]]; then
    echo "building Win16 binaries"
    (
        cd "$ROOT_DIR"
        ./build.sh clean
        ./build.sh all
    )
fi

if [[ ! -f "$CLIENT_EXE" || ! -f "$UPDATER_EXE" ]]; then
    echo "missing Win16 binaries: $CLIENT_EXE / $UPDATER_EXE" >&2
    exit 1
fi

mkdir -p "$PACKAGE_DIR"
install -m 0644 "$CLIENT_EXE" "$PACKAGE_DIR/BLUETODO.EXE"
install -m 0644 "$UPDATER_EXE" "$PACKAGE_DIR/BTUPDT16.EXE"
install -m 0644 "$ROOT_DIR/README.md" "$PACKAGE_DIR/README.md"
install -m 0644 "$ROOT_DIR/LICENSE" "$PACKAGE_DIR/LICENSE"

cat >"$PACKAGE_DIR/BLUETODO.INI" <<'EOF'
[server]
host=127.0.0.1
port=5877
token=
EOF

(
    cd "$DIST_DIR"
    zip -qr "$(basename "$ARCHIVE_PATH")" "$PACKAGE_BASENAME"
    sha256sum "$(basename "$ARCHIVE_PATH")" >"$(basename "$CHECKSUM_PATH")"
)

echo "created:"
echo "  $ARCHIVE_PATH"
echo "  $CHECKSUM_PATH"
