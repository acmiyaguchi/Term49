# Term50

Term50 is a terminal emulator for BlackBerry 10. It uses `libghostty-vt` as
its terminal parser/state model and renders the visible grid directly through
the BB10 Screen API + FreeType.

The [current release](https://github.com/acmiyaguchi/Term50/releases) requires OS version >= 10.3.

## Development

To compile Term50 you need the BlackBerry 10 NDK. There are no vendored ARM
prebuilts -- everything links against headers + libraries that ship with the
NDK (`libscreen`, `libbps`, `libfreetype`, `libicu*`, `libclipboard`).

Third-party source checkouts (libghostty-vt, Lua 5.4) live under `vendor/`.
After cloning, run `git submodule update --init --recursive`. See
`vendor/README.md` and `vendor/manifest.md` for the dependency inventory and
build notes.
The Ghostty VT core is vendored under `vendor/libghostty-vt/`. The top-level
Makefile builds `vendor/libghostty-vt/build/ghostty/lib/libghostty-vt.a` on
demand before linking Term50. Lua 5.4 is vendored under `vendor/lua/` and
compiled into a static `liblua.a` by the Makefile; Term50 uses it as the
config language (`~/.term.lua`) and as the scripting runtime for
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

Term50 itself is the primary application example. The small standalone checks
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

Term50 runs multiple shells in a single app instance. Each tab owns its
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
custom keybindings or scripted tab opening from `.term.lua` work with
no extra wiring.

## Cross-app invoke

Term50 registers the **`term://` URI scheme** as a navigator invoke target
(`bar-descriptor.xml`), so a tapped link, a homescreen shortcut, another app, or
one of our own notifications can ask the OS to open Term50 and act. This is the
idiomatic cross-app entry point on BB10 — anything that can open a URI can drive
Term50 — and is distinct from the #5 control socket: the socket is for processes
*inside* Term50's sandbox; the URI handler is for openers *outside* it.

The scheme is opened with the standard `bb.action.OPEN` action. Grammar:

| URI | Effect |
|-----|--------|
| `term://tab` | Open a new (empty) tab. |
| `term://tab/N` | Focus the 1-indexed tab `N` (the same numbering as the tab strip), if it is still live; otherwise open a fresh tab. |
| `term://focus` | Re-foreground only (the OS already raised us). |

Unknown verbs (and any query string) are ignored.

> **Navigation-only by design.** The scheme cannot run commands or inject input
> — it only opens or focuses tabs. Because *any* app, or even a tapped web link,
> can open a `term://` URI, letting it drive the shell would be the same abuse
> surface as an OSC 52 paste but reachable from a link. Running a command is
> therefore restricted to the in-sandbox control socket (#5, `$TERMCTL_SOCKET`),
> which only Term50's own child processes can reach.

## Notifications

Term50 exposes two distinct primitives (#35), named consistently across the
control socket and Lua:

* **toast** — a transient, auto-dismissing flash. No Hub entry, so it never
  accumulates. `term.toast("msg")`, or the `toast:<msg>` action string.
* **notify** — a persistent, **replaceable** BlackBerry Hub entry.

### Replaceable Hub entries

The Hub reuse key is the `(app_id, item_id)` pair: posting again with the same
pair **updates that entry in place** instead of stacking a new one. Term50 makes
reuse the default — `item_id` is the logical slot, and a repeated post to the
same slot replaces it. This keeps high-frequency callers (build status, job
progress) from clogging the Hub.

Control socket:

```sh
termctl notify --id build --title "Build" --body "job #1"   # one Hub entry
termctl notify --id build --title "Build" --body "job #2"   # SAME entry, updated
termctl notify --id deploy --body "shipping"                # a separate entry
```

| Flag | Meaning | Default |
|------|---------|---------|
| `--id S` | logical slot; same id replaces in place | a single shared slot |
| `--title T` | Hub entry title | `Term50` |
| `--body B` | subtitle / body text | none |
| `--uri U` | `term://…` invoke payload (see below) | no invoke |
| `--app-id A` | route through a specific app identity | Term50's own identity |
| `--alert` | post with sound/vibrate (`notification_alert`) | silent (`notification_notify`) |

Lua takes the same fields as a table (or a bare string for the body):

```lua
term.notify{ id = "build", title = "Build", body = "job #2" }
term.notify("quick status")           -- body only, default slot
```

`app_id`/`item_id` are restricted to `[A-Za-z0-9_]`; other bytes are folded to
`_` so a caller's name still maps to one stable slot. Foreign `--app-id` values
are passed through, but reuse is only reliable for Term50's own identity (the
default). The invoke `target`/`action` are intentionally **not** exposed: the
only invocation Term50 routes is back into itself, so the target is fixed to its
own id and the action to `bb.action.OPEN`. (Posting needs the
`post_notification` permission, already in `bar-descriptor.xml`.)

A device-side proof of concept at
`/accounts/1000/shared/documents/scripts/bb10-notify.py` exercises the same
`libbps` setters (`notification_message_set_app_id` / `_item_id` / `_title` /
`_subtitle` / `_invocation_*`) directly via `ctypes`, and mirrors this flag
shape.

### Round-trip: notify, then tap to return

Give a notification a `term://` invoke URI and tapping it re-foregrounds Term50
and acts on the URI. To jump back to a specific tab, point it at that tab:

```sh
termctl notify --id return --body "tap to return" --uri term://tab/2
```

To verify: post it, switch to another tab, open the notification center, and tap
the Term50 entry — you should land on tab 2.

### Discoverability

* **Another app** finds Term50 the BB10-native way: `navigator_invoke_query()`
  reports Term50 as a `term://` handler because the target is declared in the
  bar descriptor — no out-of-band registry needed.
* **An agent inside a shell** has the capability summary bundled on-device at
  `$TERMCTL_AGENT_DOC` (`cat "$TERMCTL_AGENT_DOC"`), and the control socket's
  `termctl help` lists both this socket and the `term://` scheme.

## Distributing

`make package-release` builds the optimized binaries into `Device-Release/` and
packages a release bar (`Term50.bar`) — no `-devMode`, so the manifest reports
`Application-Development-Mode: false` (unlike a `make package-dev` bar).

BlackBerry's code-signing / BBID servers are gone, so the bar can no longer be
signed for BlackBerry World. The unsigned release bar therefore still carries a
locally-derived `test`-prefixed `Package-Id`/`Package-Author-Id` (a `testRel_…`
prefix, versus a dev bar's `testDev_…`); that prefix is unavoidable without
signing. Distribute it by **sideloading** (e.g. Sachesi, or `blackberry-deploy`
to a device with Development Mode on); recipients install it the same way.

The legacy `make sign` target (and `signing/`) is kept for reference only; it
depends on signing infrastructure that no longer exists.

## See also

* [Term48 in BlackBerry AppWorld](http://appworld.blackberry.com/webstore/content/26272878/)
