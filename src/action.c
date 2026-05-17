#include <string.h>

#include "action.h"

static int is_terminfo_name(const char *value) {
	if (value == NULL || value[0] != 'k') {
		return 0;
	}

	return strcmp(value, "kcub1") == 0 ||
	       strcmp(value, "kcud1") == 0 ||
	       strcmp(value, "kcuf1") == 0 ||
	       strcmp(value, "kcuu1") == 0 ||
	       strcmp(value, "khome") == 0 ||
	       strcmp(value, "kend") == 0 ||
	       strcmp(value, "kf1") == 0 ||
	       strcmp(value, "kf2") == 0 ||
	       strcmp(value, "kf3") == 0 ||
	       strcmp(value, "kf4") == 0 ||
	       strcmp(value, "kf5") == 0 ||
	       strcmp(value, "kf6") == 0 ||
	       strcmp(value, "kf7") == 0 ||
	       strcmp(value, "kf8") == 0 ||
	       strcmp(value, "kf9") == 0 ||
	       strcmp(value, "kf10") == 0 ||
	       strcmp(value, "kf11") == 0 ||
	       strcmp(value, "kf12") == 0;
}

static int parse_builtin(const char *value, t49_builtin_action_t *out) {
	if (strcmp(value, "alt_down") == 0) {
		*out = T49_BUILTIN_ALT_DOWN;
		return 1;
	}
	if (strcmp(value, "ctrl_down") == 0) {
		*out = T49_BUILTIN_CTRL_DOWN;
		return 1;
	}
	if (strcmp(value, "rescreen") == 0) {
		*out = T49_BUILTIN_RESCREEN;
		return 1;
	}
	if (strcmp(value, "paste_clipboard") == 0) {
		*out = T49_BUILTIN_PASTE_CLIPBOARD;
		return 1;
	}
	return 0;
}

int action_parse(const char *value, t49_action_t *out) {
	t49_builtin_action_t builtin;

	if (value == NULL || out == NULL) {
		return 0;
	}

	if (parse_builtin(value, &builtin)) {
		out->kind = T49_ACTION_BUILTIN;
		out->as.builtin.id = builtin;
		out->as.builtin.arg = NULL;
		out->as.builtin.arg_len = 0;
		return 1;
	}

	if (is_terminfo_name(value)) {
		out->kind = T49_ACTION_SEND_TERMINFO;
		out->as.terminfo_name = value;
		return 1;
	}

	out->kind = T49_ACTION_SEND_BYTES;
	out->as.bytes.data = value;
	out->as.bytes.len = strlen(value);
	return 1;
}

int action_is_builtin(const t49_action_t *action, t49_builtin_action_t id) {
	return action != NULL &&
	       action->kind == T49_ACTION_BUILTIN &&
	       action->as.builtin.id == id;
}
