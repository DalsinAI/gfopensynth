/* One synthesiser partial of our own MT-32 engine (gs_la.h).
 * MIT, Copyright (c) 2026 AmiCradle Team. */
#include <string.h>
#include "gs_la.h"
#include "gs_tables.h"

#define GSL_FULL (100L << 20)

/* key follow, in eighths: -1, -1/2, -1/4, 0, 1/8 ... 7/8, 1, 5/4, 3/2, 2, then the two stretched tunings (our fit) */
static const int16_t gsl_keyfollow_q13[17] = {
	-8192, -4096, -2048, 0, 1024, 2048, 3072, 4096, 5120, 6144, 7168, 8192, 10240, 12288, 16384, 8256, 8320
};

static int32_t gsl_clamp(int32_t v, int32_t lo, int32_t hi)
{
	return v < lo ? lo : v > hi ? hi : v;
}

/* 2^(c/1200) in Q16 for c >= 0 (up to 2^15) */
static uint32_t gsl_exp2(uint32_t c)
{
	uint32_t oct = c / 1200;
	if (oct > 15) oct = 15;
	return (uint32_t)gs_exp2_q16[c % 1200] << oct;
}

/* the frequency of a pitch in cents above MIDI note 0, Hz in Q16 */
static uint32_t gsl_freq_q16(int32_t cents)
{
	uint32_t c = (uint32_t)gsl_clamp(cents, 0, 13199);
	/* note 0 is 8.1758 Hz: 535809 in Q16 */
	return (uint32_t)(((uint64_t)535809u * gsl_exp2(c)) >> 16);
}

/* Our fit: a stage's time doubles every 7 steps, 1 ms at 0 to about 20 s at 100. */
uint32_t gs_la_time_ms_q8(int t)
{
	return gsl_exp2((uint32_t)gsl_clamp(t, 0, 100) * 1200u / 7u) >> 8;
}

static void gsl_env_stage(struct gs_la_env *e, uint32_t rate)
{
	uint64_t b;
	int32_t target;
	uint8_t i = e->stage <= e->n ? e->stage : e->n;      /* [n] is the release */
	if (e->stage == e->n + 1) i = e->n;
	target = (int32_t)e->to[i] << 20;
	b = ((uint64_t)gs_la_time_ms_q8(e->time[i]) * rate) / (256u * 1000u * GS_LA_BLOCK);
	e->blocks = b ? (uint32_t)b : 1;
	e->step = (target - e->level) / (int32_t)e->blocks;
}

static void gsl_env_start(struct gs_la_env *e, int start, uint32_t rate)
{
	e->level = (int32_t)start << 20;
	e->stage = 0;
	gsl_env_stage(e, rate);
}

static void gsl_env_release(struct gs_la_env *e, uint32_t rate)
{
	if (e->stage > e->n)
		return;
	e->stage = (uint8_t)(e->n + 1);
	gsl_env_stage(e, rate);
}

/* one block on; hold: stay at the sustain level until released */
static void gsl_env_block(struct gs_la_env *e, int hold, uint32_t rate)
{
	if (e->stage == e->n) {                      /* holding */
		if (hold)
			return;
		gsl_env_release(e, rate);
	}
	if (e->stage > e->n + 1)
		return;
	e->level += e->step;
	if (--e->blocks == 0) {
		uint8_t i = e->stage == e->n + 1 ? e->n : e->stage;
		e->level = (int32_t)e->to[i] << 20;
		if (e->stage == e->n + 1)
			e->stage = (uint8_t)(e->n + 2);      /* done */
		else if (++e->stage < e->n)
			gsl_env_stage(e, rate);
	}
}

void gs_la_partial_control(struct gs_la_partial *p, int32_t bend_cents, int mod)
{
	p->bend = p->tp.bender ? bend_cents : 0;
	p->mod = (uint8_t)gsl_clamp(mod, 0, 127);
}

void gs_la_partial_start(struct gs_la_partial *p, const struct gs_mt32_partial *tp, int key, int vel, int no_sustain, uint32_t rate)
{
	int i, pt;
	int32_t cb;
	memset(p, 0, sizeof *p);
	p->tp = *tp;
	p->rate = rate ? rate : 22050;
	p->key = (uint8_t)gsl_clamp(key, 0, 127);
	p->vel = (uint8_t)gsl_clamp(vel, 1, 127);
	p->no_sustain = (uint8_t)(no_sustain != 0);
	p->active = 1;
	/* pitch: coarse 36 and fine 50 play the key itself; key follow turns about middle C */
	p->base_cents = 6000 + ((int32_t)(p->key - 60) * 100 * gsl_keyfollow_q13[tp->pitch_keyfollow > 16 ? 11 : tp->pitch_keyfollow]) / 8192
	              + ((int32_t)tp->pitch_coarse - 36) * 100 + ((int32_t)tp->pitch_fine - 50);
	/* waveforms 0 and 2 are the pulse, 1 and 3 the sawtooth; pulse width 0 is a square, 100 our narrowest (3 per cent) */
	p->width = 0x80000000u - (uint32_t)(tp->pulse_width > 100 ? 100 : tp->pulse_width) * 20186346u;
	/* the amplifier: level 100 is full, each step under it 0.48 dB (our fit) */
	cb = (100 - (int32_t)tp->tva_level) * 48 / 10;
	/* velocity: 50 takes none, above it soft notes are quieter, below it loud ones are (to 20 dB, our fit) */
	i = (int)tp->tva_velo - 50;
	cb += (i > 0 ? i * (127 - p->vel) : -i * p->vel) * 4 / 127;
	/* the two bias points: keys past the point lose (12 - level) * 0.2 dB a key (our fit) */
	for (i = 0; i < 2; i++) {
		pt = i ? tp->tva_bias_point2 : tp->tva_bias_point1;
		{
			int note = (pt & 63) + 33, d = pt < 64 ? note - p->key : p->key - note;
			int lv = i ? tp->tva_bias_level2 : tp->tva_bias_level1;
			if (d > 0 && lv < 12)
				cb += d * (12 - lv) * 2;
		}
	}
	p->static_cb = cb;
	/* the envelopes: amplifier and filter rise through three levels to sustain and fall to nothing;
	 * pitch starts at level 0, passes two levels to sustain and ends at its end level */
	p->tva.n = 4; p->tvf.n = 4; p->pit.n = 3;
	for (i = 0; i < 5; i++) { p->tva.time[i] = tp->tva_env_time[i]; p->tvf.time[i] = tp->tvf_env_time[i]; }
	for (i = 0; i < 4; i++) { p->tva.to[i] = tp->tva_env_level[i]; p->tvf.to[i] = tp->tvf_env_level[i]; }
	p->tva.to[4] = 0; p->tvf.to[4] = 0;
	for (i = 0; i < 4; i++) p->pit.time[i] = tp->penv_time[i];
	p->pit.to[0] = tp->penv_level[1]; p->pit.to[1] = tp->penv_level[2]; p->pit.to[2] = tp->penv_level[3]; p->pit.to[3] = tp->penv_level[4];
	gsl_env_start(&p->tva, 0, p->rate);
	gsl_env_start(&p->tvf, 0, p->rate);
	gsl_env_start(&p->pit, tp->penv_level[0], p->rate);
	/* the LFO: 0.1 Hz at rate 0, doubling every 14 steps to 14 Hz (our fit) */
	p->lfo_inc = (uint32_t)((((uint64_t)6554u * gsl_exp2((uint32_t)tp->lfo_rate * 1200u / 14u)) >> 16) * GS_LA_BLOCK * 65536u / p->rate);
}

void gs_la_partial_release(struct gs_la_partial *p)
{
	if (p->released)
		return;
	p->released = 1;
	gsl_env_release(&p->tva, p->rate);
	gsl_env_release(&p->tvf, p->rate);
	gsl_env_release(&p->pit, p->rate);
}

/* the settings for the next block; 0 when the partial has ended */
static int gsl_block(struct gs_la_partial *p)
{
	const struct gs_mt32_partial *tp = &p->tp;
	int32_t cents, lfo, fc, a, k, g, den, cb, gain;
	uint64_t f;
	int hold = !p->no_sustain;

	gsl_env_block(&p->tva, hold, p->rate);
	gsl_env_block(&p->tvf, hold, p->rate);
	gsl_env_block(&p->pit, hold, p->rate);
	if (p->tva.stage > p->tva.n + 1) {
		p->active = 0;
		return 0;
	}
	/* pitch: the envelope swings depth * 240 cents either side of level 50, the LFO its depth in cents (our fits) */
	p->lfo_phase += p->lfo_inc;
	lfo = (int32_t)(p->lfo_phase >> 16);                       /* 0..65535 */
	lfo = lfo < 32768 ? lfo - 16384 : 49152 - lfo;             /* a triangle, -16384..16384 */
	cents = p->base_cents + p->bend
	      + (((p->pit.level >> 10) - 51200) * (int32_t)tp->penv_depth * 240) / 51200
	      + lfo * ((int32_t)tp->lfo_depth + (int32_t)tp->lfo_mod * p->mod / 127) / 16384;
	f = ((uint64_t)gsl_freq_q16(cents) << 16) / p->rate;       /* cycles a sample, Q32 */
	p->inc = f > 0x7FFFFFFFu ? 0x7FFFFFFFu : (uint32_t)f;
	if (!p->inc) p->inc = 1;
	/* the filter: cutoff 0 is C1 and each step a semitone; its envelope opens it depth * 0.48 semitones at
	 * level 100; key follow as the pitch's (our fits). A trapezoidal state-variable low-pass. */
	fc = 100 * (24 + (int32_t)tp->tvf_cutoff)
	   + ((int32_t)(p->key - 60) * 100 * gsl_keyfollow_q13[tp->tvf_keyfollow > 14 ? 11 : tp->tvf_keyfollow]) / 8192
	   + (int32_t)(((int64_t)(p->tvf.level >> 10) * tp->tvf_env_depth * 48) / 102400);
	/* a = pi * fc / rate (Q16), held under 1.45 so the tangent stays finite */
	a = (int32_t)((((uint64_t)gsl_freq_q16(fc) * 205887u) >> 16) / p->rate);
	a = gsl_clamp(a, 64, 95027);
	{
		int64_t a2 = ((int64_t)a * a) >> 16;                   /* tan(a) about a(15 - a*a)/(15 - 6a*a) */
		g = (int32_t)(((int64_t)a * (15 * 65536 - a2)) / (15 * 65536 - 6 * a2));
	}
	/* resonance 0 is none (damping 2), 30 a peak of about 24 dB (damping 1/8) (our fit) */
	k = (int32_t)(((uint64_t)131072u << 16) / gsl_exp2((uint32_t)(tp->tvf_resonance > 30 ? 30 : tp->tvf_resonance) * 160u));
	den = 65536 + (int32_t)(((int64_t)g * (g + k)) >> 16);
	p->a1 = (int32_t)(((int64_t)65536 << 16) / den);
	p->a2 = (int32_t)(((int64_t)g * p->a1) >> 16);
	p->a3 = (int32_t)(((int64_t)g * p->a2) >> 16);
	/* the amplifier: the envelope's level in steps of 0.96 dB under 100, nothing at 0 */
	cb = p->static_cb + (int32_t)(((int64_t)(GSL_FULL - p->tva.level) * 96) >> 20) / 10;
	gain = p->tva.level <= 0 ? 0 : (int32_t)gs_cb_gain_q15[gsl_clamp(cb, 0, 1440)] << 8;
	p->gain_step = (gain - p->gain) / GS_LA_BLOCK;
	p->left = GS_LA_BLOCK;
	return 1;
}

/* a rising sawtooth, -32768..32767, with its step smoothed over the two samples it crosses */
static int32_t gsl_saw(uint32_t phase, uint32_t inc)
{
	int32_t v = (int32_t)(phase >> 16) - 32768;
	if (phase < inc) {                                         /* just past the step */
		int32_t u = 32768 - (int32_t)(((uint64_t)phase << 15) / inc);
		v += (u * u) >> 15;
	} else if ((uint32_t)(0u - phase) < inc) {                 /* just before it */
		int32_t u = 32768 - (int32_t)(((uint64_t)(0u - phase) << 15) / inc);
		v -= (u * u) >> 15;
	}
	return v;
}

int gs_la_partial_render(struct gs_la_partial *p, int32_t *out, uint32_t frames)
{
	int saw = p->tp.waveform & 1;
	while (frames && p->active) {
		uint32_t n;
		if (!p->left && !gsl_block(p))
			break;
		n = frames < p->left ? frames : p->left;
		frames -= n;
		p->left -= n;
		while (n--) {
			int32_t in, v1, v2, v3;
			if (saw)
				in = gsl_saw(p->phase, p->inc);
			else                                               /* a pulse is two sawtooths, one late by the width */
				in = gsl_saw(p->phase, p->inc) - gsl_saw(p->phase - p->width, p->inc);
			p->phase += p->inc;
			v3 = in - p->ic2;
			v1 = (int32_t)(((int64_t)p->a1 * p->ic1 + (int64_t)p->a2 * v3) >> 16);
			v2 = p->ic2 + (int32_t)(((int64_t)p->a2 * p->ic1 + (int64_t)p->a3 * v3) >> 16);
			p->ic1 = 2 * v1 - p->ic1;
			p->ic2 = 2 * v2 - p->ic2;
			p->gain += p->gain_step;
			*out++ += (int32_t)(((int64_t)v2 * (p->gain >> 8)) >> 15);
		}
	}
	return p->active;
}
