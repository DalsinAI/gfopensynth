/* smf2wav: plays a MIDI file through the engine into a WAV file, on a PC:
 * the same source the library runs on the 68k and the port runs in the
 * machine, so this is how a file sounds there.
 *   smf2wav SOUNDFONT.sf2 IN.mid OUT.wav [RATE] [MT32]
 * MIT, Copyright (c) 2026 AmiCradle Team. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "gs_synth.h"

static unsigned char *slurp(const char *name, unsigned long *len)
{
	FILE *f = fopen(name, "rb");
	unsigned char *d;
	long n;
	if (!f) { fprintf(stderr, "smf2wav: cannot read %s\n", name); exit(1); }
	fseek(f, 0, SEEK_END); n = ftell(f); fseek(f, 0, SEEK_SET);
	if (n <= 0 || !(d = malloc((size_t)n)) || fread(d, 1, (size_t)n, f) != (size_t)n) { fprintf(stderr, "smf2wav: cannot read %s\n", name); exit(1); }
	fclose(f);
	*len = (unsigned long)n;
	return d;
}

static void le32(FILE *f, unsigned long v) { fputc((int)(v & 255), f); fputc((int)((v >> 8) & 255), f); fputc((int)((v >> 16) & 255), f); fputc((int)((v >> 24) & 255), f); }
static void le16(FILE *f, unsigned v) { fputc((int)(v & 255), f); fputc((int)((v >> 8) & 255), f); }

int main(int argc, char **argv)
{
	static struct gs_sf2 font;
	static struct gs_synth syn;
	static struct gs_seq seq;
	static int16_t buf[2 * 1024];
	unsigned long sflen, midlen, frames = 0, rate = 22050, tail, peak = 0;
	unsigned char *sf, *mid;
	int mt32 = 0, i;
	FILE *out;

	if (argc < 4) { fprintf(stderr, "smf2wav SOUNDFONT.sf2 IN.mid OUT.wav [RATE] [MT32]\n"); return 1; }
	for (i = 4; i < argc; i++) {
		if (!strcmp(argv[i], "MT32")) mt32 = 1;
		else rate = strtoul(argv[i], 0, 10);
	}
	if (rate < 8000 || rate > 48000) rate = 22050;
	sf = slurp(argv[1], &sflen);
	mid = slurp(argv[2], &midlen);
	if (gs_sf2_open(&font, sf, (uint32_t)sflen)) { fprintf(stderr, "smf2wav: %s is not a SoundFont\n", argv[1]); return 1; }
	gs_synth_init(&syn, &font, (uint32_t)rate, 48, mt32 ? GS_MODE_MT32_GM : GS_MODE_GM);
	if (gs_seq_open(&seq, mid, (uint32_t)midlen, (uint32_t)rate, 0)) { fprintf(stderr, "smf2wav: %s is not a MIDI file\n", argv[2]); return 1; }
	if (!(out = fopen(argv[3], "wb"))) { fprintf(stderr, "smf2wav: cannot write %s\n", argv[3]); return 1; }
	for (i = 0; i < 44; i++) fputc(0, out);
	tail = rate * 2;                                /* the last notes' release */
	while (!seq.ended || (tail && gs_synth_active(&syn))) {
		unsigned n = 1024, j;
		gs_seq_render(&seq, &syn, buf, n);
		if (seq.ended) tail = tail > n ? tail - n : 0;
		for (j = 0; j < n * 2; j++) {
			unsigned long a = (unsigned long)(buf[j] < 0 ? -(long)buf[j] : buf[j]);
			if (a > peak) peak = a;
			le16(out, (unsigned)buf[j] & 0xFFFF);
		}
		frames += n;
		if (frames > rate * 3600) break;
	}
	fseek(out, 0, SEEK_SET);
	fwrite("RIFF", 1, 4, out); le32(out, 36 + frames * 4); fwrite("WAVEfmt ", 1, 8, out);
	le32(out, 16); le16(out, 1); le16(out, 2); le32(out, rate); le32(out, rate * 4); le16(out, 4); le16(out, 16);
	fwrite("data", 1, 4, out); le32(out, frames * 4);
	fclose(out);
	printf("%s: %lu.%lu s at %lu Hz, peak %lu of 32767\n", argv[3], frames / rate, frames % rate * 10 / rate, rate, peak);
	return 0;
}
