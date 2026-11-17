/* gs_la: one synthesiser partial of our own MT-32 engine. No ROM is used.
 * MIT, Copyright (c) 2026 AmiCradle Team. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "gs_la.h"

#define RATE 32000u
static int checks, fails;
#define CHECK(what, ok) do { checks++; if (!(ok)) { fails++; printf("FAIL: %s\n", what); } } while (0)

static int32_t buf[RATE * 4];

/* a plain partial: the key's own pitch, a square, filter wide open, full level at once, a short release */
static struct gs_mt32_partial plain(void)
{
	struct gs_mt32_partial t;
	memset(&t, 0, sizeof t);
	t.pitch_coarse = 36; t.pitch_fine = 50; t.pitch_keyfollow = 11;
	t.penv_level[0] = t.penv_level[1] = t.penv_level[2] = t.penv_level[3] = t.penv_level[4] = 50;
	t.tvf_cutoff = 100; t.tvf_keyfollow = 3;
	t.tva_level = 100; t.tva_velo = 50; t.tva_bias_level1 = 12; t.tva_bias_level2 = 12;
	t.tva_env_level[0] = t.tva_env_level[1] = t.tva_env_level[2] = t.tva_env_level[3] = 100;
	t.tva_env_time[4] = 30;
	return t;
}

static void clear(uint32_t n)
{
	uint32_t i;
	for (i = 0; i < n; i++) buf[i] = 0;
}

/* plays n samples of a held note into buf; the partial is left as it ends up */
static void play(struct gs_la_partial *p, const struct gs_mt32_partial *t, int key, int vel, uint32_t n)
{
	clear(n);
	gs_la_partial_start(p, t, key, vel, 0, RATE);
	gs_la_partial_render(p, buf, n);
}

/* upward zero crossings in buf[from..to) */
static int crossings(uint32_t from, uint32_t to)
{
	int c = 0;
	uint32_t i;
	for (i = from + 1; i < to; i++)
		if (buf[i - 1] < 0 && buf[i] >= 0) c++;
	return c;
}

static long peak(uint32_t from, uint32_t to)
{
	long m = 0;
	uint32_t i;
	for (i = from; i < to; i++) { long a = labs((long)buf[i]); if (a > m) m = a; }
	return m;
}

/* mean square, scaled down to fit */
static double power(uint32_t from, uint32_t to)
{
	double s = 0;
	uint32_t i;
	for (i = from; i < to; i++) s += (double)buf[i] * (double)buf[i];
	return s / (to - from);
}

int main(void)
{
	static struct gs_la_partial p;
	struct gs_mt32_partial t;
	int c, i;
	long a, b;
	double e1, e2;

	/* pitch */
	t = plain();
	play(&p, &t, 69, 100, RATE * 2);
	c = crossings(RATE, RATE * 2);
	CHECK("A above middle C is 440 Hz", c >= 439 && c <= 441);
	play(&p, &t, 57, 100, RATE * 2);
	c = crossings(RATE, RATE * 2);
	CHECK("the A below is 220 Hz", c >= 219 && c <= 221);
	t.pitch_coarse = 48;
	play(&p, &t, 69, 100, RATE * 2);
	c = crossings(RATE, RATE * 2);
	CHECK("coarse 48 is an octave up", c >= 878 && c <= 882);
	t = plain(); t.pitch_fine = 100;
	play(&p, &t, 69, 100, RATE * 2);
	c = crossings(RATE, RATE * 2);
	CHECK("fine 100 is half a semitone up (453 Hz)", c >= 451 && c <= 454);
	t = plain(); t.pitch_keyfollow = 3;
	play(&p, &t, 84, 100, RATE * 2);
	c = crossings(RATE, RATE * 2);
	CHECK("key follow 0 plays middle C whatever the key (262 Hz)", c >= 260 && c <= 263);
	t = plain(); t.pitch_keyfollow = 7;
	play(&p, &t, 84, 100, RATE * 2);
	c = crossings(RATE, RATE * 2);
	CHECK("key follow 1/2: two octaves up the keyboard is one in pitch (523 Hz)", c >= 521 && c <= 525);
	t = plain(); t.bender = 1;
	gs_la_partial_start(&p, &t, 69, 100, 0, RATE);
	gs_la_partial_control(&p, 1200, 0);
	clear(RATE * 2);
	gs_la_partial_render(&p, buf, RATE * 2);
	c = crossings(RATE, RATE * 2);
	CHECK("the bender an octave up", c >= 878 && c <= 882);
	t.bender = 0;
	gs_la_partial_start(&p, &t, 69, 100, 0, RATE);
	gs_la_partial_control(&p, 1200, 0);
	clear(RATE * 2);
	gs_la_partial_render(&p, buf, RATE * 2);
	c = crossings(RATE, RATE * 2);
	CHECK("a partial with its bender switch off ignores it", c >= 439 && c <= 441);

	/* the pitch envelope: from level 100 (depth 5: an octave up) down to the key's pitch */
	t = plain(); t.penv_depth = 5; t.penv_level[0] = 100; t.penv_time[0] = 50;
	play(&p, &t, 69, 100, RATE * 2);
	CHECK("the pitch envelope starts high and settles on the key", crossings(0, RATE / 100) * 100 > 700
	      && crossings(RATE, RATE * 2) >= 439 && crossings(RATE, RATE * 2) <= 441);
	/* the LFO: depth 100 swings the pitch, so one second holds as many cycles but they are uneven */
	t = plain(); t.lfo_rate = 80; t.lfo_depth = 100;
	play(&p, &t, 69, 100, RATE * 2);
	{
		int lo = 1 << 30, hi = 0;
		for (i = 0; i < 50; i++) { c = crossings(RATE + i * (RATE / 50), RATE + (i + 1) * (RATE / 50)); if (c < lo) lo = c; if (c > hi) hi = c; }
		CHECK("the LFO moves the pitch about the note", hi > lo && crossings(RATE, RATE * 2) > 425 && crossings(RATE, RATE * 2) < 455);
	}

	/* waves */
	t = plain();
	play(&p, &t, 45, 100, RATE);
	{
		uint32_t pos = 0, n = 0, k;
		for (k = RATE / 2; k < RATE; k++) { n++; if (buf[k] > 0) pos++; }
		CHECK("pulse width 0 is a square: half the time high", pos * 100 / n >= 48 && pos * 100 / n <= 52);
		CHECK("a square at full level peaks near full scale", peak(RATE / 2, RATE) > 28000 && peak(RATE / 2, RATE) < 45000);
	}
	t.pulse_width = 100;
	play(&p, &t, 45, 100, RATE);
	{
		uint32_t pos = 0, n = 0, k;
		for (k = RATE / 2; k < RATE; k++) { n++; if (buf[k] > 0) pos++; }
		pos = pos * 100 / n;
		CHECK("pulse width 100 is a narrow pulse", (pos >= 1 && pos <= 8) || (pos >= 92 && pos <= 99));
	}
	t = plain(); t.waveform = 1;
	play(&p, &t, 45, 100, RATE);
	{
		uint32_t up = 0, n = 0, k;
		for (k = RATE / 2 + 1; k < RATE; k++) { n++; if (buf[k] > buf[k - 1]) up++; }
		CHECK("waveform 1 is a sawtooth: it climbs nearly all the time", up * 100 / n > 90);
	}

	/* the filter */
	t = plain(); t.waveform = 1;
	play(&p, &t, 45, 100, RATE);
	e1 = power(RATE / 2, RATE);
	t.tvf_cutoff = 20;
	play(&p, &t, 45, 100, RATE);
	e2 = power(RATE / 2, RATE);
	CHECK("a low cutoff takes most of a sawtooth's power away", e2 < e1 * 0.5 && e2 > 0);
	a = peak(RATE / 2, RATE);
	t.tvf_resonance = 30;
	play(&p, &t, 45, 100, RATE);
	CHECK("resonance rings: the same cutoff is louder with it", peak(RATE / 2, RATE) > a * 3 / 2);
	t.tvf_resonance = 0; t.tvf_env_depth = 100; t.tvf_env_level[0] = t.tvf_env_level[1] = t.tvf_env_level[2] = t.tvf_env_level[3] = 100;
	play(&p, &t, 45, 100, RATE);
	CHECK("the filter envelope opens the filter", power(RATE / 2, RATE) > e2 * 1.5);
	t = plain(); t.waveform = 1; t.tvf_cutoff = 30; t.tvf_keyfollow = 11;
	play(&p, &t, 36, 100, RATE);
	e1 = power(RATE / 2, RATE);
	t.tvf_keyfollow = 3;
	play(&p, &t, 36, 100, RATE);
	CHECK("filter key follow: a low key closes the filter", e1 < power(RATE / 2, RATE));

	/* the amplifier */
	t = plain();
	play(&p, &t, 69, 100, RATE);
	a = peak(RATE / 2, RATE);
	t.tva_level = 75;
	play(&p, &t, 69, 100, RATE);
	b = peak(RATE / 2, RATE);
	CHECK("level 75 is about 12 dB under level 100", b * 3 < a && b * 5 > a);
	t = plain(); t.tva_velo = 100;
	play(&p, &t, 69, 127, RATE);
	a = peak(RATE / 2, RATE);
	play(&p, &t, 69, 20, RATE);
	b = peak(RATE / 2, RATE);
	CHECK("velocity sense: a soft note is quieter", b * 2 < a);
	t.tva_velo = 50;
	play(&p, &t, 69, 20, RATE);
	CHECK("velocity sense 50: velocity changes nothing", labs(peak(RATE / 2, RATE) - a) < a / 50);
	t = plain(); t.tva_bias_point1 = 64 + 27; t.tva_bias_level1 = 0;      /* above key 60 */
	play(&p, &t, 84, 100, RATE);
	b = peak(RATE / 2, RATE);
	play(&p, &t, 48, 100, RATE);
	CHECK("bias: keys above the point are quieter, keys below are not", b * 2 < peak(RATE / 2, RATE));

	/* the amplifier's envelope: a slow rise, then key up and it dies away and ends */
	t = plain(); t.tva_env_time[0] = 56;              /* 2^8 = 256 ms */
	CHECK("time 0 is a millisecond, 56 a quarter of a second, 100 about 20 s", gs_la_time_ms_q8(0) == 256
	      && gs_la_time_ms_q8(56) == 256 * 256 && gs_la_time_ms_q8(100) > 256u * 15000 && gs_la_time_ms_q8(100) < 256u * 25000);
	play(&p, &t, 69, 100, RATE);
	CHECK("a slow attack: quiet at first, full by the end of its time",
	      peak(0, RATE / 100) < 1000 && peak(RATE / 8 - 400, RATE / 8) < peak(RATE / 2, RATE) && peak(RATE * 3 / 10, RATE * 4 / 10) > 28000);
	CHECK("a held note goes on", p.active && gs_la_partial_render(&p, buf, 64));
	gs_la_partial_release(&p);
	clear(RATE);
	i = gs_la_partial_render(&p, buf, RATE);
	CHECK("after key up it dies away and ends (release 30: 20 ms)", i == 0 && !p.active && peak(0, 64) > 1000 && peak(RATE / 10, RATE) == 0);
	clear(64);
	CHECK("an ended partial adds nothing", gs_la_partial_render(&p, buf, 64) == 0 && peak(0, 64) == 0);
	/* no sustain: the note ends on its own */
	t = plain(); t.tva_env_time[3] = 40;
	clear(RATE);
	gs_la_partial_start(&p, &t, 69, 100, 1, RATE);
	CHECK("env mode no sustain: the note ends without key up", gs_la_partial_render(&p, buf, RATE) == 0);

	/* every setting at its lowest and at its highest, every key: no crash, no overflow (the sanitizers watch) */
	for (i = 0; i < 2; i++) {
		int key;
		static const uint8_t max[GS_MT32_PARTIAL_SIZE] = {
			96, 100, 16, 1, 3, 127, 100, 14, 10, 3, 4, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100,
			100, 30, 14, 127, 14, 100, 100, 4, 4, 100, 100, 100, 100, 100, 100, 100, 100, 100,
			100, 100, 127, 12, 127, 12, 4, 4, 100, 100, 100, 100, 100, 100, 100, 100, 100 };
		long worst = 0;
		if (i) memcpy(&t, max, sizeof t); else memset(&t, 0, sizeof t);
		if (i) { t.tva_env_time[0] = t.tva_env_time[1] = t.tva_env_time[2] = t.tva_env_time[3] = 0; }
		for (key = 0; key < 128; key += 7) {
			clear(4096);
			gs_la_partial_start(&p, &t, key, 127, 0, key & 1 ? 8000 : 48000);
			gs_la_partial_control(&p, key & 2 ? 2400 : -2400, 127);
			gs_la_partial_render(&p, buf, 4096);
			gs_la_partial_release(&p);
			gs_la_partial_render(&p, buf, 16);
			if (peak(0, 4096) > worst) worst = peak(0, 4096);
		}
		CHECK(i ? "every setting at its highest stays in bounds" : "every setting at 0 stays in bounds", worst < 1500000);
	}

	if (fails) { printf("FAIL: %d of %d partial checks\n", fails, checks); return 1; }
	printf("PASS: %d partial checks, 0 failed\n", checks);
	return 0;
}
