#include "control_proto.h"

#include <string.h>

int proto_argv_split(char *line, char **argv, int max) {
	int argc = 0;
	char *src = line;
	char *dst = line; /* unescape in place, compacting toward the front */

	while (*src != '\0') {
		/* Skip run of unescaped spaces between tokens. */
		while (*src == ' ') {
			++src;
		}
		if (*src == '\0') {
			break;
		}
		if (argc >= max) {
			return -1;
		}
		argv[argc++] = dst;
		/* Copy one token, decoding escapes, until an unescaped space/end. */
		while (*src != '\0' && *src != ' ') {
			if (*src == '\\' && src[1] != '\0') {
				char e = src[1];
				switch (e) {
				case 'n': *dst++ = '\n'; break;
				case 't': *dst++ = '\t'; break;
				default:  *dst++ = e;    break; /* \\  \<space>  \<any> */
				}
				src += 2;
			} else {
				*dst++ = *src++;
			}
		}
		if (*src == ' ') {
			++src;          /* consume the separator before terminating */
		}
		*dst++ = '\0';          /* terminate this token */
	}
	return argc;
}

int proto_argv_join(char *out, size_t cap, int argc, char *const *argv) {
	size_t len = 0;
	for (int i = 0; i < argc; ++i) {
		if (i > 0) {
			if (len + 1 >= cap) {
				return -1;
			}
			out[len++] = ' ';
		}
		for (const char *p = argv[i]; *p != '\0'; ++p) {
			char c = *p;
			const char *esc = NULL;
			char escbuf[2];
			if (c == '\\')      { esc = "\\\\"; }
			else if (c == ' ')  { esc = "\\ "; }
			else if (c == '\n') { esc = "\\n"; }
			else if (c == '\t') { esc = "\\t"; }
			else { escbuf[0] = c; escbuf[1] = '\0'; esc = escbuf; }
			size_t elen = strlen(esc);
			if (len + elen >= cap) {
				return -1;
			}
			memcpy(out + len, esc, elen);
			len += elen;
		}
	}
	if (len >= cap) {
		return -1;
	}
	out[len] = '\0';
	return (int)len;
}
