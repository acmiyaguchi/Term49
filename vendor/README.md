# Vendored dependencies

This directory owns all third-party source checkouts used by Term50. See
`manifest.md` for exact submodule pins.

## Layout

| Path | Role |
| --- | --- |
| `libghostty-vt/` | Vendored Ghostty terminal parser/state model integration. |
| `lua/` | Source submodule for Lua 5.4 (`lua/lua` upstream), built to a static `liblua.a` by the top-level Makefile and statically linked. Term50's config language and scripting runtime. |
| `qrcodegen/` | Project Nayuki QR Code generator (MIT), upstream `nayuki/QR-Code-generator`. Vendored C sources used by the URL picker's QR view; built as a single object by the Makefile. |

## Notes

Term50 no longer vendors any ARM/QNX prebuilts. The native UI is driven
directly through the BB10 NDK (`libscreen`, `libbps`, `libfreetype`,
`libicu*`, `libclipboard`), all of which ship with the NDK. The old SDL 1.2
fork plus its TouchControlOverlay dependency were removed when the renderer
moved to the native Screen API.
