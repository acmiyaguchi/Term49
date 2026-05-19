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

#define PREFS_LUA_FILE_PATH ".term49.lua"
#define TERM_DEFAULT_FONT_PATH "/usr/fonts/font_repository/monotype/andalemo.ttf"
#define TERM_DEFAULT_FONT_SIZE 24

int preferences_guess_best_font_size(pref_t *prefs, int target_cols);

void destroy_preferences(pref_t *pref);
/* Frees pref_t's owned members but NOT the struct itself, so a reload
 * can rebuild fields in place without invalidating the global pointer
 * borrowers (app/io/renderer) hold. */
void destroy_preferences_members(pref_t *pref);

/* Loader boundary. pref_t is the loader-agnostic plain-data contract; the
 * concrete config parser (Lua) stays PRIVATE to the loader .c file -- no
 * other translation unit includes any lua headers. */
typedef struct prefs_loader {
	pref_t *(*load)(const char *path);
	void    (*save)(const pref_t *prefs, const char *path);
	void    (*destroy)(pref_t *prefs);
} prefs_loader_t;

/* Lua is the only config language. prefs_lua_loader() executes the
 * user's .term49.lua and builds pref_t directly from its globals. */
const prefs_loader_t *prefs_lua_loader(void);
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

keymap_t* keymap_lookup(char keystroke, keymap_t *keymap_head);
const char* keystroke_lookup(char keystroke, keymap_t *keymap_head);
int is_int_member(int const* list, int target);

#endif /* PREFS_H_ */
