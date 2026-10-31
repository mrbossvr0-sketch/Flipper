#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/applications_user/private_lab"
CHANNEL="${1:-release}"
if [[ "$CHANNEL" != release && "$CHANNEL" != dev ]]; then
  echo 'Usage: ./build.sh [release|dev]' >&2
  exit 2
fi
command -v ufbt >/dev/null || { echo 'Install uFBT: python3 -m pip install --upgrade ufbt' >&2; exit 1; }
ufbt update --index-url=https://up.momentum-fw.dev/firmware/directory.json --channel="$CHANNEL"
ufbt
test -s dist/private_lab.fap
printf '\nSUCCESS: %s\n' "$(pwd)/dist/private_lab.fap"
