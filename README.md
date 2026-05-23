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
drops the tab immediately. A shell that exits on its own (e.g. you
type `exit`) closes the same way — the SIGCHLD reaper drops the tab
as soon as the child is reaped. The app only quits once the last tab
is gone.

All four actions are also reachable from Lua as
`term.action("tab_new")`, `"tab_next"`, `"tab_prev"`, `"tab_close"`, so
custom keybindings or scripted tab opening from `.term49.lua` work with
no extra wiring.

## Cross-app invoke

Term49 registers the **`term49://` URI scheme** as a navigator invoke target
(`bar-descriptor.xml`), so a tapped link, a homescreen shortcut, another app, or
one of our own notifications can ask the OS to open Term49 and act. This is the
idiomatic cross-app entry point on BB10 — anything that can open a URI can drive
Term49 — and is distinct from the #5 control socket: the socket is for processes
*inside* Term49's sandbox; the URI handler is for openers *outside* it.

The scheme is opened with the standard `bb.action.OPEN` action. Grammar:

| URI | Effect |
|-----|--------|
| `term49://tab` | Open a new (empty) tab. |
| `term49://tab?cmd=<enc>` | Open a new tab and run `<cmd>`. |
| `term49://tab/N` | Focus the 1-indexed tab `N` (the same numbering as the tab strip), if it is still live. |
| `term49://tab/N?cmd=<enc>` | Focus tab `N` (or open a fresh tab if `N` is stale) and run `<cmd>`. |
| `term49://focus` | Re-foreground only (the OS already raised us). |

`cmd` is a single command, **percent-encoded** (it is a URI component, so a
space is `%20`): `term49://tab?cmd=ssh%20server`. It is written to the resolved
tab's shell with a trailing newline. Unknown verbs are ignored.

> **Trust:** any app *or web link* on the device can open a `term49://` URI, and
> a `cmd` is written straight to the shell — the same risk surface as an OSC 52
> paste, but reachable from as little as a tapped link. A hostile opener can run
> arbitrary commands in your shell. Accepted for now; revisit if the threat
> model tightens (e.g. prompt before running, or require a token).

### Round-trip: notify, then tap to return

The built-in action **`notify_invoke:<message>`** closes the loop without a
second app. It posts a persistent notification-center entry whose invocation
opens `term49://tab/<the posting tab>`. Tapping the notification re-foregrounds
Term49 and jumps to the tab that posted it.

It is a normal action string, so trigger it however you bind actions:

* **Keybinding** — the reference config binds it to metamode + `k`
  (`share/term49.lua.reference`).
* **Control socket** — `termctl run "notify_invoke:back to shell"`.
* **Lua** — `term.action("notify_invoke:back to shell")`.

To verify: post the notification, switch to another tab, open the notification
center, and tap the Term49 entry — you should land back on the originating tab.
(Posting needs the `post_notification` permission, already in
`bar-descriptor.xml`.)

## Signing the release

To distribute Term49 through the BlackBerry signing flow, run `make sign`
after configuring `signing/bbpass`.

## See also

* [Term48 in BlackBerry AppWorld](http://appworld.blackberry.com/webstore/content/26272878/)
