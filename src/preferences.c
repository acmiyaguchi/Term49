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

#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <unicode/utf.h>
#include <errno.h>
#include <stdio.h>
#include <stddef.h>
#include <sys/keycodes.h>

#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>

#include "terminal.h"
#include "platform.h"  /* notification_spec_t */
#include "accent_menus.h"
#include "action.h"
#include "io.h"
#include "symmenu.h"
#include "prefs.h"
#include "app_identity.h"

#define README_FILE_PATH "../app/native/README"
#define README45_FILE_PATH "../app/native/README45"

#define PREFS_COLOR_NUM_ELEMENTS 3
#define PREFS_SYMKEYS_DEFAULT_NUM_ROWS 2

static const int PREFS_VERSION = 10;

#define DEFAULT_FONT_PATH TERM_DEFAULT_FONT_PATH
#define DEFAULT_FONT_SIZE TERM_DEFAULT_FONT_SIZE
#define DEFAULT_TEXT_COLOR (int[]){255, 255, 255}
#define DEFAULT_BACKGROUND_COLOR (int[]){0, 0, 0}
#define DEFAULT_SCREEN_IDLE_AWAKE 0
#define DEFAULT_AUTO_SHOW_VKB 1
#define DEFAULT_METAMODE_DOUBLETAP_KEY KEYCODE_RIGHT_SHIFT
#define DEFAULT_METAMODE_DOUBLETAP_DELAY 500000000
#define DEFAULT_KEYHOLD_ACTIONS 1
#define DEFAULT_METAMODE_HOLD_KEY KEYCODE_SPACE
#define DEFAULT_ALLOW_RESIZE_COLUMNS 0
#define DEFAULT_METAMODE_HITBOX (hitbox_t){0, 0, 100, 100}
#define DEFAULT_TTY_ENCODING "UTF-8"
/* One metamode table; `sticky` entries keep metamode armed after firing (the
 * former metamode_sticky_keys), the rest exit it (former keys + func_keys). */
#define DEFAULT_METAMODE_KEYS_LEN 17
#define DEFAULT_METAMODE_KEYS (keymap_t[]){{'e', "\x1b"}, \
                                           {'s', "rescreen"}, \
                                           {'v', "paste_clipboard"}, \
                                           {'i', "font_size_increase"}, \
                                           {'o', "font_size_decrease"}, \
                                           {'z', "font_size_reset"}, \
                                           {'r', "reload_config"}, \
                                           {'c', "tab_new"}, \
                                           {'n', "tab_next"}, \
                                           {'p', "tab_prev"}, \
                                           {'x', "tab_close"}, \
                                           {'?', "help_overlay"}, \
                                           {'u', "url_pick"}, \
                                           {.from='k', .to="kcuu1", .sticky=1}, \
                                           {.from='j', .to="kcud1", .sticky=1}, \
                                           {.from='l', .to="kcuf1", .sticky=1}, \
                                           {.from='h', .to="kcub1", .sticky=1}}
#define DEFAULT_SYMMENU_NUM_ROWS 2
#define DEFAULT_SYMMENU_ROW_LENS (int[]){10, 9}
#define DEFAULT_SYMMENU_ENTRIES (keymap_t[]) {  \
    {'q', "~"}, {'w', "`"}, {'e', "{"}, {'r', "}"}, {'t', "["}, {'y', "]"}, {'u', "<"}, {'i', ">"}, {'o', "^"}, {'p', "%"}, \
    {'a', "="}, {'s', "-"}, {'d', "*"}, {'f', "/"}, {'g', "\\"},{'h', "|"}, {'j', "&"}, {'k', "'"}, {'l', "\""} \
}
#define DEFAULT_STICKY_SYM_KEY 0
#define DEFAULT_STICKY_SHIFT_KEY 1
#define DEFAULT_STICKY_ALT_KEY 1
#define DEFAULT_KEYHOLD_ACTIONS_EXEMPT_LEN 2
#define DEFAULT_KEYHOLD_ACTIONS_EXEMPT (int[]){KEYCODE_BACKSPACE, KEYCODE_RETURN}
#define DEFAULT_RESCREEN_FOR_SYMMENU 1
#define DEFAULT_KEYHOLD_ACCENTS 1
#define DEFAULT_SHOW_HELP_ON_STARTUP 0

#define DEFAULT_ALTSYM_ENTRIES_LEN 27
#define DEFAULT_ALTSYM_ENTRIES (keymap_t[]) {  \
    {'q', "#"}, {'w', "1"}, {'e', "2"}, {'r', "3"}, {'t', "("}, {'y', ")"}, {'u', "_"}, {'i', "-"}, {'o', "+"}, {'p', "@"}, \
                {'a', "*"}, {'s', "4"}, {'d', "5"}, {'f', "6"}, {'g', "/"}, {'h', ":"}, {'j', ";"}, {'k', "'"}, {'l', "\""}, \
                {'z', "7"}, {'x', "8"}, {'c', "9"}, {'v', "?"}, {'b', "!"}, {'n', ","}, {'m', "."}, \
                            {'0', "0"} \
}

#define NUM_SIZES 251
static const int font_widths[NUM_SIZES] = {0, 1, 1, 2, 2, 3, 4, 4, 5, 5, 6,
                                           7, 7, 8, 8, 9, 10, 10, 11, 11, 12,
                                           13, 13, 14, 14, 15, 16, 16, 17, 17,
                                           18, 19, 19, 20, 20, 21, 22, 22, 23,
                                           23, 24, 25, 25, 26, 26, 27, 28, 28,
                                           29, 29, 30, 31, 31, 32, 32, 33, 34,
                                           34, 35, 35, 36, 37, 37, 38, 38, 39,
                                           40, 40, 41, 41, 42, 43, 43, 44, 44,
                                           45, 46, 46, 47, 47, 48, 49, 49, 50,
                                           50, 51, 52, 52, 53, 53, 54, 55, 55,
                                           56, 56, 57, 58, 58, 59, 59, 60, 61,
                                           61, 62, 62, 63, 64, 64, 65, 65, 66,
                                           67, 67, 68, 68, 69, 70, 70, 71, 71,
                                           72, 73, 73, 74, 74, 75, 76, 76, 77,
                                           77, 78, 79, 79, 80, 80, 81, 82, 82,
                                           83, 83, 84, 85, 85, 86, 86, 87, 88,
                                           88, 89, 89, 90, 91, 91, 92, 92, 93,
                                           94, 94, 95, 95, 96, 97, 97, 98, 98,
                                           99, 100, 100, 101, 101, 102, 103, 103,
                                           104, 104, 105, 106, 106, 107, 107, 108,
                                           109, 109, 110, 110, 111, 112, 112, 113,
                                           113, 114, 115, 115, 116, 116, 117, 118,
                                           118, 119, 119, 120, 121, 121, 122, 122,
                                           123, 124, 124, 125, 125, 126, 127, 127,
                                           128, 128, 129, 130, 130, 131, 131, 132,
                                           133, 133, 134, 134, 135, 136, 136, 137,
                                           137, 138, 139, 139, 140, 140, 141, 142,
                                           142, 143, 143, 144, 145, 145, 146, 146,
                                           147, 148, 148, 149, 149};


/* First-run side effect that has nothing to do with the config format:
 * symlink the bundled README into HOME. (The default .term.lua is
 * written separately by the caller via prefs_emit_lua.) */
void prefs_first_run_readme(void) {
	char* home = getenv("HOME");
	if(home != NULL){ chdir(home); }

	char* readme_path = (atoi(getenv("WIDTH")) <= 720) ? README45_FILE_PATH : README_FILE_PATH;
	fprintf(stderr, "Updating README\n");
	if (access(readme_path, F_OK) != -1) {
		// stat success!
		if (symlink(readme_path, "./README") == -1){
			if (errno != EEXIST){
				fprintf(stderr, "Error linking README from app to PWD\n");
			}
		}
	}
}

int preferences_guess_best_font_size(pref_t *prefs, int target_cols){
	/* font widths in pixels for sizes 0-250, indexed by font size */
	int screen_width, screen_height, target_width;
	if((getenv("WIDTH") == NULL) || (getenv("HEIGHT") == NULL)){
		/* no width or height in env, just return the default */
		if (prefs == NULL) {
			PRINT(stderr, "Preferences not initalized!\n");
			return 10;
		}
		return prefs->font_size;
	}
	screen_width = atoi(getenv("WIDTH"));
	screen_height = atoi(getenv("HEIGHT"));
	target_width = screen_width < screen_height ? screen_width : screen_height;
	int num_px = 0;
	for (int i = 0; i < NUM_SIZES; ++i){
		num_px = target_cols * font_widths[i];
		if(num_px > target_width){
			/* if we are too big, return the last one. */
			PRINT(stderr, "Autodetected font size %d for screen width %d\n", (i - 1), target_width);
			return (i - 1);
		}
	}
	/* if we get here, then just return the largest font */
	return font_widths[NUM_SIZES-1];
}

static void keymap_set_to(keymap_t *entry, const char *to) {
	entry->to = strdup(to);
	if (!action_parse(entry->to, &entry->action)) {
		entry->action = (action_t){0};
	}
}

/* ====================================================================
 * Lua is the only config language. The builders below populate pref_t
 * fields directly from the user's .term.lua globals, mirroring the
 * exact allocation shape destroy_preferences() frees: calloc'd,
 * sentinel-terminated arrays; strdup'd keymap ->to via keymap_set_to;
 * symmenu_t entries + symkey matrix. A missing or ill-typed global
 * falls back to the compiled DEFAULT_* (same robustness the old
 * libconfig path had). All Lua handles stay private to this TU.
 * ==================================================================== */

/* push global `key`; return 1 with the table on the stack, else 0 with
 * nothing pushed. */
static int lua_get_table(lua_State *L, const char *key) {
	lua_getglobal(L, key);
	if (lua_type(L, -1) == LUA_TTABLE) {
		return 1;
	}
	lua_pop(L, 1);
	return 0;
}

/* Read the {from,to} pair at index `i` of the table currently on top of
 * the stack. On success returns 1 with the pair left on the stack (at
 * -1) and *from/*to pointing into Lua strings owned by it (valid until
 * the caller pops the pair). On failure returns 0 with nothing left.
 * If `sticky` is non-NULL it receives the entry's optional `sticky`
 * boolean field (default 0); pass NULL when the flag is irrelevant.
 * A boolean false in the `to` slot is an unbind marker (it removes the
 * matching default during a merge); it is accepted as valid ONLY when
 * `unbind` is non-NULL (the keymap merge opts in), in which case *unbind
 * receives 1 and *to is NULL. Callers that pass unbind == NULL (e.g. the
 * positional symmenu grid, which is replaced wholesale, not merged)
 * reject false as malformed. A boolean false is never a valid action, so
 * it cannot collide with a real binding. */
static int lua_pair_at(lua_State *L, int i, const char **from, const char **to,
                       int *sticky, int *unbind) {
	int ok = 0;
	if (unbind) { *unbind = 0; }
	lua_rawgeti(L, -1, (lua_Integer)i);          /* pair */
	if (lua_type(L, -1) == LUA_TTABLE) {
		lua_rawgeti(L, -1, 1);
		lua_rawgeti(L, -2, 2);
		const char *f = lua_type(L, -2) == LUA_TSTRING ? lua_tostring(L, -2) : NULL;
		int is_unbind = (unbind != NULL)
		             && lua_type(L, -1) == LUA_TBOOLEAN && !lua_toboolean(L, -1);
		const char *t = lua_type(L, -1) == LUA_TSTRING ? lua_tostring(L, -1) : NULL;
		if (f && f[0] && (t || is_unbind)) {
			*from = f;
			*to = t;                     /* NULL when unbind */
			if (unbind) { *unbind = is_unbind; }
			ok = 1;
		}
		lua_pop(L, 2);                       /* pop the two field values */
		if (ok && sticky != NULL) {
			lua_getfield(L, -1, "sticky");
			*sticky = lua_toboolean(L, -1);
			lua_pop(L, 1);
		}
	}
	if (!ok) {
		lua_pop(L, 1);                       /* pop the pair */
	}
	return ok;
}

/* Validate that the table currently at stack top is a sequence of valid
 * {from,to} string pairs. STACK: net zero -- the table stays at -1 in
 * every path (lua_pair_at pops what it pushed; we pop each validated
 * pair). */
static int lua_pairs_valid(lua_State *L, int allow_unbind) {
	int unbind;
	size_t n = (size_t)lua_rawlen(L, -1);
	for (size_t i = 1; i <= n; ++i) {
		const char *f, *t;
		if (!lua_pair_at(L, (int)i, &f, &t, NULL,
		                 allow_unbind ? &unbind : NULL)) {
			return 0;
		}
		lua_pop(L, 1);                       /* pop validated pair */
	}
	return 1;
}

/* Build a sentinel-terminated keymap_t[] by merging the table-of-pairs
 * at stack top OVER the compiled defaults, keyed by `from`: a user entry
 * with a new `from` is appended, one matching a default overrides it in
 * place (keeping default order), and an unbind entry ({ "x", false })
 * removes the matching default. Defaults a user never mentions survive,
 * so new defaults added in a later release reach an existing config.
 * Validate-all-then-build, so one bad entry rejects the whole table with
 * no partial allocation -- returns NULL (no log; the caller reports with
 * its key name). Assumes `from` is unique within `def` (it is for every
 * shipped table). STACK: the table stays at -1; the caller owns/pops it. */
static keymap_t *lua_keymap_merge(lua_State *L, const keymap_t *def,
                                  size_t def_len) {
	if (!lua_pairs_valid(L, 1)) {        /* 1: accept { "x", false } unbind */
		return NULL;
	}
	size_t un = (size_t)lua_rawlen(L, -1);
	/* worst case: every default kept plus every user entry appended. */
	keymap_t *result = calloc(def_len + un + 1, sizeof(keymap_t));
	size_t n = 0;
	for (size_t i = 0; i < def_len; ++i) {       /* seed with defaults */
		result[n].from = def[i].from;
		result[n].sticky = def[i].sticky;
		keymap_set_to(&result[n], def[i].to);
		++n;
	}
	for (size_t i = 1; i <= un; ++i) {           /* apply overrides in file order */
		const char *f, *t;
		int sticky = 0, unbind = 0;
		lua_pair_at(L, (int)i, &f, &t, &sticky, &unbind); /* validated => succeeds */
		char from = f[0];
		size_t j;
		int found = 0;
		for (j = 0; j < n; ++j) {
			if (result[j].from == from) { found = 1; break; }
		}
		if (unbind) {
			if (found) {
				free(result[j].to);
				memmove(&result[j], &result[j + 1],
				        (n - j - 1) * sizeof(keymap_t));
				--n;
			}
		} else if (found) {
			free(result[j].to);
			result[j].sticky = sticky;
			keymap_set_to(&result[j], t);
		} else {
			result[n].from = from;
			result[n].sticky = sticky;
			keymap_set_to(&result[n], t);
			++n;
		}
		lua_pop(L, 1);                       /* pop the pair */
	}
	result[n] = (keymap_t){0, NULL};
	return result;
}

/* Allocate a sentinel-terminated keymap_t[] from a compiled default
 * table (the shared fallback shape). */
static keymap_t *lua_keymap_defaults(const keymap_t *def, size_t def_len) {
	keymap_t *result = calloc(def_len + 1, sizeof(keymap_t));
	result[def_len] = (keymap_t){0, NULL};
	for (size_t i = 0; i < def_len; ++i) {
		result[i].from = def[i].from;
		result[i].sticky = def[i].sticky;
		keymap_set_to(&result[i], def[i].to);
	}
	return result;
}

static int *lua_create_int_array(lua_State *L, const char *key,
                                 size_t def_len, const int *def) {
	int *vals = NULL;
	size_t len = 0;
	int ok = 0;

	if (lua_get_table(L, key)) {
		size_t n = (size_t)lua_rawlen(L, -1);
		int good = 1;
		int *tmp = calloc(n + 1, sizeof(int));
		for (size_t i = 1; i <= n; ++i) {
			lua_rawgeti(L, -1, (lua_Integer)i);
			if (lua_type(L, -1) == LUA_TNUMBER) {
				tmp[i - 1] = (int)lua_tointeger(L, -1);
			} else {
				good = 0;
			}
			lua_pop(L, 1);
		}
		lua_pop(L, 1);                        /* pop the global table */
		if (good) { vals = tmp; len = n; ok = 1; }
		else { free(tmp); }
	}
	if (!ok) {
		fprintf(stderr, "invalid array %s, using default\n", key);
		len = def_len;
	}

	int *result = calloc(len + 1, sizeof(int));
	result[len] = -1;  /* sentinel (positive-array convention) */
	for (size_t i = 0; i < len; ++i) {
		result[i] = ok ? vals[i] : def[i];
	}
	free(vals);
	return result;
}

/* Fixed-length colour reader: always exactly PREFS_COLOR_NUM_ELEMENTS
 * ints plus the positive-array -1 sentinel. Per-element fallback to
 * def[i] for a missing/short/non-numeric entry; extra Lua elements are
 * ignored. So a hand-edited {} / {1,2} / overlong colour can never make
 * a consumer over- or under-read (lua_create_int_array rejected the
 * whole array on any flaw; this degrades per element instead). STACK:
 * net zero. */
static int *lua_create_color(lua_State *L, const char *key, const int *def) {
	int *result = calloc(PREFS_COLOR_NUM_ELEMENTS + 1, sizeof(int));
	result[PREFS_COLOR_NUM_ELEMENTS] = -1;   /* sentinel */
	int have = lua_get_table(L, key);
	for (int i = 0; i < PREFS_COLOR_NUM_ELEMENTS; ++i) {
		int v = def[i];
		if (have) {
			lua_rawgeti(L, -1, i + 1);
			if (lua_type(L, -1) == LUA_TNUMBER) {
				v = (int)lua_tointeger(L, -1);
			}
			lua_pop(L, 1);
		}
		result[i] = v;
	}
	if (have) {
		lua_pop(L, 1);                       /* pop the global table */
	}
	return result;
}

static hitbox_t *lua_create_hitbox(lua_State *L, const char *key, hitbox_t def) {
	hitbox_t *result = calloc(1, sizeof(hitbox_t));
	int v[4];
	int ok = 0;

	if (lua_get_table(L, key)) {
		if ((size_t)lua_rawlen(L, -1) >= 4) {
			ok = 1;
			for (int i = 0; i < 4; ++i) {
				lua_rawgeti(L, -1, i + 1);
				if (lua_type(L, -1) == LUA_TNUMBER) {
					v[i] = (int)lua_tointeger(L, -1);
				} else {
					ok = 0;
				}
				lua_pop(L, 1);
			}
		}
		lua_pop(L, 1);
	}
	if (!ok) {
		fprintf(stderr, "invalid array %s, using default\n", key);
		result->x = def.x; result->y = def.y;
		result->w = def.w; result->h = def.h;
	} else {
		result->x = v[0]; result->y = v[1];
		result->w = v[2]; result->h = v[3];
	}
	return result;
}

static keymap_t *lua_create_keymap_array(lua_State *L, const char *key,
                                         size_t def_len, const keymap_t *def) {
	if (lua_get_table(L, key)) {
		keymap_t *result = lua_keymap_merge(L, def, def_len);
		lua_pop(L, 1);                       /* pop the global table */
		if (result != NULL) {
			return result;
		}
		fprintf(stderr, "invalid keymap list %s, using default\n", key);
	}
	return lua_keymap_defaults(def, def_len);
}

/* --- chord_bindings loader -------------------------------------------
 * chord_bindings is a table of records (not the positional {from,to}
 * pairs the keymap tables use), so this is the one loader that reads by
 * named field:
 *   { key = "t", mods = {"ctrl","shift"}, action = "tab_new", label = "New tab" }
 * Validate-all-then-build like lua_keymap_from_top: one malformed record
 * rejects the whole table (-> compiled defaults), never a partial build.
 * An explicit empty table disables the defaults; an absent or malformed
 * table falls back to them. */

typedef struct {
	int keycode;
	unsigned mods;
	const char *spec;
	const char *label;
} chord_def_t;

/* Shipped defaults for the bare Q10 keyboard. Each lives on a modifier
 * *combination* otherwise unused on-device, so they shadow no plain key or
 * TUI binding: shift+alt and shift+sym give the missing Ctrl and an extra
 * Meta, and alt+enter sends Tab. (Esc stays on metamode "e" -- backspace
 * can't carry a chord because the OS remaps modifier+backspace to Delete.)
 * External-keyboard chords (ctrl+key) stay opt-in, emitted as commented
 * examples by prefs_emit_lua. */
#define DEFAULT_CHORD_BINDINGS_LEN 3
static const chord_def_t DEFAULT_CHORD_BINDINGS[] = {
	{ KEYCODE_BB_ALT_KEY, KEYMOD_SHIFT, "ctrl_down",       "shift+alt = Ctrl" },
	{ KEYCODE_BB_SYM_KEY, KEYMOD_SHIFT, "metamode_toggle", "shift+sym = Meta" },
	{ KEYCODE_RETURN,     KEYMOD_ALT,   "\x09",            "alt+enter = Tab" },
};

/* spec string is owned by the chord (action fields point into it). */
static void chord_set_action(chord_t *c, const char *spec) {
	c->spec = strdup(spec);
	if (!action_parse(c->spec, &c->action)) {
		c->action = (action_t){0};
	}
}

/* "t" -> 't' (single chars are their own byte value, as app_handle_key
 * compares (char)k->sym for letter/digit triggers); named keys cover the
 * modifier and whitespace keys a chord can trigger on. Unknown -> 0,
 * which the caller treats as an invalid record. */
static int chord_keycode_for(const char *name) {
	if (name == NULL || name[0] == '\0') return 0;
	if (name[1] == '\0') return (unsigned char)name[0];
	if (strcmp(name, "sym") == 0)       return KEYCODE_BB_SYM_KEY;
	if (strcmp(name, "alt") == 0)       return KEYCODE_BB_ALT_KEY;
	if (strcmp(name, "shift") == 0)     return KEYCODE_LEFT_SHIFT;
	if (strcmp(name, "space") == 0)     return KEYCODE_SPACE;
	if (strcmp(name, "enter") == 0 ||
	    strcmp(name, "return") == 0)    return KEYCODE_RETURN;
	if (strcmp(name, "tab") == 0)       return KEYCODE_TAB;
	if (strcmp(name, "escape") == 0 ||
	    strcmp(name, "esc") == 0)       return KEYCODE_ESCAPE;
	if (strcmp(name, "backspace") == 0) return KEYCODE_BACKSPACE;
	return 0;
}

/* modifier name -> mask bit; unknown -> 0. */
static unsigned chord_mod_for(const char *name) {
	if (name == NULL) return 0;
	if (strcmp(name, "ctrl") == 0)  return KEYMOD_CTRL;
	if (strcmp(name, "alt") == 0)   return KEYMOD_ALT;
	if (strcmp(name, "shift") == 0) return KEYMOD_SHIFT;
	if (strcmp(name, "sym") == 0)   return CHORD_MOD_SYM;
	return 0;
}

/* Read the chord record at index `i` of the array on top of the stack.
 * out != NULL builds it (allocating spec/label); out == NULL validates
 * only. Returns 1 if the record is well-formed. STACK: net zero -- the
 * array stays at -1. */
static int lua_chord_at(lua_State *L, int i, chord_t *out, int *unbind) {
	int ok = 0;
	if (unbind) { *unbind = 0; }
	lua_rawgeti(L, -1, (lua_Integer)i);              /* record */
	if (lua_type(L, -1) == LUA_TTABLE) {
		lua_getfield(L, -1, "key");
		int keycode = lua_type(L, -1) == LUA_TSTRING
		            ? chord_keycode_for(lua_tostring(L, -1)) : 0;
		lua_pop(L, 1);                           /* key */

		lua_getfield(L, -1, "action");           /* kept on stack */
		int is_unbind = (lua_type(L, -1) == LUA_TBOOLEAN
		                 && !lua_toboolean(L, -1));
		const char *act = lua_type(L, -1) == LUA_TSTRING
		                ? lua_tostring(L, -1) : NULL;

		unsigned mods = 0;
		int mods_ok = 1;
		lua_getfield(L, -2, "mods");             /* -2 = record */
		if (lua_type(L, -1) == LUA_TTABLE) {
			size_t n = (size_t)lua_rawlen(L, -1);
			for (size_t j = 1; j <= n; ++j) {
				lua_rawgeti(L, -1, (lua_Integer)j);
				unsigned b = lua_type(L, -1) == LUA_TSTRING
				           ? chord_mod_for(lua_tostring(L, -1)) : 0;
				if (b) { mods |= b; } else { mods_ok = 0; }
				lua_pop(L, 1);
			}
		} else if (lua_type(L, -1) != LUA_TNIL) {
			mods_ok = 0;                         /* present but not a table */
		}

		lua_getfield(L, -3, "label");            /* -3 = record */
		const char *label = lua_type(L, -1) == LUA_TSTRING
		                  ? lua_tostring(L, -1) : NULL;

		/* action = false unbinds the matching default; the (keycode,mods)
		 * still identify which one, so they are required even to unbind. */
		int act_ok = is_unbind || (act != NULL && act[0]);
		if (keycode != 0 && act_ok && mods_ok) {
			if (out != NULL) {
				out->keycode = keycode;
				out->mods = mods;
				if (!is_unbind) {
					chord_set_action(out, act);
					out->label = label ? strdup(label) : NULL;
				}
			}
			if (unbind) { *unbind = is_unbind; }
			ok = 1;
		}
		lua_pop(L, 3);                           /* label, mods, action */
	}
	lua_pop(L, 1);                                   /* record */
	return ok;
}

static chord_t *chord_defaults(void) {
	chord_t *result = calloc(DEFAULT_CHORD_BINDINGS_LEN + 1, sizeof(chord_t));
	for (size_t i = 0; i < DEFAULT_CHORD_BINDINGS_LEN; ++i) {
		const chord_def_t *d = &DEFAULT_CHORD_BINDINGS[i];
		result[i].keycode = d->keycode;
		result[i].mods = d->mods;
		chord_set_action(&result[i], d->spec);
		result[i].label = d->label ? strdup(d->label) : NULL;
	}
	return result;                                   /* calloc => keycode==0 sentinel */
}

/* Merge the user's chord_bindings OVER DEFAULT_CHORD_BINDINGS, keyed by
 * (keycode, mods): a record with a new trigger is appended, one matching
 * a default overrides it in place, and `action = false` unbinds the
 * matching default. Defaults the user never mentions survive (so new
 * shipped chords reach an existing config). Validate-all-then-build:
 * one malformed record rejects the whole table -> compiled defaults. */
static chord_t *lua_create_chord_array(lua_State *L, const char *key) {
	if (lua_get_table(L, key)) {
		size_t un = (size_t)lua_rawlen(L, -1);
		int valid = 1;
		for (size_t i = 1; i <= un && valid; ++i) {
			if (!lua_chord_at(L, (int)i, NULL, NULL)) {
				valid = 0;
			}
		}
		if (valid) {
			/* worst case: every default kept plus every record appended. */
			chord_t *result = calloc(DEFAULT_CHORD_BINDINGS_LEN + un + 1,
			                         sizeof(chord_t));
			size_t n = 0;
			for (size_t i = 0; i < DEFAULT_CHORD_BINDINGS_LEN; ++i) {
				const chord_def_t *d = &DEFAULT_CHORD_BINDINGS[i];
				result[n].keycode = d->keycode;
				result[n].mods = d->mods;
				chord_set_action(&result[n], d->spec);
				result[n].label = d->label ? strdup(d->label) : NULL;
				++n;
			}
			for (size_t i = 1; i <= un; ++i) {
				chord_t entry = {0};
				int unbind = 0;
				lua_chord_at(L, (int)i, &entry, &unbind);
				size_t j;
				int found = 0;
				for (j = 0; j < n; ++j) {
					if (result[j].keycode == entry.keycode
					    && result[j].mods == entry.mods) {
						found = 1;
						break;
					}
				}
				if (unbind) {
					if (found) {
						free(result[j].spec);
						free(result[j].label);
						memmove(&result[j], &result[j + 1],
						        (n - j - 1) * sizeof(chord_t));
						--n;
					}
				} else if (found) {
					free(result[j].spec);
					free(result[j].label);
					result[j] = entry;   /* takes ownership of spec/label */
				} else {
					result[n++] = entry;
				}
			}
			result[n] = (chord_t){0};
			lua_pop(L, 1);                       /* pop the global table */
			return result;
		}
		lua_pop(L, 1);                               /* pop the global table */
		fprintf(stderr, "invalid chord_bindings %s, using default\n", key);
	}
	return chord_defaults();
}

static symmenu_t *lua_create_symmenu(lua_State *L, const char *key,
                                     int def_num_rows, const int *def_row_lens,
                                     const keymap_t *def_entries) {
	symmenu_t *menu = calloc(1, sizeof(symmenu_t));

	if (lua_get_table(L, key)) {
		int nrows = (int)lua_rawlen(L, -1);
		int valid = 1;
		for (int r = 1; r <= nrows && valid; ++r) {
			lua_rawgeti(L, -1, r);               /* row */
			if (lua_type(L, -1) != LUA_TTABLE || !lua_pairs_valid(L, 0)) {
				valid = 0;                   /* 0: symmenu is replaced, no unbind */
			}
			lua_pop(L, 1);                        /* pop the row */
		}
		if (valid) {
			int num_keys = 0;
			for (int r = 1; r <= nrows; ++r) {
				lua_rawgeti(L, -1, r);
				num_keys += (int)lua_rawlen(L, -1);
				lua_pop(L, 1);
			}
			menu->entries = calloc(num_keys + 1, sizeof(keymap_t));
			menu->entries[num_keys] = (keymap_t){0, NULL};
			menu->keys = calloc(nrows + 1, sizeof(symkey_t *));
			menu->keys[nrows] = NULL;

			int entry_idx = 0;
			for (int r = 0; r < nrows; ++r) {
				lua_rawgeti(L, -1, r + 1);       /* row */
				int col_len = (int)lua_rawlen(L, -1);
				menu->keys[r] = calloc(col_len + 1, sizeof(symkey_t));
				menu->keys[r][col_len].map = NULL;
				for (int c = 0; c < col_len; ++c) {
					const char *f, *t;
					lua_pair_at(L, c + 1, &f, &t, NULL, NULL); /* valid => succeeds */
					menu->entries[entry_idx].from = f[0];
					keymap_set_to(&menu->entries[entry_idx], t);
					lua_pop(L, 1);           /* pop the pair */
					menu->keys[r][c].flash = '\0';
					menu->keys[r][c].map = &menu->entries[entry_idx];
					++entry_idx;
				}
				lua_pop(L, 1);                   /* pop the row */
			}
			lua_pop(L, 1);                           /* pop the global table */
			return menu;
		}
		lua_pop(L, 1);                                   /* pop the global table */
	}

	fprintf(stderr, "invalid symmenu %s, using default\n", key);
	int def_num_keys = 0;
	for (int i = 0; i < def_num_rows; ++i) {
		def_num_keys += def_row_lens[i];
	}
	menu->entries = calloc(def_num_keys + 1, sizeof(keymap_t));
	menu->entries[def_num_keys] = (keymap_t){0, NULL};
	menu->keys = calloc(def_num_rows + 1, sizeof(symkey_t *));
	menu->keys[def_num_rows] = NULL;
	int entry_idx = 0;
	for (int row = 0; row < def_num_rows; ++row) {
		menu->keys[row] = calloc(def_row_lens[row] + 1, sizeof(symkey_t));
		menu->keys[row][def_row_lens[row]].map = NULL;
		for (int col = 0; col < def_row_lens[row]; ++col) {
			menu->entries[entry_idx].from = def_entries[entry_idx].from;
			keymap_set_to(&menu->entries[entry_idx], def_entries[entry_idx].to);
			menu->keys[row][col].flash = '\0';
			menu->keys[row][col].map = &menu->entries[entry_idx];
			++entry_idx;
		}
	}
	return menu;
}

/* Frees any pre-existing sk->uc first so a re-run doesn't leak --
 * lets callers (notably app_reload_config) invoke this on an already
 * decoded prefs struct. */
static void decode_one_symmenu(symmenu_t *menu) {
	if (menu == NULL || menu->keys == NULL) {
		return;
	}
	for (symkey_t **row = menu->keys; *row != NULL; ++row) {
		for (symkey_t *sk = *row; sk->map != NULL; ++sk) {
			free(sk->uc);
			sk->uc = NULL;
			if (sk->map->to == NULL) {
				continue;
			}
			size_t to_len = strlen(sk->map->to);
			sk->uc = (UChar *)calloc(to_len + 1, sizeof(UChar));
			if (sk->uc != NULL) {
				io_read_utf8_string(sk->map->to, to_len, sk->uc);
			}
		}
	}
}

void preferences_decode_symmenu_labels(pref_t *prefs) {
	if (prefs == NULL) {
		return;
	}
	decode_one_symmenu(prefs->main_symmenu);
	for (int i = 0; i < 26; ++i) {
		decode_one_symmenu(prefs->accent_menus[i][0]);
		decode_one_symmenu(prefs->accent_menus[i][1]);
	}
}

void destroy_preferences_members(pref_t *pref) {
	free(pref->font_path);
	free(pref->tty_encoding);

	free(pref->text_color);
	free(pref->background_color);
	free(pref->metamode_hitbox);

	keymap_t *m = pref->metamode_keys;
	while (m->to != NULL) { free(m->to); ++m; }
	free(pref->metamode_keys);

	destroy_symmenu(pref->main_symmenu);
	for (int i = 0; i < 26; ++i) {
		destroy_symmenu(pref->accent_menus[i][0]);
		destroy_symmenu(pref->accent_menus[i][1]);
	}

	m = pref->altsym_entries;
	while (m->to != NULL) { free(m->to); ++m; }
	free(pref->altsym_entries);

	chord_t *c = pref->chord_bindings;
	while (c != NULL && c->keycode != 0) {
		free(c->spec);
		free(c->label);
		++c;
	}
	free(pref->chord_bindings);

	free(pref->keyhold_actions_exempt);
}

void destroy_preferences(pref_t *pref) {
	destroy_preferences_members(pref);
	free(pref);
}

/* --- scalar/string preference schema ---------------------------------
 * Single source of truth for every plain int/bool/string preference:
 * key name, type, the pref_t field (by offset), and default. The Lua
 * scalar reader and the .term.lua emitter are both driven from this
 * table, so a preference cannot drift between the two sides. Structured
 * prefs (colour/hitbox/keymap/symmenu) keep their dedicated builders. */
typedef enum { PS_INT, PS_BOOL, PS_STRING } prefs_scalar_type;

typedef struct {
	const char *key;
	prefs_scalar_type type;
	size_t offset;            /* offsetof(pref_t, field) */
	int int_default;          /* PS_INT / PS_BOOL */
	const char *str_default;  /* PS_STRING */
} prefs_scalar_desc;

static const prefs_scalar_desc PREFS_SCALARS[] = {
	{ "font_path",                PS_STRING, offsetof(pref_t, font_path),                0,                              DEFAULT_FONT_PATH },
	{ "font_size",                PS_INT,    offsetof(pref_t, font_size),                DEFAULT_FONT_SIZE,              NULL },
	{ "screen_idle_awake",        PS_BOOL,   offsetof(pref_t, screen_idle_awake),        DEFAULT_SCREEN_IDLE_AWAKE,      NULL },
	{ "auto_show_vkb",            PS_BOOL,   offsetof(pref_t, auto_show_vkb),            DEFAULT_AUTO_SHOW_VKB,          NULL },
	{ "metamode_doubletap_key",   PS_INT,    offsetof(pref_t, metamode_doubletap_key),   DEFAULT_METAMODE_DOUBLETAP_KEY, NULL },
	{ "metamode_doubletap_delay", PS_INT,    offsetof(pref_t, metamode_doubletap_delay), DEFAULT_METAMODE_DOUBLETAP_DELAY, NULL },
	{ "keyhold_actions",          PS_BOOL,   offsetof(pref_t, keyhold_actions),          DEFAULT_KEYHOLD_ACTIONS,        NULL },
	/* keycode, not a flag */
	{ "metamode_hold_key",        PS_INT,    offsetof(pref_t, metamode_hold_key),        DEFAULT_METAMODE_HOLD_KEY,      NULL },
	{ "allow_resize_columns",     PS_BOOL,   offsetof(pref_t, allow_resize_columns),     DEFAULT_ALLOW_RESIZE_COLUMNS,   NULL },
	{ "tty_encoding",             PS_STRING, offsetof(pref_t, tty_encoding),             0,                              DEFAULT_TTY_ENCODING },
	{ "sticky_sym_key",           PS_BOOL,   offsetof(pref_t, sticky_sym_key),           DEFAULT_STICKY_SYM_KEY,         NULL },
	{ "sticky_shift_key",         PS_BOOL,   offsetof(pref_t, sticky_shift_key),         DEFAULT_STICKY_SHIFT_KEY,       NULL },
	{ "sticky_alt_key",           PS_BOOL,   offsetof(pref_t, sticky_alt_key),           DEFAULT_STICKY_ALT_KEY,         NULL },
	{ "rescreen_for_symmenu",     PS_BOOL,   offsetof(pref_t, rescreen_for_symmenu),     DEFAULT_RESCREEN_FOR_SYMMENU,   NULL },
	{ "keyhold_accents",          PS_BOOL,   offsetof(pref_t, keyhold_accents),          DEFAULT_KEYHOLD_ACCENTS,        NULL },
	{ "show_help_on_startup",     PS_BOOL,   offsetof(pref_t, show_help_on_startup),     DEFAULT_SHOW_HELP_ON_STARTUP,   NULL },
};

static void lua_read_scalars(lua_State *L, pref_t *prefs) {
	for (size_t i = 0; i < sizeof(PREFS_SCALARS) / sizeof(PREFS_SCALARS[0]); ++i) {
		const prefs_scalar_desc *d = &PREFS_SCALARS[i];
		void *field = (char *)prefs + d->offset;
		lua_getglobal(L, d->key);
		int ty = lua_type(L, -1);
		switch (d->type) {
		case PS_INT:
			*(int *)field = (ty == LUA_TNUMBER) ? (int)lua_tointeger(L, -1)
			                                    : d->int_default;
			break;
		case PS_BOOL:
			*(int *)field = (ty == LUA_TBOOLEAN) ? lua_toboolean(L, -1)
			              : (ty == LUA_TNUMBER)  ? (lua_tointeger(L, -1) != 0)
			                                     : d->int_default;
			break;
		case PS_STRING:
			*(char **)field = strdup(ty == LUA_TSTRING ? lua_tostring(L, -1)
			                                            : d->str_default);
			break;
		}
		lua_pop(L, 1);
	}
}

/* Single Lua -> pref_t population routine. Mirrors the field set and
 * defaults of the old libconfig path exactly. */
static void prefs_build_from_lua(lua_State *L, pref_t *prefs) {
	lua_read_scalars(L, prefs);

	/* The version the file declares (0 if absent/non-numeric), kept so
	 * the startup path can detect a config that predates the current
	 * defaults and surface the newly-added/changed bindings (see
	 * prefs_config_outdated). NOT the running code version. */
	lua_getglobal(L, "prefs_version");
	prefs->prefs_version = lua_type(L, -1) == LUA_TNUMBER
	                     ? (int)lua_tointeger(L, -1) : 0;
	lua_pop(L, 1);

	prefs->text_color = lua_create_color(L, "text_color", DEFAULT_TEXT_COLOR);
	prefs->background_color = lua_create_color(L, "background_color", DEFAULT_BACKGROUND_COLOR);
	prefs->metamode_hitbox = lua_create_hitbox(L, "metamode_hitbox", DEFAULT_METAMODE_HITBOX);
	prefs->metamode_keys = lua_create_keymap_array(L, "metamode_keys", DEFAULT_METAMODE_KEYS_LEN, DEFAULT_METAMODE_KEYS);
	prefs->keyhold_actions_exempt = lua_create_int_array(L, "keyhold_actions_exempt", DEFAULT_KEYHOLD_ACTIONS_EXEMPT_LEN, DEFAULT_KEYHOLD_ACTIONS_EXEMPT);

	prefs->main_symmenu = lua_create_symmenu(L, "main_symmenu", DEFAULT_SYMMENU_NUM_ROWS, DEFAULT_SYMMENU_ROW_LENS, DEFAULT_SYMMENU_ENTRIES);
	prefs->altsym_entries = lua_create_keymap_array(L, "altsym_entries", DEFAULT_ALTSYM_ENTRIES_LEN, DEFAULT_ALTSYM_ENTRIES);
	prefs->chord_bindings = lua_create_chord_array(L, "chord_bindings");

	/* accent menus are configurable but not part of the default config */
	char am_name[] = {' ', '_', 'a', 'c', 'c', 'e', 'n', 't', 's', '\0'};
	for (char c = 'a'; c <= 'z'; ++c) {
		size_t idx = (size_t)(c - 'a');
		am_name[0] = c;
		prefs->accent_menus[idx][0] = lua_create_symmenu(L, am_name, 1, &accent_row_lens[idx], lowercase_accent_entries[idx]);
		prefs->accent_menus[idx][1] = lua_create_symmenu(L, am_name, 1, &accent_row_lens[idx], uppercase_accent_entries[idx]);
	}
}

int is_int_member(int const* list, int target) {
	for (int i = 0; list[i] != -1; ++i) {
		if (list[i] == target) {
			return 1;
		}
	}
	return 0;
}

keymap_t* keymap_lookup(char keystroke, keymap_t *keymap_head) {
	while (keymap_head->to != NULL) {
		if (keymap_head->from == keystroke) {
			return keymap_head;
		}
		++keymap_head;
	}
	return NULL;
}

const char* keystroke_lookup(char keystroke, keymap_t *keymap_head) {
	keymap_t *entry = keymap_lookup(keystroke, keymap_head);
	return entry != NULL ? entry->to : NULL;
}

/* chord_lookup / chord_lookup_subset live in chord_match.c (pure, host-testable). */

/* ====================================================================
 * Lua loader + scripting. The lua_State is created here, kept for the
 * process lifetime so config-defined functions remain callable from
 * keybindings, and closed in prefs_lua_destroy. It never escapes this
 * TU; main.c reaches scripting only through prefs_lua_invoke().
 * ==================================================================== */

static lua_State *g_lua_state = NULL;

/* Last config load/reload error, for queryable validation feedback (the
 * control `validate`/error path) and a reload-failure toast. Empty = none. */
static char g_last_config_error[512];

/* Minimal Lua-callable surface. Glue (terminal.h) keeps app/renderer
 * internals out of this TU. Broader APIs are intentionally deferred. */
static int luaC_font_size_set(lua_State *L) {
	set_font_size((int)luaL_checkinteger(L, 1));
	return 0;
}
static int luaC_font_size_get(lua_State *L) {
	lua_pushinteger(L, term_current_font_size());
	return 1;
}
static int luaC_action(lua_State *L) {
	lua_pushboolean(L, app_run_action_string(luaL_checkstring(L, 1)));
	return 1;
}
/* term.toast(msg) / term.open_url(uri): build the prefixed action string
 * (lua_pushfstring keeps it alive on the stack across the synchronous
 * dispatch) and route through the one action path. */
static int luaC_toast(lua_State *L) {
	const char *s = lua_pushfstring(L, "toast:%s", luaL_checkstring(L, 1));
	lua_pushboolean(L, app_run_action_string(s));
	return 1;
}
/* term.notify(msg) | term.notify{ id=, title=, body=, uri=, app_id=, alert= }:
 * post or update a replaceable Hub entry (#35). The string form fills body and
 * takes defaults; the table form names a reusable slot via `id` (same id
 * updates that entry in place). Field strings stay on the Lua stack across the
 * synchronous post, so the borrowed pointers in the spec remain valid. */
static int luaC_notify(lua_State *L) {
	notification_spec_t spec = {0};
	if (lua_istable(L, 1)) {
		lua_getfield(L, 1, "app_id"); spec.app_id  = lua_tostring(L, -1);
		lua_getfield(L, 1, "id");     spec.item_id = lua_tostring(L, -1);
		lua_getfield(L, 1, "title");  spec.title   = lua_tostring(L, -1);
		lua_getfield(L, 1, "body");   spec.body    = lua_tostring(L, -1);
		lua_getfield(L, 1, "uri");    spec.uri     = lua_tostring(L, -1);
		lua_getfield(L, 1, "alert");  spec.alert   = lua_toboolean(L, -1);
	} else {
		spec.body = luaL_checkstring(L, 1);
	}
	lua_pushboolean(L, app_post_notification(&spec));
	return 1;
}
static int luaC_open_url(lua_State *L) {
	const char *s = lua_pushfstring(L, "open_url:%s", luaL_checkstring(L, 1));
	lua_pushboolean(L, app_run_action_string(s));
	return 1;
}
static int luaC_url_pick(lua_State *L) {
	lua_pushboolean(L, app_run_action_string("url_pick"));
	return 1;
}
static int luaC_keyboard_show(lua_State *L) {
	lua_pushboolean(L, app_run_action_string("keyboard_show"));
	return 1;
}
static int luaC_keyboard_hide(lua_State *L) {
	lua_pushboolean(L, app_run_action_string("keyboard_hide"));
	return 1;
}
static const luaL_Reg TERM_LUA_LIB[] = {
	{ "font_size_set", luaC_font_size_set },
	{ "font_size_get", luaC_font_size_get },
	{ "action",        luaC_action },
	{ "toast",         luaC_toast },
	{ "notify",        luaC_notify },
	{ "open_url",      luaC_open_url },
	{ "url_pick",      luaC_url_pick },
	{ "keyboard_show", luaC_keyboard_show },
	{ "keyboard_hide", luaC_keyboard_hide },
	{ NULL, NULL }
};

/* Fresh Lua state with stdlib + the `term` table registered (so the
 * config can use it at top level and in keybinding functions). NULL on
 * OOM; the caller decides whether that is fatal. */
static lua_State *lua_new_state(void) {
	lua_State *L = luaL_newstate();
	if (L == NULL) {
		return NULL;
	}
	luaL_openlibs(L);
	luaL_newlib(L, TERM_LUA_LIB);
	lua_setglobal(L, "term");
	return L;
}

/* lua_pcall message handler: prepend a Lua-level traceback (file:line + call
 * stack) to the error so config/eval failures locate the fault, not just name
 * it. Mirrors the standard lua.c msgh. */
static int lua_traceback_msgh(lua_State *L) {
	const char *msg = lua_tostring(L, 1);
	if (msg == NULL) {
		if (luaL_callmeta(L, 1, "__tostring") &&
		    lua_type(L, -1) == LUA_TSTRING) {
			return 1;
		}
		msg = lua_pushfstring(L, "(error object is a %s value)",
		                      luaL_typename(L, 1));
	}
	luaL_traceback(L, L, msg, 1);
	return 1;
}

/* luaL_dofile, but with lua_traceback_msgh installed for the execution phase.
 * Compile errors carry no stack so they pass through unchanged; runtime errors
 * gain a traceback. Matches luaL_dofile's contract: nothing left on the stack
 * on success, the (traceback-augmented) error message on top on failure. */
static int lua_dofile_traceback(lua_State *L, const char *path) {
	lua_pushcfunction(L, lua_traceback_msgh);
	int msgh = lua_gettop(L);
	if (luaL_loadfile(L, path) != LUA_OK) {
		lua_remove(L, msgh);   /* compile error on top; drop the handler under it */
		return LUA_ERRSYNTAX;
	}
	int rc = lua_pcall(L, 0, 0, msgh);
	lua_remove(L, msgh);       /* on error this leaves the error object on top */
	return rc;
}

/* Shared core for the startup loader and the live reloader. Creates a
 * fresh lua_State (returned via *out_L; NULL only on luaL_newstate OOM),
 * runs the file, and -- unless the file failed to parse and the caller
 * opted out via build_on_parse_error -- builds a pref_t from it.
 *
 * *out_parsed is 1 iff the file parsed. On a parse failure the Lua error
 * message is left at the top of *out_L for the caller to log; it is NOT
 * popped here (prefs_build_from_lua is stack-neutral, so it survives a
 * build-on-error). Return value:
 *   - non-NULL  : built pref_t (all-defaults if !*out_parsed)
 *   - NULL, *out_L == NULL          : luaL_newstate OOM
 *   - NULL, *out_L != NULL, !parsed : parse error, build_on_parse_error=0
 *                                     (no pref_t allocated)
 *   - NULL, *out_L != NULL, parsed  : calloc OOM
 * The caller owns *out_L: commit it to g_lua_state, or lua_close() it. */
static pref_t *prefs_lua_try_build(const char *path, int build_on_parse_error,
                                   lua_State **out_L, int *out_parsed) {
	lua_State *L = lua_new_state();
	*out_L = L;
	*out_parsed = 0;
	if (L == NULL) {
		return NULL;
	}
	if (lua_dofile_traceback(L, path) != LUA_OK) {
		if (!build_on_parse_error) {
			return NULL;  /* error message left at L's stack top */
		}
		/* fall through: every global absent -> all defaults */
	} else {
		*out_parsed = 1;
	}

	pref_t *prefs = calloc(1, sizeof(pref_t));
	if (prefs == NULL) {
		return NULL;
	}
	prefs_build_from_lua(L, prefs);   /* sets prefs_version from the file */
	return prefs;
}

/* Startup loader. Always returns a usable pref_t: a missing/broken
 * .term.lua falls back to compiled defaults, which is the only
 * sensible behaviour at startup (there is no prior config to keep).
 * Destructive: commits the new lua_State unconditionally. */
pref_t *prefs_lua_load(const char *path) {
	if (g_lua_state) { lua_close(g_lua_state); g_lua_state = NULL; }

	lua_State *L;
	int parsed;
	pref_t *prefs = prefs_lua_try_build(path, 1, &L, &parsed);
	if (prefs == NULL) {
		fprintf(stderr, "fatal error: %s\n",
		        L == NULL ? "luaL_newstate failed"
		                  : "failed to calloc prefs structure");
		exit(1);
	}
	if (!parsed) {
		snprintf(g_last_config_error, sizeof(g_last_config_error), "%s",
		         lua_tostring(L, -1));
		fprintf(stderr, APP_LOG_TAG ": error loading %s: %s\n",
		        path, lua_tostring(L, -1));
		lua_pop(L, 1);
	} else {
		g_last_config_error[0] = '\0';
	}

	g_lua_state = L; /* retained for scripting; closed in prefs_lua_destroy */
	return prefs;
}

/* Reload loader. Non-destructive on failure: a Lua syntax/parse error
 * (or OOM) returns NULL with the live g_lua_state and the caller's
 * pref_t left completely untouched, so a fat-fingered config edit can
 * never silently wipe a running setup back to defaults. On success the
 * new scripting state is committed and a fresh pref_t returned for the
 * caller to move into place. */
pref_t *prefs_lua_reload(void) {
	lua_State *L;
	int parsed;
	pref_t *prefs = prefs_lua_try_build(PREFS_LUA_FILE_PATH, 0, &L, &parsed);
	if (prefs == NULL) {
		if (L == NULL) {
			snprintf(g_last_config_error, sizeof(g_last_config_error),
			         "out of memory");
			fprintf(stderr, APP_LOG_TAG ": reload aborted: out of memory\n");
		} else if (!parsed) {
			snprintf(g_last_config_error, sizeof(g_last_config_error), "%s",
			         lua_tostring(L, -1));
			fprintf(stderr, APP_LOG_TAG ": reload rejected, keeping current config: %s\n",
			        lua_tostring(L, -1));
			lua_close(L);
		} else {
			snprintf(g_last_config_error, sizeof(g_last_config_error),
			         "out of memory building prefs");
			lua_close(L);  /* calloc OOM after a good parse */
		}
		return NULL;
	}

	/* Commit the scripting state only now that the file parsed. */
	g_last_config_error[0] = '\0';   /* parsed clean; clear any stale error */
	if (g_lua_state) { lua_close(g_lua_state); }
	g_lua_state = L;
	return prefs;
}

const char *prefs_lua_last_error(void) {
	return g_last_config_error[0] != '\0' ? g_last_config_error : NULL;
}

int prefs_lua_validate(const char *path, char *out, size_t outsz) {
	if (out != NULL && outsz > 0) {
		out[0] = '\0';
	}
	if (path == NULL || path[0] == '\0') {
		path = PREFS_LUA_FILE_PATH;
	}
	/* Compile-check only: luaL_loadfile parses without executing, so no config
	 * side effects (term.* calls, font changes) fire -- safe to run inline off
	 * the control socket. Catches syntax errors; runtime errors surface on the
	 * real reload, which is non-destructive and now reports via the toast. */
	lua_State *L = lua_new_state();
	if (L == NULL) {
		if (out != NULL && outsz > 0) {
			snprintf(out, outsz, "out of memory");
		}
		return -1;
	}
	if (luaL_loadfile(L, path) != LUA_OK) {
		if (out != NULL && outsz > 0) {
			snprintf(out, outsz, "%s", lua_tostring(L, -1));
		}
		lua_close(L);
		return -1;
	}
	lua_close(L);
	if (out != NULL && outsz > 0) {
		snprintf(out, outsz, "ok");
	}
	return 0;
}

/* .term.lua is hand-authored; Term50 never writes it back (a first-run
 * default is emitted by prefs_emit_lua, not through this path). */
void prefs_lua_destroy(pref_t *pref) {
	destroy_preferences(pref); /* format-agnostic: frees pref_t only */
	if (g_lua_state) {
		lua_close(g_lua_state);
		g_lua_state = NULL;
	}
}

int prefs_lua_invoke(const char *name) {
	if (g_lua_state == NULL || name == NULL) {
		return 0;
	}
	lua_State *L = g_lua_state;
	lua_getglobal(L, name);
	if (!lua_isfunction(L, -1)) {
		lua_pop(L, 1);
		return 0;
	}
	if (lua_pcall(L, 0, 0, 0) != LUA_OK) {
		fprintf(stderr, APP_LOG_TAG ": lua error in '%s': %s\n",
		        name, lua_tostring(L, -1));
		lua_pop(L, 1);
		return 0;
	}
	return 1;
}

int prefs_lua_eval(const char *src, char *out, size_t outsz) {
	if (out != NULL && outsz > 0) {
		out[0] = '\0';
	}
	if (g_lua_state == NULL || src == NULL) {
		if (out != NULL && outsz > 0) {
			snprintf(out, outsz, "lua scripting unavailable");
		}
		return -1;
	}
	lua_State *L = g_lua_state;
	int base = lua_gettop(L);
	lua_pushcfunction(L, lua_traceback_msgh);   /* msgh at base+1 */

	if (luaL_loadstring(L, src) != LUA_OK ||
	    lua_pcall(L, 0, LUA_MULTRET, base + 1) != LUA_OK) {
		if (out != NULL && outsz > 0) {
			snprintf(out, outsz, "%s", lua_tostring(L, -1));
		}
		lua_settop(L, base);
		return -1;
	}

	/* Stringify any return values, tab-separated, into out. Results sit above
	 * the msgh slot (base+1), so they start at base+2. */
	int nres = lua_gettop(L) - (base + 1);
	if (out != NULL && outsz > 0 && nres > 0) {
		size_t off = 0;
		for (int i = 1; i <= nres && off + 1 < outsz; ++i) {
			if (i > 1) {
				out[off++] = '\t';
			}
			const char *s = luaL_tolstring(L, base + 1 + i, NULL); /* pushes a string */
			int n = snprintf(out + off, outsz - off, "%s", s != NULL ? s : "");
			lua_pop(L, 1); /* drop the luaL_tolstring result */
			if (n > 0) {
				off += (size_t)n < outsz - off ? (size_t)n : outsz - off - 1;
			}
		}
		out[off] = '\0';
	}
	lua_settop(L, base);
	return 0;
}

/* --- pref_t -> .term.lua emitter (first-run default config) --------
 * Emits the scalar schema + structured fields a user is expected to
 * tweak (accent menus omitted, matching the historical default). */
static void lua_emit_qstr(FILE *f, const char *s) {
	fputc('"', f);
	if (s) {
		for (; *s; ++s) {
			unsigned char c = (unsigned char)*s;
			if (c == '"')       fputs("\\\"", f);
			else if (c == '\\') fputs("\\\\", f);
			else if (c >= 0x20 && c < 0x7f) fputc(c, f);
			else fprintf(f, "\\x%02X", c); /* Lua 5.2+ hex escape */
		}
	}
	fputc('"', f);
}

static void lua_emit_keymap(FILE *f, const char *key, const keymap_t *km) {
	fprintf(f, "%s = {\n", key);
	for (; km && km->to != NULL; ++km) {
		char from[2] = { km->from, '\0' };
		fputs("  { ", f);
		lua_emit_qstr(f, from);
		fputs(", ", f);
		lua_emit_qstr(f, km->to);
		fputs(km->sticky ? ", sticky = true },\n" : " },\n", f);
	}
	fputs("}\n\n", f);
}

static void lua_emit_symmenu(FILE *f, const char *key, const symmenu_t *m) {
	fprintf(f, "%s = {\n", key);
	if (m && m->keys && m->keys[0]) {
		int row = 0, col = 0;
		fputs("  {", f);
		while (1) {
			if (m->keys[row][col].map == NULL) {
				fputs(" },\n", f);
				++row;
				if (m->keys[row] == NULL) break;
				col = 0;
				fputs("  {", f);
				continue;
			}
			char from[2] = { m->keys[row][col].map->from, '\0' };
			fputs(" { ", f);
			lua_emit_qstr(f, from);
			fputs(", ", f);
			lua_emit_qstr(f, m->keys[row][col].map->to);
			fputs(" },", f);
			++col;
		}
	}
	fputs("}\n\n", f);
}

/* Reconstruct a chord's key-name for emission (inverse of
 * chord_keycode_for). Single-byte triggers go into the caller's buf. */
static const char *chord_keyname(int keycode, char buf[2]) {
	switch (keycode) {
	case KEYCODE_BB_SYM_KEY:  return "sym";
	case KEYCODE_BB_ALT_KEY:  return "alt";
	case KEYCODE_LEFT_SHIFT:
	case KEYCODE_RIGHT_SHIFT: return "shift";
	case KEYCODE_SPACE:       return "space";
	case KEYCODE_RETURN:      return "enter";
	case KEYCODE_TAB:         return "tab";
	case KEYCODE_ESCAPE:      return "escape";
	case KEYCODE_BACKSPACE:   return "backspace";
	default:
		buf[0] = (char)keycode;
		buf[1] = '\0';
		return buf;
	}
}

static void lua_emit_chords(FILE *f, const char *key, const chord_t *cb) {
	static const struct { unsigned bit; const char *name; } mods[] = {
		{ KEYMOD_CTRL, "ctrl" }, { KEYMOD_ALT, "alt" },
		{ KEYMOD_SHIFT, "shift" }, { CHORD_MOD_SYM, "sym" },
	};
	fprintf(f, "%s = {\n", key);
	for (; cb && cb->keycode != 0; ++cb) {
		char kb[2];
		fputs("  { key = ", f);
		lua_emit_qstr(f, chord_keyname(cb->keycode, kb));
		fputs(", mods = {", f);
		int first = 1;
		for (size_t i = 0; i < sizeof(mods) / sizeof(mods[0]); ++i) {
			if (cb->mods & mods[i].bit) {
				fputs(first ? " " : ", ", f);
				lua_emit_qstr(f, mods[i].name);
				first = 0;
			}
		}
		fputs(first ? "}, action = " : " }, action = ", f);
		lua_emit_qstr(f, cb->spec ? cb->spec : "");
		if (cb->label) {
			fputs(", label = ", f);
			lua_emit_qstr(f, cb->label);
		}
		fputs(" },\n", f);
	}
	fputs("}\n\n", f);
}

/* True if the loaded config declares an older prefs_version than the
 * running build -- i.e. it predates some current defaults. The startup
 * path uses this to pop the help overlay once so newly-added or
 * re-merged default bindings are visible. A first-run stub is written at
 * the current version, so a fresh install is never "outdated". */
int prefs_config_outdated(const pref_t *prefs) {
	return prefs != NULL && prefs->prefs_version < PREFS_VERSION;
}

/* First-run config: a SPARSE stub, not a full snapshot. Keybinding
 * tables merge over the compiled defaults, so a file that lists only the
 * user's changes keeps receiving new defaults on upgrade. The live help
 * overlay (metamode + '?') is the source of truth for what's bound. */
void prefs_emit_lua_stub(const char *path) {
	FILE *f = fopen(path, "w");
	if (f == NULL) {
		fprintf(stderr, APP_LOG_TAG ": cannot write %s: %s\n", path, strerror(errno));
		return;
	}
	fputs("-- " APP_DISPLAY_NAME " configuration (Lua). Generated on first run;\n"
	      "-- safe to edit. " APP_DISPLAY_NAME " never rewrites it.\n"
	      "--\n"
	      "-- SECURITY: this file is executed as a full Lua program at startup\n"
	      "-- and on reload, with the full standard library (including os and\n"
	      "-- io). Treat it like a shell rc -- only run a " APP_CONFIG_BASENAME " you wrote\n"
	      "-- or trust.\n\n", f);
	fprintf(f, "prefs_version = %d\n\n", PREFS_VERSION);
	fputs("-- This file starts empty: every setting and keybinding uses its\n"
	      "-- built-in default until you override it below.\n"
	      "--\n"
	      "-- To see every binding that is ACTIVE right now: tap the metamode\n"
	      "-- key (right shift by default), then press '?'. That on-screen help\n"
	      "-- overlay always lists the live, effective bindings -- it is the\n"
	      "-- source of truth, not this file.\n"
	      "--\n"
	      "-- The keybinding tables (metamode_keys, chord_bindings,\n"
	      "-- altsym_entries) MERGE over the defaults, so list only what you\n"
	      "-- change. Rebind a key by listing it again; remove a default with\n"
	      "-- false, e.g.\n"
	      "--   metamode_keys = { { \"r\", false } }   -- unbind reload_config\n"
	      "-- Because this file holds only your changes, default bindings added\n"
	      "-- in future " APP_DISPLAY_NAME " releases reach you automatically.\n"
	      "--\n"
	      "-- The bundled share/term.lua.reference lists every default and\n"
	      "-- documents the full configuration surface.\n", f);
	fclose(f);
}

void prefs_emit_lua(const pref_t *prefs, const char *path) {
	FILE *f = fopen(path, "w");
	if (f == NULL) {
		fprintf(stderr, APP_LOG_TAG ": cannot write %s: %s\n", path, strerror(errno));
		return;
	}

	fputs("-- " APP_DISPLAY_NAME " configuration (Lua). Generated automatically; safe to\n"
	      "-- edit. This file is never rewritten by " APP_DISPLAY_NAME ".\n"
	      "--\n"
	      "-- Bundled font examples (Term50 expands a leading $SANDBOX/):\n"
	      "--   font_path = \"$SANDBOX/app/native/fonts/IosevkaTerm-Regular.ttf\"\n"
	      "--   font_path = \"$SANDBOX/app/native/fonts/JetBrainsMono-Regular.ttf\"\n"
	      "--   font_path = \"$SANDBOX/app/native/fonts/SourceCodePro-Regular.otf\"\n"
	      "--   font_path = \"$SANDBOX/app/native/fonts/UbuntuMono-R.ttf\"\n"
	      "--\n"
	      "-- SECURITY: this file is executed as a full Lua program at startup\n"
	      "-- and on reload, with the full standard library (including os and\n"
	      "-- io). Treat it like a shell rc -- only run a " APP_CONFIG_BASENAME " you wrote\n"
	      "-- or trust.\n\n", f);
	fprintf(f, "prefs_version = %d\n\n", PREFS_VERSION);

	for (size_t i = 0; i < sizeof(PREFS_SCALARS) / sizeof(PREFS_SCALARS[0]); ++i) {
		const prefs_scalar_desc *d = &PREFS_SCALARS[i];
		const void *field = (const char *)prefs + d->offset;
		switch (d->type) {
		case PS_INT:
			fprintf(f, "%s = %d\n", d->key, *(const int *)field);
			break;
		case PS_BOOL:
			fprintf(f, "%s = %s\n", d->key,
			        *(const int *)field ? "true" : "false");
			break;
		case PS_STRING:
			fprintf(f, "%s = ", d->key);
			lua_emit_qstr(f, *(char *const *)field);
			fputc('\n', f);
			break;
		}
	}
	fputc('\n', f);

	fprintf(f, "text_color = { %d, %d, %d }\n",
	        prefs->text_color[0], prefs->text_color[1], prefs->text_color[2]);
	fprintf(f, "background_color = { %d, %d, %d }\n",
	        prefs->background_color[0], prefs->background_color[1],
	        prefs->background_color[2]);
	if (prefs->metamode_hitbox) {
		fprintf(f, "metamode_hitbox = { %d, %d, %d, %d }\n",
		        prefs->metamode_hitbox->x, prefs->metamode_hitbox->y,
		        prefs->metamode_hitbox->w, prefs->metamode_hitbox->h);
	}
	fputs("keyhold_actions_exempt = {", f);
	for (int i = 0; prefs->keyhold_actions_exempt &&
	                prefs->keyhold_actions_exempt[i] > 0; ++i) {
		fprintf(f, "%s %d", i ? "," : "", prefs->keyhold_actions_exempt[i]);
	}
	fputs(" }\n\n", f);

	fputs("-- metamode_keys: tap the metamode key, then one of these. An entry\n"
	      "-- exits metamode after firing unless it sets sticky = true (which\n"
	      "-- keeps metamode armed, e.g. the arrow keys below). Targets accept\n"
	      "-- raw bytes, terminfo names, builtins, or \"lua:<fn>\".\n"
	      "--\n"
	      "-- This table MERGES over the defaults below: in your own config you\n"
	      "-- only need the entries you change. List a key again to rebind it,\n"
	      "-- or set its target to false to unbind a default, e.g.\n"
	      "--   metamode_keys = { { \"r\", false } }   -- drop reload_config\n", f);
	lua_emit_keymap(f, "metamode_keys", prefs->metamode_keys);
	fputs("-- altsym_entries: ALT layer symbols, merged over the defaults the\n"
	      "-- same way (rebind by listing a key; { \"x\", false } unbinds it).\n", f);
	lua_emit_keymap(f, "altsym_entries", prefs->altsym_entries);
	fputs("-- main_symmenu: the SYM overlay grid. Unlike the keymap tables this\n"
	      "-- is a positional layout and is REPLACED wholesale if you define it.\n", f);
	lua_emit_symmenu(f, "main_symmenu", prefs->main_symmenu);

	fputs("-- Modifier-aware chord bindings: a trigger key plus a set of\n"
	      "-- held/stuck modifiers dispatch to an action (the same action\n"
	      "-- strings metamode_keys accepts: raw bytes, terminfo names,\n"
	      "-- builtins, or \"lua:<fn>\"). mods may be \"ctrl\", \"alt\", \"shift\",\n"
	      "-- \"sym\"; an empty/absent mods means no modifiers. label is shown\n"
	      "-- in the help overlay (toggle a key bound to \"help_overlay\").\n"
	      "--\n"
	      "-- The defaults below give the bare BlackBerry keyboard (no ctrl\n"
	      "-- key) a Ctrl and an extra Meta, plus easy Tab: tap shift, then\n"
	      "-- alt -> the next key is Ctrl+key; tap shift, then sym -> toggles\n"
	      "-- metamode; alt+enter -> Tab.\n"
	      "-- This table MERGES over the defaults too: unbind one with a record\n"
	      "-- whose action is false, e.g. { key = \"alt\", mods = {\"shift\"},\n"
	      "-- action = false }.\n", f);
	lua_emit_chords(f, "chord_bindings", prefs->chord_bindings);
	fputs("-- External-keyboard examples (a real Ctrl key); uncomment to use:\n"
	      "--   { key = \"t\", mods = {\"ctrl\", \"shift\"}, action = \"tab_new\",   label = \"New tab\" },\n"
	      "--   { key = \"w\", mods = {\"ctrl\", \"shift\"}, action = \"tab_close\", label = \"Close tab\" },\n\n", f);

	fputs("-- Scripting example (uncomment to use). A key bound to\n"
	      "-- \"lua:<name>\" in metamode_keys calls the matching Lua\n"
	      "-- function via lua_pcall. term.font_size_get/set and\n"
	      "-- term.action(\"<builtin>\") (e.g. \"reload_config\") are available.\n"
	      "--\n"
	      "-- function zoom_in() term.font_size_set(term.font_size_get() + 3) end\n"
	      "-- then add  { \"z\", \"lua:zoom_in\" }  to metamode_keys above.\n", f);

	fclose(f);
}
