#!/usr/bin/env bash
# On-device spike gates. Push a self-contained ARM ELF to the rooted Q10,
# run it by absolute path, echo its stdout, assert the PASS token.
#
#   abi   (Gate E): build/abi/abi-q10  -> must print ABI_OK + bit-exact values
#   spike (Gate G): build/spike-q10    -> must print SPIKE_OK + "hi" + red
#   probe         : build/probe-steps-q10 -> must reach PROBE_OK
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
. "$ROOT/scripts/lib-ssh.sh"

DEPLOY_DIR="${DEPLOY_DIR:-/accounts/1000/shared/documents}"
MODE="${1:-spike}"

case "$MODE" in
  abi)   BIN="$ROOT/build/abi/abi-q10";       TOKEN="ABI_OK" ;;
  spike) BIN="$ROOT/build/spike-q10";         TOKEN="SPIKE_OK" ;;
  probe) BIN="$ROOT/build/probe-steps-q10";   TOKEN="PROBE_OK" ;;
  *) echo "usage: smoke-device.sh abi|spike|probe" >&2; exit 2 ;;
esac
[ -f "$BIN" ] || { echo "error: $BIN missing — build it first" >&2; exit 1; }

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
  echo "smoke ($MODE) PASS — $TOKEN observed on device"
else
  echo "smoke ($MODE) FAIL — $TOKEN not in device output" >&2
  exit 1
fi
