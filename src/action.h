/*
 * Typed Term49 actions/commands.
 *
 * Keybindings, future OSC/control-socket commands, Lua hooks, and touch UI
 * should converge on this representation before dispatch through app code.
 */

#ifndef ACTION_H_
#define ACTION_H_

#include <stddef.h>

#include "term_types.h"

/* Which session an action applies to. session == 0 means "the active
 * session"; every keybinding parsed today resolves to 0, so behavior is
 * unchanged. Control-socket / Lua / TAB_* (#5/#4) set a real id without
 * any signature churn. */
typedef struct action_target {
	session_id_t session;
} action_target_t;

typedef enum action_kind {
	TERM_ACTION_SEND_BYTES,
	TERM_ACTION_SEND_TERMINFO,
	TERM_ACTION_BUILTIN,
} action_kind_t;

typedef enum builtin_action {
	TERM_BUILTIN_ALT_DOWN,
	TERM_BUILTIN_CTRL_DOWN,
	TERM_BUILTIN_RESCREEN,
	TERM_BUILTIN_PASTE_CLIPBOARD,
	TERM_BUILTIN_KEYBOARD_SHOW,
	TERM_BUILTIN_KEYBOARD_HIDE,
	/* arg = message; transient auto-dismiss flash, no Hub entry. The
	 * persistent, replaceable Hub notification is posted via the
	 * control socket / Lua (term.notify), not a keybinding. */
	TERM_BUILTIN_TOAST,
	TERM_BUILTIN_OPEN_URL,
	TERM_BUILTIN_TAB_NEW,
	TERM_BUILTIN_TAB_NEXT,
	TERM_BUILTIN_TAB_PREV,
	TERM_BUILTIN_TAB_CLOSE,
	TERM_BUILTIN_FONT_SIZE_INCREASE,
	TERM_BUILTIN_FONT_SIZE_DECREASE,
	TERM_BUILTIN_FONT_SIZE_RESET,
	/* arg = name of a no-arg Lua function defined in .term49.lua */
	TERM_BUILTIN_LUA_CALL,
	/* re-run .term49.lua and re-apply it live (deferred to a safe point) */
	TERM_BUILTIN_RELOAD_CONFIG,
	/* toggle metamode (the modal meta layer); a chordable alternative to
	 * the doubletap entry. */
	TERM_BUILTIN_METAMODE_TOGGLE,
	/* toggle the on-screen keybinding help overlay. */
	TERM_BUILTIN_HELP_OVERLAY,
} builtin_action_t;

typedef struct action {
	action_kind_t kind;
	action_target_t target;   /* {0} => active session (set by action_parse) */
	union {
		struct {
			const char *data;
			size_t len;
		} bytes;

		const char *terminfo_name;

		struct {
			builtin_action_t id;
			const char *arg;
			size_t arg_len;
		} builtin;
	} as;
} action_t;

int action_parse(const char *value, action_t *out);
int action_is_builtin(const action_t *action, builtin_action_t id);

#endif /* ACTION_H_ */
