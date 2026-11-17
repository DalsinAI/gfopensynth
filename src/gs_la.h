/* One partial of our own MT-32 (LA) engine, of the synthesiser kind: a
 * sawtooth or pulse wave, a resonant low-pass filter and an amplifier, each
 * with its envelope, and a pitch LFO. Step 2 of docs/MT32-ENGINE.md.
 * Whole-number arithmetic only. It takes its settings from a timbre's
 * partial (gs_mt32mem.h) and needs no ROM.
 *
 * What the numbers mean is ours for now: the MT-32's manual gives each
 * setting's range, not its curve, so times, depths and the filter's scale
 * are first fits (gs_la.c says which) to be tuned by ear against a real
 * unit in step 6. Not yet read: the envelopes' time key follow and
 * velocity follow, the filter's bias and its depth key follow, and the
 * pulse width's velocity sense.
 * MIT, Copyright (c) 2026 AmiCradle Team. */
#ifndef GS_LA_H
#define GS_LA_H

#include <stdint.h>
#include "gs_mt32mem.h"

#define GS_LA_BLOCK 32          /* samples between envelope, pitch and filter updates */

struct gs_la_env {
	int32_t level;              /* Q20: 0 to 100 << 20 */
	int32_t step;               /* per block */
	uint32_t blocks;            /* left in this stage */
	uint8_t stage;              /* 0..n-1 the way up to sustain, n holding, n+1 releasing, n+2 done */
	uint8_t n;
	uint8_t time[6];            /* [0..n-1] the stages, [n] the release */
	uint8_t to[6];              /* their target levels, [n] the release's */
};

struct gs_la_partial {
	struct gs_mt32_partial tp;
	uint32_t rate;
	uint8_t key, vel, active, released, no_sustain, mod;
	int32_t bend;               /* cents, from the part's bender */
	uint32_t phase, inc, width;
	uint32_t lfo_phase, lfo_inc;
	int32_t ic1, ic2;           /* the filter's two states */
	int32_t a1, a2, a3;         /* its coefficients, Q16 */
	int32_t gain, gain_step;    /* Q15 << 8, ramped across a block */
	int32_t base_cents, static_cb;
	uint32_t left;              /* samples left in this block */
	struct gs_la_env tva, tvf, pit;
};

/* starts a note: key and velocity 1-127; no_sustain is the timbre's env mode */
void gs_la_partial_start(struct gs_la_partial *p, const struct gs_mt32_partial *tp, int key, int vel, int no_sustain, uint32_t rate);
/* key up: every envelope goes to its release */
void gs_la_partial_release(struct gs_la_partial *p);
/* the part's bender in cents and modulation wheel 0-127 */
void gs_la_partial_control(struct gs_la_partial *p, int32_t bend_cents, int mod);
/* adds frames samples (16-bit scale) to out; 0 once the partial has ended */
int gs_la_partial_render(struct gs_la_partial *p, int32_t *out, uint32_t frames);

/* the stage time of an envelope setting 0-100, in milliseconds << 8 */
uint32_t gs_la_time_ms_q8(int t);

#endif
