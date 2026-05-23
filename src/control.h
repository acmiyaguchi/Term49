/*
 * Term49 control socket (#5). A Unix-domain stream socket that lets external
 * processes drive the terminal through the same action IR keybindings use.
 *
 * Runs entirely on the single-threaded BPS pump (#16-H): the listener and
 * every client fd are registered with bps_add_fd, so io_handlers fire on the
 * pump thread with no locking. Commands that must not re-enter the lua_State
 * mid-dispatch (`eval`) are queued and drained at the run-loop safe point via
 * control_drain_deferred().
 *
 * control.c is a leaf TU: it reaches app/renderer state only through the small
 * ctl_* glue below, which main.c defines.
 */

#ifndef CONTROL_H_
#define CONTROL_H_

#include <stddef.h>   /* size_t */

/* Create $HOME/.term49/control.sock (or a /tmp fallback if that path is too
 * long for sun_path), listen, register the listener with the BPS pump, and
 * export its path as $TERM49_CONTROL. Returns 0 on success, -1 on failure
 * (non-fatal: the app runs fine without a control socket). */
int  control_init(void);

/* Deregister + close every client and the listener, then unlink the socket.
 * Must run before platform_destroy() tears the BPS channel down. */
void control_shutdown(void);

/* Run any commands queued for the lua safe point. Call once per loop after
 * the reload check in main(). */
void control_drain_deferred(void);

/* ---- glue implemented in main.c (keeps control.c free of app internals) ---- */
int      ctl_run_action_string(const char *s); /* parse+dispatch one action */
void     ctl_wake(void);                        /* push a BPS wake event */
int      ctl_screen_size(int *cols, int *rows); /* active session geometry; 1 ok */
unsigned ctl_session_count(void);               /* live session count */
int      ctl_tab_stats(unsigned id, char *buf, size_t cap); /* one tab; 0 ok, 1 none */
int      ctl_tabs_stats(char *buf, size_t cap); /* all tabs, one line each; ret count */

#endif /* CONTROL_H_ */
