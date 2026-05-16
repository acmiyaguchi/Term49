# CLAUDE.md — libghostty-vt spike

Feasibility spike: cross-build Ghostty's libc-free Zig VT core
(libghostty-vt) to ARM-EABI and link it into Term49/BB10 via BBNDK `qcc`.
**vendor/ghostty is never modified** — it is a SHA-pinned git submodule of
the `acmiyaguchi/Term49` fork; patches (if needed) apply to a writable copy.

## Architecture (do not relitigate)

Ratified as item **#36** of `../../../docs/term49-modernization.md`. Full
plan: `~/.claude/plans/okay-in-the-worktree-eager-whisper.md`.

- **Zig has NO QNX target.** Route = build libghostty-vt *freestanding* for a
  generic `arm-*-eabi` triple → pure-computation static `.a` → linked by
  `qcc -V4.6.3,gcc_ntoarmv7le`. QNX-ness lives only on the qcc side.
- Nix only MATERIALIZES a pinned Zig (`nix/deps.nix` → `make deps` →
  `build/deps/zig/bin/zig`). It does NOT compile. The qcc/zig/readelf build
  runs OUTSIDE Nix in the parent BBNDK FHS:
  `nix run /mnt/data/fun/blackberry#shell -- bash scripts/cross-build.sh <stage>`.
- Self-contained: Term49's flat `Makefile`/`external/` is NOT touched. This
  spike only proves feasibility; integration is separate, later, gated.
- Crux risk = the Zig↔qcc C-boundary float ABI (soft/`softfp` are
  call-compatible; `hard`/`eabihf` is not). Determined empirically at Gate D
  (`readelf -A` Tag_ABI_VFP_args), proven on a toy at Gate E *before* the lib.

## Gate D RESULT (cf24a48, qcc -V4.6.3,gcc_ntoarmv7le)

`Tag_ABI_VFP_args` ABSENT; `Tag_CPU_arch v7` / profile Application;
`Tag_FP_arch VFPv3-D16`; `Tag_ABI_HardFP_use SP and DP`. ⇒ **softfp**
(hardware-FP codegen, core-register float *argument passing*) ⇒ Zig
`arm-freestanding-eabi` (soft-float ABI, boundary-compatible). NOT eabihf.
Decisive confirmation is Gate E (bit-exact on-device). Raw attrs saved at
`build/abi/qcc_ref.attrs`.

## Gate E RESULT — DECISIVE, PASS (the crux risk is retired)

`arm-freestanding-eabi` (first ladder rung, no fallback) zig build-obj +
qcc-link produced a valid QNX ARM ELF (no ABI-conflict diagnostic). On the
rooted Q10 it returned **bit-exact** across the C boundary: double 3.75,
float 0.75, struct-by-value `Vec3` 6.0, mixed int/double 7.5, int 42 →
`ABI_OK`, exit 0. Zig also runs INSIDE the BBNDK FHS chroot (no devShell
split needed). K1/K2/K3 passed; K4 strongly de-risked (ghostty bundles
compiler-rt). Remaining: Gate F build, Gate G real parse, K5 (NEON) at G.

## Gate F RESULT — static .a built (ladder rung 3)

`arm-freestanding-eabi` could NOT build the real lib (std.heap page_size +
std.posix.timespec undefined for freestanding; shared-lib `-dynamic`
unavailable). Advanced ladder → **`arm-linux-musleabi`** (codegen-only;
SAME soft-float boundary ABI — Gate E re-validated bit-exact on it).
`-Dsimd=false` (vendored C++ simdutf needs libc headers; scalar Zig path
instead; SIMD is perf-only per #36). Result: `build/ghostty/lib/
libghostty-vt.a` (1.15 MB) + `include/ghostty/vt.h`. `nm -u` = all
libgcc/libm/libc-resolvable EXCEPT two bounded Linux-isms needing shims at
Gate G: **`getauxval`** (→ return 0, std fallback) and **`__tls_get_addr`**
(TLS). `__aeabi_unwind_cpp_pr0/1` resolve via libgcc. NOT a kill — exactly
the plan's "stub + one bounded patch" path.

## Gate G UPDATE 2 — libc-free build, runs on Q10, one null-deref left

Reframed via the "be like the wasm config" insight: every freestanding
blocker = a ghostty OS-less carve-out scoped to `isWasm()` only.
Generalized them to `os.tag == .freestanding`:
- patches/0002 std_options page_size (QNX/ARMv7 = 4 KiB)
- patches/0003 kitty_graphics off on any freestanding (their own
  "freestanding can't get timestamps" rationale; kills posix.timespec)
- patches/0004 skip the shared lib on freestanding (static-only)
- patches/0005 allocator default = empty FBA on freestanding (no mmap;
  embedder MUST pass an allocator — Term49 always will)
- patches/0006 logStderr no-op on freestanding (its own doc says so;
  kills posix write/pwritev/lseek)
- patches/0007 PageList pageAllocator = static 8 MiB arena on
  freestanding (no OS page_allocator). PRODUCTION TODO: thread the
  embedder/QNX allocator through PageList instead of a fixed buffer.
- **patches/0001 REVERTED** (back to PIC): non-PIC was a musl-rung
  hack; the wasm/freestanding config is PIC-clean. Non-PIC produced a
  QNX-hostile `DT_TEXTREL` → that was the earlier SIGSEGV. Reverting it
  fixed it.

State: `arm-freestanding-eabi` `libghostty-vt.a` is TRULY libc-free
(undef = libgcc `__aeabi_*`/`__extend*tf2` + libm `ceil`/`fmax`/`round`
only; zero TLS/CRT/posix/errno/getauxval). Links + loads + **executes on
the Q10** past the embedder allocator (spike provides a malloc-backed
GhosttyAllocator). Remaining: one `SIGSEGV code=1 ref=0x00000007`
(near-null deref) in the `terminal_new→vt_write→formatter` path —
ordinary null-pointer bug, NOT an ABI/toolchain/arch wall. Next:
on-device core/backtrace to localize the faulting call.

## Gate G RESULT — links + loads + runs; runtime SIGSEGV remains (superseded by UPDATE 2)

Harness links clean vs only libghostty-vt.a + tests/shims.c + -lm -lgcc.
On Q10, in order, each fixed by a bounded step (plan's "stub + one patch"):
1. musl-CRT-isms (`_init_libc`/`_init_array`/`_fini_array`/`_preinit_array`)
   → no-op shims (tests/shims.c). Cleared.
2. dynamic-TLS relocs ldqnx can't process (4 nameless UND FATAL) → root
   cause: initLib forces -fPIC ⇒ general-dynamic TLS. patches/0001 sets the
   static lib non-PIC ⇒ local-exec TLS. Binary now has ZERO TLS relocs;
   loader FATAL gone; program loads & executes. (Side effect: the *shared*
   libghostty-vt can't build non-PIC; `-Demit-lib-vt=true` builds both, so
   `zig build` exits non-zero — but the static `.a` IS regenerated first
   and is what links. A static-only build target is the clean fix.)
3. Now: runtime **SIGSEGV** in libghostty-vt logic (not stale artifact —
   timestamps confirm fresh non-PIC .a). Likely local-exec TLS vs QNX
   thread-pointer / std-internal (errno) bring-up, or single_threaded.
   Next probes: `-Dtarget` module `single_threaded=true` patch; on-device
   core/gdb; or freestanding-rung + std.options.page_size patch (avoids
   musl/Linux std entirely). This is the scoped ~week-class Gate-G tail.

**VERDICT: the decisive risk is RETIRED.** K3 (the one fundamentally
unfixable risk per #36 — Zig↔qcc soft-float ABI on QNX/ARMv7) PASSED
bit-exact on real hardware, twice (eabi + musleabi). libghostty-vt builds
+ links + loads on the device. Remainder = bounded, characterized
TLS/CRT-on-QNX runtime bring-up. NOT a clean green, NOT a kill (K4 kills
only if irreducible after a bounded patch; the patch reduced
loader-FATAL → runtime-SIGSEGV — reducible, more bounded work).

## Gate map (PASS advances; KILL stops — see plan)

| stage (`make`) | gate | what |
|---|---|---|
| `deps` | — | nix: pinned zig |
| `discover` (cross-build.sh) | C | read-only: find static-lib+header build step → set `GHOSTTY_LIB_STEP`/`_ARGS` here |
| `abi-probe` | D | qcc `readelf -A` → `build/abi/zig_target` |
| `abi-zig`+`smoke-abi` | E | DECISIVE: bit-exact Zig↔qcc on Q10 |
| `lib` | F | `zig build` libghostty-vt.a + header; `nm -u` audit |
| `harness`+`smoke` | G | qcc-link `spike_main` vs only the .a; `SPIKE_OK` on Q10 |

## Gotchas / iterate-don't-pre-guess

- Match Term49's compiler EXACTLY: `qcc -V4.6.3,gcc_ntoarmv7le` (GCC 4.6.3) —
  NOT fen-blackberry's bare `-Vgcc_ntoarmv7le` (4.8.3). ABI must match Term49.
- Zig run inside the BBNDK FHS chroot is unverified — if it fails, run the
  zig-only stages via `nix develop` (devShell ships `pkgs.zig`) and qcc
  stages in FHS, like fen's stage3 split.
- Gate B pinned **Zig 0.16.0**. `abi_zig.zig` still carries the *pre-0.14*
  `pub fn panic(msg, ?*StackTrace, ?usize)` form — Zig 0.16 uses the new
  panic interface (`pub const panic = std.debug.FullPanic(handler)` or a
  root `panic` namespace). EXPECT to fix this at Gate E. `zig targets` emits
  ZON (not JSON) in 0.16; QNX/nto absent there (recorded build/zig-targets.json).
- `tests/spike_main.c` is a PROVISIONAL stub — finalize against the
  PRE-COMMITTED headers `vendor/ghostty/include/ghostty/vt.h` (umbrella) +
  `include/ghostty/vt/{terminal,sgr,screen,types,...}.h`. They are checked
  in, NOT build-generated — readable now, no build needed.
- **Gate C RESOLVED** (cf24a48): build invocation is the DEFAULT install
  step with `-Demit-lib-vt=true` (also sets emit_exe=false → no GUI build;
  app_runtime defaults .none). `zig build -Demit-lib-vt=true -Dtarget=<t>
  -Dcpu=cortex_a9 -Doptimize=ReleaseSmall --prefix <out>` →
  `<out>/lib/libghostty-vt.a` + `<out>/include/ghostty/vt.h`. The static lib
  `initLib` already sets `bundle_compiler_rt=true`, `bundle_ubsan_rt=true`,
  `pic=true` — strongly de-risks Gate F/K4 (builtins bundled in the .a).
  `.zon` deps are `.lazy=true` + GUI-oriented (libxev/vaxis/z2d/zig_objc);
  network available for any lazy fetch.
- Tool names probed (`pick`): `ntoarmv7-{readelf,nm}` etc. — don't hardcode.

## Pinned ghostty SHA

`cf24a4856b24f7b381c13f1491421e84b3bf802a` — ghostty `main` tip as of
2026-05-16 (well past PRs #11676 + #11814, the Terminal C API). Pinned as
the `spike/libghostty-vt/vendor/ghostty` submodule of the `acmiyaguchi/Term49`
fork (shallow). Gate A confirmed `vendor/ghostty/src/terminal/c/` exists at
this SHA (allocator/cell/color/key_encode … .zig present).
