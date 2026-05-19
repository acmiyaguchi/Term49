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
#include "accent_menus.h"
#include "action.h"
#include "symmenu.h"
#include "prefs.h"

#define README_FILE_PATH "../app/native/README"
#define README45_FILE_PATH "../app/native/README45"

#define PREFS_COLOR_NUM_ELEMENTS 3
#define PREFS_SYMKEYS_DEFAULT_NUM_ROWS 2

static const int PREFS_VERSION = 9;

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
#define DEFAULT_METAMODE_KEYS_LEN 2
#define DEFAULT_METAMODE_KEYS (keymap_t[]){{'e', "\x1b"}, {'t', "\x09"}}
#define DEFAULT_METAMODE_STICKY_KEYS_LEN 4
#define DEFAULT_METAMODE_STICKY_KEYS (keymap_t[]){{'k', "kcuu1"}, \
                                                  {'j', "kcud1"}, \
                                                  {'l', "kcuf1"}, \
                                                  {'h', "kcub1"}}
#define DEFAULT_METAMODE_FUNC_KEYS_LEN 8
#define DEFAULT_METAMODE_FUNC_KEYS (keymap_t[]){{'a', "alt_down"}, \
                                                {'c', "ctrl_down"}, \
                                                {'s', "rescreen"}, \
                                                {'v', "paste_clipboard"}, \
                                                {'=', "font_size_increase"}, \
                                                {'-', "font_size_decrease"}, \
                                                {'0', "font_size_reset"}, \
                                                {'r', "reload_config"}}
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
 * symlink the bundled README into HOME. (The default .term49.lua is
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
 * fields directly from the user's .term49.lua globals, mirroring the
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
 * the caller pops the pair). On failure returns 0 with nothing left. */
static int lua_pair_at(lua_State *L, int i, const char **from, const char **to) {
	int ok = 0;
	lua_rawgeti(L, -1, (lua_Integer)i);          /* pair */
	if (lua_type(L, -1) == LUA_TTABLE) {
		lua_rawgeti(L, -1, 1);
		lua_rawgeti(L, -2, 2);
		const char *f = lua_type(L, -2) == LUA_TSTRING ? lua_tostring(L, -2) : NULL;
		const char *t = lua_type(L, -1) == LUA_TSTRING ? lua_tostring(L, -1) : NULL;
		if (f && f[0] && t) { *from = f; *to = t; ok = 1; }
		lua_pop(L, 2);                       /* pop the two field values */
	}
	if (!ok) {
		lua_pop(L, 1);                       /* pop the pair */
	}
	return ok;
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
		size_t n = (size_t)lua_rawlen(L, -1);
		int good = 1;
		for (size_t i = 1; i <= n && good; ++i) {
			const char *f, *t;
			if (lua_pair_at(L, (int)i, &f, &t)) {
				lua_pop(L, 1);               /* pop validated pair */
			} else {
				good = 0;
			}
		}
		if (good) {
			keymap_t *result = calloc(n + 1, sizeof(keymap_t));
			result[n] = (keymap_t){0, NULL};
			for (size_t i = 1; i <= n; ++i) {
				const char *f, *t;
				lua_pair_at(L, (int)i, &f, &t);  /* good => succeeds */
				result[i - 1].from = f[0];
				keymap_set_to(&result[i - 1], t);
				lua_pop(L, 1);               /* pop the pair */
			}
			lua_pop(L, 1);                       /* pop the global table */
			return result;
		}
		lua_pop(L, 1);                               /* pop the global table */
	}

	fprintf(stderr, "invalid keymap list %s, using default\n", key);
	keymap_t *result = calloc(def_len + 1, sizeof(keymap_t));
	result[def_len] = (keymap_t){0, NULL};
	for (size_t i = 0; i < def_len; ++i) {
		result[i].from = def[i].from;
		keymap_set_to(&result[i], def[i].to);
	}
	return result;
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
			if (lua_type(L, -1) != LUA_TTABLE) {
				valid = 0;
			} else {
				int nc = (int)lua_rawlen(L, -1);
				for (int c = 1; c <= nc && valid; ++c) {
					const char *f, *t;
					if (lua_pair_at(L, c, &f, &t)) {
						lua_pop(L, 1);       /* pop validated pair */
					} else {
						valid = 0;
					}
				}
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
					lua_pair_at(L, c + 1, &f, &t); /* valid => succeeds */
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

void destroy_preferences_members(pref_t *pref) {
	free(pref->font_path);
	free(pref->tty_encoding);

	free(pref->text_color);
	free(pref->background_color);
	free(pref->metamode_hitbox);

	keymap_t *m = pref->metamode_keys;
	while (m->to != NULL) { free(m->to); ++m; }
	free(pref->metamode_keys);

	m = pref->metamode_sticky_keys;
	while (m->to != NULL) { free(m->to); ++m; }
	free(pref->metamode_sticky_keys);

	m = pref->metamode_func_keys;
	while (m->to != NULL) { free(m->to); ++m; }
	free(pref->metamode_func_keys);

	destroy_symmenu(pref->main_symmenu);
	for (int i = 0; i < 26; ++i) {
		destroy_symmenu(pref->accent_menus[i][0]);
		destroy_symmenu(pref->accent_menus[i][1]);
	}

	m = pref->altsym_entries;
	while (m->to != NULL) { free(m->to); ++m; }
	free(pref->altsym_entries);

	free(pref->keyhold_actions_exempt);
}

void destroy_preferences(pref_t *pref) {
	destroy_preferences_members(pref);
	free(pref);
}

/* --- scalar/string preference schema ---------------------------------
 * Single source of truth for every plain int/bool/string preference:
 * key name, type, the pref_t field (by offset), and default. The Lua
 * scalar reader and the .term49.lua emitter are both driven from this
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

	prefs->text_color = lua_create_int_array(L, "text_color", PREFS_COLOR_NUM_ELEMENTS, DEFAULT_TEXT_COLOR);
	prefs->background_color = lua_create_int_array(L, "background_color", PREFS_COLOR_NUM_ELEMENTS, DEFAULT_BACKGROUND_COLOR);
	prefs->metamode_hitbox = lua_create_hitbox(L, "metamode_hitbox", DEFAULT_METAMODE_HITBOX);
	prefs->metamode_keys = lua_create_keymap_array(L, "metamode_keys", DEFAULT_METAMODE_KEYS_LEN, DEFAULT_METAMODE_KEYS);
	prefs->metamode_sticky_keys = lua_create_keymap_array(L, "metamode_sticky_keys", DEFAULT_METAMODE_STICKY_KEYS_LEN, DEFAULT_METAMODE_STICKY_KEYS);
	prefs->metamode_func_keys = lua_create_keymap_array(L, "metamode_func_keys", DEFAULT_METAMODE_FUNC_KEYS_LEN, DEFAULT_METAMODE_FUNC_KEYS);
	prefs->keyhold_actions_exempt = lua_create_int_array(L, "keyhold_actions_exempt", DEFAULT_KEYHOLD_ACTIONS_EXEMPT_LEN, DEFAULT_KEYHOLD_ACTIONS_EXEMPT);

	prefs->main_symmenu = lua_create_symmenu(L, "main_symmenu", DEFAULT_SYMMENU_NUM_ROWS, DEFAULT_SYMMENU_ROW_LENS, DEFAULT_SYMMENU_ENTRIES);
	prefs->altsym_entries = lua_create_keymap_array(L, "altsym_entries", DEFAULT_ALTSYM_ENTRIES_LEN, DEFAULT_ALTSYM_ENTRIES);

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

/* ====================================================================
 * Lua loader + scripting. The lua_State is created here, kept for the
 * process lifetime so config-defined functions remain callable from
 * keybindings, and closed in prefs_lua_destroy. It never escapes this
 * TU; main.c reaches scripting only through prefs_lua_invoke().
 * ==================================================================== */

static lua_State *g_lua_state = NULL;

/* Minimal Lua-callable surface. Glue (terminal.h) keeps app/SDL
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
static const luaL_Reg TERM_LUA_LIB[] = {
	{ "font_size_set", luaC_font_size_set },
	{ "font_size_get", luaC_font_size_get },
	{ "action",        luaC_action },
	{ NULL, NULL }
};

static pref_t *prefs_lua_load(const char *path) {
	if (g_lua_state) { lua_close(g_lua_state); g_lua_state = NULL; }

	pref_t *prefs = calloc(1, sizeof(pref_t));
	if (prefs == NULL) {
		fprintf(stderr, "fatal error: failed to calloc prefs structure\n");
		exit(1);
	}

	lua_State *L = luaL_newstate();
	if (L == NULL) {
		fprintf(stderr, "fatal error: luaL_newstate failed\n");
		exit(1);
	}
	luaL_openlibs(L);
	/* Register `term` before running the config so it is usable at top
	 * level and by functions the config defines for keybindings. */
	luaL_newlib(L, TERM_LUA_LIB);
	lua_setglobal(L, "term");

	if (luaL_dofile(L, path) != LUA_OK) {
		/* missing/erroring config -> every global absent -> all defaults */
		fprintf(stderr, "term49: error loading %s: %s\n",
		        path, lua_tostring(L, -1));
		lua_pop(L, 1);
	}

	prefs->prefs_version = PREFS_VERSION;
	prefs_build_from_lua(L, prefs);

	g_lua_state = L; /* retained for scripting; closed in prefs_lua_destroy */
	return prefs;
}

/* .term49.lua is hand-authored; never silently rewritten. */
static void prefs_lua_save(const pref_t *prefs, const char *path) {
	(void)prefs;
	(void)path;
}

static void prefs_lua_destroy(pref_t *pref) {
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
		fprintf(stderr, "term49: lua error in '%s': %s\n",
		        name, lua_tostring(L, -1));
		lua_pop(L, 1);
		return 0;
	}
	return 1;
}

static const prefs_loader_t LUA_LOADER = {
	prefs_lua_load,
	prefs_lua_save,
	prefs_lua_destroy,
};

const prefs_loader_t *prefs_lua_loader(void) {
	return &LUA_LOADER;
}

/* --- pref_t -> .term49.lua emitter (first-run default config) --------
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
		fputs(" },\n", f);
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

void prefs_emit_lua(const pref_t *prefs, const char *path) {
	FILE *f = fopen(path, "w");
	if (f == NULL) {
		fprintf(stderr, "term49: cannot write %s: %s\n", path, strerror(errno));
		return;
	}

	fputs("-- Term49 configuration (Lua). Generated automatically; safe to\n"
	      "-- edit. This file is never rewritten by Term49.\n\n", f);
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

	lua_emit_keymap(f, "metamode_keys", prefs->metamode_keys);
	lua_emit_keymap(f, "metamode_sticky_keys", prefs->metamode_sticky_keys);
	lua_emit_keymap(f, "metamode_func_keys", prefs->metamode_func_keys);
	lua_emit_symmenu(f, "main_symmenu", prefs->main_symmenu);

	fputs("-- Scripting example (uncomment to use). A key bound to\n"
	      "-- \"lua:<name>\" in metamode_func_keys calls the matching Lua\n"
	      "-- function via lua_pcall. term.font_size_get/set and\n"
	      "-- term.action(\"<builtin>\") (e.g. \"reload_config\") are available.\n"
	      "--\n"
	      "-- function zoom_in() term.font_size_set(term.font_size_get() + 3) end\n"
	      "-- then add  { \"z\", \"lua:zoom_in\" }  to metamode_func_keys above.\n", f);

	fclose(f);
}
