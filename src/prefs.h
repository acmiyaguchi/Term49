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


#include <unicode/utf.h>
#include "types.h"

#define PREFS_FILE_PATH ".term49rc"
#define PREFS_LUA_FILE_PATH ".term49.lua"
#define TERM_DEFAULT_FONT_PATH "/usr/fonts/font_repository/monotype/andalemo.ttf"
#define TERM_DEFAULT_FONT_SIZE 24

int preferences_guess_best_font_size(pref_t *prefs, int target_cols);

pref_t* read_preferences(const char* filename);
void save_preferences(pref_t const* pref, char const* filename);
void destroy_preferences(pref_t *pref);

/* Loader boundary. pref_t is the loader-agnostic plain-data contract; the
 * concrete config parser (libconfig today, Lua in #7) stays PRIVATE to the
 * loader .c file -- no other translation unit includes <libconfig.h> or any
 * future lua headers. #7 adds prefs_lua_loader() populating the SAME pref_t
 * and selects it here, with no consumer changes. */
typedef struct prefs_loader {
	pref_t *(*load)(const char *path);
	void    (*save)(const pref_t *prefs, const char *path);
	void    (*destroy)(pref_t *prefs);
} prefs_loader_t;

const prefs_loader_t *prefs_libconfig_loader(void);

/* #7: Lua is the config language. prefs_lua_loader() populates the SAME
 * pref_t as the libconfig loader by translating the user's .term49.lua
 * into the in-memory representation the validated builders already
 * consume, so all validation/defaulting/teardown is reused unchanged.
 * Both parsers stay private to preferences.c (the single loader TU).
 * prefs_migrate_libconfig_to_lua() does a one-time import of a legacy
 * .term49rc, writing an equivalent .term49.lua. */
const prefs_loader_t *prefs_lua_loader(void);
void prefs_migrate_libconfig_to_lua(const char *rc_path, const char *lua_path);
/* Serialize pref_t to a .term49.lua (used by migration and to persist a
 * first-run default config, analogous to the old libconfig first_run). */
void prefs_emit_lua(const pref_t *prefs, const char *path);

/* Invoke a no-argument Lua function (by global name) registered from the
 * user's config, via lua_pcall so a Lua error cannot longjmp through C.
 * Returns 1 on success, 0 if unavailable or the call errored. The Lua
 * state is owned by, and stays private to, the loader TU. */
int prefs_lua_invoke(const char *name);

keymap_t* keymap_lookup(char keystroke, keymap_t *keymap_head);
const char* keystroke_lookup(char keystroke, keymap_t *keymap_head);
int is_int_member(int const* list, int target);

#endif /* PREFS_H_ */
