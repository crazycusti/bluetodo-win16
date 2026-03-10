#!/usr/bin/env bash
set -eu

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
VERSION_HEADER="$SCRIPT_DIR/client_version.h"
DEFAULT_VERSION="0.9.0"

write_version_header() {
  local version="$1"

  cat >"$VERSION_HEADER" <<EOF
#ifndef BLUETODO_CLIENT_VERSION_H
#define BLUETODO_CLIENT_VERSION_H

#define CLIENT_APP_VERSION "$version"

#endif
EOF
}

detect_client_version() {
  local candidate=""

  if [ -n "${BLUETODO_APP_VERSION:-}" ]; then
    candidate="$BLUETODO_APP_VERSION"
  elif [ -f "$SCRIPT_DIR/../bluetodo/Cargo.toml" ]; then
    candidate="$(python3 - <<'PY' "$SCRIPT_DIR/../bluetodo/Cargo.toml"
import pathlib
import sys
import tomllib

with pathlib.Path(sys.argv[1]).open("rb") as fh:
    data = tomllib.load(fh)

print(data["package"]["version"])
PY
)"
  fi

  if [ -z "$candidate" ]; then
    candidate="$DEFAULT_VERSION"
  fi

  write_version_header "$candidate"
}

if [ -n "${WATCOM_ENV_SCRIPT:-}" ]; then
  # shellcheck source=/dev/null
  . "${WATCOM_ENV_SCRIPT}"
fi

if [ -n "${WATCOM:-}" ]; then
  export EDPATH="${EDPATH:-$WATCOM/eddat}"
  export INCLUDE="${INCLUDE:-$WATCOM/h/win:$WATCOM/h:$WATCOM/lh}"
  export PATH="$WATCOM/binl64:$WATCOM/binl:${PATH}"
fi

if ! command -v wmake >/dev/null 2>&1; then
  echo "wmake not found. Source the Open Watcom environment or set WATCOM_ENV_SCRIPT." >&2
  exit 1
fi

detect_client_version

exec wmake "$@"
