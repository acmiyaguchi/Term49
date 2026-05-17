#include <stdlib.h>

#include "symmenu.h"

void destroy_symmenu(symmenu_t *menu) {
	if (menu == NULL) {
		return;
	}

	/* keys: NULL-pointer-terminated array of rows. Each row is a single
	 * calloc block terminated by a .map == NULL sentinel, so it is freed
	 * once via free(*row) -- never per element (interior pointer). Per-key
	 * .uc is lazily calloc'd by the renderer (NULL until then; free(NULL)
	 * is safe). .map points INTO menu->entries and is not owned here. */
	if (menu->keys != NULL) {
		for (symkey_t **row = menu->keys; *row != NULL; ++row) {
			for (symkey_t *key = *row; key->map != NULL; ++key) {
				free(key->uc);
			}
			free(*row);
		}
		free(menu->keys);
	}

	/* entries: single calloc block terminated by a .to == NULL sentinel.
	 * Each .to is strdup'd by keymap_set_to (same ownership as the
	 * metamode keymaps freed in destroy_preferences). */
	if (menu->entries != NULL) {
		for (keymap_t *entry = menu->entries; entry->to != NULL; ++entry) {
			free(entry->to);
		}
		free(menu->entries);
	}

	free(menu);
}
