#!/bin/sh
# term-loop.sh -- headless automation harness for Term50, driven through the
# termctl control socket. Three jobs:
#   1. diagnose the real tab-creation ceiling   (probe)
#   2. stress-test the pty FD_CLOEXEC leak fix   (stress)
#   3. general scriptable driver                 (status/spawn/run/fanout/watch)
#
# Runs ON the device (QNX, busybox/ksh-ish sh -- so: no `head`/`tail`, no
# `local`, parse key=value with shell word-split + `case`, pty scan via `pidin`).
# Drive it from a laptop over dev-mode ssh, e.g.:
#   SOCK=/accounts/1000/shared/documents/.term/control.sock
#   DIR=/accounts/1000/shared/documents/scratch/term-loop
#   bb-ssh "cd $DIR && TERMCTL_SOCKET=$SOCK TERMCTL=./termctl ./term-loop.sh probe"
#
# Reaching the socket as devuser requires the TERM50_CONTROL_SHARED opt-in build
# (socket 0660 + 1000_shared group). Without it, run this inside a Term50 tab,
# where termctl is on PATH and $TERMCTL_SOCKET is already set for the app's uid.

TC=${TERMCTL:-termctl}            # path to the termctl binary (PATH or ./termctl)

tc() { "$TC" "$@"; }             # exit status = frame flag: 0 ok, 1 err, 2 conn/arg

# field "<key=val key=val ...>" <key>  ->  the value, or empty
field() {
	for _kv in $1; do
		case $_kv in "$2"=*) echo "${_kv#*=}"; return 0;; esac
	done
	return 1
}

# count=N  ->  N
session_count() { field "$(tc sessions 2>/dev/null)" count; }

# --- pty pool inspection (device-only; needs pidin) -----------------------
# `pidin fd` walks every process's fd table -- expensive on the device -- so we
# snapshot it ONCE into $_PTY_SNAP and tally all 8 pairs against that capture,
# instead of re-forking pidin per pair (was 8 scans per call).
pty_snapshot() { _PTY_SNAP=$(pidin fd 2>/dev/null); }

pty_busy() {  # arg: pair number 0..7  ->  open fd refs in $_PTY_SNAP (0 == free)
	# echo (not printf: this device sh has no printf) the captured snapshot.
	echo "$_PTY_SNAP" | grep -c -E "/dev/[pt]typ$1( |\$)"
}
pty_free_count() {  # refreshes the snapshot
	pty_snapshot
	_f=0; _n=0
	while [ "$_n" -le 7 ]; do
		[ "$(pty_busy "$_n")" -eq 0 ] && _f=$((_f + 1))
		_n=$((_n + 1))
	done
	echo "$_f"
}
pty_scan() {  # refreshes the snapshot
	pty_snapshot
	_n=0
	while [ "$_n" -le 7 ]; do
		_r=$(pty_busy "$_n")
		if [ "$_r" -eq 0 ]; then echo "  ttyp$_n FREE"
		else echo "  ttyp$_n BUSY($_r)"; fi
		_n=$((_n + 1))
	done
}

# Reserved action names: send-text routes through the action parser, so a bare
# one of these would be EXECUTED as an action rather than typed. (src/action.c)
RESERVED="tab_new tab_next tab_prev tab_close reload_config url_pick rescreen
paste_clipboard keyboard_show keyboard_hide metamode_toggle help_overlay
alt_down ctrl_down font_size_increase font_size_decrease font_size_reset"

is_reserved() {  # arg: first token of a command
	for _w in $RESERVED; do [ "$1" = "$_w" ] && return 0; done
	return 1
}

_ensure_socket() {
	if ! tc sessions >/dev/null 2>&1; then
		echo "term-loop: control socket unreachable via '$TC'" >&2
		echo "  TERMCTL_SOCKET=${TERMCTL_SOCKET:-<unset, falls back to \$HOME/.term/control.sock>}" >&2
		echo "  - over ssh you need the TERM50_CONTROL_SHARED opt-in build (0660 socket)" >&2
		echo "  - otherwise run this inside a Term50 tab" >&2
		exit 2
	fi
}

# --- subcommands ----------------------------------------------------------

cmd_status() {
	_ensure_socket
	echo "sessions: $(session_count)"
	echo "tabs:"
	tc tabs stats | while IFS= read -r _l; do echo "  $_l"; done
	echo "ptys:"
	pty_scan
}

# Spawn tabs until tab_new fails; report the real ceiling. tab_new bypasses the
# UI "+" overflow, so 'created' exceeding the manual limit proves the manual
# blocker was the tab-strip overflow, not a hard cap.
cmd_probe() {
	_ensure_socket
	_keep=0
	[ "$1" = "--keep" ] && _keep=1
	_base=$(session_count)
	_made=0
	_reason=
	while :; do
		_out=$(tc action tab_new 2>&1)
		if [ $? -ne 0 ]; then
			_reason=$_out
			break
		fi
		_made=$((_made + 1))
		[ "$_made" -ge 64 ] && { _reason="(safety stop at 64)"; break; }
	done
	_final=$(session_count)
	echo "baseline=$_base created=$_made final_count=$_final ceiling=$_final"
	echo "tab_new failed with: ${_reason:-<no reason returned>}"
	echo "ptys at ceiling:"
	pty_scan
	if [ "$_final" -ge 8 ] && [ "$(pty_free_count)" -eq 0 ]; then
		echo "verdict: ceiling=$_final, all 8 ptys busy -> registry cap (8) == pty pool."
	else
		echo "verdict: ceiling=$_final (registry cap APP_MAX_SESSIONS=8 / pty pool)."
	fi
	echo "note: tab_new bypasses the UI '+' overflow -- if 'created' exceeds what"
	echo "      the on-screen '+' allowed, the manual limit was the tab-strip overflow."
	if [ "$_keep" -eq 0 ]; then
		echo "cleaning back to baseline ($_base)..."
		while [ "$(session_count)" -gt "$_base" ]; do
			tc action tab_close >/dev/null 2>&1 || break
		done
		echo "final_count=$(session_count)"
	fi
}

cmd_spawn() {
	_ensure_socket
	_want=${1:-1}
	_i=0
	while [ "$_i" -lt "$_want" ]; do
		_out=$(tc action tab_new 2>&1)
		if [ $? -ne 0 ]; then
			echo "tab_new failed after $_i: ${_out:-<no reason>}; count=$(session_count)" >&2
			return 1
		fi
		_i=$((_i + 1))
	done
	echo "spawned=$_i count=$(session_count)"
}

# Type a command into the ACTIVE tab and submit it.
cmd_run() {
	_ensure_socket
	_cmd=$1
	[ -z "$_cmd" ] && { echo "run: need a command string" >&2; return 2; }
	set -- $_cmd
	if is_reserved "$1"; then
		echo "run: refusing '$1' -- it is a termctl action name; send-text would" >&2
		echo "     execute it instead of typing it. Use 'action $1' or run in-tab." >&2
		return 2
	fi
	tc send-text "$_cmd" || return 1
	tc send-text "
"            # bare newline submits the line
}

# Run a command in every tab by walking focus with tab_next (no focus-by-index
# over the socket). Best-effort: a shell exiting mid-walk shifts indices.
cmd_fanout() {
	_ensure_socket
	_cmd=$1
	_n=$(session_count)
	_i=0
	while [ "$_i" -lt "$_n" ]; do
		cmd_run "$_cmd"
		tc action tab_next >/dev/null 2>&1
		_i=$((_i + 1))
	done
	echo "fanout to $_n tab(s)"
}

cmd_watch() {
	_ensure_socket
	_iv=${1:-2}
	while :; do
		clear 2>/dev/null || true
		date 2>/dev/null
		echo "sessions: $(session_count)"
		tc tabs stats | while IFS= read -r _l; do echo "  $_l"; done
		sleep "$_iv"
	done
}

# Open/close churn; confirm the pty pool fully recovers each round (validates
# the FD_CLOEXEC fix). Set STRESS_DAEMON=ssh-agent to also spawn a daemon per
# tab -- the inherited-fd case the fix targets.
cmd_stress() {
	_ensure_socket
	_rounds=${1:-20}
	_base_free=$(pty_free_count)
	echo "stress: $_rounds rounds, baseline free ptys=$_base_free, daemon='${STRESS_DAEMON:-none}'"
	_r=1
	while [ "$_r" -le "$_rounds" ]; do
		if ! tc action tab_new >/dev/null 2>&1; then
			echo "round=$_r tab_new FAILED (ceiling) -- stopping" >&2
			break
		fi
		[ -n "$STRESS_DAEMON" ] && cmd_run "$STRESS_DAEMON" >/dev/null 2>&1
		tc action tab_close >/dev/null 2>&1
		# reclaim is async on the BPS pump -- poll until it settles.
		_s=0
		while [ "$_s" -lt 10 ]; do
			_cur=$(pty_free_count)
			[ "$_cur" -ge "$_base_free" ] && break
			sleep 1; _s=$((_s + 1))
		done
		_free=$(pty_free_count)
		_msg="round=$_r free=$_free base=$_base_free sessions=$(session_count)"
		if [ "$_free" -lt "$_base_free" ]; then
			echo "$_msg  LEAK SUSPECT: pty pool did not recover"
		else
			echo "$_msg"
		fi
		_r=$((_r + 1))
	done
	echo "final pty scan:"
	pty_scan
}

usage() {
	cat <<EOF
term-loop.sh -- drive Term50 headlessly via termctl

  status              sessions + tabs stats + pty pool scan
  probe [--keep]      spawn tabs until tab_new fails; report the real ceiling
  spawn N             open N tabs (stops at the ceiling)
  run "<cmd>"         type <cmd> into the active tab and submit
  fanout "<cmd>"      run <cmd> in every tab (walks focus with tab_next)
  watch [interval]    poll sessions/tabs stats (events have no emitter)
  stress [N]          open/close churn N rounds; verify pty pool recovers
                      (STRESS_DAEMON=ssh-agent to exercise the inherited-fd leak)
  help                this text

env:
  TERMCTL         path to termctl binary (default: 'termctl' on PATH)
  TERMCTL_SOCKET  socket path (default: \$HOME/.term/control.sock)
  STRESS_DAEMON   daemon to spawn per round in 'stress'
EOF
}

case ${1:-help} in
	status) cmd_status ;;
	probe)  shift; cmd_probe "$@" ;;
	spawn)  shift; cmd_spawn "$@" ;;
	run)    shift; cmd_run "$@" ;;
	fanout) shift; cmd_fanout "$@" ;;
	watch)  shift; cmd_watch "$@" ;;
	stress) shift; cmd_stress "$@" ;;
	help|-h|--help) usage ;;
	*) echo "term-loop.sh: unknown command '$1'" >&2; usage >&2; exit 2 ;;
esac
