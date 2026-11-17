/* GFOpenSynth Basic (tools/make_basic_sf2.c) through the reader and the
 * synth: every General MIDI program and the kit are there and sound, a
 * loud chord does not clip, high notes use the wave with few harmonics.
 *   basic_sf2_test basic.sf2
 * MIT, Copyright (c) 2026 AmiCradle Team. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "gs_synth.h"

#define RATE 22050
static int fails, checks;
#define CHECK(c) do { checks++; if (!(c)) { fails++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)

static struct gs_sf2 font;
static struct gs_synth syn;
static short buf[RATE * 2];

static void msg(int st, int d1, int d2)
{
	struct gs_msg m;
	m.status = (uint8_t)st; m.d1 = (uint8_t)d1; m.d2 = (uint8_t)d2; m.len = 3;
	gs_synth_msg(&syn, &m);
}

static long peak(int frames)
{
	long p = 0;
	int i;
	for (i = 0; i < frames * 2; i++) {
		long v = buf[i] < 0 ? -(long)buf[i] : buf[i];
		if (v > p) p = v;
	}
	return p;
}

int main(int argc, char **argv)
{
	FILE *f;
	long len;
	unsigned char *data;
	int p, k, quiet = 0, silent = 0;
	struct gs_zone z[4];
	long pk;

	if (argc != 2 || !(f = fopen(argv[1], "rb"))) { printf("basic_sf2_test basic.sf2\n"); return 1; }
	fseek(f, 0, SEEK_END); len = ftell(f); fseek(f, 0, SEEK_SET);
	data = malloc((size_t)len);
	CHECK(fread(data, 1, (size_t)len, f) == (size_t)len);
	fclose(f);
	CHECK(len < 200 * 1024);                                /* small: it ships in the release */
	CHECK(gs_sf2_open(&font, data, (uint32_t)len) == 0);
	for (p = 0; p < 128; p++)
		if (gs_sf2_preset(&font, 0, p) < 0) silent++;
	CHECK(silent == 0);
	CHECK(gs_sf2_preset(&font, 128, 0) >= 0);
	CHECK(strncmp(gs_sf2_preset_name(&font, gs_sf2_preset(&font, 0, 0)), "Grand Piano", 20) == 0);
	/* one zone a key: the low wave to 71, the high one above */
	CHECK(gs_sf2_zones(&font, gs_sf2_preset(&font, 0, 48), 60, 100, z, 4) == 1 && z[0].root == 48 && z[0].loopend - z[0].loopstart == 512);
	CHECK(gs_sf2_zones(&font, gs_sf2_preset(&font, 0, 48), 96, 100, z, 4) == 1 && z[0].root == 84 && z[0].loopend - z[0].loopstart == 64);

	/* every program sounds at middle C */
	gs_synth_init(&syn, &font, RATE, 32, GS_MODE_GM);
	for (p = 0; p < 128; p++) {
		gs_synth_reset(&syn);
		msg(0xC0, p, 0);
		msg(0x90, 60, 100);
		gs_synth_render(&syn, buf, RATE / 2);           /* past the slowest attack */
		pk = peak(RATE / 2);
		if (pk < 800) { quiet++; printf("program %d is quiet: %ld\n", p, pk); }
		if (pk > 20000) { quiet++; printf("program %d is too loud: %ld\n", p, pk); }
		msg(0x80, 60, 0);
		gs_synth_render(&syn, buf, RATE * 3 / 2 > RATE ? RATE : RATE);
		gs_synth_render(&syn, buf, RATE);
		if (gs_synth_active(&syn)) { quiet++; printf("program %d does not end: %d voices\n", p, gs_synth_active(&syn)); }
	}
	CHECK(quiet == 0);

	/* the kit: every General MIDI drum key sounds, and ends by itself */
	quiet = 0;
	for (k = 35; k <= 81; k++) {
		gs_synth_reset(&syn);
		msg(0x99, k, 110);
		gs_synth_render(&syn, buf, RATE / 10);
		pk = peak(RATE / 10);
		if (pk < 800 || pk > 26000) { quiet++; printf("drum key %d: %ld\n", k, pk); }
		gs_synth_render(&syn, buf, RATE);
		gs_synth_render(&syn, buf, RATE);
		if (gs_synth_active(&syn)) { quiet++; printf("drum key %d does not end\n", k); }
	}
	CHECK(quiet == 0);
	/* the closed hat stops the open one */
	gs_synth_reset(&syn);
	msg(0x99, 46, 100);
	gs_synth_render(&syn, buf, 64);
	CHECK(gs_synth_active(&syn) == 1);
	msg(0x99, 42, 100);
	gs_synth_render(&syn, buf, RATE / 20);
	CHECK(gs_synth_active(&syn) == 1);

	/* a full band, loud: no clipping */
	gs_synth_reset(&syn);
	msg(0xC0, 48, 0); msg(0xC1, 56, 0); msg(0xC2, 33, 0); msg(0xC3, 0, 0);
	msg(0x90, 60, 127); msg(0x90, 64, 127); msg(0x90, 67, 127); msg(0x90, 72, 127);
	msg(0x91, 76, 127); msg(0x91, 79, 127);
	msg(0x92, 36, 127);
	msg(0x93, 48, 127); msg(0x93, 55, 127); msg(0x93, 60, 127);
	msg(0x99, 36, 127); msg(0x99, 38, 127); msg(0x99, 49, 127);
	gs_synth_render(&syn, buf, RATE / 2);
	pk = peak(RATE / 2);
	printf("13 notes at full velocity peak at %ld\n", pk);
	CHECK(pk > 6000 && pk < 32767);

	free(data);
	printf("%s: basic.sf2 (%ld bytes), %d checks, %d failed\n", fails ? "FAIL" : "PASS", len, checks, fails);
	return fails != 0;
}
