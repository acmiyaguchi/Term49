#!/usr/bin/env bash
# libghostty-vt BB10/QNX build driver.
#
#   lib     : zig build libghostty-vt.a (+ header) freestanding
#   harness : qcc-link smoke_terminal.c against only libghostty-vt.a
#
# qcc stages need the parent BBNDK FHS (QNX_TARGET set). Zig is run by
# absolute path from build/deps.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
DEPS="$ROOT/build/deps"
GH="$ROOT/build/ghostty"
GSRC="$ROOT/vendor/ghostty"

CC="${CC:-qcc -V4.6.3,gcc_ntoarmv7le}"
ZIG="${ZIG:-$DEPS/zig/bin/zig}"
ZIG_TARGET="${ZIG_TARGET:-arm-freestanding-eabi}"
ZIG_MCPU="${ZIG_MCPU:-cortex_a9}"
ZIG_OPT="${ZIG_OPT:-ReleaseSmall}"

# Build the VT static library and headers. Disable SIMD because the vendored
# C++ simdutf path needs libc headers that arm-freestanding-eabi lacks; the
# scalar Zig UTF-8 path is enough for this integration.
GHOSTTY_LIB_ARGS="${GHOSTTY_LIB_ARGS:--Demit-lib-vt=true -Dsimd=false}"

pick() { local c; for c in "$@"; do command -v "$c" >/dev/null 2>&1 && { echo "$c"; return; }; done; echo "$1"; }
need_deps() { [ -x "$ZIG" ] || { echo "error: no zig at $ZIG — run 'make deps' first" >&2; exit 1; }; }
need_fhs()  { : "${QNX_TARGET:?error: not in BBNDK FHS shell (QNX_TARGET unset); use 'make <stage>'}"; }
need_ghostty() { [ -e "$GSRC/build.zig" ] || { echo "error: vendor/ghostty submodule not checked out" >&2; exit 1; }; }

# Apply the freestanding patch set to the pinned Ghostty submodule. Patches
# live as files for reproducibility. Idempotent: reset to the pinned commit
# first when git is available, else forward-apply.
patch_ghostty() {
  if command -v git >/dev/null 2>&1; then
    git -C "$GSRC" checkout -- . 2>/dev/null || true
    git -C "$GSRC" clean -fdq 2>/dev/null || true
  fi
  local p
  for p in "$ROOT"/patches/[0-9]*.patch; do
    [ -e "$p" ] || continue
    echo ">> applying $(basename "$p")"
    patch -p1 --forward --fuzz=3 -d "$GSRC" < "$p" \
      || { echo "error: failed to apply $p" >&2; exit 1; }
  done
}

lib() {
  need_deps; need_fhs; need_ghostty
  patch_ghostty
  mkdir -p "$GH"
  echo ">> zig build -Dtarget=$ZIG_TARGET -Dcpu=$ZIG_MCPU -Doptimize=$ZIG_OPT $GHOSTTY_LIB_ARGS"
  ( cd "$GSRC" && "$ZIG" build \
      -Dtarget="$ZIG_TARGET" -Dcpu="$ZIG_MCPU" -Doptimize="$ZIG_OPT" $GHOSTTY_LIB_ARGS \
      --prefix "$GH" )
  [ -f "$GH/lib/libghostty-vt.a" ] || { echo "error: $GH/lib/libghostty-vt.a not produced" >&2; exit 1; }
  [ -f "$GH/include/ghostty/vt.h" ] || { echo "error: $GH/include/ghostty/vt.h not produced" >&2; exit 1; }
  echo "== libghostty-vt.a undefined symbols (nm -u) =="
  local NM; NM="$(pick ntoarmv7-nm arm-unknown-nto-qnx8.0.0eabi-nm nm)"
  "$NM" -u "$GH/lib/libghostty-vt.a" 2>/dev/null | sort -u | tee "$GH/undef.syms"
  echo ">> review build/ghostty/undef.syms — only QNX libc/libgcc symbols allowed (compiler-rt is bundled)"
}

harness() {
  need_fhs
  local A="$GH/lib/libghostty-vt.a"
  [ -f "$A" ] || { echo "error: $A missing — run 'make lib' first" >&2; exit 1; }
  echo ">> linking against: $A  (headers: $GH/include) + tests/shims.c"
  $CC -O2 -std=gnu99 -I"$GH/include" \
    "$ROOT/tests/smoke_terminal.c" "$ROOT/tests/shims.c" "$A" \
    -lm -lgcc -o "$ROOT/build/smoke-terminal-q10"
  file "$ROOT/build/smoke-terminal-q10"
  file "$ROOT/build/smoke-terminal-q10" | grep -qiE 'ARM' || { echo "error: not an ARM binary" >&2; exit 1; }
  echo ">> built: build/smoke-terminal-q10 — run 'make smoke'"
}

case "${1:?usage: cross-build.sh lib|harness}" in
  lib)     lib ;;
  harness) harness ;;
  *) echo "unknown stage: $1" >&2; exit 2 ;;
esac
