/* make_basic_sf2: writes GFOpenSynth Basic, our own small General MIDI
 * SoundFont, so music plays where no other SoundFont is installed. Every
 * sample is made here from arithmetic (sums of sines, and noise from a
 * counter): nothing is recorded or taken from another SoundFont.
 *
 *   make_basic_sf2 OUT.sf2
 *
 * 128 presets in bank 0, one instrument for each General MIDI family (8
 * programs), each a looped waveform with its own envelope, in two versions:
 * many harmonics for keys up to 71, few above, so high notes do not alias.
 * Bank 128 preset 0 is a drum kit: kick, snare, toms, hats (one exclusive
 * class), cymbals and small percussion.
 *
 * A PC tool: the build runs it (build-amiga.sh) and the tests read what it
 * writes. MIT, Copyright (c) 2026 AmiCradle Team. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

enum {
	GEN_pan = 17, GEN_attackVolEnv = 34, GEN_decayVolEnv = 36, GEN_sustainVolEnv = 37,
	GEN_releaseVolEnv = 38, GEN_instrument = 41, GEN_keyRange = 43,
	GEN_initialAttenuation = 48, GEN_coarseTune = 51, GEN_sampleID = 53, GEN_sampleModes = 54,
	GEN_scaleTuning = 56, GEN_exclusiveClass = 57, GEN_overridingRootKey = 58
};

static unsigned char *sf;
static size_t sflen, sfcap;

static void put8(unsigned v)
{
	if (sflen == sfcap) {
		sfcap = sfcap ? sfcap * 2 : 1 << 16;
		if (!(sf = realloc(sf, sfcap))) { fprintf(stderr, "no memory\n"); exit(1); }
	}
	sf[sflen++] = (unsigned char)v;
}
static void put16(unsigned v) { put8(v & 255); put8((v >> 8) & 255); }
static void put32(unsigned long v) { put16((unsigned)(v & 0xFFFF)); put16((unsigned)(v >> 16)); }
static void putid(const char *id) { int i; for (i = 0; i < 4; i++) put8((unsigned char)id[i]); }
static void putname(const char *n) { int i, l = (int)strlen(n); for (i = 0; i < 20; i++) put8(i < l && i < 19 ? (unsigned char)n[i] : 0); }
static size_t open_chunk(const char *id) { size_t at; putid(id); at = sflen; put32(0); return at; }
static void close_chunk(size_t at)
{
	unsigned long n = (unsigned long)(sflen - at - 4);
	sf[at] = n & 255; sf[at + 1] = (n >> 8) & 255; sf[at + 2] = (n >> 16) & 255; sf[at + 3] = (n >> 24) & 255;
	if (n & 1) put8(0);
}
static size_t open_list(const char *type) { size_t at; putid("LIST"); at = sflen; put32(0); putid(type); return at; }
static void gen(int g, int v) { put16((unsigned)g); put16((unsigned)(short)v & 0xFFFF); }
static void gen_range(int g, int lo, int hi) { put16((unsigned)g); put8((unsigned)lo); put8((unsigned)hi); }
static int tc(double seconds) { return (int)lrint(1200.0 * log2(seconds)); }

/* ---- the samples ---- */

#define MAX_SAMPLES 32
static short *pcm;
static size_t npcm, pcmcap;
static struct smp { char name[20]; unsigned long start, end, ls, le, rate; int root; } smp[MAX_SAMPLES];
static int nsmp;

static void pcm_put(double v)
{
	if (npcm == pcmcap) {
		pcmcap = pcmcap ? pcmcap * 2 : 1 << 16;
		if (!(pcm = realloc(pcm, pcmcap * sizeof *pcm))) { fprintf(stderr, "no memory\n"); exit(1); }
	}
	if (v > 32767) v = 32767;
	if (v < -32768) v = -32768;
	pcm[npcm++] = (short)lrint(v);
}

static int smp_begin(const char *name, unsigned long rate, int root)
{
	struct smp *s = &smp[nsmp];
	memset(s, 0, sizeof *s);
	strncpy(s->name, name, 19);
	s->start = (unsigned long)npcm;
	s->rate = rate;
	s->root = root;
	return nsmp;
}
static void smp_end(unsigned long ls, unsigned long le)
{
	struct smp *s = &smp[nsmp++];
	int i;
	s->end = (unsigned long)npcm;
	s->ls = s->start + ls;
	s->le = s->start + le;
	for (i = 0; i < 46; i++) pcm_put(0);        /* the specification's silence after each sample */
}

/* the waveforms: harmonic h's level (0: not in it) */
enum { W_SINE, W_TRI, W_SAW, W_SQUARE, W_PULSE, W_ORGAN, W_BRIGHT, W_SOFT, W_COUNT };
static const char *wave_name[W_COUNT] = { "sine", "triangle", "saw", "square", "pulse", "organ", "bright", "soft" };

static double harmonic(int w, int h)
{
	switch (w) {
	case W_SINE:   return h == 1;
	case W_TRI:    return h & 1 ? ((h >> 1) & 1 ? -1.0 : 1.0) / (h * h) : 0;
	case W_SAW:    return 1.0 / h;
	case W_SQUARE: return h & 1 ? 1.0 / h : 0;
	case W_PULSE:  return sin(M_PI * h * 0.25) / h * 1.4;            /* a quarter wide */
	case W_ORGAN:  return h == 1 ? 1 : h == 2 ? 0.7 : h == 3 ? 0.5 : h == 4 ? 0.4 : h == 6 ? 0.25 : h == 8 ? 0.2 : 0;
	case W_BRIGHT: return 1.0 / sqrt(h) * (h > 12 ? 0.5 : 1);
	case W_SOFT:   return h == 1 ? 1 : h == 2 ? 0.35 : h == 3 ? 0.12 : h == 4 ? 0.05 : 0;
	}
	return 0;
}

/* a looped wave: cycles of len samples with up to maxh harmonics, as loud
 * as the others; the loop is its middle cycles */
#define WAVE_CYCLES 4
static int make_wave(int w, int len, int maxh, unsigned long rate, int root, const char *suffix)
{
	char name[24];
	double peak = 0, v;
	int i, h, id;
	double *t = malloc(sizeof *t * (size_t)len);
	for (i = 0; i < len; i++) {
		v = 0;
		for (h = 1; h <= maxh; h++)
			v += harmonic(w, h) * sin(2 * M_PI * h * i / len);
		t[i] = v;
		if (fabs(v) > peak) peak = fabs(v);
	}
	snprintf(name, sizeof name, "%s %s", wave_name[w], suffix);
	id = smp_begin(name, rate, root);
	for (h = 0; h < WAVE_CYCLES; h++)
		for (i = 0; i < len; i++)
			pcm_put(t[i] / peak * 11000);
	smp_end((unsigned long)len, (unsigned long)len * (WAVE_CYCLES - 1));
	free(t);
	return id;
}

/* noise from a counter (the same file every build) */
static unsigned long seed = 0x1234567;
static double noise(void)
{
	seed = seed * 1103515245UL + 12345UL;
	return (double)((seed >> 16) & 0x7FFF) / 16384.0 - 1.0;
}

#define DRUM_RATE 22050
/* a drum: a sine falling from f0 to f1 Hz (tone), noise (hiss, high: its
 * low part taken out), both dying over seconds */
static int make_drum(const char *name, double seconds, double f0, double f1, double tone, double hiss, int high)
{
	int n = (int)(seconds * DRUM_RATE), i, id = smp_begin(name, DRUM_RATE, 60);
	double ph = 0, last = 0, lp = 0;
	for (i = 0; i < n; i++) {
		double t = (double)i / n, e = exp(-6.0 * t), f = f1 + (f0 - f1) * exp(-18.0 * t), x = noise(), hn;
		ph += 2 * M_PI * f / DRUM_RATE;
		if (high) { hn = x - last; last = x; }
		else { lp += 0.35 * (x - lp); hn = lp * 1.6; }
		pcm_put((tone * sin(ph) * e + hiss * hn * e * (high ? 0.6 : 1.0)) * 14000 * (i < 8 ? i / 8.0 : 1.0));
	}
	smp_end(0, 0);
	return id;
}

/* ---- the instruments ---- */

struct family { const char *name; int wave; double attack, decay; int sustain; double release; int atten; };
/* decay: seconds to fall the envelope's whole 100 dB; sustain: cB below full (1000: dies away) */
static const struct family fam[16] = {
	{ "Piano",      W_SOFT,   0.002, 5.0, 1000, 0.35, 0 },
	{ "Chromatic",  W_SINE,   0.002, 3.0, 1000, 0.50, 0 },
	{ "Organ",      W_ORGAN,  0.006, 0.5,    0, 0.07, 60 },
	{ "Guitar",     W_SAW,    0.002, 3.5, 1000, 0.25, 40 },
	{ "Bass",       W_SOFT,   0.003, 6.0,  400, 0.10, 0 },
	{ "Strings",    W_SAW,    0.070, 1.0,   30, 0.25, 80 },
	{ "Ensemble",   W_SAW,    0.110, 1.0,   30, 0.35, 80 },
	{ "Brass",      W_BRIGHT, 0.030, 1.5,   60, 0.12, 80 },
	{ "Reed",       W_SQUARE, 0.020, 1.0,   30, 0.10, 80 },
	{ "Pipe",       W_SOFT,   0.040, 1.0,   20, 0.12, 20 },
	{ "Synth Lead", W_PULSE,  0.004, 1.0,   30, 0.10, 80 },
	{ "Synth Pad",  W_TRI,    0.300, 2.0,   40, 0.70, 20 },
	{ "Synth FX",   W_PULSE,  0.100, 6.0,  300, 0.70, 80 },
	{ "Ethnic",     W_SAW,    0.002, 2.5, 1000, 0.25, 40 },
	{ "Percussive", W_SINE,   0.002, 1.2, 1000, 0.25, 0 },
	{ "Effects",    W_TRI,    0.050, 4.0,  300, 0.50, 40 },
};

static const char *gm_name[128] = {
	"Grand Piano", "Bright Piano", "Electric Grand", "Honky-tonk", "Electric Piano 1", "Electric Piano 2", "Harpsichord", "Clavi",
	"Celesta", "Glockenspiel", "Music Box", "Vibraphone", "Marimba", "Xylophone", "Tubular Bells", "Dulcimer",
	"Drawbar Organ", "Percussive Organ", "Rock Organ", "Church Organ", "Reed Organ", "Accordion", "Harmonica", "Tango Accordion",
	"Nylon Guitar", "Steel Guitar", "Jazz Guitar", "Clean Guitar", "Muted Guitar", "Overdriven Guitar", "Distortion Guitar", "Guitar Harmonics",
	"Acoustic Bass", "Finger Bass", "Pick Bass", "Fretless Bass", "Slap Bass 1", "Slap Bass 2", "Synth Bass 1", "Synth Bass 2",
	"Violin", "Viola", "Cello", "Contrabass", "Tremolo Strings", "Pizzicato Strings", "Harp", "Timpani",
	"String Ensemble 1", "String Ensemble 2", "Synth Strings 1", "Synth Strings 2", "Choir Aahs", "Voice Oohs", "Synth Voice", "Orchestra Hit",
	"Trumpet", "Trombone", "Tuba", "Muted Trumpet", "French Horn", "Brass Section", "Synth Brass 1", "Synth Brass 2",
	"Soprano Sax", "Alto Sax", "Tenor Sax", "Baritone Sax", "Oboe", "English Horn", "Bassoon", "Clarinet",
	"Piccolo", "Flute", "Recorder", "Pan Flute", "Blown Bottle", "Shakuhachi", "Whistle", "Ocarina",
	"Square Lead", "Saw Lead", "Calliope Lead", "Chiff Lead", "Charang Lead", "Voice Lead", "Fifths Lead", "Bass and Lead",
	"New Age Pad", "Warm Pad", "Polysynth Pad", "Choir Pad", "Bowed Pad", "Metallic Pad", "Halo Pad", "Sweep Pad",
	"Rain", "Soundtrack", "Crystal", "Atmosphere", "Brightness", "Goblins", "Echoes", "Sci-fi",
	"Sitar", "Banjo", "Shamisen", "Koto", "Kalimba", "Bag Pipe", "Fiddle", "Shanai",
	"Tinkle Bell", "Agogo", "Steel Drums", "Woodblock", "Taiko Drum", "Melodic Tom", "Synth Drum", "Reverse Cymbal",
	"Fret Noise", "Breath Noise", "Seashore", "Bird Tweet", "Telephone Ring", "Helicopter", "Applause", "Gunshot"
};

/* the instrument a program plays: its family's, but for the few a family's
 * one wave suits badly */
static int program_instrument(int p)
{
	switch (p) {
	case 45: case 46: return 3;     /* pizzicato strings, harp: plucked */
	case 47: return 14;             /* timpani: struck */
	case 80: return 8;              /* square lead */
	case 81: case 62: case 63: return 7;   /* saw lead, synth brass */
	case 38: case 39: return 10;    /* synth bass */
	}
	return p / 8;
}

/* the kit: keys lo..hi play sample: moving with the key around root
 * (follow 1), or every key the same, 60 - root semitones up (follow 0) */
struct kit { int lo, hi, sample, root, follow, excl, atten, pan; };

int main(int argc, char **argv)
{
	int lo[W_COUNT], hi[W_COUNT], w, i, p, nkit = 0;
	int kick, snare, tom, hat, openhat, crash, ride, perc, clap;
	struct kit kit[24];
	size_t riff, list, ck;
	unsigned bag;
	FILE *f;

	if (argc != 2) { fprintf(stderr, "make_basic_sf2 OUT.sf2\n"); return 1; }
	for (w = 0; w < W_COUNT; w++) {
		lo[w] = make_wave(w, 256, 24, 33488, 48, "low");     /* 256 samples a cycle at 33488 Hz: key 48 */
		hi[w] = make_wave(w, 32, 5, 33488, 84, "high");      /* 32 a cycle: key 84 */
	}
	kick    = make_drum("kick",     0.30, 160,  45, 1.0, 0.10, 0);
	snare   = make_drum("snare",    0.22, 240, 170, 0.5, 0.90, 0);
	clap    = make_drum("clap",     0.16,   0,   0, 0.0, 1.00, 0);
	tom     = make_drum("tom",      0.35, 190, 110, 1.0, 0.08, 0);
	hat     = make_drum("hat",      0.06,   0,   0, 0.0, 1.00, 1);
	openhat = make_drum("open hat", 0.40,   0,   0, 0.0, 1.00, 1);
	crash   = make_drum("crash",    1.40,   0,   0, 0.0, 1.00, 1);
	ride    = make_drum("ride",     0.70, 5200, 5200, 0.25, 0.70, 1);
	perc    = make_drum("click",    0.09, 900, 700, 0.6, 0.50, 1);

#define KIT(l, h, s, r, fo, ex, at, pn) do { struct kit k = { l, h, s, r, fo, ex, at, pn }; kit[nkit++] = k; } while (0)
	KIT(0, 34, perc, 60, 0, 0, 100, 0);
	KIT(35, 36, kick, 60, 0, 0, 0, 0);
	KIT(37, 37, perc, 60, 0, 0, 40, 0);
	KIT(38, 38, snare, 60, 0, 0, 0, 0);
	KIT(39, 39, clap, 60, 0, 0, 40, 0);
	KIT(40, 40, snare, 58, 0, 0, 0, 0);
	KIT(41, 41, tom, 48, 1, 0, 0, -200);
	KIT(42, 42, hat, 60, 0, 1, 60, 150);
	KIT(43, 43, tom, 48, 1, 0, 0, -120);
	KIT(44, 44, hat, 60, 0, 1, 80, 150);
	KIT(45, 45, tom, 48, 1, 0, 0, -40);
	KIT(46, 46, openhat, 60, 0, 1, 60, 150);
	KIT(47, 48, tom, 48, 1, 0, 0, 60);
	KIT(49, 49, crash, 60, 0, 0, 40, -150);
	KIT(50, 50, tom, 48, 1, 0, 0, 180);
	KIT(51, 51, ride, 60, 0, 0, 60, 200);
	KIT(52, 52, crash, 57, 0, 0, 40, 100);
	KIT(53, 53, ride, 63, 0, 0, 60, 200);
	KIT(54, 54, hat, 60, 0, 0, 60, -100);
	KIT(55, 55, crash, 65, 0, 0, 60, 0);
	KIT(56, 56, perc, 62, 0, 0, 40, 100);
	KIT(57, 57, crash, 62, 0, 0, 40, 150);
	KIT(58, 59, ride, 58, 0, 0, 60, -100);
	KIT(60, 127, perc, 70, 1, 0, 60, 0);

	riff = sflen; putid("RIFF"); riff = sflen; put32(0); putid("sfbk");
	list = open_list("INFO");
	ck = open_chunk("ifil"); put16(2); put16(4); close_chunk(ck);
	ck = open_chunk("isng"); putid("EMU8"); putid("000"); put8(0); close_chunk(ck);
	ck = open_chunk("INAM"); putname("GFOpenSynth Basic"); close_chunk(ck);
	ck = open_chunk("ICOP"); { const char *c = "MIT, Copyright (c) 2026 AmiCradle Team"; size_t n = strlen(c) + 1, k; for (k = 0; k < n; k++) put8((unsigned char)c[k]); } close_chunk(ck);
	close_chunk(list);

	list = open_list("sdta");
	ck = open_chunk("smpl");
	for (i = 0; i < (int)npcm; i++) put16((unsigned)pcm[i] & 0xFFFF);
	close_chunk(ck);
	close_chunk(list);

	list = open_list("pdta");
	/* presets: one zone each, naming its instrument; then the kit */
	ck = open_chunk("phdr");
	for (p = 0; p < 128; p++) { putname(gm_name[p]); put16((unsigned)p); put16(0); put16((unsigned)p); put32(0); put32(0); put32(0); }
	putname("Basic Kit"); put16(0); put16(128); put16(128); put32(0); put32(0); put32(0);
	putname("EOP"); put16(0); put16(0); put16(129); put32(0); put32(0); put32(0);
	close_chunk(ck);
	ck = open_chunk("pbag"); for (p = 0; p <= 129; p++) { put16((unsigned)p); put16(0); } close_chunk(ck);
	ck = open_chunk("pmod"); for (i = 0; i < 10; i++) put8(0); close_chunk(ck);
	ck = open_chunk("pgen");
	for (p = 0; p < 128; p++) gen(GEN_instrument, program_instrument(p));
	gen(GEN_instrument, 16);
	gen(0, 0);
	close_chunk(ck);
	/* instruments: the 16 families (a global zone with the envelope, then
	 * the low and the high wave), then the kit (a zone a row) */
	ck = open_chunk("inst");
	for (i = 0; i < 16; i++) { putname(fam[i].name); put16((unsigned)(i * 3)); }
	putname("Basic Kit"); put16(48);
	putname("EOI"); put16((unsigned)(48 + nkit));
	close_chunk(ck);
	ck = open_chunk("ibag");
	bag = 0;
	for (i = 0; i < 16; i++) { put16(bag); put16(0); bag += 6; put16(bag); put16(0); bag += 3; put16(bag); put16(0); bag += 3; }
	for (i = 0; i < nkit; i++) { put16(bag); put16(0); bag += 9; }
	put16(bag); put16(0);
	close_chunk(ck);
	ck = open_chunk("imod"); for (i = 0; i < 10; i++) put8(0); close_chunk(ck);
	ck = open_chunk("igen");
	for (i = 0; i < 16; i++) {
		gen(GEN_attackVolEnv, tc(fam[i].attack));
		gen(GEN_decayVolEnv, tc(fam[i].decay));
		gen(GEN_sustainVolEnv, fam[i].sustain);
		gen(GEN_releaseVolEnv, tc(fam[i].release));
		gen(GEN_initialAttenuation, fam[i].atten);
		gen(GEN_sampleModes, 1);
		gen_range(GEN_keyRange, 0, 71); gen(GEN_overridingRootKey, 48); gen(GEN_sampleID, lo[fam[i].wave]);
		gen_range(GEN_keyRange, 72, 127); gen(GEN_overridingRootKey, 84); gen(GEN_sampleID, hi[fam[i].wave]);
	}
	for (i = 0; i < nkit; i++) {
		gen_range(GEN_keyRange, kit[i].lo, kit[i].hi);
		gen(GEN_pan, kit[i].pan);
		gen(GEN_releaseVolEnv, tc(0.8));                 /* a drum rings on after its key is let go */
		gen(GEN_initialAttenuation, kit[i].atten);
		gen(GEN_coarseTune, kit[i].follow ? 0 : 60 - kit[i].root);
		gen(GEN_scaleTuning, kit[i].follow ? 100 : 0);
		gen(GEN_exclusiveClass, kit[i].excl);
		gen(GEN_overridingRootKey, kit[i].follow ? kit[i].root : 60);
		gen(GEN_sampleID, kit[i].sample);
	}
	gen(0, 0);
	close_chunk(ck);
	ck = open_chunk("shdr");
	for (i = 0; i < nsmp; i++) {
		putname(smp[i].name); put32(smp[i].start); put32(smp[i].end); put32(smp[i].ls); put32(smp[i].le);
		put32(smp[i].rate); put8((unsigned)smp[i].root); put8(0); put16(0); put16(1);
	}
	putname("EOS"); put32(0); put32(0); put32(0); put32(0); put32(0); put8(0); put8(0); put16(0); put16(0);
	close_chunk(ck);
	close_chunk(list);
	close_chunk(riff);

	if (!(f = fopen(argv[1], "wb")) || fwrite(sf, 1, sflen, f) != sflen || fclose(f)) {
		fprintf(stderr, "make_basic_sf2: cannot write %s\n", argv[1]);
		return 1;
	}
	printf("%s: %lu bytes, 129 presets, %d samples\n", argv[1], (unsigned long)sflen, nsmp);
	return 0;
}
