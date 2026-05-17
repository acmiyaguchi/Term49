# Term49

Term49 is a terminal emulator for BlackBerry 10. It uses `libghostty-vt` as
its terminal parser/state model and renders the visible grid from Ghostty
through the existing SDL/SDL_ttf UI.

The [current release](https://github.com/BerryFarm/Term49/releases) requires OS version >= 10.3.

## Development

To compile Term49, you will need the BlackBerry 10 NDK plus the vendored third
party libraries in `vendor/prebuilt-bb10/lib`:

* [libSDL][libsdl]
* [Touch Control Overlay][tco]
* [libconfig][libconfig]

Third-party source checkouts and BB10 prebuilt binaries live under `vendor/`.
After cloning, run `git submodule update --init --recursive`. See
`vendor/README.md` and `vendor/manifest.md` for the dependency inventory,
prebuilt hashes, and upgrade notes.
The Ghostty VT core is vendored under `vendor/libghostty-vt/`. The top-level
Makefile builds `vendor/libghostty-vt/build/ghostty/lib/libghostty-vt.a` on
demand before linking Term49.

Typical local build inside the BBNDK shell:

```sh
make
```

Package and deploy to a rooted/dev-mode BB10 device:

```sh
make package-dev
make deploy
```

## libghostty-vt examples and checks

`vendor/libghostty-vt/` is the standalone integration/example entry point for
building Ghostty's VT C API for BB10/QNX:

```sh
make -C vendor/libghostty-vt deps
make -C vendor/libghostty-vt lib
make -C vendor/libghostty-vt harness
```

Term49 itself is the primary application example. The small standalone checks
live in `vendor/libghostty-vt/tests/`.

## Architecture notes

* `src/ghostty_bridge.c` owns the Ghostty terminal and render-state objects.
* PTY output bytes are fed directly to `ghostty_terminal_vt_write()`.
* The renderer reads visible cells via Ghostty's render-state row iterator.
* BB10 pty output may contain bare LF, so the bridge normalizes bare `\n` to
  `\r\n` before feeding Ghostty to avoid staircase newlines.
* The old Term49 ECMA-48 parser and screen buffer have been removed.

## Signing the release

To distribute Term49 through the BlackBerry signing flow, run `make sign`
after configuring `signing/bbpass`.

## See also

* [Term48 in BlackBerry AppWorld](http://appworld.blackberry.com/webstore/content/26272878/)

[libsdl]: https://github.com/mordak/SDL/tree/term48
[tco]: https://github.com/blackberry/TouchControlOverlay
[libconfig]: http://www.hyperrealm.com/libconfig/
