/*
 * Host unit tests for the pure chord-matching logic (src/chord_match.c).
 *
 * Build + run: `make test` from the project root (uses the host compiler, not
 * qcc -- chord_match.c is deliberately ICU/QNX-free so this runs anywhere).
 *
 * The scenarios mirror the BlackBerry Q10 modifier state machine, where Ctrl is
 * not a physical key but the `shift+alt` chord toggling a sticky virtual Ctrl
 * (see main.c try_chord / preferences.c DEFAULT_CHORD_BINDINGS). The regression
 * these guard: once Ctrl is latched, pressing shift+alt again carries CTRL in
 * the prefix; exact matching then fails to re-find the SHIFT chord, so Ctrl can
 * never be toggled back off. Subset matching fixes that.
 */

#include <stdio.h>
#include <stddef.h>

#include "chord.h"

/* Mirror of the real constants (sys/keycodes.h, terminal.h) -- the host has no
 * QNX headers, and chord matching is generic over the mask bits regardless. */
#define KEYMOD_SHIFT        (1u << 0)
#define KEYMOD_CTRL         (1u << 1)
#define KEYMOD_ALT          (1u << 2)
#define KEYCODE_BB_ALT_KEY  0xF0E9
#define KEYCODE_BB_SYM_KEY  0xF0D3

static int failures = 0;
static int checks = 0;

#define CHECK(cond, msg) do {                                       \
	++checks;                                                        \
	if (!(cond)) {                                                   \
		++failures;                                                  \
		printf("FAIL: %s (%s:%d)\n", (msg), __FILE__, __LINE__);     \
	}                                                                \
} while (0)

/* The shipped Q10 Site-A defaults: shift+alt = Ctrl, shift+sym = Meta. */
static chord_t default_chords[] = {
	{ KEYCODE_BB_ALT_KEY, KEYMOD_SHIFT, NULL, NULL, {0} },
	{ KEYCODE_BB_SYM_KEY, KEYMOD_SHIFT, NULL, NULL, {0} },
	{ 0, 0, NULL, NULL, {0} },   /* sentinel */
};

int main(void) {
	chord_t *m;

	/* SET path (clean prefix): shift stuck, press alt -> prefix is SHIFT only.
	 * Subset and exact agree and both find the ctrl chord. */
	m = chord_lookup_subset(KEYCODE_BB_ALT_KEY, KEYMOD_SHIFT, default_chords);
	CHECK(m == &default_chords[0], "subset finds shift+alt chord on clean prefix");
	m = chord_lookup(KEYCODE_BB_ALT_KEY, KEYMOD_SHIFT, default_chords);
	CHECK(m == &default_chords[0], "exact finds shift+alt chord on clean prefix");

	/* UNSET path (the regression): Ctrl already latched, shift stuck, press alt
	 * -> prefix is CTRL|SHIFT. Exact fails (this is the original bug); subset
	 * still finds the SHIFT chord so ctrl_down can toggle Ctrl back off. */
	m = chord_lookup(KEYCODE_BB_ALT_KEY, KEYMOD_CTRL | KEYMOD_SHIFT, default_chords);
	CHECK(m == NULL, "exact does NOT match when an extra latched mod pollutes prefix");
	m = chord_lookup_subset(KEYCODE_BB_ALT_KEY, KEYMOD_CTRL | KEYMOD_SHIFT, default_chords);
	CHECK(m == &default_chords[0], "subset re-finds shift+alt chord despite latched Ctrl");

	/* Subset requires the chord's mods to be PRESENT: alt alone (no shift) must
	 * not fire the shift+alt chord. */
	m = chord_lookup_subset(KEYCODE_BB_ALT_KEY, KEYMOD_ALT, default_chords);
	CHECK(m == NULL, "subset needs the chord's own mods present (alt-only misses)");

	/* Wrong trigger keycode never matches. */
	m = chord_lookup_subset(KEYCODE_BB_SYM_KEY, KEYMOD_CTRL | KEYMOD_SHIFT, default_chords);
	CHECK(m == &default_chords[1], "subset matches shift+sym chord on its own keycode");
	m = chord_lookup_subset(0xBEEF, KEYMOD_SHIFT, default_chords);
	CHECK(m == NULL, "subset misses on an unrelated keycode");

	/* Most-specific wins: with both a SHIFT and a SHIFT|CTRL chord on one
	 * keycode, a CTRL|SHIFT prefix selects the 2-bit chord, not the 1-bit one.
	 * First-in-array order is deliberately the less-specific one to prove the
	 * popcount tie-break (not array order) decides. */
	chord_t overlap[] = {
		{ KEYCODE_BB_ALT_KEY, KEYMOD_SHIFT,               NULL, NULL, {0} },
		{ KEYCODE_BB_ALT_KEY, KEYMOD_SHIFT | KEYMOD_CTRL, NULL, NULL, {0} },
		{ 0, 0, NULL, NULL, {0} },
	};
	m = chord_lookup_subset(KEYCODE_BB_ALT_KEY, KEYMOD_SHIFT | KEYMOD_CTRL, overlap);
	CHECK(m == &overlap[1], "subset picks the most-specific (most-bits) chord");
	m = chord_lookup_subset(KEYCODE_BB_ALT_KEY, KEYMOD_SHIFT, overlap);
	CHECK(m == &overlap[0], "subset falls back to the 1-bit chord when CTRL absent");

	/* A zero-mod chord must never subset-match a nonzero prefix (guards Site A,
	 * which is only reached when some prefix is stuck). */
	chord_t zero_mod[] = {
		{ KEYCODE_BB_ALT_KEY, 0, NULL, NULL, {0} },
		{ 0, 0, NULL, NULL, {0} },
	};
	m = chord_lookup_subset(KEYCODE_BB_ALT_KEY, KEYMOD_SHIFT, zero_mod);
	CHECK(m == NULL, "subset never matches a zero-mod chord against a stuck prefix");

	/* NULL head is tolerated by both. */
	CHECK(chord_lookup(KEYCODE_BB_ALT_KEY, KEYMOD_SHIFT, NULL) == NULL, "exact tolerates NULL head");
	CHECK(chord_lookup_subset(KEYCODE_BB_ALT_KEY, KEYMOD_SHIFT, NULL) == NULL, "subset tolerates NULL head");

	printf("%d checks, %d failures\n", checks, failures);
	return failures == 0 ? 0 : 1;
}
