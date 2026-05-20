/*
 * Single-session implementation. The session now owns its own
 * ghostty_bridge_t (constructed at create time). The pty master fd and
 * child_pid still live in main.c / io.c globals at this stage; step 1.5 and
 * step 2 of #4 move them into the session and finish closing this seam.
 */

#include <stdlib.h>

#include "session.h"
#include "ghostty_bridge.h"
#include "io.h"
#include "terminal.h"

struct session {
	session_id_t id;
	ghostty_bridge_t *bridge;
};

int session_create(session_t **out, session_id_t id,
                   uint16_t cols, uint16_t rows, size_t max_scrollback) {
	session_t *s;
	if (out == NULL) {
		return -1;
	}
	s = calloc(1, sizeof(*s));
	if (s == NULL) {
		return -1;
	}
	s->id = id;
	if (ghostty_bridge_create(&s->bridge, cols, rows, max_scrollback, s) != 0) {
		free(s);
		return -1;
	}
	*out = s;
	return 0;
}

void session_destroy(session_t *s) {
	if (s == NULL) {
		return;
	}
	ghostty_bridge_destroy(s->bridge);
	free(s);
}

session_id_t session_id(const session_t *s) {
	return s ? s->id : 0;
}

ghostty_bridge_t *session_bridge(session_t *s) {
	return s ? s->bridge : NULL;
}

int session_master_fd(const session_t *s) {
	(void)s;
	return io_get_master();
}

ssize_t session_write_text(session_t *s, const UChar *buf, size_t n) {
	(void)s;
	return io_write_master(buf, n);
}

ssize_t session_write_bytes(session_t *s, const char *buf, size_t n) {
	(void)s;
	return io_write_master_char(buf, n);
}

int session_dispatch_action(session_t *s, const action_t *a) {
	if (s == NULL || a == NULL) {
		return 0;
	}

	switch (a->kind) {
	case TERM_ACTION_SEND_BYTES:
		return send_metamode_keystrokes(a->as.bytes.data);
	case TERM_ACTION_SEND_TERMINFO:
		return send_metamode_keystrokes(a->as.terminfo_name);
	case TERM_ACTION_BUILTIN:
		if (a->as.builtin.id == TERM_BUILTIN_PASTE_CLIPBOARD) {
			io_paste_from_clipboard();
			return 1;
		}
		return 0;
	}

	return 0;
}
