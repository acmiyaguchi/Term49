# libghostty-vt for BB10/QNX

This directory builds Ghostty's `libghostty-vt` C API as a freestanding static
library that can be linked into BlackBerry 10 native apps with BBNDK `qcc`.
Term49 uses this tree as its production terminal parser/state dependency.

## Build

Run from the Term49 top-level BBNDK shell:

```sh
make -C vendor/libghostty-vt deps       # materialize pinned Zig + deps
make -C vendor/libghostty-vt lib        # build build/ghostty/lib/libghostty-vt.a
```

The top-level Term49 `make` depends on this library and will build it on demand
if missing.

## Standalone examples/checks

```sh
make -C vendor/libghostty-vt harness     # tests/smoke_terminal.c
make -C vendor/libghostty-vt smoke       # run smoke-terminal-q10 on the Q10
```

`tests/smoke_terminal.c` is the minimal terminal API example: create a Ghostty
terminal, feed VT bytes, query the visible grid/style, and assert the expected
text/color.

## Layout

* `vendor/ghostty/` — pinned Ghostty submodule.
* `patches/` — freestanding/BB10 patch set applied before building.
* `scripts/cross-build.sh` — qcc/Zig build driver.
* `scripts/smoke-device.sh` — rooted Q10 smoke runner.
* `tests/` — standalone smoke-test sources.
