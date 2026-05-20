/*
 * A Term49 session: one pty/child + terminal emulator + IO + scrollback.
 *
 * Single session today. The app owns a session set so that multi-session
 * (#4) only grows the app's registry, not these signatures or the
 * event/action structs. In this stage `session_t` owns its own
 * ghostty_bridge_t; pty master fd and child_pid still live in main.c and
 * move here in step 1.5 / step 2.
 */

#ifndef SESSION_H_
#define SESSION_H_

#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>
#include <unicode/utf.h>

#include "term_types.h"
#include "action.h"

typedef struct ghostty_bridge ghostty_bridge_t;
typedef struct session session_t;

/* Owns its ghostty_bridge_t, constructed at the given geometry / scrollback.
 * Pty master fd + child_pid are still adopted from io_* / main.c globals in
 * this stage; step 1.5 / step 2 move them into the session. */
int  session_create(session_t **out, session_id_t id,
                    uint16_t cols, uint16_t rows, size_t max_scrollback);
void session_destroy(session_t *s);                 /* NULL-safe */

session_id_t      session_id(const session_t *s);
ghostty_bridge_t *session_bridge(session_t *s);
int               session_master_fd(const session_t *s);

ssize_t session_write_text(session_t *s, const UChar *buf, size_t n);
ssize_t session_write_bytes(session_t *s, const char *buf, size_t n);

/* Session-scoped action subset: SEND_BYTES / SEND_TERMINFO / PASTE_CLIPBOARD.
 * Returns 1 if handled, 0 otherwise. NULL session => 0. */
int  session_dispatch_action(session_t *s, const action_t *a);

#endif /* SESSION_H_ */
