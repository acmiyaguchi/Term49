/*
 * Single-session implementation. The session owns its ghostty_bridge_t and
 * its pty master fd. child_pid still lives on a main.c global at this stage
 * and lands here in step 2 of #4.
 */

#include <stdlib.h>
#include <unistd.h>

#include "session.h"
#include "ghostty_bridge.h"
#include "io.h"
#include "terminal.h"

struct session {
	session_id_t id;
	ghostty_bridge_t *bridge;
	int master_fd;          /* -1 until session_set_master_fd() runs */
	pid_t child_pid;        /* 0 until session_set_child_pid() runs */
	int exited;             /* set by session_mark_exited via SIGCHLD reaper */
	int exit_status;        /* raw waitpid() status; valid when exited */
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
	s->master_fd = -1;
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
	if (s->master_fd >= 0) {
		close(s->master_fd);
		s->master_fd = -1;
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
	return s ? s->master_fd : -1;
}

void session_set_master_fd(session_t *s, int fd) {
	if (s == NULL) {
		return;
	}
	s->master_fd = fd;
}

pid_t session_child_pid(const session_t *s) {
	return s ? s->child_pid : 0;
}

void session_set_child_pid(session_t *s, pid_t pid) {
	if (s == NULL) {
		return;
	}
	s->child_pid = pid;
}

int session_is_exited(const session_t *s) {
	return s ? s->exited : 0;
}

int session_exit_status(const session_t *s) {
	return s ? s->exit_status : 0;
}

void session_mark_exited(session_t *s, int status) {
	if (s == NULL || s->exited) {
		return;
	}
	s->exited = 1;
	s->exit_status = status;
	/* The shell is gone; close the master so select() stops waking on the
	 * fd's EOF. session_master_fd() now returns -1 and FD_SET will skip it.
	 * Scrollback in the bridge persists so the user can read the final
	 * output until they dismiss the [exited] tab. */
	if (s->master_fd >= 0) {
		close(s->master_fd);
		s->master_fd = -1;
	}
}

ssize_t session_write_text(session_t *s, const UChar *buf, size_t n) {
	if (s == NULL || s->master_fd < 0) {
		return -1;
	}
	return io_write_master(s->master_fd, buf, n);
}

ssize_t session_write_bytes(session_t *s, const char *buf, size_t n) {
	if (s == NULL || s->master_fd < 0) {
		return -1;
	}
	return io_write_master_char(s->master_fd, buf, n);
}

ssize_t session_read_bytes(session_t *s, char *buf, size_t n) {
	if (s == NULL || s->master_fd < 0) {
		return -1;
	}
	return io_read_master_raw(s->master_fd, buf, n);
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
			if (s->master_fd >= 0) {
				io_paste_from_clipboard(s->master_fd);
			}
			return 1;
		}
		return 0;
	}

	return 0;
}
