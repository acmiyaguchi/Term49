/*
 * termctl -- command-line client for the Term49 control socket (#5).
 *
 * Connects to $TERM49_CONTROL (a Unix-domain socket exported by a running
 * Term49), sends one command built from argv, prints the response body, and
 * exits with the frame's status flag (0 = ok, non-zero = error).
 *
 * Speaks the tmux-style control-mode framing implemented by src/control.c:
 *   %begin <ts> <id> 0
 *   <body lines>
 *   %end <ts> <id> <flag>
 *
 * Links nothing from the app -- only libsocket and the shared framing parser
 * src/control_proto.c.
 *
 * Usage:
 *   termctl help              (or -h/--help: lists the live command surface)
 *   termctl screen size
 *   termctl clipboard read
 *   termctl sessions
 *   termctl action reload_config
 *   termctl eval lua 'return term.font_size_get()'
 */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <sys/socket.h>
#include <sys/un.h>

#include "control_proto.h"

#define LINE_MAX_   8192

static int resolve_path(char *out, size_t cap) {
	const char *env = getenv("TERM49_CONTROL");
	if (env != NULL && env[0] != '\0') {
		if (strlen(env) >= cap) {
			return -1;
		}
		strcpy(out, env);
		return 0;
	}
	const char *home = getenv("HOME");
	if (home != NULL) {
		int n = snprintf(out, cap, "%s/.term49/control.sock", home);
		if (n > 0 && n < (int)cap) {
			return 0;
		}
	}
	return -1;
}

static int connect_sock(const char *path) {
	struct sockaddr_un addr;
	if (strlen(path) >= sizeof(addr.sun_path)) {
		fprintf(stderr, "termctl: socket path too long: %s\n", path);
		return -1;
	}
	int fd = socket(AF_UNIX, SOCK_STREAM, 0);
	if (fd < 0) {
		perror("termctl: socket");
		return -1;
	}
	memset(&addr, 0, sizeof(addr));
	addr.sun_family = AF_UNIX;
	strncpy(addr.sun_path, path, sizeof(addr.sun_path) - 1);
	if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
		fprintf(stderr, "termctl: connect(%s): %s\n", path, strerror(errno));
		close(fd);
		return -1;
	}
	return fd;
}

static int write_all(int fd, const char *buf, size_t len) {
	size_t off = 0;
	while (off < len) {
		ssize_t n = write(fd, buf + off, len - off);
		if (n > 0) {
			off += (size_t)n;
		} else if (n < 0 && errno == EINTR) {
			continue;
		} else {
			return -1;
		}
	}
	return 0;
}

/* Read framed response, printing body lines to stdout. Returns the %end flag
 * (0 ok / non-zero error), or -1 on a protocol/IO failure. */
static int read_response(int fd) {
	char buf[LINE_MAX_];
	size_t len = 0;
	int in_frame = 0;
	int flag = -1;
	int done = 0;

	while (!done) {
		ssize_t n = read(fd, buf + len, sizeof(buf) - len);
		if (n < 0) {
			if (errno == EINTR) {
				continue;
			}
			perror("termctl: read");
			return -1;
		}
		if (n == 0) {
			break; /* server closed */
		}
		len += (size_t)n;

		size_t start = 0;
		for (size_t i = 0; i < len && !done; ++i) {
			if (buf[i] != '\n') {
				continue;
			}
			buf[i] = '\0';
			char *line = buf + start;
			start = i + 1;

			if (strncmp(line, "%begin", 6) == 0) {
				in_frame = 1;
			} else if (strncmp(line, "%end", 4) == 0) {
				/* "%end <ts> <id> <flag>" -- flag is the last token. */
				char *sp = strrchr(line, ' ');
				flag = (sp != NULL) ? atoi(sp + 1) : -1;
				done = 1;
			} else if (in_frame) {
				/* Body line: a leading '%' was doubled by the server (so it
				 * could not look like a frame marker); recover the original. */
				puts(proto_unescape_line(line));
			} else if (line[0] == '%') {
				/* async notification outside a frame: surface it on stderr */
				fprintf(stderr, "%s\n", line);
			}
		}
		if (start > 0) {
			memmove(buf, buf + start, len - start);
			len -= start;
		}
		if (len == sizeof(buf)) {
			fprintf(stderr, "termctl: response line too long\n");
			return -1;
		}
	}
	return flag;
}

static void print_usage(FILE *out, const char *prog) {
	fprintf(out,
	        "usage: %s <command> [args...]\n"
	        "  connects to $TERM49_CONTROL (a running Term49) and runs one command.\n"
	        "  `%s help` lists the live command surface from the server.\n",
	        prog, prog);
}

int main(int argc, char **argv) {
	if (argc < 2) {
		print_usage(stderr, argv[0]);
		return 2;
	}
	if (strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0) {
		/* Print client usage, then fall through to forward `help` to the
		 * server so the command list always reflects the real dispatcher. */
		print_usage(stdout, argv[0]);
		argv[1] = (char *)"help";
		argc = 2;
	}

	char path[256];
	if (resolve_path(path, sizeof(path)) != 0) {
		fprintf(stderr, "termctl: cannot determine control socket path "
		                "(set $TERM49_CONTROL)\n");
		return 2;
	}

	char line[LINE_MAX_];
	int n = proto_argv_join(line, sizeof(line) - 1, argc - 1, argv + 1);
	if (n < 0) {
		fprintf(stderr, "termctl: command too long\n");
		return 2;
	}
	line[n++] = '\n';

	int fd = connect_sock(path);
	if (fd < 0) {
		return 2;
	}
	if (write_all(fd, line, (size_t)n) != 0) {
		perror("termctl: write");
		close(fd);
		return 2;
	}

	int flag = read_response(fd);
	close(fd);
	if (flag < 0) {
		return 2;
	}
	return flag;
}
