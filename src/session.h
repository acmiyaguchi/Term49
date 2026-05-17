/*
 * A Term49 session: one pty/child + terminal emulator + IO + scrollback.
 *
 * Single session today. The app owns a session set so that multi-session
 * (#4) only grows the app's registry, not these signatures or the
 * event/action structs. In this stage session_* are thin 1:1 forwarders
 * over the existing io_* / ghostty_bridge_* singletons; #4 replaces the
 * bodies with per-session ownership without touching callers.
 */

#ifndef SESSION_H_
#define SESSION_H_

#include <stddef.h>
#include <sys/types.h>
#include <unicode/utf.h>

#include "term_types.h"
#include "action.h"

typedef struct session session_t;

/* Adopts the already-initialised io master fd + ghostty bridge singleton.
 * Must be called after pty_init() and ghostty_bridge_init(). */
int  session_create(session_t **out, session_id_t id);
void session_destroy(session_t *s);                 /* NULL-safe */

session_id_t session_id(const session_t *s);
int  session_master_fd(const session_t *s);

ssize_t session_write_text(session_t *s, const UChar *buf, size_t n);
ssize_t session_write_bytes(session_t *s, const char *buf, size_t n);

/* Session-scoped action subset: SEND_BYTES / SEND_TERMINFO / PASTE_CLIPBOARD.
 * Returns 1 if handled, 0 otherwise. NULL session => 0. */
int  session_dispatch_action(session_t *s, const action_t *a);

#endif /* SESSION_H_ */
