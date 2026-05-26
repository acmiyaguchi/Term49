/*
 * Application identity -- the single source of truth for the app's name,
 * package id, and runtime surface. The bar descriptor (bar-descriptor.xml)
 * is the only other place identity lives; keep the two in sync.
 *
 * Branding is deliberately split from the runtime surface: only
 * APP_DISPLAY_NAME and APP_ID carry the version-specific name ("Term50"), so a
 * future version bump touches just those two lines. Everything the user and
 * other apps interact with at runtime (config file, state dir, URI scheme,
 * env vars) is version-neutral and never needs to change on a rename.
 *
 * Header-only macros so the standalone termctl client (which links none of the
 * app) can include this too.
 */
#ifndef APP_IDENTITY_H_
#define APP_IDENTITY_H_

/* Version-specific branding -- the ONLY strings to change next version. */
#define APP_DISPLAY_NAME           "Term50"            /* help text, notify title */
#define APP_ID                     "io.github.term50"  /* bar id + invoke-target id */

/* Version-neutral runtime surface -- stable across renames. */
#define APP_URI_SCHEME             "term://"           /* navigator invoke scheme */
#define APP_NOTIFY_ITEM_ID         "term"              /* default notification slot */
#define APP_CONFIG_BASENAME        ".term.lua"         /* ~/.term.lua */
#define APP_STATE_DIRNAME          ".term"             /* ~/.term/ (control.sock) */
#define APP_CONTROL_SOCK_FALLBACK  "/tmp/term.sock"
#define APP_ENV_CONTROL            "TERMCTL_SOCKET"    /* control-socket path env */
#define APP_ENV_AGENT_DOC          "TERMCTL_AGENT_DOC" /* agent capability doc env */
#define APP_LOG_TAG                "term"              /* stderr "term: ..." prefix */

#endif /* APP_IDENTITY_H_ */
