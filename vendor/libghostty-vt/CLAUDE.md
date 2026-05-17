# CLAUDE.md — libghostty-vt BB10/QNX integration

This directory builds Ghostty's `libghostty-vt` C API as a freestanding static
library for BB10/QNX and provides small on-device smoke tests. Term49 is now the
primary production integration.

Key facts:

- Run from the parent BBNDK FHS shell.
- `make deps` materializes pinned Zig 0.15.2 via Nix.
- `make lib` applies `patches/` to the pinned `vendor/ghostty` submodule and
  emits `build/ghostty/lib/libghostty-vt.a` plus headers.
- `tests/smoke_terminal.c` is the minimal standalone terminal API example.
- `tests/probe_api.c` is a stepwise diagnostic harness.
- The formatter API path was unstable on the Q10; Term49 renders through
  terminal/grid-ref APIs instead.
