/*
 * Copyright (c) 2013 Todd Mortimer
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

// Preferences contract. Keep file-format parser dependencies private to implementations.
#ifndef PREFS_H_
#define PREFS_H_


#include <stddef.h>
#include <unicode/utf.h>
#include "types.h"

#define PREFS_LUA_FILE_PATH ".term49.lua"
#define TERM_DEFAULT_FONT_PATH "/usr/fonts/font_repository/monotype/andalemo.ttf"
#define TERM_DEFAULT_FONT_SIZE 24

int preferences_guess_best_font_size(pref_t *prefs, int target_cols);

void destroy_preferences(pref_t *pref);
/* Frees pref_t's owned members but NOT the struct itself, so a reload
 * can rebuild fields in place without invalidating the global pointer
 * borrowers (app/io/renderer) hold. */
void destroy_preferences_members(pref_t *pref);

/* Decode every symkey label (sk->map->to) into sk->uc once. Owned by
 * the symmenu_t (freed by destroy_symmenu); must be called AFTER
 * io_init so the ICU UTF-8 converter is live. Idempotent: re-running
 * frees and re-decodes, so reload can call this after a prefs swap. */
void preferences_decode_symmenu_labels(pref_t *prefs);

/* Loader boundary. pref_t is the loader-agnostic plain-data contract;
 * the concrete config parser (Lua) stays PRIVATE to preferences.c -- no
 * other translation unit includes any lua headers. Lua is the only
 * config language, so these are called directly (no loader vtable). */

/* Execute the user's .term49.lua and build pref_t from its globals. A
 * missing/broken file falls back to compiled defaults (the only sane
 * behaviour at startup). */
pref_t *prefs_lua_load(const char *path);
/* Free a pref_t from prefs_lua_load() and close the scripting state. */
void prefs_lua_destroy(pref_t *pref);
/* Re-run .term49.lua for a live reload. Returns a fresh pref_t on
 * success (scripting state already committed); returns NULL on a
 * parse error / OOM WITHOUT disturbing the running config or Lua
 * state, so a broken edit can't wipe a working setup to defaults. */
pref_t *prefs_lua_reload(void);
/* Serialize pref_t to a .term49.lua; used to persist a first-run
 * default config. */
void prefs_emit_lua(const pref_t *prefs, const char *path);
/* First-run README symlink (config-format independent). */
void prefs_first_run_readme(void);

/* Invoke a no-argument Lua function (by global name) registered from the
 * user's config, via lua_pcall so a Lua error cannot longjmp through C.
 * Returns 1 on success, 0 if unavailable or the call errored. The Lua
 * state is owned by, and stays private to, the loader TU. */
int prefs_lua_invoke(const char *name);

/* Run an arbitrary Lua chunk on the persistent scripting state (via
 * luaL_loadstring + lua_pcall, so a Lua error cannot longjmp through C).
 * Any return values are written to `out` (tab-separated, truncated to
 * outsz), as is the error message on failure. Returns 0 on success, -1 on
 * load/runtime error or if scripting is unavailable. MUST be called from a
 * safe point -- never from inside an active lua_pcall. */
int prefs_lua_eval(const char *src, char *out, size_t outsz);

/* Compile-check a config file without executing it or touching the live
 * scripting state. `path` NULL/"" => the user's PREFS_LUA_FILE_PATH. Writes
 * "ok" or the parse error to `out`. Returns 0 if it compiles, -1 otherwise.
 * Safe to call inline (no config side effects run). */
int prefs_lua_validate(const char *path, char *out, size_t outsz);

/* Last config load/reload error message, or NULL if the last load/reload
 * succeeded. Points into static storage owned by the loader TU. */
const char *prefs_lua_last_error(void);

keymap_t* keymap_lookup(char keystroke, keymap_t *keymap_head);
const char* keystroke_lookup(char keystroke, keymap_t *keymap_head);
int is_int_member(int const* list, int target);

/* Private "sym held" bit for chord matching. SYM is a menu key with no
 * NDK KEYMOD_* of its own, so we synthesize this bit from menu state at
 * the dispatch site. Chosen well outside the NDK KEYMOD_* range; it is
 * internal only and never passed to terminal_key_sequence. */
#define CHORD_MOD_SYM (1u << 16)

/* Find the chord whose trigger keycode and modifier mask match exactly.
 * Returns NULL if none. `chord_head` is a keycode==0-terminated array. */
chord_t* chord_lookup(int keycode, unsigned mods, chord_t *chord_head);

#endif /* PREFS_H_ */
