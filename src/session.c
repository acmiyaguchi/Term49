/*
 * Single-session implementation. Every entry point is a 1:1 forwarder over
 * the existing io_* singletons so behavior is byte-identical to the pre-seam
 * code. Multi-session (#4) replaces these bodies with per-session pty/bridge
 * ownership without changing the session.h contract or its callers.
 */

#include <stdlib.h>

#include "session.h"
#include "io.h"
#include "terminal.h"

struct t49_session {
	t49_session_id_t id;
};

int session_create(t49_session_t **out, t49_session_id_t id) {
	t49_session_t *s;
	if (out == NULL) {
		return -1;
	}
	s = calloc(1, sizeof(*s));
	if (s == NULL) {
		return -1;
	}
	s->id = id;
	*out = s;
	return 0;
}

void session_destroy(t49_session_t *s) {
	/* This stage owns nothing: the io master fd and ghostty bridge are torn
	 * down by app_shutdown() after this returns. #4 moves per-session
	 * pty/bridge teardown here. */
	free(s);
}

t49_session_id_t session_id(const t49_session_t *s) {
	return s ? s->id : 0;
}

int session_master_fd(const t49_session_t *s) {
	(void)s;
	return io_get_master();
}

ssize_t session_write_text(t49_session_t *s, const UChar *buf, size_t n) {
	(void)s;
	return io_write_master(buf, n);
}

ssize_t session_write_bytes(t49_session_t *s, const char *buf, size_t n) {
	(void)s;
	return io_write_master_char(buf, n);
}

int session_resize(t49_session_t *s, int cols, int rows) {
	/* Window-driven resize still flows through rescreen()/setup_screen_size()
	 * in this stage; this forwarder exists so #4 can move the pty/bridge
	 * resize here without changing callers. */
	(void)s;
	(void)cols;
	(void)rows;
	return 0;
}

int session_dispatch_action(t49_session_t *s, const t49_action_t *a) {
	if (s == NULL || a == NULL) {
		return 0;
	}

	switch (a->kind) {
	case T49_ACTION_SEND_BYTES:
		return send_metamode_keystrokes(a->as.bytes.data);
	case T49_ACTION_SEND_TERMINFO:
		return send_metamode_keystrokes(a->as.terminfo_name);
	case T49_ACTION_BUILTIN:
		if (a->as.builtin.id == T49_BUILTIN_PASTE_CLIPBOARD) {
			io_paste_from_clipboard();
			return 1;
		}
		return 0;
	}

	return 0;
}
