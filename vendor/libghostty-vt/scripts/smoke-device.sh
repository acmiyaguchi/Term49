#!/usr/bin/env bash
# Push the standalone ARM smoke binary to the rooted Q10, run it, and assert
# that it prints SMOKE_OK.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
. "$ROOT/scripts/lib-ssh.sh"

DEPLOY_DIR="${DEPLOY_DIR:-/accounts/1000/shared/documents}"
BIN="$ROOT/build/smoke-terminal-q10"
TOKEN="SMOKE_OK"
[ -f "$BIN" ] || { echo "error: $BIN missing — run 'make harness' first" >&2; exit 1; }

RBIN="$DEPLOY_DIR/$(basename "$BIN")"
trap bb_close EXIT
bb_open

echo ">> scp $(basename "$BIN") -> $RBIN"
bb_in_shell "scp $SSHOPTS -O '$BIN' devuser@$BB_DEVICE:$RBIN"
bb_ssh_q "chmod +x $RBIN"

echo ">> run $RBIN"
out="$(bb_ssh_q "$RBIN" 2>&1)" || true
echo "---- device output ----"; printf '%s\n' "$out"; echo "-----------------------"

if printf '%s' "$out" | grep -q "$TOKEN"; then
  echo "smoke PASS — $TOKEN observed on device"
else
  echo "smoke FAIL — $TOKEN not in device output" >&2
  exit 1
fi
