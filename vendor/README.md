# Vendored dependencies

This directory owns all third-party code and binary artifacts used by Term49.
See `manifest.md` for exact submodule pins, prebuilt hashes, and the current
source-vs-prebuilt reproducibility status.

## Layout

| Path | Role |
| --- | --- |
| `prebuilt-bb10/` | Checked-in ARMv7 BB10 headers and shared libraries used by the top-level build and packaged into the BAR. |
| `sdl/` | Source submodule for the BB10/PlayBook SDL 1.2 fork that produced `prebuilt-bb10/lib/libSDL12.so`. |
| `touch-control-overlay/` | Source submodule for `prebuilt-bb10/lib/libTouchControlOverlay.so`; this is an indirect dependency of the SDL fork. |
| `libconfig/` | Source submodule for the libconfig C library that produced `prebuilt-bb10/lib/libconfig.so`. |
| `libghostty-vt/` | Vendored Ghostty terminal parser/state model integration. |

## Current prebuilt inventory

The checked-in BB10 prebuilts are ARM shared libraries. Verify them with
`sha256sum -c vendor/prebuilt-bb10/SHA256SUMS` from the repo root.

* SDL headers report SDL `1.2.14` and the packaged binary is `libSDL12.so`.
* libconfig headers report libconfig `1.5.0` and the BAR installs the binary as `lib/libconfig.so.11`.
* TouchControlOverlay is packaged as `libTouchControlOverlay.so`; Term49 does not call it directly, but `libSDL12.so` requires its `tco_*` symbols.

The application still uses the SDL 1.2 API directly (`SDL_Surface`, `SDL_SetVideoMode`, `SDL_Event`, etc.) and carries an in-tree copy of `src/SDL_ttf.c`. Moving to SDL2/SDL3 is therefore a real port, not just a library swap.

## Upgrade notes

No dependency versions were changed by the vendor-layout refactor; the headers and binaries still match the old `external/` contents.

* Prefer rebuilding prebuilts from source submodules with the BB10 NDK, then replacing `prebuilt-bb10/include` and `prebuilt-bb10/lib` together so headers and binaries stay ABI-matched.
* libconfig can likely be modernized independently. Upstream currently has newer tags through `v1.8.2`; keep the C API includes as `<libconfig.h>` and verify the shared-library soname expected in `bar-descriptor.xml`.
* SDL is the larger project. Upstream SDL is now SDL3, but Term49 is still written against SDL 1.2 APIs (`SDL_Surface`, `SDL_SetVideoMode`, `SDL_Event`, etc.) and depends on the BB10/PlayBook screen integration in the fork. Practical paths are:
  * refresh the existing BB10 SDL 1.2 fork while preserving the `libSDL12.so` ABI; or
  * port Term49 to SDL2/SDL3 APIs and package the corresponding BB10/QNX library under a new name.
