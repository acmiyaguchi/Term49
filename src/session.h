/*
 * A Term49 session: one pty/child + terminal emulator + IO + scrollback.
 *
 * Single session today. The app owns a session set so that multi-session
 * (#4) only grows the app's registry, not these signatures or the
 * event/action structs. `session_t` owns its ghostty_bridge_t and the pty
 * master fd; child_pid still lives in main.c and lands here in step 2.
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
 * The pty master fd is adopted via session_set_master_fd() after the caller
 * runs openpty()/forkpty(); session_destroy closes it. child_pid still
 * lives on main.c globals at this stage and lands on the session in step 2. */
int  session_create(session_t **out, session_id_t id,
                    uint16_t cols, uint16_t rows, size_t max_scrollback);
void session_destroy(session_t *s);                 /* NULL-safe */

session_id_t      session_id(const session_t *s);
ghostty_bridge_t *session_bridge(session_t *s);
int               session_master_fd(const session_t *s);
void              session_set_master_fd(session_t *s, int fd);

ssize_t session_write_text(session_t *s, const UChar *buf, size_t n);
ssize_t session_write_bytes(session_t *s, const char *buf, size_t n);
ssize_t session_read_bytes(session_t *s, char *buf, size_t n);

/* Session-scoped action subset: SEND_BYTES / SEND_TERMINFO / PASTE_CLIPBOARD.
 * Returns 1 if handled, 0 otherwise. NULL session => 0. */
int  session_dispatch_action(session_t *s, const action_t *a);

#endif /* SESSION_H_ */
