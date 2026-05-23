#include "control.h"

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/un.h>

#include <bps/bps.h>
#include <clipboard/clipboard.h>

#include "control_proto.h"
#include "prefs.h"     /* prefs_lua_eval */

#define CTL_MAX_CLIENTS 8
#define CTL_LINE_MAX    4096
#define CTL_MAX_ARGV    16
#define CTL_DEFER_MAX   8
#define CTL_REPLY_MAX   8192

/* Subscription event bits (reserved for async %-notifications). No publisher
 * emits these yet; subscribe/unsubscribe only track the per-client mask so the
 * wire command is stable for when an emitter lands. CTL_EVENTS_HELP is the
 * single source of truth for the names, shared by `help` and the error path. */
#define CTL_EV_BELL  (1u << 0)
#define CTL_EV_TITLE (1u << 1)
#define CTL_EVENTS_HELP "bell, title (accepted, but no emitter delivers them yet)"

typedef struct ctl_client {
	int      fd;        /* -1 = free slot */
	uint32_t conn_id;   /* unique per accepted connection; guards deferred replies */
	char     inbuf[CTL_LINE_MAX];
	size_t   inlen;
	int      oversize;  /* current line overran inbuf; drop to next '\n' */
	uint32_t sub_mask;
} ctl_client_t;

typedef struct ctl_defer {
	int      fd;
	uint32_t conn_id;
	uint32_t reqid;
	char    *chunk;     /* heap-owned lua source */
} ctl_defer_t;

static int          g_listen_fd = -1;
static char         g_sock_path[108];
static ctl_client_t g_clients[CTL_MAX_CLIENTS];
static uint32_t     g_next_reqid = 1;
static uint32_t     g_next_conn = 1;

static ctl_defer_t  g_defer[CTL_DEFER_MAX];
static int          g_defer_head;  /* next to pop */
static int          g_defer_count;

/* ---------------------------------------------------------------------- */

static void client_clear(ctl_client_t *c) {
	memset(c, 0, sizeof(*c));
	c->fd = -1;            /* conn_id := 0: a free slot never matches a defer */
}

static ctl_client_t *client_for_fd(int fd) {
	for (int i = 0; i < CTL_MAX_CLIENTS; ++i) {
		if (g_clients[i].fd == fd) {
			return &g_clients[i];
		}
	}
	return NULL;
}

static ctl_client_t *client_alloc(int fd) {
	for (int i = 0; i < CTL_MAX_CLIENTS; ++i) {
		if (g_clients[i].fd < 0) {
			client_clear(&g_clients[i]);
			g_clients[i].fd = fd;
			g_clients[i].conn_id = g_next_conn++;
			return &g_clients[i];
		}
	}
	return NULL;
}

static void client_close(ctl_client_t *c) {
	if (c->fd < 0) {
		return;
	}
	bps_remove_fd(c->fd);
	close(c->fd);
	client_clear(c);     /* conn_id := 0 invalidates any deferred reply for it */
}

/* Best-effort full write to a nonblocking client fd. Runs on the single BPS
 * pump thread, so it must never sleep/spin: control replies are tiny and the
 * socket buffer is large, so a full write is the norm. If a client pushes back
 * (EAGAIN) we abandon the write and return -1 rather than stall the whole UI;
 * that client may then see a truncated frame, but only it is affected. */
static int write_all(int fd, const char *buf, size_t len) {
	size_t off = 0;
	while (off < len) {
		ssize_t n = write(fd, buf + off, len - off);
		if (n > 0) {
			off += (size_t)n;
			continue;
		}
		if (n < 0 && errno == EINTR) {
			continue;
		}
		return -1;  /* EAGAIN/EWOULDBLOCK or hard error: drop, caller closes */
	}
	return 0;
}

/* Frame a reply: %begin <ts> <id> 0 / <body> / %end <ts> <id> <flag>.
 * `body` may be NULL/empty (no body line) or contain embedded newlines. */
static void ctl_reply_fd(int fd, uint32_t id, int flag, const char *body) {
	/* static: replies are only ever framed on the single-threaded BPS pump
	 * (client io_handlers + control_drain_deferred), so this scratch buffer is
	 * non-reentrant -- keeping it off the stack saves ~8KB per call frame. */
	static char buf[CTL_REPLY_MAX];
	long ts = (long)time(NULL);
	int n = snprintf(buf, sizeof(buf), "%%begin %ld %u 0\n", ts, id);
	if (n < 0 || n >= (int)sizeof(buf)) {
		return;
	}
	size_t len = (size_t)n;
	if (body != NULL && body[0] != '\0') {
		/* Copy the body, escaping any line that begins with '%' (double the
		 * leading percent) so a body line like "%end ..." cannot be mistaken
		 * for a frame marker by the client. proto_unescape_line() reverses it.
		 * Reserve tail room for the trailing "%end ..." line. */
		const size_t tail_reserve = 64;
		size_t cap = sizeof(buf) > tail_reserve ? sizeof(buf) - tail_reserve : 0;
		int at_line_start = 1;
		for (const char *p = body; *p != '\0' && len < cap; ++p) {
			if (at_line_start && *p == '%') {
				if (len + 1 >= cap) {
					break;
				}
				buf[len++] = '%';
			}
			buf[len++] = *p;
			at_line_start = (*p == '\n');
		}
		if (len < cap) {
			buf[len++] = '\n';
		}
	}
	n = snprintf(buf + len, sizeof(buf) - len, "%%end %ld %u %d\n", ts, id, flag);
	if (n < 0 || (size_t)n >= sizeof(buf) - len) {
		return;
	}
	len += (size_t)n;
	(void)write_all(fd, buf, len);
}

static void ctl_reply(ctl_client_t *c, uint32_t id, int flag, const char *body) {
	ctl_reply_fd(c->fd, id, flag, body);
}

/* ---------------------------------------------------------------------- */

static int read_clipboard(char *out, size_t cap) {
	char *data = NULL;
	if (is_clipboard_format_present("text/plain") != 0) {
		return -1;
	}
	int n = get_clipboard_data("text/plain", &data);
	if (n <= 0 || data == NULL) {
		return -1;
	}
	size_t copy = (size_t)n < cap - 1 ? (size_t)n : cap - 1;
	memcpy(out, data, copy);
	out[copy] = '\0';
	free(data);
	return (int)copy;
}

static int defer_push(int fd, uint32_t conn_id, uint32_t reqid, const char *chunk) {
	if (g_defer_count >= CTL_DEFER_MAX) {
		return -1;
	}
	char *dup = strdup(chunk);
	if (dup == NULL) {
		return -1;
	}
	int slot = (g_defer_head + g_defer_count) % CTL_DEFER_MAX;
	g_defer[slot].fd = fd;
	g_defer[slot].conn_id = conn_id;
	g_defer[slot].reqid = reqid;
	g_defer[slot].chunk = dup;
	g_defer_count++;
	return 0;
}

/* Dispatch one fully-received command line (already split into argv). */
static void dispatch(ctl_client_t *c, int argc, char **argv) {
	uint32_t id = g_next_reqid++;
	if (argc <= 0) {
		ctl_reply(c, id, 1, "empty command");
		return;
	}

	if (strcmp(argv[0], "help") == 0) {
		/* Single source of truth for the command surface: any client (termctl
		 * -h, socat, python) gets the live list straight from the dispatcher. */
		ctl_reply(c, id, 0,
		          "commands:\n"
		          "  help                list commands\n"
		          "  screen size         report terminal cols/rows\n"
		          "  sessions            report open session count\n"
		          "  clipboard read      print current clipboard text\n"
		          "  action <name>       run an action (keybinding/builtin/lua:fn)\n"
		          "  send-text <text>    send text to the active session\n"
		          "  subscribe <event>   enable async events\n"
		          "  unsubscribe <event> disable async events\n"
		          "  validate [path]     compile-check a config file (default: yours)\n"
		          "  eval [lua] <chunk>  run a Lua chunk, print its return value\n"
		          "events (for subscribe/unsubscribe): " CTL_EVENTS_HELP "\n"
		          "cross-app entry (#23): other apps/links open term49://tab[/N] or\n"
		          "  term49://focus to open/focus a tab (navigation only -- it cannot\n"
		          "  run commands; that is what this socket is for). The action\n"
		          "  notify_invoke:<msg> posts a Hub entry that opens term49://tab/<this tab>.\n"
		          "agent capability doc: $TERM49_AGENT_DOC");
		return;
	}
	if (strcmp(argv[0], "screen") == 0 && argc >= 2 && strcmp(argv[1], "size") == 0) {
		int cols = 0, rows = 0;
		ctl_screen_size(&cols, &rows);
		char body[64];
		snprintf(body, sizeof(body), "cols=%d rows=%d", cols, rows);
		ctl_reply(c, id, 0, body);
		return;
	}
	if (strcmp(argv[0], "sessions") == 0) {
		char body[64];
		snprintf(body, sizeof(body), "count=%u", ctl_session_count());
		ctl_reply(c, id, 0, body);
		return;
	}
	if (strcmp(argv[0], "clipboard") == 0 && argc >= 2 && strcmp(argv[1], "read") == 0) {
		char body[CTL_REPLY_MAX / 2];
		if (read_clipboard(body, sizeof(body)) < 0) {
			ctl_reply(c, id, 1, "clipboard empty or not text");
		} else {
			ctl_reply(c, id, 0, body);
		}
		return;
	}
	if (strcmp(argv[0], "action") == 0 && argc >= 2) {
		int ok = ctl_run_action_string(argv[1]);
		ctl_reply(c, id, ok ? 0 : 1, NULL);
		return;
	}
	if (strcmp(argv[0], "send-text") == 0 && argc >= 2) {
		/* Routes through the action parser, which sends unrecognised strings
		 * to the session as SEND_BYTES -- so plain text is delivered verbatim,
		 * but a reserved builtin/terminfo name (e.g. "rescreen", "kcuu1") is
		 * interpreted as that action rather than sent literally. */
		int ok = ctl_run_action_string(argv[1]);
		ctl_reply(c, id, ok ? 0 : 1, NULL);
		return;
	}
	if (strcmp(argv[0], "subscribe") == 0 || strcmp(argv[0], "unsubscribe") == 0) {
		int on = (strcmp(argv[0], "subscribe") == 0);
		uint32_t bit = 0;
		if (argc >= 2) {
			if (strcmp(argv[1], "bell") == 0)  bit = CTL_EV_BELL;
			else if (strcmp(argv[1], "title") == 0) bit = CTL_EV_TITLE;
		}
		if (bit == 0) {
			ctl_reply(c, id, 1,
			          argc >= 2 ? "unknown event; valid events: " CTL_EVENTS_HELP
			                    : "missing event; valid events: " CTL_EVENTS_HELP);
		} else {
			if (on) c->sub_mask |= bit; else c->sub_mask &= ~bit;
			ctl_reply(c, id, 0, NULL);
		}
		return;
	}
	if (strcmp(argv[0], "validate") == 0) {
		/* Compile-check a config file without executing it (no side effects),
		 * so this is safe inline -- unlike eval, it never touches the live
		 * scripting state. Default target is the user's config. */
		char body[CTL_REPLY_MAX / 2];
		int rc = prefs_lua_validate(argc >= 2 ? argv[1] : NULL,
		                            body, sizeof(body));
		ctl_reply(c, id, rc == 0 ? 0 : 1, body);
		return;
	}
	if (strcmp(argv[0], "eval") == 0) {
		/* `eval lua <chunk>` or `eval <chunk>`; deferred to the safe point. */
		const char *chunk = NULL;
		if (argc >= 3 && strcmp(argv[1], "lua") == 0) {
			chunk = argv[2];
		} else if (argc >= 2) {
			chunk = argv[1];
		}
		if (chunk == NULL) {
			ctl_reply(c, id, 1, "eval: missing chunk");
		} else if (defer_push(c->fd, c->conn_id, id, chunk) != 0) {
			ctl_reply(c, id, 1, "eval: queue full");
		}
		/* success path replies later from control_drain_deferred */
		return;
	}

	ctl_reply(c, id, 1, "unknown command (try: help)");
}

/* Pull complete '\n'-terminated lines out of the client's accumulator. */
static void consume_lines(ctl_client_t *c) {
	size_t start = 0;
	for (size_t i = 0; i < c->inlen; ++i) {
		if (c->inbuf[i] != '\n') {
			continue;
		}
		size_t linelen = i - start;
		if (c->oversize) {
			/* tail of a line we already rejected */
			c->oversize = 0;
		} else {
			char line[CTL_LINE_MAX];
			memcpy(line, c->inbuf + start, linelen);
			line[linelen] = '\0';
			if (linelen > 0 && line[linelen - 1] == '\r') {
				line[linelen - 1] = '\0';
			}
			char *argv[CTL_MAX_ARGV];
			int argc = proto_argv_split(line, argv, CTL_MAX_ARGV);
			if (argc < 0) {
				ctl_reply(c, g_next_reqid++, 1, "too many arguments");
			} else if (argc > 0) {
				dispatch(c, argc, argv);
			}
		}
		start = i + 1;
	}
	if (start > 0) {
		memmove(c->inbuf, c->inbuf + start, c->inlen - start);
		c->inlen -= start;
	}
	if (c->inlen == sizeof(c->inbuf)) {
		/* No newline in a full buffer: reject this line, resync at next '\n'. */
		ctl_reply(c, g_next_reqid++, 1, "line too long");
		c->inlen = 0;
		c->oversize = 1;
	}
}

static int control_client_handler(int fd, int io_events, void *data) {
	ctl_client_t *c = (ctl_client_t *)data;
	if (c == NULL || c->fd != fd) {
		return BPS_SUCCESS;
	}
	if (io_events & BPS_IO_EXCEPT) {
		client_close(c);
		ctl_wake();
		return BPS_SUCCESS;
	}
	for (;;) {
		ssize_t n = read(fd, c->inbuf + c->inlen, sizeof(c->inbuf) - c->inlen);
		if (n > 0) {
			c->inlen += (size_t)n;
			consume_lines(c);
			continue;
		}
		if (n == 0) {              /* EOF */
			client_close(c);
			break;
		}
		if (errno == EINTR) {
			continue;
		}
		if (errno == EAGAIN || errno == EWOULDBLOCK) {
			break;
		}
		client_close(c);           /* hard error */
		break;
	}
	ctl_wake();
	return BPS_SUCCESS;
}

static int control_accept_handler(int fd, int io_events, void *data) {
	(void)data;
	if (io_events & BPS_IO_EXCEPT) {
		return BPS_FAILURE;
	}
	for (;;) {
		int cfd = accept(fd, NULL, NULL);
		if (cfd < 0) {
			if (errno == EINTR) {
				continue;
			}
			break;                 /* EAGAIN/EWOULDBLOCK: backlog drained */
		}
		ctl_client_t *c = client_alloc(cfd);
		if (c == NULL) {
			close(cfd);            /* at capacity */
			continue;
		}
		fcntl(cfd, F_SETFL, fcntl(cfd, F_GETFL) | O_NONBLOCK);
		fcntl(cfd, F_SETFD, FD_CLOEXEC);
		if (bps_add_fd(cfd, BPS_IO_INPUT, control_client_handler, c) != BPS_SUCCESS) {
			close(cfd);
			client_clear(c);
		}
	}
	ctl_wake();
	return BPS_SUCCESS;
}

/* ---------------------------------------------------------------------- */

static int build_sock_path(char *out, size_t cap) {
	const char *home = getenv("HOME");
	if (home != NULL) {
		char dir[96];
		int n = snprintf(dir, sizeof(dir), "%s/.term49", home);
		if (n > 0 && n < (int)sizeof(dir)) {
			mkdir(dir, 0700); /* ignore EEXIST */
			n = snprintf(out, cap, "%s/control.sock", dir);
			if (n > 0 && n < (int)cap) {
				return 0;
			}
		}
	}
	/* Fallback: per-app /tmp, which child shells in the same sandbox share. */
	{
		int n = snprintf(out, cap, "/tmp/term49.sock");
		if (n > 0 && n < (int)cap) {
			return 0;
		}
	}
	return -1;
}

int control_init(void) {
	struct sockaddr_un addr;

	/* Writing a reply to a client that already closed its end would otherwise
	 * raise SIGPIPE and kill the whole app; turn those writes into EPIPE. */
	signal(SIGPIPE, SIG_IGN);

	for (int i = 0; i < CTL_MAX_CLIENTS; ++i) {
		g_clients[i].fd = -1;
	}

	if (build_sock_path(g_sock_path, sizeof(g_sock_path)) != 0) {
		fprintf(stderr, "control: could not build socket path\n");
		return -1;
	}
	if (strlen(g_sock_path) >= sizeof(addr.sun_path)) {
		fprintf(stderr, "control: socket path too long: %s\n", g_sock_path);
		g_sock_path[0] = '\0';
		return -1;
	}

	g_listen_fd = socket(AF_UNIX, SOCK_STREAM, 0);
	if (g_listen_fd < 0) {
		fprintf(stderr, "control: socket(): %s\n", strerror(errno));
		return -1;
	}
	fcntl(g_listen_fd, F_SETFL, fcntl(g_listen_fd, F_GETFL) | O_NONBLOCK);
	fcntl(g_listen_fd, F_SETFD, FD_CLOEXEC);

	unlink(g_sock_path); /* clear a stale socket from a prior run */
	memset(&addr, 0, sizeof(addr));
	addr.sun_family = AF_UNIX;
	strncpy(addr.sun_path, g_sock_path, sizeof(addr.sun_path) - 1);
	if (bind(g_listen_fd, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
		fprintf(stderr, "control: bind(%s): %s\n", g_sock_path, strerror(errno));
		close(g_listen_fd);
		g_listen_fd = -1;
		return -1;
	}
	chmod(g_sock_path, 0600);
	if (listen(g_listen_fd, 4) != 0) {
		fprintf(stderr, "control: listen(): %s\n", strerror(errno));
		close(g_listen_fd);
		g_listen_fd = -1;
		unlink(g_sock_path);
		return -1;
	}
	if (bps_add_fd(g_listen_fd, BPS_IO_INPUT, control_accept_handler, NULL) != BPS_SUCCESS) {
		fprintf(stderr, "control: bps_add_fd(): %s\n", strerror(errno));
		close(g_listen_fd);
		g_listen_fd = -1;
		unlink(g_sock_path);
		return -1;
	}

	setenv("TERM49_CONTROL", g_sock_path, 1);
	return 0;
}

void control_drain_deferred(void) {
	while (g_defer_count > 0) {
		ctl_defer_t d = g_defer[g_defer_head];
		g_defer_head = (g_defer_head + 1) % CTL_DEFER_MAX;
		g_defer_count--;

		char out[CTL_REPLY_MAX / 2];
		out[0] = '\0';
		int rc = prefs_lua_eval(d.chunk, out, sizeof(out));
		free(d.chunk);

		/* Only reply if the fd still belongs to the exact connection that
		 * issued the eval -- a unique conn_id, so an fd reused by a later
		 * client (even in a different slot) will not match. */
		ctl_client_t *c = client_for_fd(d.fd);
		if (c != NULL && c->conn_id == d.conn_id) {
			ctl_reply(c, d.reqid, rc == 0 ? 0 : 1, out);
		}
	}
}

void control_shutdown(void) {
	for (int i = 0; i < CTL_MAX_CLIENTS; ++i) {
		if (g_clients[i].fd >= 0) {
			client_close(&g_clients[i]);
		}
	}
	if (g_listen_fd >= 0) {
		bps_remove_fd(g_listen_fd);
		close(g_listen_fd);
		g_listen_fd = -1;
	}
	if (g_sock_path[0] != '\0') {
		unlink(g_sock_path);
		g_sock_path[0] = '\0';
	}
	while (g_defer_count > 0) {
		free(g_defer[g_defer_head].chunk);
		g_defer_head = (g_defer_head + 1) % CTL_DEFER_MAX;
		g_defer_count--;
	}
}
