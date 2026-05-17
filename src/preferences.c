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

#include <libconfig.h>

#include "terminal.h"
#include "accent_menus.h"
#include "action.h"
#include "symmenu.h"
#include "prefs.h"

#define PREFS_FILE_BACKUP ".term49rc-old"
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
#define DEFAULT_METAMODE_FUNC_KEYS_LEN 4
#define DEFAULT_METAMODE_FUNC_KEYS (keymap_t[]){{'a', "alt_down"}, \
                                                {'c', "ctrl_down"}, \
                                                {'s', "rescreen"}, \
                                                {'v', "paste_clipboard"}}
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


static void first_run(pref_t *prefs) {
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

	save_preferences(prefs, PREFS_FILE_PATH);
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

static void upgrade_config_v8(config_t *dst, config_t *src) {
	config_setting_t *dst_root = config_root_setting(dst);

	/* upgrade metamode hitbox */

	config_setting_t *old_hitbox_s = config_lookup(src, "metamode_hitbox");
	if (old_hitbox_s != NULL) {
		config_setting_t *x_s = config_setting_get_member(old_hitbox_s, "x");
		config_setting_t *y_s = config_setting_get_member(old_hitbox_s, "y");
		config_setting_t *w_s = config_setting_get_member(old_hitbox_s, "w");
		config_setting_t *h_s = config_setting_get_member(old_hitbox_s, "h");

		if (x_s && y_s && w_s && h_s) {
			int x = config_setting_get_int(x_s);
			int y = config_setting_get_int(y_s);
			int w = config_setting_get_int(w_s);
			int h = config_setting_get_int(h_s);

			config_setting_remove(dst_root, "metamode_hitbox");
			config_setting_t *new_hitbox_s = config_setting_add(dst_root, "metamode_hitbox", CONFIG_TYPE_ARRAY);
			config_setting_set_int_elem(new_hitbox_s, -1, x);
			config_setting_set_int_elem(new_hitbox_s, -1, y);
			config_setting_set_int_elem(new_hitbox_s, -1, w);
			config_setting_set_int_elem(new_hitbox_s, -1, h);
		}
	}

	/* upgrade various keymaps */
	char const *keymap_keys[] = {
		"metamode_keys",
		"metamode_sticky_keys",
		"metamode_func_keys",
		NULL
	};

	for (char const **key_ptr = keymap_keys; *key_ptr != NULL; ++key_ptr) {
		char const *key = *key_ptr;
		config_setting_t *old_map_s = config_lookup(src, key);
		if (old_map_s == NULL) {
			continue;
		}

		config_setting_remove(dst_root, key);
		config_setting_t *new_map_s = config_setting_add(dst_root, key, CONFIG_TYPE_LIST);
		
		for (size_t i = 0; i < config_setting_length(old_map_s); ++i) {
			config_setting_t *old_key_s = config_setting_get_elem(old_map_s, i);
			
			char const *from = config_setting_name(old_key_s);
			char const *to = config_setting_get_string(old_key_s);
			
			config_setting_t *new_key_s = config_setting_add(new_map_s, NULL, CONFIG_TYPE_LIST);
			config_setting_set_string_elem(new_key_s, -1, from);
			config_setting_set_string_elem(new_key_s, -1, to);
		}
	}

	/* upgrade symmenu */
	config_setting_t *old_map_s = config_lookup(src, "sym_keys");
	if (old_map_s == NULL) {
		return;
	}
	
	config_setting_t *new_map_s = config_setting_add(dst_root, "main_symmenu", CONFIG_TYPE_LIST);

	for (int row = config_setting_length(old_map_s) - 1; row >= 0; --row) {
		config_setting_t *old_row_s = config_setting_get_elem(old_map_s, row);
		config_setting_t *new_row_s = config_setting_add(new_map_s, NULL, CONFIG_TYPE_LIST);
			
		for (size_t col = 0; col < config_setting_length(old_row_s); ++col) {
			config_setting_t *old_key_s = config_setting_get_elem(old_row_s, col);
			
			char const *from = config_setting_name(old_key_s);
			char const *to = config_setting_get_string(old_key_s);
			
			config_setting_t *new_key_s = config_setting_add(new_row_s, NULL, CONFIG_TYPE_LIST);
			config_setting_set_string_elem(new_key_s, -1, from);
			config_setting_set_string_elem(new_key_s, -1, to);
		}
	}
}

typedef void (*prefs_upgrade_fn)(config_t *dst, config_t *src);

/* Explicit, table-driven version dispatch. Each row upgrades a config AT
 * from_version TO to_version; adding a future migration is one row plus
 * one function. NOTE: this applies a single step (the on-disk file is
 * the only input). True multi-step chaining (e.g. v7 -> v8 -> v9) needs
 * an intermediate-config deep copy libconfig does not provide; deferred
 * until a second step actually exists, which is why the table is kept
 * simple rather than abstracted now. */
static const struct {
	int from_version;
	int to_version;
	prefs_upgrade_fn apply;
} PREFS_UPGRADES[] = {
	{ 8, 9, upgrade_config_v8 },
};

static config_t *upgrade_config(char const *path, int old_version) {
	config_t src_data;
	config_t *src = &src_data;
	config_t *dst = (config_t*)malloc(sizeof(config_t));

	config_init(src);
	config_init(dst);
	config_read_file(src, path);
	config_read_file(dst, path);

	int handled = 0;
	for (size_t i = 0; i < sizeof(PREFS_UPGRADES) / sizeof(PREFS_UPGRADES[0]); ++i) {
		if (PREFS_UPGRADES[i].from_version == old_version) {
			fprintf(stderr, "Upgrading prefs v%d -> v%d. Old prefs in %s\n",
			        PREFS_UPGRADES[i].from_version, PREFS_UPGRADES[i].to_version,
			        PREFS_FILE_BACKUP);
			PREFS_UPGRADES[i].apply(dst, src);
			handled = 1;
			break;
		}
	}
	if (!handled) {
		fprintf(stderr, "Preferences version %d not supported!\n", old_version);
	}

	config_destroy(src);
	return dst;
}

/* --- libconfig structural validation ---------------------------------
 * The loaders below index into config aggregates. A malformed .term49rc
 * (wrong element types, scalars where lists are expected, short arrays)
 * must fall back to defaults instead of dereferencing NULL. These
 * helpers are NULL-tolerant: config_setting_type/is_* are macros that
 * deref (S)->type and config_setting_get_elem() derefs its argument, so
 * every setting pointer is checked before use. They are only invoked
 * once the caller has confirmed the top-level setting is the right
 * aggregate type, so config_setting_length() here is always safe. */

static int keymap_elem_valid(config_setting_t *m) {
	/* a keymap entry is an aggregate whose elements 0 and 1 are strings;
	 * get_string_elem() is type/range-safe once m itself is non-NULL */
	return m != NULL
		&& config_setting_get_string_elem(m, 0) != NULL
		&& config_setting_get_string_elem(m, 1) != NULL;
}

static int keymap_list_valid(config_setting_t *setting) {
	for (int i = 0; i < config_setting_length(setting); ++i) {
		if (!keymap_elem_valid(config_setting_get_elem(setting, i))) {
			return 0;
		}
	}
	return 1;
}

static int symmenu_rows_valid(config_setting_t *rows_s) {
	for (int row = 0; row < config_setting_length(rows_s); ++row) {
		config_setting_t *col_s = config_setting_get_elem(rows_s, row);
		if (col_s == NULL || !config_setting_is_aggregate(col_s)) {
			return 0;
		}
		for (int col = 0; col < config_setting_length(col_s); ++col) {
			if (!keymap_elem_valid(config_setting_get_elem(col_s, col))) {
				return 0;
			}
		}
	}
	return 1;
}

static int number_array_valid(config_setting_t *setting, int min_len) {
	if (config_setting_length(setting) < min_len) {
		return 0;
	}
	for (int i = 0; i < config_setting_length(setting); ++i) {
		config_setting_t *e = config_setting_get_elem(setting, i);
		if (e == NULL || !config_setting_is_number(e)) {
			return 0;
		}
	}
	return 1;
}

static int* create_int_array(config_t const *config, char const *path, size_t def_len, int const *def, int dynamic) {
	config_setting_t *setting = config_lookup(config, path);
	int use_default = 0;
	size_t source_len = 0;

	if (!setting || (config_setting_type(setting) != CONFIG_TYPE_ARRAY) || !number_array_valid(setting, 0)) {
		fprintf(stderr, "invalid array %s, using default\n", path);
		source_len = def_len;
		use_default = 1;
	} else {
		source_len = config_setting_length(setting);
	}
	
	int *result = calloc(source_len + 1, sizeof(int));
	result[source_len] = -1;  // sentinel for end of array, only useful for positive arrays

	if (use_default) {
		for (int i = 0; i < source_len; i++) {
			result[i] = def[i];
		}
	} else {
		for (int i = 0; i < source_len; i++) {
			result[i] = config_setting_get_int_elem(setting, i);
		}
	}

	return result;
}

static void keymap_set_to(keymap_t *entry, const char *to) {
	entry->to = strdup(to);
	if (!action_parse(entry->to, &entry->action)) {
		entry->action = (action_t){0};
	}
}

static keymap_t* create_keymap_array(config_t const *config, char const *path, size_t def_len, keymap_t const *def) {
	config_setting_t *setting = config_lookup(config, path);
	int use_default = 0;
	size_t source_len = 0;

	if (!setting || (config_setting_type(setting) != CONFIG_TYPE_LIST) || !keymap_list_valid(setting)) {
		fprintf(stderr, "invalid keymap list %s, using default\n", path);
		source_len = def_len;
		use_default = 1;
	} else {
		source_len = config_setting_length(setting);
	}
	
	keymap_t *result = calloc(source_len + 1, sizeof(keymap_t));
	result[source_len] = (keymap_t){0, NULL}; // sentinel for end of array
	
	if (use_default) {
		for (int i = 0; i < source_len; i++) {
			result[i].from = def[i].from;
			keymap_set_to(&result[i], def[i].to);
		}
	} else {
		for (int i = 0; i < source_len; i++) {
			config_setting_t *m = config_setting_get_elem(setting, i);
			char const *from_str = config_setting_get_string_elem(m, 0);
			result[i].from = from_str[0];
			keymap_set_to(&result[i], config_setting_get_string_elem(m, 1));
		}
	}

	return result;
}

static symmenu_t* create_symmenu(config_t const *config, char const *path, int def_num_rows, int const *def_row_lens, keymap_t const *def_entries) {
	config_setting_t *rows_s = config_lookup(config, path);
	int use_default = 0;

	symmenu_t *menu = calloc(1, sizeof(symmenu_t));
	
	if (!rows_s || (config_setting_type(rows_s) != CONFIG_TYPE_LIST) || !symmenu_rows_valid(rows_s)) {
		fprintf(stderr, "invalid symmenu %s, using default\n", path);

		/* calculate the length of the keymap entry array */
		int def_num_keys = 0;
		for (int i = 0; i < def_num_rows; ++i) {
			def_num_keys += def_row_lens[i];
		}
		
		/* allocate the keymap entry and symkey arrays */
		menu->entries = calloc(def_num_keys + 1, sizeof(keymap_t));
		menu->entries[def_num_keys] = (keymap_t){0, NULL}; // sentinel for end of array
		
		menu->keys = calloc(def_num_rows + 1, sizeof(symkey_t*));
		menu->keys[def_num_rows] = NULL;
		
		/* fill in the keymap entry array */
		int entry_idx = 0;
		for (int row = 0; row < def_num_rows; ++row) {
			/* allocate the symkey row */
			menu->keys[row] = calloc(def_row_lens[row] + 1, sizeof(symkey_t));
			menu->keys[row][def_row_lens[row]].map = NULL;
			
			/* fill in the symkey row (rest done during render) */
			for (int col = 0; col < def_row_lens[row]; ++col) {
				menu->entries[entry_idx].from = def_entries[entry_idx].from;
				keymap_set_to(&menu->entries[entry_idx], def_entries[entry_idx].to);
				
				menu->keys[row][col].flash = '\0';
				menu->keys[row][col].map = &menu->entries[entry_idx];
				
				++entry_idx;
			}
		}

	} else {
		/* calculate the length of the keymap entry array */
		int num_keys = 0;
		for (int row = 0; row < config_setting_length(rows_s); ++row) {
			config_setting_t *col_s = config_setting_get_elem(rows_s, row);
			num_keys += config_setting_length(col_s);
		}
	
		/* allocate the keymap entry and symkey arrays */
		menu->entries = calloc(num_keys + 1, sizeof(keymap_t));
		menu->entries[num_keys] = (keymap_t){0, NULL}; // sentinel for end of array
		
		menu->keys = calloc(config_setting_length(rows_s) + 1, sizeof(symkey_t*));
		menu->keys[config_setting_length(rows_s)] = NULL;

		/* fill in the keymap entry array */
		int entry_idx = 0;
		for (int row = 0; row < config_setting_length(rows_s); ++row) {
			config_setting_t *col_s = config_setting_get_elem(rows_s, row);
			int col_len = config_setting_length(col_s);
			
			/* allocate the symkey row */
			menu->keys[row] = calloc(col_len + 1, sizeof(symkey_t));
			menu->keys[row][col_len].map = NULL;
			
			/* fill in the symkey row (rest done during render) */
			for (int col = 0; col < col_len; ++col) {
				config_setting_t *m = config_setting_get_elem(col_s, col);
				char const *from_str = config_setting_get_string_elem(m, 0);
				menu->entries[entry_idx].from = from_str[0];
				keymap_set_to(&menu->entries[entry_idx], config_setting_get_string_elem(m, 1));
			
				menu->keys[row][col].flash = '\0';
				menu->keys[row][col].map = &menu->entries[entry_idx];

				++entry_idx;
			}
		}
	}

	return menu;
}

static hitbox_t* create_hitbox(config_t const *config, char const *path, hitbox_t def) {
	config_setting_t *setting = config_lookup(config, path);
	int use_default = 0;

	if (!setting || (config_setting_type(setting) != CONFIG_TYPE_ARRAY) || !number_array_valid(setting, 4)) {
		fprintf(stderr, "invalid array %s, using default\n", path);
		use_default = 1;
	}
	
	hitbox_t *result = calloc(1, sizeof(hitbox_t));

	if (use_default) {
		result->x = def.x;
		result->y = def.y;
		result->w = def.w;
		result->h = def.h;
	} else {
		result->x = config_setting_get_int_elem(setting, 0);
		result->y = config_setting_get_int_elem(setting, 1);
		result->w = config_setting_get_int_elem(setting, 2);
		result->h = config_setting_get_int_elem(setting, 3);
	}

	return result;
}

void destroy_preferences(pref_t *pref) {
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

	free(pref);
}

/* --- scalar/string preference schema ---------------------------------
 * Single source of truth for every plain int/bool/string preference:
 * key name, type, the pref_t field (by offset), and default. read and
 * save are both driven from this table, so a preference cannot drift
 * between the two sides (which previously caused metamode_hold_key to be
 * saved as a bool while read as an int keycode, and keyhold_accents to
 * be read but never written). Structured prefs (colour/hitbox/keymap/
 * symmenu arrays) keep their dedicated create_ / set_ helpers. */
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
	/* keycode, not a flag: previously saved as bool (a drift bug) -> INT */
	{ "metamode_hold_key",        PS_INT,    offsetof(pref_t, metamode_hold_key),        DEFAULT_METAMODE_HOLD_KEY,      NULL },
	{ "allow_resize_columns",     PS_BOOL,   offsetof(pref_t, allow_resize_columns),     DEFAULT_ALLOW_RESIZE_COLUMNS,   NULL },
	{ "tty_encoding",             PS_STRING, offsetof(pref_t, tty_encoding),             0,                              DEFAULT_TTY_ENCODING },
	{ "sticky_sym_key",           PS_BOOL,   offsetof(pref_t, sticky_sym_key),           DEFAULT_STICKY_SYM_KEY,         NULL },
	{ "sticky_shift_key",         PS_BOOL,   offsetof(pref_t, sticky_shift_key),         DEFAULT_STICKY_SHIFT_KEY,       NULL },
	{ "sticky_alt_key",           PS_BOOL,   offsetof(pref_t, sticky_alt_key),           DEFAULT_STICKY_ALT_KEY,         NULL },
	{ "rescreen_for_symmenu",     PS_BOOL,   offsetof(pref_t, rescreen_for_symmenu),     DEFAULT_RESCREEN_FOR_SYMMENU,   NULL },
	{ "keyhold_accents",          PS_BOOL,   offsetof(pref_t, keyhold_accents),          DEFAULT_KEYHOLD_ACCENTS,        NULL },
};

static void prefs_read_scalars(config_t const *config, pref_t *prefs) {
	for (size_t i = 0; i < sizeof(PREFS_SCALARS) / sizeof(PREFS_SCALARS[0]); ++i) {
		const prefs_scalar_desc *d = &PREFS_SCALARS[i];
		void *field = (char *)prefs + d->offset;
		switch (d->type) {
		case PS_INT:
			if (config_lookup_int(config, d->key, (int *)field) != CONFIG_TRUE) {
				*(int *)field = d->int_default;
			}
			break;
		case PS_BOOL:
			if (config_lookup_bool(config, d->key, (int *)field) != CONFIG_TRUE) {
				*(int *)field = d->int_default;
			}
			break;
		case PS_STRING: {
			const char *s = NULL;
			if (config_lookup_string(config, d->key, &s) != CONFIG_TRUE) {
				s = d->str_default;
			}
			*(char **)field = strdup(s);  /* prefs owns its strings */
			break;
		}
		}
	}
}

static void prefs_save_scalars(config_setting_t *root, pref_t const *prefs) {
	for (size_t i = 0; i < sizeof(PREFS_SCALARS) / sizeof(PREFS_SCALARS[0]); ++i) {
		const prefs_scalar_desc *d = &PREFS_SCALARS[i];
		const void *field = (const char *)prefs + d->offset;
		config_setting_t *s;
		switch (d->type) {
		case PS_INT:
			s = config_setting_add(root, d->key, CONFIG_TYPE_INT);
			config_setting_set_int(s, *(const int *)field);
			break;
		case PS_BOOL:
			s = config_setting_add(root, d->key, CONFIG_TYPE_BOOL);
			config_setting_set_bool(s, *(const int *)field);
			break;
		case PS_STRING:
			s = config_setting_add(root, d->key, CONFIG_TYPE_STRING);
			config_setting_set_string(s, *(char *const *)field);
			break;
		}
	}
}

pref_t *read_preferences(const char* filename) {
	pref_t *prefs = calloc(1, sizeof(pref_t)); // our internal data structure
	if (prefs == NULL) {
		fprintf(stderr, "fatal error: failed to calloc prefs structure\n");
		exit(1);
	}

	int is_first_run = 0; int upgraded = 0;
	
	config_t config_data; // what libconfig parses out of the file
	config_t *config = &config_data;
	config_init(config);
	
	if (access(filename, F_OK) == -1) {
		PRINT(stderr, "Preferences file not found, assuming first run\n");
		is_first_run = 1;
	} else {
		if(config_read_file(config, filename) != CONFIG_TRUE){
			fprintf(stderr, "%s:%d - %s\n", config_error_file(config),
			        config_error_line(config), config_error_text(config));
		}
	}

	if (config_lookup_int(config, "prefs_version", &prefs->prefs_version) != CONFIG_TRUE) {
		prefs->prefs_version = PREFS_VERSION;
	}
	if(prefs->prefs_version != PREFS_VERSION) {
		config = upgrade_config(filename, prefs->prefs_version);
		upgraded = 1;
	}
	
	int default_font_columns = (atoi(getenv("WIDTH")) <= 720) ? 45 : 60;

	prefs_read_scalars(config, prefs);

	prefs->text_color = create_int_array(config, "text_color", PREFS_COLOR_NUM_ELEMENTS, DEFAULT_TEXT_COLOR, 0);
	prefs->background_color = create_int_array(config, "background_color", PREFS_COLOR_NUM_ELEMENTS, DEFAULT_BACKGROUND_COLOR, 0);
	prefs->metamode_hitbox = create_hitbox(config, "metamode_hitbox", DEFAULT_METAMODE_HITBOX);
	prefs->metamode_keys = create_keymap_array(config, "metamode_keys", DEFAULT_METAMODE_KEYS_LEN, DEFAULT_METAMODE_KEYS);
	prefs->metamode_sticky_keys = create_keymap_array(config, "metamode_sticky_keys", DEFAULT_METAMODE_STICKY_KEYS_LEN, DEFAULT_METAMODE_STICKY_KEYS);
	prefs->metamode_func_keys = create_keymap_array(config, "metamode_func_keys", DEFAULT_METAMODE_FUNC_KEYS_LEN, DEFAULT_METAMODE_FUNC_KEYS);
	prefs->keyhold_actions_exempt = create_int_array(config, "keyhold_actions_exempt", DEFAULT_KEYHOLD_ACTIONS_EXEMPT_LEN, DEFAULT_KEYHOLD_ACTIONS_EXEMPT, 1);

	prefs->main_symmenu = create_symmenu(config, "main_symmenu", DEFAULT_SYMMENU_NUM_ROWS, DEFAULT_SYMMENU_ROW_LENS, DEFAULT_SYMMENU_ENTRIES);
	prefs->altsym_entries = create_keymap_array(config, "altsym_entries", DEFAULT_ALTSYM_ENTRIES_LEN, DEFAULT_ALTSYM_ENTRIES);

	/* the accent menus are configurable, but we won't include them in the default config */
	char am_name[] = {' ', '_', 'a', 'c', 'c', 'e', 'n', 't', 's', '\0'};
	for (char c = 'a'; c <= 'z'; ++c) {
		size_t idx = (size_t)(c - 'a');
		am_name[0] = c;
		prefs->accent_menus[idx][0] = create_symmenu(config, am_name, 1, &accent_row_lens[idx], lowercase_accent_entries[idx]);
		prefs->accent_menus[idx][1] = create_symmenu(config, am_name, 1, &accent_row_lens[idx], uppercase_accent_entries[idx]);
	}

	if (is_first_run) {
		first_run(prefs);
	}

	if (upgraded) {
		if (rename(PREFS_FILE_PATH, PREFS_FILE_BACKUP)) {
			fprintf(stderr, "Failed to back up old prefs! Won't overwrite %s\n", PREFS_FILE_PATH);
		} else {
			save_preferences(prefs, PREFS_FILE_PATH);
		}
	}

	return prefs;
}

void set_int_array(config_setting_t *root, char const *key, size_t num_elems, int const *source) {
	config_setting_t *setting = config_setting_add(root, key, CONFIG_TYPE_ARRAY);
	for (size_t i = 0; i < num_elems; ++i) {
		config_setting_t *elem = config_setting_add(setting, NULL, CONFIG_TYPE_INT);
		config_setting_set_int(elem, source[i]);
	}
}

void set_keymap_array(config_setting_t *root, char const *key, keymap_t const *source) {
	config_setting_t *setting = config_setting_add(root, key, CONFIG_TYPE_LIST);
	for (; source->to != NULL; ++source) {
		config_setting_t *group = config_setting_add(setting, NULL, CONFIG_TYPE_LIST);
		
		config_setting_t *from_s = config_setting_add(group, NULL, CONFIG_TYPE_STRING);
		char from_str[2] = {source->from, '\0'};
		config_setting_set_string(from_s, from_str);

		config_setting_t *to_s = config_setting_add(group, NULL, CONFIG_TYPE_STRING);
		config_setting_set_string(to_s, source->to);
	}
}

void set_symmenu(config_setting_t *root, char const *key, symmenu_t const *source) {
	config_setting_t *rows_s = config_setting_add(root, key, CONFIG_TYPE_LIST);
	config_setting_t *col_s = config_setting_add(rows_s, NULL, CONFIG_TYPE_LIST);

	int row = 0; int col = 0;
	while (1) {
		if (source->keys[row][col].map == NULL) {
			++row;
			if (source->keys[row] == NULL) {
				return;
			}
			col = 0;
			col_s = config_setting_add(rows_s, NULL, CONFIG_TYPE_LIST);
			continue;
		}
		
		config_setting_t *group = config_setting_add(col_s, NULL, CONFIG_TYPE_LIST);
		
		config_setting_t *from_s = config_setting_add(group, NULL, CONFIG_TYPE_STRING);
		char from_str[2] = {source->keys[row][col].map->from, '\0'};
		config_setting_set_string(from_s, from_str);

		config_setting_t *to_s = config_setting_add(group, NULL, CONFIG_TYPE_STRING);
		config_setting_set_string(to_s, source->keys[row][col].map->to);

		++col;
	}
}

void save_preferences(pref_t const* prefs, char const* filename) {
	config_t config;
	config_init(&config);
	config_setting_t *root = config_root_setting(&config);

	/* Stamp the file with the schema version being written so a later
	 * Term49 can detect and upgrade it. Previously omitted -- upgrade
	 * detection only worked because the read-side default happened to be
	 * the current version. */
	config_setting_t *ver_s = config_setting_add(root, "prefs_version", CONFIG_TYPE_INT);
	config_setting_set_int(ver_s, PREFS_VERSION);

	prefs_save_scalars(root, prefs);

	set_int_array(root, "text_color", PREFS_COLOR_NUM_ELEMENTS, prefs->text_color);
	set_int_array(root, "background_color", PREFS_COLOR_NUM_ELEMENTS, prefs->background_color);
	set_int_array(root, "metamode_hitbox", 4, prefs->metamode_hitbox);
	set_keymap_array(root, "metamode_keys", prefs->metamode_keys);
	set_keymap_array(root, "metamode_sticky_keys", prefs->metamode_sticky_keys);
	set_keymap_array(root, "metamode_func_keys", prefs->metamode_func_keys);
	set_symmenu(root, "main_symmenu", prefs->main_symmenu);

	int num_exempt = 0;
	for (; prefs->keyhold_actions_exempt[num_exempt] > 0; ++num_exempt) { }
	set_int_array(root, "keyhold_actions_exempt", num_exempt, prefs->keyhold_actions_exempt);

	config_write_file(&config, filename);
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

/* The .term49rc/libconfig loader behind the prefs_loader_t seam.
 *
 * #7 Phase 3 decision -- KEEP libconfig, fully encapsulated. libconfig
 * stays as the legacy .term49rc reader/writer; it is already private to
 * this translation unit, so a future parser swap or a Lua loader is a
 * sibling behind prefs_loader_t, NOT a rewrite. We deliberately do NOT
 * replace it with a custom format: that risks the ".term49rc still
 * loads" guarantee for no near-term gain. Per #9 staging, an alternate
 * Lua loader populating the SAME pref_t is post-#6 follow-up work.
 *
 * #7 Phase 2 note -- no separate parser-neutral IR is introduced
 * because pref_t (plain data) + prefs_loader_t (this vtable) already ARE
 * that representation. The scalar schema table above is the single
 * source of truth a Lua loader would target. */
static const prefs_loader_t LIBCONFIG_LOADER = {
	read_preferences,
	save_preferences,
	destroy_preferences,
};

const prefs_loader_t *prefs_libconfig_loader(void) {
	return &LIBCONFIG_LOADER;
}
