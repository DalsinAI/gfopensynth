/* midi_collection_test: every file of the MIDI collection (midi/) opens in
 * the engine's player and sounds within its first seconds through
 * GFOpenSynth Basic.
 *   midi_collection_test SOUNDFONT.sf2 FILE.mid...
 * MIT, Copyright (c) 2026 AmiCradle Team. */
#include <stdio.h>
#include <stdlib.h>
#include "gs_synth.h"

#define RATE 11025u
#define SECONDS 20u

static unsigned char *slurp(const char *name, unsigned long *len)
{
	FILE *f = fopen(name, "rb");
	unsigned char *d = 0;
	long n = 0;
	if (f) { fseek(f, 0, SEEK_END); n = ftell(f); fseek(f, 0, SEEK_SET); }
	if (!f || n <= 0 || !(d = malloc((size_t)n)) || fread(d, 1, (size_t)n, f) != (size_t)n) { fprintf(stderr, "cannot read %s\n", name); exit(1); }
	fclose(f);
	*len = (unsigned long)n;
	return d;
}

int main(int argc, char **argv)
{
	static struct gs_sf2 font;
	static struct gs_synth syn;
	static struct gs_seq seq;
	static int16_t buf[2 * 1024];
	unsigned long sflen, len;
	unsigned char *sf, *mid;
	int i, bad = 0;

	if (argc < 3) { fprintf(stderr, "midi_collection_test SOUNDFONT.sf2 FILE.mid...\n"); return 1; }
	sf = slurp(argv[1], &sflen);
	if (gs_sf2_open(&font, sf, (uint32_t)sflen)) { fprintf(stderr, "%s is not a SoundFont\n", argv[1]); return 1; }
	for (i = 2; i < argc; i++) {
		unsigned long frames = 0;
		int heard = 0;
		mid = slurp(argv[i], &len);
		gs_synth_init(&syn, &font, RATE, 48, GS_MODE_GM);
		if (gs_seq_open(&seq, mid, (uint32_t)len, RATE, 0)) {
			printf("FAIL: %s is not a MIDI file the player takes\n", argv[i]);
			bad++; free(mid);
			continue;
		}
		while (!seq.ended && !heard && frames < RATE * SECONDS) {
			unsigned j;
			gs_seq_render(&seq, &syn, buf, 1024);
			frames += 1024;
			for (j = 0; j < 2048; j++) if (buf[j]) { heard = 1; break; }
		}
		if (!heard) { printf("FAIL: %s is silent for its first %u seconds\n", argv[i], SECONDS); bad++; }
		free(mid);
	}
	if (bad) { printf("FAIL: %d of %d files\n", bad, argc - 2); return 1; }
	printf("PASS: %d collection files open and sound\n", argc - 2);
	return 0;
}
