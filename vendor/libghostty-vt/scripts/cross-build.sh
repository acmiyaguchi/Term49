#!/usr/bin/env bash
# libghostty-vt BB10/QNX build driver.
#
#   discover  : read-only — find the static-lib + header build step
#   abi-probe : qcc-emit a ref object, readelf -A -> pick Zig triple
#   abi-zig   : zig build-obj + qcc-link the ABI probe binary
#   lib       : zig build libghostty-vt.a (+ header) freestanding
#   harness   : qcc-link smoke_terminal.c against ONLY libghostty-vt.a
#   probe     : qcc-link stepwise API probe
#
# qcc/readelf stages need the parent BBNDK FHS (QNX_TARGET set). Zig is run
# by absolute path from build/deps; if it will not run inside the FHS chroot,
# use this directory's `nix develop` shell for Zig-only stages.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
DEPS="$ROOT/build/deps"
ABI="$ROOT/build/abi"
GH="$ROOT/build/ghostty"
GSRC="$ROOT/vendor/ghostty"

CC="${CC:-qcc -V4.6.3,gcc_ntoarmv7le}"
ZIG="${ZIG:-$DEPS/zig/bin/zig}"
ZIG_MCPU="${ZIG_MCPU:-cortex_a9}"
ZIG_OPT="${ZIG_OPT:-ReleaseSmall}"
# Static-lib build step (ghostty cf24a48): the static lib + headers are the
# DEFAULT install step gated by -Demit-lib-vt=true (which also sets
# emit_exe=false, so no GUI app build). app_runtime defaults to .none.
# Static lib already bundles compiler-rt + ubsan-rt (initLib), + PIC.
# Produces <prefix>/lib/libghostty-vt.a and <prefix>/include/ghostty/vt.h.
# -Dsimd=false: the vendored C++ simdutf needs libc headers (stdlib.h /
# strings.h / lldiv) which `arm-freestanding-eabi` has none of. Off => pure
# Zig scalar UTF-8 path, libghostty-vt stays truly libc-free on the exact
# ABI ABI probe validated. SIMD is a perf bonus only (#36), fine to drop.
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

unpatch_ghostty() {
  command -v git >/dev/null 2>&1 || { echo "error: need git to unpatch" >&2; exit 1; }
  git -C "$GSRC" checkout -- . && git -C "$GSRC" clean -fdq
  echo ">> vendor/ghostty reset to pinned $(git -C "$GSRC" rev-parse --short HEAD)"
}

# Resolve the probed Zig triple, else use the config.mk default.
zig_target() { [ -s "$ABI/zig_target" ] && cat "$ABI/zig_target" || echo "${ZIG_TARGET:-arm-freestanding-eabi}"; }

discover() { # Read-only. No artifacts, just facts to fill GHOSTTY_LIB_*.
  need_deps; need_ghostty
  echo ">> zig version: $("$ZIG" version)"
  echo ">> ghostty HEAD: $(git -C "$GSRC" rev-parse --short HEAD 2>/dev/null || echo '?')"
  echo "== zig build --help (steps) =="
  ( cd "$GSRC" && "$ZIG" build --help 2>&1 ) | sed -n '/Steps/,/General Options/p' || true
  echo "== build.zig files =="
  find "$GSRC" -maxdepth 2 -name build.zig -print
  echo "== src/terminal/c (C API header location) =="
  ls -1 "$GSRC/src/terminal/c" 2>/dev/null || echo "(no src/terminal/c)"
  echo "== build.zig hits: static lib / vt / terminal / header =="
  grep -nE 'addStaticLibrary|addLibrary|installArtifact|installHeader|libghostty|terminal|"vt"' \
    "$GSRC/build.zig" 2>/dev/null | head -40 || true
  echo ">> discovery complete: set GHOSTTY_LIB_ARGS from the above if needed"
}

abi-probe() { # Determine qcc's ARM float ABI empirically.
  need_fhs
  mkdir -p "$ABI"
  $CC -O2 -c "$ROOT/tests/abi_probe.c" -o "$ABI/qcc_ref.o"
  local RE; RE="$(pick ntoarmv7-readelf arm-unknown-nto-qnx8.0.0eabi-readelf readelf)"
  echo "== $RE -A qcc_ref.o (ARM EABI attributes) =="
  "$RE" -A "$ABI/qcc_ref.o" | tee "$ABI/qcc_ref.attrs"
  # Tag_ABI_VFP_args present & "VFP registers" => hardfp; absent/base => soft.
  local zt
  if grep -qi 'Tag_ABI_VFP_args:.*VFP' "$ABI/qcc_ref.attrs"; then
    zt="arm-freestanding-eabihf"
  else
    zt="arm-freestanding-eabi"
  fi
  echo "$zt" > "$ABI/zig_target"
  echo ">> qcc float-ABI => Zig target '$zt' (build/abi/zig_target)"
}

abi-zig() { # Prove Zig<->qcc C-boundary ABI on a toy.
  need_deps; need_fhs
  mkdir -p "$ABI"
  local ZT; ZT="$(zig_target)"
  echo ">> zig target: $ZT  mcpu=$ZIG_MCPU opt=$ZIG_OPT"
  "$ZIG" build-obj "$ROOT/tests/abi_zig.zig" \
    -target "$ZT" -mcpu "$ZIG_MCPU" -O "$ZIG_OPT" \
    -femit-bin="$ABI/abi_zig.o"
  $CC -O2 "$ROOT/tests/abi_main.c" "$ABI/abi_zig.o" -o "$ABI/abi-q10"
  file "$ABI/abi-q10"
  file "$ABI/abi-q10" | grep -qiE 'ARM' || { echo "error: not an ARM binary" >&2; exit 1; }
  echo ">> built: $ABI/abi-q10 — run 'make smoke-abi'"
}

lib() { # Build libghostty-vt freestanding (.a + header).
  need_deps; need_fhs; need_ghostty
  patch_ghostty
  mkdir -p "$GH"
  local ZT; ZT="$(zig_target)"
  echo ">> zig build -Dtarget=$ZT -Dcpu=$ZIG_MCPU -Doptimize=$ZIG_OPT $GHOSTTY_LIB_ARGS"
  ( cd "$GSRC" && "$ZIG" build \
      -Dtarget="$ZT" -Dcpu="$ZIG_MCPU" -Doptimize="$ZIG_OPT" $GHOSTTY_LIB_ARGS \
      --prefix "$GH" )
  [ -f "$GH/lib/libghostty-vt.a" ] || { echo "error: $GH/lib/libghostty-vt.a not produced" >&2; exit 1; }
  [ -f "$GH/include/ghostty/vt.h" ] || { echo "error: $GH/include/ghostty/vt.h not produced" >&2; exit 1; }
  echo "== libghostty-vt.a undefined symbols (nm -u) =="
  local NM; NM="$(pick ntoarmv7-nm arm-unknown-nto-qnx8.0.0eabi-nm nm)"
  "$NM" -u "$GH/lib/libghostty-vt.a" 2>/dev/null | sort -u | tee "$GH/undef.syms"
  echo ">> review build/ghostty/undef.syms — only QNX libc/libgcc symbols allowed (compiler-rt is bundled)"
}

harness() { # qcc-link the terminal smoke example vs ONLY the .a.
  need_fhs
  local A="$GH/lib/libghostty-vt.a"
  [ -f "$A" ] || { echo "error: $A missing — run 'make lib' first" >&2; exit 1; }
  echo ">> linking against: $A  (headers: $GH/include) + tests/shims.c"
  # shims.c: QNX compatibility glue for getauxval/__tls_get_addr.
  # -lgcc: __aeabi_*/__extend*tf2/__aeabi_unwind_cpp_pr*; -lm: ceil/fmax/round.
  $CC -O2 -std=gnu99 -I"$GH/include" \
    "$ROOT/tests/smoke_terminal.c" "$ROOT/tests/shims.c" "$A" \
    -lm -lgcc -o "$ROOT/build/smoke-terminal-q10"
  file "$ROOT/build/smoke-terminal-q10"
  file "$ROOT/build/smoke-terminal-q10" | grep -qiE 'ARM' || { echo "error: not an ARM binary" >&2; exit 1; }
  echo ">> built: build/smoke-terminal-q10 — run 'make smoke'"
}

probe() { # qcc-link the stepwise API probe vs ONLY the .a.
  need_fhs
  local A="$GH/lib/libghostty-vt.a"
  [ -f "$A" ] || { echo "error: $A missing — run 'make lib' first" >&2; exit 1; }
  echo ">> linking API probe against: $A  (headers: $GH/include) + tests/shims.c"
  $CC -O2 -std=gnu99 -I"$GH/include" \
    "$ROOT/tests/probe_api.c" "$ROOT/tests/shims.c" "$A" \
    -lm -lgcc -o "$ROOT/build/probe-api-q10"
  file "$ROOT/build/probe-api-q10"
  file "$ROOT/build/probe-api-q10" | grep -qiE 'ARM' || { echo "error: not an ARM binary" >&2; exit 1; }
  echo ">> built: build/probe-api-q10 — run 'make smoke-probe'"
}

case "${1:?usage: cross-build.sh discover|patch|unpatch|abi-probe|abi-zig|lib|harness|probe}" in
  discover)  discover ;;
  patch)     need_ghostty; patch_ghostty ;;
  unpatch)   need_ghostty; unpatch_ghostty ;;
  abi-probe) abi-probe ;;
  abi-zig)   abi-zig ;;
  lib)       lib ;;
  harness)   harness ;;
  probe)     probe ;;
  *) echo "unknown stage: $1" >&2; exit 2 ;;
esac
