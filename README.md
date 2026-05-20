# Term49

Term49 is a terminal emulator for BlackBerry 10. It uses `libghostty-vt` as
its terminal parser/state model and renders the visible grid directly through
the BB10 Screen API + FreeType.

The [current release](https://github.com/BerryFarm/Term49/releases) requires OS version >= 10.3.

## Development

To compile Term49 you need the BlackBerry 10 NDK. There are no vendored ARM
prebuilts -- everything links against headers + libraries that ship with the
NDK (`libscreen`, `libbps`, `libfreetype`, `libicu*`, `libclipboard`).

Third-party source checkouts (libghostty-vt, Lua 5.4) live under `vendor/`.
After cloning, run `git submodule update --init --recursive`. See
`vendor/README.md` and `vendor/manifest.md` for the dependency inventory and
build notes.
The Ghostty VT core is vendored under `vendor/libghostty-vt/`. The top-level
Makefile builds `vendor/libghostty-vt/build/ghostty/lib/libghostty-vt.a` on
demand before linking Term49. Lua 5.4 is vendored under `vendor/lua/` and
compiled into a static `liblua.a` by the Makefile; Term49 uses it as the
config language (`~/.term49.lua`) and as the scripting runtime for
`lua:<fn>` keybindings.

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

* `src/platform_screen.c` owns the BB10 Screen window, BPS event pump
  (key/touch/navigator/VKB), and the platform-services vtable.
* `src/renderer_screen.c` owns the framebuffer view, the glyph cache, and the
  symmenu surface cache; it composes frames directly into the active Screen
  back buffer (RGBA8888) and posts via `screen_post_window`.
* `src/font.c` is a thin FreeType wrapper that returns project-owned RGBA
  bitmaps with shaded-fg/bg semantics (matching the old `TTF_RenderUNICODE_Shaded`
  contract).
* `src/ghostty_bridge.c` owns the Ghostty terminal and render-state objects.
* PTY output bytes are fed directly to `ghostty_terminal_vt_write()`.
* The renderer reads visible cells via Ghostty's render-state row iterator.
* BB10 pty output may contain bare LF, so the bridge normalizes bare `\n` to
  `\r\n` before feeding Ghostty to avoid staircase newlines.

## Tabs

Term49 supports multiple shells in a single app instance. Each tab is its
own pty + child shell, and Ghostty bridge with its own scrollback. Output
from background tabs continues to be buffered (their scrollback grows) even
while you're focused on a different tab.

Default keybindings (all in metamode — tap `metamode_doubletap_key` twice
or whatever you have bound, then the letter):

| Metamode key | Action       |
|--------------|--------------|
| `t`          | `tab_new`    |
| `]`          | `tab_next`   |
| `[`          | `tab_prev`   |
| `x`          | `tab_close`  |

A tab strip overlay appears at the top of the screen whenever a tab action
fires. It shows a pill per tab (`1`, `2`, ...) with the active tab
highlighted; exited tabs are marked `1.`. Tap the top edge of the screen
to reveal it manually, or tap any visible pill to jump to that tab; tap
elsewhere or press any key that isn't a tab binding to dismiss.

When a shell exits (e.g. you type `exit`), the tab stays in the strip
showing the shell's final scrollback. Press `tab_close` to dismiss the
`[exited]` tab; the app only quits when the last tab is dismissed.

All tab actions are also reachable from Lua scripting via
`term.action("tab_new")`, etc., so custom keybindings or programmatic tab
opening from `.term49.lua` work without any extra wiring. The maximum
number of simultaneous tabs is `APP_MAX_SESSIONS` (8 by default).

## Signing the release

To distribute Term49 through the BlackBerry signing flow, run `make sign`
after configuring `signing/bbpass`.

## See also

* [Term48 in BlackBerry AppWorld](http://appworld.blackberry.com/webstore/content/26272878/)
