/*
 * Term49 control-protocol framing shared by the in-app server (src/control.c)
 * and the termctl client (tools/termctl/main.c). Dependency-free (string.h
 * only, no BB10 headers) so the same source compiles into both link targets.
 *
 * Request lines are argv-style: tokens separated by unescaped spaces, with
 * backslash escapes ( \\  \<space>  \n  \t ) so an argument may contain
 * spaces or newlines. proto_argv_split (server) and proto_argv_join (client)
 * are exact inverses -- keeping them in one file is what guarantees the wire
 * contract stays symmetric.
 *
 * Replies are framed as:
 *     %begin <ts> <id> 0
 *     <body lines>
 *     %end <ts> <id> <flag>
 * To keep body content from colliding with those markers, the server doubles
 * the leading '%' of any body line that starts with one (so "%end" on the wire
 * becomes "%%end"); the client passes each in-frame line through
 * proto_unescape_line() to recover the original. Frame-control lines keep their
 * single leading '%', so they remain distinguishable from escaped body.
 */

#ifndef CONTROL_PROTO_H_
#define CONTROL_PROTO_H_

#include <stddef.h>

/* Tokenize `line` in place into `argv` on unescaped spaces, decoding the
 * backslash escapes above. argv[i] point into `line`. Returns argc, or -1 if
 * the token count would exceed `max`. A blank/whitespace-only line yields 0. */
int proto_argv_split(char *line, char **argv, int max);

/* Join argc tokens into `out` (capacity `cap`, always NUL-terminated on
 * success) separated by single spaces, escaping each token so that
 * proto_argv_split reverses the result exactly. Returns the string length
 * written (excluding the NUL), or -1 if it would not fit. */
int proto_argv_join(char *out, size_t cap, int argc, char *const *argv);

/* Recover an escaped reply-body line: if `line` begins with '%' the server
 * doubled it, so return line+1; otherwise return line unchanged. The result
 * points into `line` (no copy). Call only on lines known to be body (inside a
 * %begin/%end frame), never on the frame-control lines themselves. */
const char *proto_unescape_line(const char *line);

#endif /* CONTROL_PROTO_H_ */
