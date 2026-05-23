/*
 * A Term49 session: one pty/child + terminal emulator + IO + scrollback.
 *
 * Single session today. The app owns a session set so that multi-session
 * (#4) only grows the app's registry, not these signatures or the
 * event/action structs. `session_t` owns its ghostty_bridge_t, the pty
 * master fd, and the child shell's pid + exit state.
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
 * runs openpty()/forkpty(); session_destroy closes it. The child pid is
 * adopted via session_set_child_pid() and the SIGCHLD reaper marks the
 * session exited; the bridge keeps its scrollback so the user can read
 * the shell's last output until they dismiss the [exited] tab. */
int  session_create(session_t **out, session_id_t id,
                    uint16_t cols, uint16_t rows, size_t max_scrollback);
void session_destroy(session_t *s);                 /* NULL-safe */

session_id_t      session_id(const session_t *s);
ghostty_bridge_t *session_bridge(session_t *s);
int               session_master_fd(const session_t *s);
void              session_set_master_fd(session_t *s, int fd);

pid_t session_child_pid(const session_t *s);
void  session_set_child_pid(session_t *s, pid_t pid);
int   session_is_exited(const session_t *s);
int   session_exit_status(const session_t *s);    /* waitpid status, valid when is_exited */
void  session_mark_exited(session_t *s, int status);

/* Cumulative pty byte counters, maintained by the read/write helpers below.
 * "in" = bytes read from the shell; "out" = bytes written to the shell. */
uint64_t session_bytes_in(const session_t *s);
uint64_t session_bytes_out(const session_t *s);

ssize_t session_write_text(session_t *s, const UChar *buf, size_t n);
ssize_t session_write_bytes(session_t *s, const char *buf, size_t n);
ssize_t session_read_bytes(session_t *s, char *buf, size_t n);

/* Session-scoped action subset: SEND_BYTES / SEND_TERMINFO / PASTE_CLIPBOARD.
 * Returns 1 if handled, 0 otherwise. NULL session => 0. */
int  session_dispatch_action(session_t *s, const action_t *a);

#endif /* SESSION_H_ */
