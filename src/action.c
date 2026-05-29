#include <string.h>

#include "action.h"

/* The terminfo cursor/function-key capabilities Term50 understands, each
 * paired with the label the help overlay shows. Single source of truth:
 * is_terminfo_name (config validation, below) and keymap_to_display (help
 * rendering, main.c) both read it, so a new cap is added in one place. */
static const struct { const char *cap; const char *label; } terminfo_caps[] = {
	{"kcuu1", "Up"},   {"kcud1", "Down"}, {"kcuf1", "Right"}, {"kcub1", "Left"},
	{"khome", "Home"}, {"kend",  "End"},
	{"kf1",  "F1"},  {"kf2",  "F2"},  {"kf3",  "F3"},  {"kf4",  "F4"},
	{"kf5",  "F5"},  {"kf6",  "F6"},  {"kf7",  "F7"},  {"kf8",  "F8"},
	{"kf9",  "F9"},  {"kf10", "F10"}, {"kf11", "F11"}, {"kf12", "F12"},
};

const char *terminfo_display_name(const char *value) {
	if (value == NULL) {
		return NULL;
	}
	for (size_t i = 0; i < sizeof(terminfo_caps) / sizeof(terminfo_caps[0]); ++i) {
		if (strcmp(value, terminfo_caps[i].cap) == 0) {
			return terminfo_caps[i].label;
		}
	}
	return NULL;
}

static int is_terminfo_name(const char *value) {
	return terminfo_display_name(value) != NULL;
}

static int parse_builtin(const char *value, builtin_action_t *out) {
	if (strcmp(value, "alt_down") == 0) {
		*out = TERM_BUILTIN_ALT_DOWN;
		return 1;
	}
	if (strcmp(value, "ctrl_down") == 0) {
		*out = TERM_BUILTIN_CTRL_DOWN;
		return 1;
	}
	if (strcmp(value, "rescreen") == 0) {
		*out = TERM_BUILTIN_RESCREEN;
		return 1;
	}
	if (strcmp(value, "paste_clipboard") == 0) {
		*out = TERM_BUILTIN_PASTE_CLIPBOARD;
		return 1;
	}
	if (strcmp(value, "keyboard_show") == 0) {
		*out = TERM_BUILTIN_KEYBOARD_SHOW;
		return 1;
	}
	if (strcmp(value, "keyboard_hide") == 0) {
		*out = TERM_BUILTIN_KEYBOARD_HIDE;
		return 1;
	}
	if (strcmp(value, "font_size_increase") == 0) {
		*out = TERM_BUILTIN_FONT_SIZE_INCREASE;
		return 1;
	}
	if (strcmp(value, "font_size_decrease") == 0) {
		*out = TERM_BUILTIN_FONT_SIZE_DECREASE;
		return 1;
	}
	if (strcmp(value, "font_size_reset") == 0) {
		*out = TERM_BUILTIN_FONT_SIZE_RESET;
		return 1;
	}
	if (strcmp(value, "reload_config") == 0) {
		*out = TERM_BUILTIN_RELOAD_CONFIG;
		return 1;
	}
	if (strcmp(value, "metamode_toggle") == 0) {
		*out = TERM_BUILTIN_METAMODE_TOGGLE;
		return 1;
	}
	if (strcmp(value, "help_overlay") == 0) {
		*out = TERM_BUILTIN_HELP_OVERLAY;
		return 1;
	}
	if (strcmp(value, "url_pick") == 0) {
		*out = TERM_BUILTIN_URL_PICK;
		return 1;
	}
	if (strcmp(value, "tab_new") == 0) {
		*out = TERM_BUILTIN_TAB_NEW;
		return 1;
	}
	if (strcmp(value, "tab_next") == 0) {
		*out = TERM_BUILTIN_TAB_NEXT;
		return 1;
	}
	if (strcmp(value, "tab_prev") == 0) {
		*out = TERM_BUILTIN_TAB_PREV;
		return 1;
	}
	if (strcmp(value, "tab_close") == 0) {
		*out = TERM_BUILTIN_TAB_CLOSE;
		return 1;
	}
	return 0;
}

int action_parse(const char *value, action_t *out) {
	builtin_action_t builtin;

	if (value == NULL || out == NULL) {
		return 0;
	}

	/* Parsed keybindings always target the active session; #4/#5 set this
	 * explicitly when routing to a specific session. */
	out->target.session = 0;

	/* "lua:<fn>" binds a key to a no-arg Lua function from .term.lua.
	 * arg points into `value` (the keymap's heap-owned ->to string), the
	 * same lifetime model as TERM_ACTION_SEND_BYTES below. */
	if (strncmp(value, "lua:", 4) == 0) {
		out->kind = TERM_ACTION_BUILTIN;
		out->as.builtin.id = TERM_BUILTIN_LUA_CALL;
		out->as.builtin.arg = value + 4;
		out->as.builtin.arg_len = strlen(value + 4);
		return 1;
	}

	/* "toast:<msg>" / "open_url:<uri>" carry an argument the same way
	 * "lua:<fn>" does: arg points into `value` (the keymap's heap-owned
	 * ->to string), so it lives as long as the binding. */
	if (strncmp(value, "toast:", 6) == 0) {
		out->kind = TERM_ACTION_BUILTIN;
		out->as.builtin.id = TERM_BUILTIN_TOAST;
		out->as.builtin.arg = value + 6;
		out->as.builtin.arg_len = strlen(value + 6);
		return 1;
	}
	if (strncmp(value, "open_url:", 9) == 0) {
		out->kind = TERM_ACTION_BUILTIN;
		out->as.builtin.id = TERM_BUILTIN_OPEN_URL;
		out->as.builtin.arg = value + 9;
		out->as.builtin.arg_len = strlen(value + 9);
		return 1;
	}

	if (parse_builtin(value, &builtin)) {
		out->kind = TERM_ACTION_BUILTIN;
		out->as.builtin.id = builtin;
		out->as.builtin.arg = NULL;
		out->as.builtin.arg_len = 0;
		return 1;
	}

	if (is_terminfo_name(value)) {
		out->kind = TERM_ACTION_SEND_TERMINFO;
		out->as.terminfo_name = value;
		return 1;
	}

	out->kind = TERM_ACTION_SEND_BYTES;
	out->as.bytes.data = value;
	out->as.bytes.len = strlen(value);
	return 1;
}

int action_is_builtin(const action_t *action, builtin_action_t id) {
	return action != NULL &&
	       action->kind == TERM_ACTION_BUILTIN &&
	       action->as.builtin.id == id;
}
