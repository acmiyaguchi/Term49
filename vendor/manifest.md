# Vendor manifest

This file records the dependency pins used by the top-level Term49 build.

## Source pins

| Dependency | Path | Upstream | Pin |
| --- | --- | --- | --- |
| Ghostty | `vendor/libghostty-vt/vendor/ghostty` | `https://github.com/ghostty-org/ghostty.git` | `cf24a4856b24f7b381c13f1491421e84b3bf802a` |
| Lua | `vendor/lua` | `https://github.com/lua/lua.git` | `6e22fedb74cf0c9b6656e9fce8b7331db847c605` (`v5.4.8`) |

Initialize/update the source pins with:

```sh
git submodule update --init --recursive
```

## Dependency graph

```text
Term49
├── libghostty-vt.a       built from vendor/libghostty-vt/ on demand
├── liblua.a              built from vendor/lua/ by the Makefile
└── BB10 NDK libs         libscreen, libbps, libfreetype, libicui18n,
                          libicuuc, libclipboard (ship with the NDK)
```

`libghostty-vt.a` and `liblua.a` are rebuilt from pinned source submodules by
the Makefile. Lua is plain C compiled directly by `qcc` (no Zig/nix step,
unlike libghostty-vt) and statically linked, so it adds no extra packaged
asset to the BAR.

`~/.term49.lua` is executed as arbitrary Lua with the full standard library
(`os`, `io`, `package`, ...) at startup and on every live reload. It is a
trusted, user-owned config/script file (like a shell rc), not a sandbox; this
is intentional so the config can script the terminal.
