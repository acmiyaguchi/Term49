#ifndef TYPES_H_
#define TYPES_H_

#include <unicode/utf.h>

#include "action.h"
#include "term_types.h"
#include "chord.h"

typedef struct keymap {
	char from;
	char *to;
	action_t action;
	int sticky;          /* metamode: keep metamode armed after firing (else exit) */
} keymap_t;

/* chord_t lives in chord.h (included above) so the pure chord-matching
 * functions stay free of this header's ICU dependency and host-testable. */

typedef rect_t hitbox_t;

typedef struct symkey {
	char flash;
	keymap_t *map; /* pointer to corresponding map */
	hitbox_t hitbox; /* used for mousedown */
	UChar *uc;
} symkey_t;

typedef struct symmenu {
	/* row terminated by NULL pointer, col by NULL symkey_t->map */
	symkey_t **keys; 
	keymap_t *entries; /* terminated by NULL keymap_t.to */
} symmenu_t;

typedef struct pref {
	char *font_path;
	int font_size, *text_color, *background_color, screen_idle_awake,
		auto_show_vkb, metamode_doubletap_key, metamode_doubletap_delay,
		keyhold_actions, metamode_hold_key, allow_resize_columns;
	hitbox_t *metamode_hitbox;
	char *tty_encoding;
	
	/* terminated by NULL pointer; each entry's `sticky` flag decides whether
	 * a hit keeps metamode armed (former metamode_sticky_keys) or exits it
	 * (former metamode_keys / metamode_func_keys). */
	keymap_t *metamode_keys;
	
	symmenu_t *main_symmenu;
	symmenu_t *accent_menus[26][2];
	keymap_t *altsym_entries;

	/* modifier-aware chord bindings; terminated by keycode==0 */
	chord_t *chord_bindings;

	int sticky_sym_key, sticky_shift_key, sticky_alt_key;
	int *keyhold_actions_exempt; /* terminated by -1 */
	int rescreen_for_symmenu, keyhold_accents, prefs_version;
	int show_help_on_startup;
} pref_t;

#endif /* TYPES_H_ */
