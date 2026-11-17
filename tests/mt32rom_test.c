/* gs_mt32rom: groups A and B from a control ROM, in both layouts (step 4, part one). No Roland ROM is used:
 * the test writes ROM-shaped images of its own, with partials it can tell apart.
 * MIT, Copyright (c) 2026 AmiCradle Team. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "gs_mt32.h"
#include "gs_mt32rom.h"

#define RATE 32000u
static int checks, fails;
#define CHECK(what, ok) do { checks++; if (!(ok)) { fails++; printf("FAIL: %s\n", what); } } while (0)

static uint8_t rom[0x20000];
static struct gs_mt32rom r;
static struct gs_mt32 s;
static int32_t L[8192], R[8192];

/* a playable partial whose first byte is mark: pitch, square, open filter, full level */
static void partial(uint8_t *p, uint8_t mark)
{
	struct gs_mt32_partial t;
	memset(&t, 0, sizeof t);
	t.pitch_coarse = mark; t.pitch_fine = 50; t.pitch_keyfollow = 11; t.bender = 1;
	t.penv_level[0] = t.penv_level[1] = t.penv_level[2] = t.penv_level[3] = t.penv_level[4] = 50;
	t.tvf_cutoff = 100; t.tvf_keyfollow = 3;
	t.tva_level = 100; t.tva_velo = 50; t.tva_bias_level1 = 12; t.tva_bias_level2 = 12;
	t.tva_env_level[0] = t.tva_env_level[1] = t.tva_env_level[2] = t.tva_env_level[3] = 100;
	t.tva_env_time[4] = 30;
	memcpy(p, &t, GS_MT32_PARTIAL_SIZE);
}

/* timbre i's 14 common bytes: its name says which, structure s12 for partials 1-2 */
static void common(uint8_t *t, int i, int s12, int on)
{
	char name[24];
	snprintf(name, sizeof name, "Rom%03u    ", (unsigned)i & 127u);
	memcpy(t, name, 10);
	t[10] = (uint8_t)s12; t[11] = 0; t[12] = (uint8_t)on; t[13] = 0;
}

/* timbre i's switches: every one has partial 1, the rest vary; 100 has a sampled first partial (structure 2) */
static int on_of(int i) { return 1 | ((i % 8) << 1); }
static int s12_of(int i) { return i == 100 ? 2 : 0; }

static void make_whole(void)
{
	int i, k;
	memset(rom, 0xFF, sizeof rom);
	for (i = 0; i < 128; i++) {
		uint8_t *t = rom + (i < 64 ? 0x8080 : 0xC080) + (uint32_t)(i % 64) * GS_MT32_TIMBRE_SIZE;
		common(t, i, s12_of(i), on_of(i));
		for (k = 0; k < 4; k++) partial(t + 14 + k * 58, (uint8_t)(10 + k));
	}
}

static uint32_t make_packed(void)
{
	uint32_t at = 0x9500;
	int i, k;
	memset(rom, 0xFF, sizeof rom);
	for (i = 0; i < 128; i++) {
		uint8_t *t = rom + at;
		rom[0x8000 + 2 * i] = (uint8_t)((at - 0x8000) & 255);
		rom[0x8000 + 2 * i + 1] = (uint8_t)((at - 0x8000) >> 8);
		common(t, i, s12_of(i), on_of(i));
		t += 14;
		for (k = 0; k < 4; k++)
			if (on_of(i) & (1 << k)) { partial(t, (uint8_t)(10 + k)); t += 58; }
		at = (uint32_t)(t - rom);
	}
	return at;
}

static int names_right(void)
{
	int i;
	for (i = 0; i < 128; i++) {
		char name[24];
		const uint8_t *t = gs_mt32rom_timbre(&r, i / 64, i % 64);
		snprintf(name, sizeof name, "Rom%03u    ", (unsigned)i & 127u);
		if (!t || memcmp(t, name, 10) || t[12] != on_of(i)) return 0;
	}
	return 1;
}

/* every switched-on partial is at its own place, with its own mark */
static int partials_placed(void)
{
	int i, k;
	for (i = 0; i < 128; i++) {
		const uint8_t *t = gs_mt32rom_timbre(&r, i / 64, i % 64);
		for (k = 0; k < 4; k++)
			if ((on_of(i) & (1 << k)) && t[14 + k * 58] != 10 + k) return 0;
	}
	return 1;
}

static long peak(uint32_t n)
{
	long m = 0;
	uint32_t i;
	for (i = 0; i < n; i++) { long a = labs((long)L[i]); if (a > m) m = a; }
	return m;
}

static void render(uint32_t n)
{
	memset(L, 0, sizeof L); memset(R, 0, sizeof R);
	gs_mt32_render(&s, L, R, n);
}

int main(void)
{
	uint32_t end;
	int i;
	uint8_t v[1];
	static uint8_t one[GS_MT32_TIMBRE_SIZE];

	/* the whole layout (MT-32 1.04-1.07, blue ribbon) */
	make_whole();
	CHECK("whole: read as the whole layout", gs_mt32rom_read(&r, rom, 0x10000) == GS_MT32ROM_WHOLE);
	CHECK("whole: 128 timbres in order, groups A then B", names_right());
	CHECK("whole: each partial as the ROM has it", partials_placed());
	CHECK("whole: too short a file is refused", gs_mt32rom_read(&r, rom, 0xC080 + 63 * 246) == 0 && !r.layout);
	rom[0xC080 + 5 * 246 + 3] = 7;                 /* a name with a control character */
	CHECK("whole: a timbre with a bad name refuses the ROM", gs_mt32rom_read(&r, rom, 0x10000) == 0);
	make_whole();
	rom[0x8080 + 14 + 3] = 200;                    /* a partial byte over 127 */
	CHECK("whole: a partial byte over 127 refuses the ROM", gs_mt32rom_read(&r, rom, 0x10000) == 0);

	/* the packed layout (MT-32 2.04-2.07, CM-32L) */
	end = make_packed();
	CHECK("packed: the test image fits", end < sizeof rom);
	CHECK("packed: read as the packed layout", gs_mt32rom_read(&r, rom, sizeof rom) == GS_MT32ROM_PACKED);
	CHECK("packed: 128 timbres in order, groups A then B", names_right());
	CHECK("packed: switched-on partials go to their own places", partials_placed());
	CHECK("packed: a partial that isn't stored reads as zeros",
	      gs_mt32rom_timbre(&r, 0, 0)[14 + 58] == 0 && gs_mt32rom_timbre(&r, 0, 0)[14 + 3 * 58 + 10] == 0);
	CHECK("packed: a timbre past the file's end refuses the ROM", gs_mt32rom_read(&r, rom, end - 10) == 0);
	make_packed();
	rom[0x8000 + 2 * 127] = 0x00; rom[0x8000 + 2 * 127 + 1] = 0x00;   /* an offset onto the table itself */
	CHECK("packed: an offset onto something not a timbre refuses the ROM", gs_mt32rom_read(&r, rom, sizeof rom) == 0);
	make_packed();
	rom[0x9500 + 10] = 13;                          /* structure 14 */
	CHECK("packed: a structure over 13 refuses the ROM", gs_mt32rom_read(&r, rom, sizeof rom) == 0);

	/* anything else */
	memset(rom, 0, sizeof rom);
	CHECK("a file of zeros is no ROM", gs_mt32rom_read(&r, rom, sizeof rom) == 0);
	srand(7);
	for (i = 0; i < (int)sizeof rom; i++) rom[i] = (uint8_t)rand();
	CHECK("a file of noise is no ROM", gs_mt32rom_read(&r, rom, sizeof rom) == 0);
	CHECK("no file is no ROM", gs_mt32rom_read(&r, NULL, 0) == 0 && gs_mt32rom_timbre(&r, 0, 0) == NULL);
	CHECK("groups other than A and B are not here", gs_mt32rom_timbre(&r, 2, 0) == NULL);

	/* the engine: parts play the ROM's timbres */
	make_packed();
	gs_mt32rom_read(&r, rom, sizeof rom);
	gs_mt32_init(&s, RATE);
	CHECK("without the ROM, part 1 on patch A01 is not playable", !gs_mt32_playable(&s, 1, 0));
	gs_mt32_use_rom(&s, &r);
	CHECK("with it, part 1 plays its patch's timbre, A01", gs_mt32_playable(&s, 1, 0)
	      && !memcmp(s.mem.timbre_temp[0], "Rom000", 6) && s.part[0].from_rom);
	gs_mt32_midi(&s, 0x91, 69, 100); render(4096);
	CHECK("and a note on it sounds", peak(4096) > 1000 && gs_mt32_partials(&s) == 1);
	gs_mt32_midi(&s, 0xC1, 70, 0);
	CHECK("program 71 is patch B07: its timbre comes from group B", !memcmp(s.mem.timbre_temp[0], "Rom070", 6));
	gs_mt32_midi(&s, 0xC1, 100, 0);
	CHECK("a ROM timbre with a sampled partial on is not played yet", !gs_mt32_playable(&s, 1, 0));
	gs_mt32_midi(&s, 0xC1, 5, 0);
	CHECK("... and the next synthesiser-only one is", gs_mt32_playable(&s, 1, 0));
	v[0] = 0;
	{   /* a reset (any write to 7F 00 00) keeps the ROM: part 1 is back on A01 */
		uint8_t m[] = { 0xF0, 0x41, 0x10, 0x16, 0x12, 0x7F, 0x00, 0x00, 0x00, 0x01, 0xF7 };
		gs_mt32_sysex(&s, m, sizeof m);
	}
	CHECK("after a reset the parts take the ROM's timbres again", gs_mt32_playable(&s, 1, 0)
	      && !memcmp(s.mem.timbre_temp[0], "Rom000", 6));
	{   /* the song writes part 1's timbre itself: that is the song's, not the ROM's */
		uint8_t m[8 + GS_MT32_TIMBRE_SIZE + 2];
		uint32_t addr = GS_MT32_ADDR(4, 0, 0), k;
		memcpy(one, gs_mt32rom_timbre(&r, 0, 1), sizeof one);
		m[0] = 0xF0; m[1] = 0x41; m[2] = 0x10; m[3] = 0x16; m[4] = 0x12;
		m[5] = (addr >> 14) & 127; m[6] = (addr >> 7) & 127; m[7] = addr & 127;
		for (k = 0; k < sizeof one; k++) m[8 + k] = one[k];
		m[8 + sizeof one] = gs_mt32_checksum(m + 5, sizeof one + 3);
		m[9 + sizeof one] = 0xF7;
		gs_mt32_sysex(&s, m, sizeof m);
	}
	CHECK("a timbre the song sends is the song's", s.part[0].known && !s.part[0].from_rom && gs_mt32_playable(&s, 1, 0));
	gs_mt32_use_rom(&s, NULL);
	gs_mt32_midi(&s, 0xC1, 1, 0);
	CHECK("the ROM taken away: a group A patch is not playable again", !gs_mt32_playable(&s, 1, 0));
	(void)v;

	printf("mt32rom_test: %d checks, %d failed\n", checks, fails);
	return fails != 0;
}
