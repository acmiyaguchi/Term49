/*
 * Pure chord-matching logic. Kept free of ICU/QNX/Lua so it builds and runs on
 * the host for unit tests (tests/test_chord_match.c). The dispatch wiring that
 * uses these lives in main.c (try_chord); the chord tables are built in
 * preferences.c.
 */

#include <stddef.h>

#include "chord.h"

chord_t* chord_lookup(int keycode, unsigned mods, chord_t *chord_head) {
	if (chord_head == NULL) {
		return NULL;
	}
	while (chord_head->keycode != 0) {
		if (chord_head->keycode == keycode && chord_head->mods == mods) {
			return chord_head;
		}
		++chord_head;
	}
	return NULL;
}

/* Site-A variant: a chord matches when its (nonzero) mods are a SUBSET of the
 * stuck prefix, so a latched output modifier (e.g. Ctrl) carried in the prefix
 * doesn't break re-triggering the chord that produced it. Most-specific match
 * wins (most mod bits set); first-in-array breaks ties. The caller clears only
 * the matched chord's own mods, leaving extra prefix bits live -- which is what
 * lets `shift+alt` toggle the latched Ctrl back off symmetrically. */
chord_t* chord_lookup_subset(int keycode, unsigned prefix, chord_t *chord_head) {
	chord_t *best = NULL;
	int best_bits = -1;
	if (chord_head == NULL) {
		return NULL;
	}
	for (; chord_head->keycode != 0; ++chord_head) {
		if (chord_head->keycode != keycode || chord_head->mods == 0) {
			continue;
		}
		if ((prefix & chord_head->mods) == chord_head->mods) {
			int bits = __builtin_popcount(chord_head->mods);
			if (bits > best_bits) {
				best = chord_head;
				best_bits = bits;
			}
		}
	}
	return best;
}
