# Term49 — capabilities for agents

Term49 is a native terminal emulator on BlackBerry 10 (QNX). You are most
likely reading this from inside a shell running in a Term49 tab. This file is
the machine-oriented summary of how to drive and integrate with Term49; the
human-facing version is the "Cross-app invoke" and scripting sections of the
project README.

Its absolute on-device path is exported as `$TERM49_AGENT_DOC`, so
`cat "$TERM49_AGENT_DOC"` always finds it.

## Two control surfaces

Term49 separates *driving the terminal from inside* (trusted) from *opening the
app from outside* (untrusted). They have different powers on purpose.

### 1. Control socket — in-sandbox, trusted (issue #5)

A Unix-domain socket reachable only by Term49's own child processes. Its path is
in `$TERM49_CONTROL`. The bundled `termctl` client (on `$PATH`) speaks to it;
`termctl help` prints the live command surface straight from the server (the
authoritative list — prefer it over this file if they ever disagree).

This is the surface that can change the terminal: run actions, send input,
evaluate Lua, read the clipboard, query geometry. Highlights:

    termctl help                  # live command list
    termctl screen size           # cols/rows of the active tab
    termctl sessions              # open tab count
    termctl tab stats [id]        # one tab's stats (id 0/omitted = active)
    termctl tabs stats            # stats for every tab, one line each
    termctl action <name>         # run a keybinding/builtin/lua:fn action
    termctl notify [flags]        # post/update a replaceable Hub entry (below)
    termctl send-text <text>      # type text into the active tab
    termctl eval [lua] <chunk>    # run a Lua chunk, print its return
    termctl clipboard read        # print clipboard text

`action <name>` accepts any Term49 action string, including `toast:<msg>` (a
transient flash) and `lua:<fn>`.

### 2. Cross-app invoke — out-of-sandbox, untrusted, NAVIGATION-ONLY (issue #23)

Term49 registers the **`term49://` URI scheme** (opened with the standard
`bb.action.OPEN` navigator action). Any app — or a tapped web link — can open
such a URI to bring Term49 forward and select a tab. Because the caller is
untrusted, this surface **cannot run commands or inject input**; it only
navigates. To run a command, use the control socket above.

Grammar:

| URI | Effect |
|-----|--------|
| `term49://tab` | open a new (empty) tab |
| `term49://tab/N` | focus the 1-based tab `N` if live, else open a new tab |
| `term49://focus` | re-foreground only |

Unknown verbs and any query string are ignored.

Discover it from another app at runtime with `navigator_invoke_query()` (the
BB10 invocation framework reports Term49 as a `term49://` handler because the
target is declared in `bar-descriptor.xml`). Open one with `navigator_invoke()`
on an `navigator_invoke_invocation_t` whose action is `bb.action.OPEN` and uri
is the `term49://...` string.

## Notifications — replaceable Hub entries (issue #35)

`termctl notify` posts a persistent BlackBerry Hub entry. The reuse key is
`--id`: posting again with the same id **updates that entry in place** instead of
stacking a new one, so high-frequency callers don't clog the Hub.

    termctl notify --id build --title "Build" --body "job #1"   # one entry
    termctl notify --id build --title "Build" --body "job #2"   # updates it
    termctl notify --id deploy --body "shipping"                # separate entry

Flags: `--id S` (slot; same id replaces), `--title T`, `--body B`, `--uri U`
(a `term49://…` invoke payload), `--app-id A` (own identity by default), `--alert`
(sound/vibrate). From Lua: `term.notify{ id=, title=, body=, uri=, alert= }`, or
`term.notify("body")`. The invoke target/action are fixed (own id +
`bb.action.OPEN`) — the only invocation Term49 routes is back into itself.

### Round-trip: a notification that returns to a tab

Give the entry a `term49://` URI and tapping it re-foregrounds Term49 and acts on
the URI — the one built-in producer of a `term49://` invocation (everything else
is a consumer: another app, a link, a shortcut):

    termctl notify --id return --body "back to build" --uri term49://tab/2
