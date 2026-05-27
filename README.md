# Term50

Term50 is a terminal emulator for BlackBerry 10.
It uses `libghostty-vt` as its terminal parser/state model and renders the visible grid directly through the BB10 Screen API + FreeType.
It runs multiple shells as tabs, and is configured and scripted in Lua (`~/.term.lua`).

The [current release](https://github.com/acmiyaguchi/Term50/releases) requires OS version >= 10.3.

## Building

You need the BlackBerry 10 NDK (for `qcc` and the packager) and Nix (for the bbnix userland).
Everything links against headers + libraries that ship with the NDK (`libscreen`, `libbps`, `libfreetype`, `libicu*`, `libclipboard`); there are no vendored ARM prebuilts.

Third-party source lives under `vendor/` as submodules (libghostty-vt, Lua 5.4).
After cloning:

```sh
git submodule update --init --recursive
```

Compile the binaries (inside the BBNDK shell, e.g. `nix run .#shell` from the bbdev workspace):

```sh
make            # builds Term50 + termctl
```

## Packaging & deploying

bbnix is a **required** dependency: it supplies the login shell (zsh), the terminfo database, and ssh/tmux/mosh.
The package targets stage it automatically, so they need Nix and a BB10 sysroot (`BBNIX_SYSROOT`); bbnix builds are impure.

```sh
export BBNIX_SYSROOT=/path/to/bbndk-linux
make package-dev      # stage bbnix, build, package a dev-mode Term50.bar
make deploy           # package-dev, then install + launch on the device
```

Device credentials for `deploy`/`connect` live in an untracked `.env` — copy `.env.example` to `.env` and set `BBIP`/`BBPASS`.

## Distributing

```sh
make package-release  # optimized, non-devMode Term50.bar
```

BlackBerry's code-signing / BBID servers are gone, so the bar can't be signed.
Distribute the unsigned release bar by **sideloading** (Sachesi, or `blackberry-deploy` to a device with Development Mode on).

## Tabs

Each tab owns its own pty, child shell, and Ghostty bridge (so its own scrollback); background tabs keep consuming output.
Default tab keys sit under metamode (double-tap the metamode key — right shift by default — then a letter), mirroring tmux's window keys:

| Metamode key | Action      | tmux analogue |
|--------------|-------------|---------------|
| `c`          | `tab_new`   | `prefix c`    |
| `n`          | `tab_next`  | `prefix n`    |
| `p`          | `tab_prev`  | `prefix p`    |
| `x`          | `tab_close` | `prefix &`    |

A one-row tab strip appears at the top when more than one tab is open (and flashes in on any tab action or top-edge tap); tap a numbered pill to jump to a tab or `+` to open one.
All four actions are also callable from Lua, e.g. `term.action("tab_new")`.
The app quits once the last tab is gone.

## Configuration & control surface

`~/.term.lua` configures keybindings and scripts the terminal; see `share/term.lua.reference` for the full reference.
Term50 also exposes a `term://` URI invoke target and an in-sandbox control socket (`termctl`) for notifications and tab control — the bundled `share/AGENTS.md` (also at `$TERMCTL_AGENT_DOC` on-device) documents both surfaces.

## See also

* [Term48 in BlackBerry AppWorld](http://appworld.blackberry.com/webstore/content/26272878/)
