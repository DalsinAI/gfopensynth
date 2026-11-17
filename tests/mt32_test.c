/* gs_mt32: notes on our own MT-32 engine, from timbres a song sends. No ROM is used.
 * MIT, Copyright (c) 2026 AmiCradle Team. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "gs_mt32.h"

#define RATE 32000u
static int checks, fails;
#define CHECK(what, ok) do { checks++; if (!(ok)) { fails++; printf("FAIL: %s\n", what); } } while (0)

static struct gs_mt32 s;
static int32_t L[RATE * 2], R[RATE * 2];

static void render(uint32_t n)
{
	uint32_t i;
	for (i = 0; i < n; i++) L[i] = R[i] = 0;
	gs_mt32_render(&s, L, R, n);
}

static long peak(const int32_t *b, uint32_t from, uint32_t to)
{
	long m = 0;
	uint32_t i;
	for (i = from; i < to; i++) { long a = labs((long)b[i]); if (a > m) m = a; }
	return m;
}

static int crossings(const int32_t *b, uint32_t from, uint32_t to)
{
	int c = 0;
	uint32_t i;
	for (i = from + 1; i < to; i++)
		if (b[i - 1] < 0 && b[i] >= 0) c++;
	return c;
}

/* a DT1 to a linear address */
static int dt1(uint32_t addr, const uint8_t *data, uint32_t n)
{
	static uint8_t m[600];
	uint32_t i;
	m[0] = 0xF0; m[1] = 0x41; m[2] = 0x10; m[3] = 0x16; m[4] = 0x12;
	m[5] = (addr >> 14) & 127; m[6] = (addr >> 7) & 127; m[7] = addr & 127;
	for (i = 0; i < n; i++) m[8 + i] = data[i];
	m[8 + n] = gs_mt32_checksum(m + 5, n + 3);
	m[9 + n] = 0xF7;
	return gs_mt32_sysex(&s, m, n + 10);
}

/* a plain partial: the key's own pitch, a square, filter open, full level at once, a short release */
static struct gs_mt32_partial plain(void)
{
	struct gs_mt32_partial t;
	memset(&t, 0, sizeof t);
	t.pitch_coarse = 36; t.pitch_fine = 50; t.pitch_keyfollow = 11; t.bender = 1;
	t.penv_level[0] = t.penv_level[1] = t.penv_level[2] = t.penv_level[3] = t.penv_level[4] = 50;
	t.tvf_cutoff = 100; t.tvf_keyfollow = 3;
	t.tva_level = 100; t.tva_velo = 50; t.tva_bias_level1 = 12; t.tva_bias_level2 = 12;
	t.tva_env_level[0] = t.tva_env_level[1] = t.tva_env_level[2] = t.tva_env_level[3] = 100;
	t.tva_env_time[4] = 30;
	return t;
}

/* the 246 bytes of a timbre of plain partials: its two structures and which partials are on */
static uint8_t tb[GS_MT32_TIMBRE_SIZE];
static void timbre(int s12, int s34, int on)
{
	struct gs_mt32_partial p = plain();
	int i;
	memcpy(tb, "TEST      ", 10);
	tb[10] = (uint8_t)s12; tb[11] = (uint8_t)s34; tb[12] = (uint8_t)on; tb[13] = 0;
	for (i = 0; i < 4; i++)
		memcpy(tb + GS_MT32_COMMON_SIZE + i * GS_MT32_PARTIAL_SIZE, &p, sizeof p);
}
static struct gs_mt32_partial *part_of(int i) { return (struct gs_mt32_partial *)(tb + GS_MT32_COMMON_SIZE + i * GS_MT32_PARTIAL_SIZE); }

#define TEMP(part)  (GS_MT32_ADDR(4, 0, 0) + (uint32_t)(part) * GS_MT32_TIMBRE_SIZE)
#define PATCH_TEMP  GS_MT32_ADDR(3, 0, 0)
#define on(k)  gs_mt32_midi(&s, 0x91, (k), 100)     /* MIDI channel 2 is part 1 */
#define off(k) gs_mt32_midi(&s, 0x81, (k), 0)

/* a fresh engine with this timbre on part 1 */
static void fresh(int s12, int s34, int onbits)
{
	gs_mt32_init(&s, RATE);
	timbre(s12, s34, onbits);
	dt1(TEMP(0), tb, sizeof tb);
}

int main(void)
{
	long a;
	int c, i;
	uint8_t v[16];

	/* nothing from the song yet: every timbre is a ROM's */
	gs_mt32_init(&s, RATE);
	CHECK("a part on a ROM timbre is not playable", !gs_mt32_playable(&s, 1, 0));
	on(69); render(4096);
	CHECK("and a note on it is silent here", peak(L, 0, 4096) == 0 && gs_mt32_partials(&s) == 0);
	CHECK("a channel no part listens to is not playable", !gs_mt32_playable(&s, 0, 0) && !gs_mt32_playable(&s, 15, 0));

	/* one partial */
	fresh(0, 0, 1);
	CHECK("a timbre the song wrote to the part is playable", gs_mt32_playable(&s, 1, 0) && !gs_mt32_playable(&s, 2, 0));
	on(69); render(RATE * 2);
	c = crossings(L, RATE, RATE * 2);
	CHECK("its note sounds at the key's pitch (440 Hz) on both sides", c >= 439 && c <= 441 && gs_mt32_partials(&s) == 1
	      && peak(L, RATE, RATE * 2) > 2000 && labs(peak(L, RATE, RATE * 2) - peak(R, RATE, RATE * 2)) < 50);
	a = peak(L, RATE, RATE * 2);
	off(69); render(RATE);
	CHECK("key up: the note dies away and its partial is free", peak(L, RATE / 2, RATE) == 0 && gs_mt32_partials(&s) == 0);

	/* two partials mixed */
	fresh(0, 0, 3);
	on(69); render(RATE);
	CHECK("structure 1 mixes its two partials: twice as loud", gs_mt32_partials(&s) == 2
	      && peak(L, RATE / 2, RATE) > a * 19 / 10 && peak(L, RATE / 2, RATE) < a * 21 / 10);
	fresh(0, 0, 15);
	on(69); render(RATE);
	CHECK("four partials on: four sound", gs_mt32_partials(&s) == 4 && peak(L, RATE / 2, RATE) > a * 38 / 10);

	/* ring modulation */
	fresh(9, 0, 1);
	on(69); render(RATE);
	CHECK("structure 10 is ring modulation only: one partial alone gives silence", peak(L, 0, RATE) == 0);
	timbre(9, 0, 3); part_of(1)->pitch_coarse = 55;
	gs_mt32_init(&s, RATE); dt1(TEMP(0), tb, sizeof tb);
	on(69); render(RATE * 2);
	c = crossings(L, RATE, RATE * 2);
	CHECK("two squares, one at three times the other's pitch, ring modulated: a new sound above the lower pitch", peak(L, RATE, RATE * 2) > 2000 && c > 500);
	fresh(1, 0, 1);
	on(69); render(RATE);
	CHECK("structure 2 is the first partial and the ring: the first alone is itself", labs(peak(L, RATE / 2, RATE) - a) < a / 50);
	fresh(1, 0, 3);
	on(69); render(RATE);
	CHECK("structure 2 with both: louder than the first alone", peak(L, RATE / 2, RATE) > a * 3 / 2);

	/* sampled partials need the ROM */
	fresh(2, 0, 3);
	on(69); render(RATE);
	CHECK("structure 3 (sampled + synthesiser): only the synthesiser partial sounds", gs_mt32_playable(&s, 1, 0) && gs_mt32_partials(&s) == 1);
	fresh(5, 5, 15);
	CHECK("a timbre of sampled partials only is not playable", !gs_mt32_playable(&s, 1, 0));
	fresh(0, 0, 0);
	CHECK("a timbre with every partial off is not playable", !gs_mt32_playable(&s, 1, 0));

	/* timbre memory, patch memory, program change */
	gs_mt32_init(&s, RATE);
	timbre(0, 0, 1);
	dt1(GS_MT32_ADDR(8, 2 * 5, 0), tb, sizeof tb);                  /* timbre memory 5 */
	memset(v, 0, sizeof v); v[0] = GS_MT32_GROUP_MEMORY; v[1] = 5; v[2] = 24; v[3] = 50; v[4] = 12;
	dt1(GS_MT32_ADDR(5, 0, 0) + 20 * 8, v, 8);                      /* patch 20 names it */
	CHECK("writing timbre memory alone changes no part", !gs_mt32_playable(&s, 1, 0));
	gs_mt32_midi(&s, 0xC1, 20, 0);
	on(69); render(RATE);
	CHECK("a program change to a patch on timbre memory plays it", gs_mt32_playable(&s, 1, 0) && peak(L, RATE / 2, RATE) > 2000);
	gs_mt32_midi(&s, 0xC1, 3, 0);
	CHECK("a program change to a ROM patch is not playable again", !gs_mt32_playable(&s, 1, 0));
	v[0] = GS_MT32_GROUP_MEMORY; v[1] = 5;
	dt1(PATCH_TEMP, v, 2);
	CHECK("a patch written to the part that names timbre memory loads it", gs_mt32_playable(&s, 1, 0));

	/* key shift, fine tune, bender */
	fresh(0, 0, 1);
	v[0] = 36; dt1(PATCH_TEMP + GS_MT32_P_KEYSHIFT, v, 1);
	on(69); render(RATE * 2);
	c = crossings(L, RATE, RATE * 2);
	CHECK("key shift 36 is an octave up", c >= 878 && c <= 882);
	fresh(0, 0, 1);
	v[0] = 12; dt1(PATCH_TEMP + GS_MT32_P_BEND, v, 1);
	on(69);
	gs_mt32_midi(&s, 0xE1, 127, 127);
	render(RATE * 2);
	c = crossings(L, RATE, RATE * 2);
	CHECK("the bender at its top with range 12 is an octave up", c >= 875 && c <= 882);
	gs_mt32_midi(&s, 0xE1, 0, 64);
	render(RATE * 2);
	c = crossings(L, RATE, RATE * 2);
	CHECK("and back at its middle the pitch returns", c >= 439 && c <= 441);
	fresh(0, 0, 1);
	v[0] = 100; dt1(PATCH_TEMP + GS_MT32_P_FINETUNE, v, 1);
	on(69); render(RATE * 2);
	c = crossings(L, RATE, RATE * 2);
	CHECK("fine tune 100 is half a semitone up (453 Hz)", c >= 451 && c <= 454);

	/* level, volume, expression, pan */
	fresh(0, 0, 1);
	on(69); render(RATE);
	a = peak(L, RATE / 2, RATE);
	gs_mt32_midi(&s, 0xB1, 7, 50); render(RATE);
	CHECK("volume 50 is half of 100", labs(peak(L, RATE / 2, RATE) * 2 - a) < a / 20);
	gs_mt32_midi(&s, 0xB1, 7, 127); gs_mt32_midi(&s, 0xB1, 11, 64); render(RATE);
	CHECK("volume stops at 100; expression 64 is about half", labs(peak(L, RATE / 2, RATE) * 2 - a) < a / 10);
	gs_mt32_midi(&s, 0xB1, 121, 0); render(RATE);
	CHECK("reset all controllers puts them back", labs(peak(L, RATE / 2, RATE) - a) < a / 50);
	fresh(0, 0, 1);
	v[0] = 40; dt1(PATCH_TEMP + GS_MT32_P_LEVEL, v, 1);
	on(69); render(RATE);
	CHECK("the part's level 40 is half of its 80", labs(peak(L, RATE / 2, RATE) * 2 - a) < a / 20);
	fresh(0, 0, 1);
	v[0] = 0; dt1(PATCH_TEMP + GS_MT32_P_PAN, v, 1);
	on(69); render(RATE);
	CHECK("pan 0 is hard right", peak(L, 0, RATE) == 0 && peak(R, RATE / 2, RATE) > a);
	fresh(0, 0, 1);
	gs_mt32_midi(&s, 0xB1, 10, 127);
	on(69); render(RATE);
	CHECK("the pan controller at 127 is hard left", peak(R, 0, RATE) == 0 && peak(L, RATE / 2, RATE) > a);

	/* the hold pedal */
	fresh(0, 0, 1);
	gs_mt32_midi(&s, 0xB1, 64, 127);
	on(69); off(69); render(RATE);
	CHECK("the pedal holds a note whose key came up", gs_mt32_partials(&s) == 1 && peak(L, RATE / 2, RATE) > 2000);
	gs_mt32_midi(&s, 0xB1, 64, 0); render(RATE);
	CHECK("pedal up lets it go", gs_mt32_partials(&s) == 0);
	on(60); on(64); gs_mt32_midi(&s, 0xB1, 123, 0); render(RATE);
	CHECK("all notes off", gs_mt32_partials(&s) == 0);

	/* assign modes */
	fresh(0, 0, 1);
	on(69); on(69);
	CHECK("assign mode 1: a key struck again has one note", gs_mt32_partials(&s) == 1);
	v[0] = 2; dt1(PATCH_TEMP + GS_MT32_P_ASSIGN, v, 1);
	on(72); on(72);
	CHECK("assign mode 3: it rings over itself", gs_mt32_partials(&s) == 3);

	/* 32 partials */
	fresh(0, 0, 15);
	for (i = 0; i < 9; i++) on(40 + i);
	CHECK("a ninth note of four partials takes the oldest's place", gs_mt32_partials(&s) == 32 && s.stolen == 1 && s.dropped == 0);
	render(2048);
	for (i = 0; i < GS_MT32_NOTES; i++) if (s.note[i].active && s.note[i].key == 40) break;
	CHECK("the oldest note is the one that went", i == GS_MT32_NOTES);
	off(41); on(60);
	for (i = 0; i < GS_MT32_NOTES; i++) if (s.note[i].active && s.note[i].key == 41) break;
	CHECK("a note whose key is up goes before one still held", i == GS_MT32_NOTES && s.stolen == 2);
	fresh(0, 0, 15);
	v[0] = 32; dt1(GS_MT32_ADDR(0x10, 0, 0) + GS_MT32_S_RESERVE, v, 1);    /* part 1 may keep all 32 */
	v[0] = 1; dt1(PATCH_TEMP + GS_MT32_P_ASSIGN, v, 1);
	for (i = 0; i < 9; i++) on(40 + i);
	CHECK("assign mode 2 within its reserve keeps what sounds: the ninth note is lost", gs_mt32_partials(&s) == 32 && s.dropped == 1 && s.stolen == 0);

	/* the rhythm part: MIDI channel 10 */
	gs_mt32_init(&s, RATE);
	timbre(0, 0, 1);
	dt1(GS_MT32_ADDR(8, 0, 0), tb, sizeof tb);                      /* timbre memory 0 */
	v[0] = 0; v[1] = 100; v[2] = 14; v[3] = 0;
	dt1(GS_MT32_ADDR(3, 1, 0x10) + (36 - 24) * 4, v, 4);            /* key 36 plays it, hard left */
	v[0] = 70;
	dt1(GS_MT32_ADDR(3, 1, 0x10) + (38 - 24) * 4, v, 1);            /* key 38 is a ROM drum */
	CHECK("a rhythm key on a timbre the song sent is playable, one on a ROM drum is not",
	      gs_mt32_playable(&s, 9, 36) && !gs_mt32_playable(&s, 9, 38) && !gs_mt32_playable(&s, 9, 10));
	gs_mt32_midi(&s, 0x99, 36, 100); gs_mt32_midi(&s, 0x99, 38, 100);
	render(RATE * 2);
	c = crossings(L, RATE, RATE * 2);
	CHECK("it sounds at the timbre's own pitch (middle C, 262 Hz), where its key's pan puts it",
	      c >= 260 && c <= 263 && peak(R, 0, RATE) == 0 && gs_mt32_partials(&s) == 1);

	/* reset */
	fresh(0, 0, 1);
	on(69);
	v[0] = 0; dt1(GS_MT32_ADDR(0x7F, 0, 0), v, 1);
	render(1024);
	CHECK("a reset stops every note and forgets the song's timbres", gs_mt32_partials(&s) == 0 && peak(L, 0, 1024) == 0 && !gs_mt32_playable(&s, 1, 0));
	v[0] = 1;
	CHECK("a message for another unit changes nothing", (s.mem.device = 0x11, dt1(TEMP(0), tb, sizeof tb)) == 0 && !gs_mt32_playable(&s, 1, 0));

	/* anything at all on the wire: no crash, no overflow (the sanitizers watch) */
	{
		uint32_t x = 12345;
		long worst = 0;
		gs_mt32_init(&s, RATE);
		for (i = 0; i < 8; i++) { timbre(i * 3 % 13, i * 5 % 13, 15); dt1(TEMP(i), tb, sizeof tb); }
		for (i = 0; i < 6000; i++) {
			x = x * 1103515245u + 12345u;
			gs_mt32_midi(&s, (uint8_t)(0x80 | ((x >> 8) & 0x7F)), (uint8_t)(x >> 16), (uint8_t)(x >> 24));
			if ((i & 15) == 0) {
				render(96);
				if (peak(L, 0, 96) > worst) worst = peak(L, 0, 96);
				if (peak(R, 0, 96) > worst) worst = peak(R, 0, 96);
			}
		}
		CHECK("6000 arbitrary messages: never more than 32 partials, the sum stays in bounds", gs_mt32_partials(&s) <= 32 && worst < 40000000);
	}

	if (fails) { printf("FAIL: %d of %d note checks\n", fails, checks); return 1; }
	printf("PASS: %d note checks, 0 failed\n", checks);
	return 0;
}
