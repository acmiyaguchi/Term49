# Shared device-SSH helpers. Sourced by smoke-device.sh.
# Copied verbatim from fen-blackberry/scripts/lib-ssh.sh.
#
# The rooted Q10's sshd (OpenSSH 6.2) needs a forced legacy algo set. The
# dev key (~/.rim/bbt_id_rsa) is authorized from the 2026-05 setup, so when
# port 22 is already open we use SSH directly and skip blackberry-connect
# entirely (its dev-mode password in .env goes stale on every Dev-Mode
# toggle). blackberry-connect remains the fallback when 22 is closed.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
find_parent_flake() {
  local d="$SCRIPT_DIR"
  while [ "$d" != / ]; do
    if [ -f "$d/flake.nix" ] && { [ -d "$d/bbndk-linux" ] || [ -d "$d/bbndk-win32" ]; }; then
      printf '%s\n' "$d"
      return 0
    fi
    d="$(dirname "$d")"
  done
  return 1
}
PARENT_FLAKE="${PARENT_FLAKE:-$(find_parent_flake)}"
BB_DEVICE="${BB_DEVICE:-169.254.0.1}"
KEY="${KEY:-$HOME/.rim/bbt_id_rsa}"
[ -f "$PARENT_FLAKE/.env" ] && . "$PARENT_FLAKE/.env" || true
: "${BB_DEVICE:?}"

SSHOPTS="-F /dev/null -i $KEY -o BatchMode=yes -o IdentitiesOnly=yes \
-o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null \
-o KexAlgorithms=diffie-hellman-group14-sha1 -o HostKeyAlgorithms=ssh-rsa \
-o PubkeyAcceptedAlgorithms=+ssh-rsa -o Ciphers=aes128-ctr -o MACs=hmac-sha1 \
-o ConnectTimeout=8"

_CONNECT_PID=""
_CONNECT_LOG=""

# Run an ssh/scp/etc. command line inside the parent BBNDK FHS shell (so the
# legacy-capable ssh client and PATH are present).
bb_in_shell() { nix run "$PARENT_FLAKE#shell" -- bash -c "$1"; }

bb_ssh()  { bb_in_shell "ssh $SSHOPTS $* devuser@$BB_DEVICE"; }
bb_ssh_q() { bb_in_shell "ssh $SSHOPTS devuser@$BB_DEVICE \"$1\""; }

# Ensure an SSH path exists. Returns 0; sets up blackberry-connect only if a
# direct probe fails. Registers cleanup via bb_close.
bb_open() {
  if bb_in_shell "ssh $SSHOPTS -o ConnectTimeout=6 devuser@$BB_DEVICE true" >/dev/null 2>&1; then
    echo ">> ssh reachable directly (no blackberry-connect needed)"
    return 0
  fi
  : "${BB_PASSWORD:?ssh not open and BB_PASSWORD unset (populate $PARENT_FLAKE/.env)}"
  echo ">> opening blackberry-connect channel..."
  _CONNECT_LOG="$(mktemp)"
  bb_in_shell "blackberry-connect '$BB_DEVICE' -password '$BB_PASSWORD' -sshPublicKey '$KEY.pub'" \
    >"$_CONNECT_LOG" 2>&1 &
  _CONNECT_PID=$!
  local i
  for i in $(seq 1 60); do grep -q "Successfully connected" "$_CONNECT_LOG" 2>/dev/null && return 0; sleep 1; done
  echo "error: blackberry-connect did not open" >&2; cat "$_CONNECT_LOG" >&2; return 1
}

bb_close() {
  [ -n "$_CONNECT_PID" ] && { pkill -f Connect.jar 2>/dev/null || true; }
  [ -n "$_CONNECT_LOG" ] && rm -f "$_CONNECT_LOG"
  return 0
}
