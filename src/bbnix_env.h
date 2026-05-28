/*
 * Parser for the bbnix deploy-bundle's activation manifest at
 * <root>/etc/bbnix-env. The manifest is the bundle's published contract for
 * the env every bbnix consumer must wire before exec'ing a bundled binary;
 * a POSIX-sh reference implementation ships alongside it at
 * <root>/etc/bbnix-activate. See bbnix pkgs/files/bbnix-env for the format.
 *
 * Format recap: one rule per line, `<mode>  KEY=VALUE`. $ROOT expands to the
 * passed-in bundle root, $HOME to the caller's HOME. Inline `#` is part of
 * VALUE, NOT a comment. Five modes:
 *   set         -- overwrite always
 *   default     -- set only if unset/empty
 *   prepend     -- compose <new>:<existing> (or <new> if empty)
 *   set-if-file -- like set, but skip when VALUE is not a regular file
 *   set-if-dir  -- like set, but skip when VALUE is not a directory
 *
 * Trusted input: the manifest is shipped from our own Nix bundle. The parser
 * is defensive about format errors (unknown mode / missing `=` / truncated
 * line) only to fail loud, not to handle hostile values.
 */

#ifndef BBNIX_ENV_H_
#define BBNIX_ENV_H_

/* Apply <root>/etc/bbnix-env to the current process env. Logs a warning to
 * stderr and returns without setting anything if the manifest is missing
 * (which implies a broken stage-bbnix). Safe to call exactly once per fork
 * before execve; never affects the parent. */
void bbnix_apply_env_manifest(const char *root);

#endif /* BBNIX_ENV_H_ */
