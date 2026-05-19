# Vendor manifest

This file records the dependency pins and checked-in BB10 binary artifacts used
by the top-level Term49 build.

## Source pins

| Dependency | Path | Upstream | Pin |
| --- | --- | --- | --- |
| SDL 1.2 BB10/PlayBook fork | `vendor/sdl` | `https://github.com/mordak/SDL.git` | `821bb639eda473a9c2219489bb18b1b7f47dab7e` |
| TouchControlOverlay | `vendor/touch-control-overlay` | `https://github.com/blackberry/TouchControlOverlay.git` | `d7f53181e6aa6df31ff62d130b0dc7d1ff5aed5c` |
| Ghostty | `vendor/libghostty-vt/vendor/ghostty` | `https://github.com/ghostty-org/ghostty.git` | `cf24a4856b24f7b381c13f1491421e84b3bf802a` |
| Lua | `vendor/lua` | `https://github.com/lua/lua.git` | `6e22fedb74cf0c9b6656e9fce8b7331db847c605` (`v5.4.8`) |

Initialize/update the source pins with:

```sh
git submodule update --init --recursive
```

## Checked-in BB10 prebuilts

The top-level build currently consumes prebuilt ARMv7 BB10 headers and shared
libraries from `vendor/prebuilt-bb10/`.

Verify the checked-in artifacts with:

```sh
sha256sum -c vendor/prebuilt-bb10/SHA256SUMS
```

Key artifacts:

| Artifact | SHA-256 | Notes |
| --- | --- | --- |
| `vendor/prebuilt-bb10/lib/libSDL12.so` | `7a427a39c1899c856a951b5be7ba6f963db1b86331748ac997704e6c672f7641` | SDL 1.2 BB10/PlayBook fork. |
| `vendor/prebuilt-bb10/lib/libTouchControlOverlay.so` | `031fc54adf03d4ebd856172ade63959aed799b0dd737193ff07d3cc5e33f61e1` | Indirect dependency of `libSDL12.so`; Term49 does not call TCO directly. |

## Dependency graph

```text
Term49
├── libghostty-vt.a       built from vendor/libghostty-vt/ on demand
├── liblua.a              built from vendor/lua/ by the Makefile
└── libSDL12.so           checked-in BB10 prebuilt
    └── libTouchControlOverlay.so
```

Full source-to-binary reproducibility is currently partial: `libghostty-vt.a`
and `liblua.a` are rebuilt from pinned source submodules by the Makefile, while
SDL/TouchControlOverlay are pinned source submodules plus checked-in known-good
BB10 prebuilts. Lua is plain C compiled directly by `qcc` (no Zig/nix step,
unlike libghostty-vt) and statically linked, so it adds no `prebuilt-bb10`
artifact or `bar-descriptor.xml` asset. libconfig was removed when Lua became
the sole config language.

`~/.term49.lua` is executed as arbitrary Lua with the full standard library
(`os`, `io`, `package`, …) at startup and on every live reload. It is a
trusted, user-owned config/script file (like a shell rc), not a sandbox; this
is intentional so the config can script the terminal.
