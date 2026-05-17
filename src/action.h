/*
 * Typed Term49 actions/commands.
 *
 * Keybindings, future OSC/control-socket commands, Lua hooks, and touch UI
 * should converge on this representation before dispatch through app code.
 */

#ifndef ACTION_H_
#define ACTION_H_

#include <stddef.h>

typedef enum t49_action_kind {
	T49_ACTION_SEND_BYTES,
	T49_ACTION_SEND_TERMINFO,
	T49_ACTION_BUILTIN,
} t49_action_kind_t;

typedef enum t49_builtin_action {
	T49_BUILTIN_ALT_DOWN,
	T49_BUILTIN_CTRL_DOWN,
	T49_BUILTIN_RESCREEN,
	T49_BUILTIN_PASTE_CLIPBOARD,
	T49_BUILTIN_KEYBOARD_SHOW,
	T49_BUILTIN_KEYBOARD_HIDE,
	T49_BUILTIN_NOTIFY,
	T49_BUILTIN_OPEN_URL,
	T49_BUILTIN_TAB_NEW,
	T49_BUILTIN_TAB_NEXT,
	T49_BUILTIN_TAB_PREV,
	T49_BUILTIN_TAB_CLOSE,
} t49_builtin_action_t;

typedef struct t49_action {
	t49_action_kind_t kind;
	union {
		struct {
			const char *data;
			size_t len;
		} bytes;

		const char *terminfo_name;

		struct {
			t49_builtin_action_t id;
			const char *arg;
			size_t arg_len;
		} builtin;
	} as;
} t49_action_t;

#endif /* ACTION_H_ */
