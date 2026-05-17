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

#include "t49_types.h"
#include "action.h"

typedef struct t49_session t49_session_t;

/* Adopts the already-initialised io master fd + ghostty bridge singleton.
 * Must be called after pty_init() and ghostty_bridge_init(). */
int  session_create(t49_session_t **out, t49_session_id_t id);
void session_destroy(t49_session_t *s);                 /* NULL-safe */

t49_session_id_t session_id(const t49_session_t *s);
int  session_master_fd(const t49_session_t *s);

ssize_t session_write_text(t49_session_t *s, const UChar *buf, size_t n);
ssize_t session_write_bytes(t49_session_t *s, const char *buf, size_t n);

/* Forwarder to the existing window-size path (cols/rows -> TIOCSWINSZ ->
 * SIGWINCH -> ghostty_bridge_resize). Provided for contract completeness;
 * window-driven resize still flows through rescreen() in this stage. */
int  session_resize(t49_session_t *s, int cols, int rows);

/* Session-scoped action subset: SEND_BYTES / SEND_TERMINFO / PASTE_CLIPBOARD.
 * Returns 1 if handled, 0 otherwise. NULL session => 0. */
int  session_dispatch_action(t49_session_t *s, const t49_action_t *a);

#endif /* SESSION_H_ */
