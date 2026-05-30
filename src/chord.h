/*
 * Modifier-aware key chords and their matching.
 *
 * Split out from types.h/prefs.h so the chord type and its (pure) matching
 * functions carry no ICU/QNX/Lua dependencies and can be unit-tested on the
 * host. action.h is the heaviest thing pulled in, and it is itself vendor-free.
 */

#ifndef CHORD_H_
#define CHORD_H_

#include "action.h"

/* A modifier-aware chord: a trigger keycode plus a modifier mask dispatch to an
 * action. Distinct from keymap_t because the trigger is a full keycode (not a
 * char -- it may be KEYCODE_BB_SYM_KEY/ALT_KEY), the match is mod-aware, and the
 * action is primary (no `to` write string). `spec` owns the parsed action source
 * string that `action` points into (mirrors keymap_set_to); `label` is display
 * text for the help overlay. Arrays are terminated by a keycode==0 sentinel. */
typedef struct chord {
	int keycode;
	unsigned mods;
	char *spec;
	char *label;
	action_t action;
} chord_t;

/* Find the chord whose trigger keycode and modifier mask match exactly.
 * Returns NULL if none. `chord_head` is a keycode==0-terminated array. */
chord_t* chord_lookup(int keycode, unsigned mods, chord_t *chord_head);

/* Site-A variant: match when the chord's (nonzero) mods are a SUBSET of the
 * stuck prefix, so a latched output modifier (e.g. Ctrl) carried in the prefix
 * doesn't block re-triggering the chord that produced it. Most-specific match
 * wins (most mod bits); first-in-array breaks ties. */
chord_t* chord_lookup_subset(int keycode, unsigned prefix, chord_t *chord_head);

#endif /* CHORD_H_ */
