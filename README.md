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

Term49 runs multiple shells in a single app instance. Each tab owns its
own pty, child shell, and Ghostty bridge (so each tab has its own
scrollback). Background tabs keep consuming output while you're focused
on another tab.

Default keybindings sit under metamode (double-tap `metamode_doubletap_key`
— right shift by default — then the letter). The set mirrors tmux's
window-management keys; all four are single unmodified letters, so no
`sym` or `alt` is needed once you're in metamode:

| Metamode key | Action      | tmux analogue   |
|--------------|-------------|-----------------|
| `c`          | `tab_new`   | `prefix c`      |
| `n`          | `tab_next`  | `prefix n`      |
| `p`          | `tab_prev`  | `prefix p`      |
| `x`          | `tab_close` | `prefix &`      |

To make room for `c` and `p`, the previous defaults moved: `ctrl_down`
is now `d`, and `font_size_reset` is now `z`.

A one-row tab strip sits at the top of the screen whenever more than
one tab is open, so it's always there to track. With a single tab the
strip stays hidden, but flashes in on any tab action and on a top-edge
tap; the next non-tab keypress dismisses it. Each tab shows as a ` N `
pill (1-indexed); the active tab inverts to white-on-black, and an
exited tab renders as ` N. `. A green ` + ` pill follows the last tab
while you're under the `APP_MAX_SESSIONS` cap (8).

The strip is touchable: tap a numbered pill to jump to that tab, tap
`+` to open a new tab, or tap the top edge while it's hidden to reveal
it. Tapping anywhere else on the strip dismisses it.

`tab_close` is a single-press kill: it SIGHUPs the live shell and
drops the tab immediately. If a shell exits on its own (e.g. you type
`exit`), the tab stays put so you can read the final scrollback — the
pill renders as ` N. ` (the `.` is a monospace stand-in for a struck-
through label), and another `tab_close` dismisses it. The app only
quits once the last tab is gone.

All four actions are also reachable from Lua as
`term.action("tab_new")`, `"tab_next"`, `"tab_prev"`, `"tab_close"`, so
custom keybindings or scripted tab opening from `.term49.lua` work with
no extra wiring.

## Signing the release

To distribute Term49 through the BlackBerry signing flow, run `make sign`
after configuring `signing/bbpass`.

## See also

* [Term48 in BlackBerry AppWorld](http://appworld.blackberry.com/webstore/content/26272878/)
