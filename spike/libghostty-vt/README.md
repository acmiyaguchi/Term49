# libghostty-vt feasibility spike

Self-contained probe answering ONE question: can Ghostty's libc-free Zig VT
core be compiled to an ARM-EABI static `.a` whose ABI matches Term49's BBNDK
`qcc -V4.6.3,gcc_ntoarmv7le`, linked into a QNX ELF, and correctly parse
`"\x1b[31mhi"` on the rooted BlackBerry Q10?

Architecture is ratified — see `../../../docs/term49-modernization.md` item
**#36** and the plan at
`~/.claude/plans/okay-in-the-worktree-eager-whisper.md`. **Do not relitigate**
the design here; this tree only proves/​disproves feasibility.

## Run

```sh
make deps        # nix: pinned zig (+ ghostty src is the vendor/ submodule)
make abi-probe   # Gate D  — qcc readelf -A => pick the Zig triple
make abi-zig     # Gate E  — zig obj + qcc-link the ABI probe
make smoke-abi   # Gate E  — DECISIVE: bit-exact across Zig<->qcc on the Q10
make lib         # Gate F  — zig build libghostty-vt.a freestanding
make harness     # Gate G  — qcc-link spike_main vs ONLY the .a
make smoke       # Gate G  — SPIKE_OK on the Q10
# or: make spike  (full chain)
```

## PASS / KILL

Green = `make smoke` prints `SPIKE_OK` + `hi` + red, from **device** stdout.
KILL criteria K1–K5 + time box: see the plan file. Three of five kill gates
fire before libghostty-vt is ever compiled (cheapest-fatal-first ordering).

## Layout

`config.mk` tunables · `Makefile` gate dispatch · `scripts/cross-build.sh`
the staged driver · `scripts/smoke-device.sh` on-device run · `tests/` the
probes/harness · `vendor/ghostty` pinned submodule · `nix/deps.nix` pins zig
· `build/` artifacts (gitignored).
