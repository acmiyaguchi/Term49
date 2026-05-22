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

#endif /* CONTROL_PROTO_H_ */
