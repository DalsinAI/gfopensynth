/* gfopensynth PC tests: the MIDI parser, SMF reader and player timing, ROM
 * identification and joining (with made-up bytes: no real ROM is ever in
 * the repo or the cloud), the SoundFont reader on a SoundFont this test
 * writes itself, and the synth's output: pitch, envelope, volume, pan,
 * bend, sustain, exclusive class, MT-32 mapping and voice stealing.
 * MIT, Copyright (c) 2026 AmiCradle Team. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "gs_midi.h"
#include "gs_rom.h"
#include "gs_sf2.h"
#include "gs_synth.h"
#include "gs_tables.h"

static int fails, checks;
#define CHECK(c) do { checks++; if (!(c)) { fails++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)

/* ---- a SoundFont written here ---- */

static unsigned char sf[1 << 20];
static size_t sflen;

static void put8(unsigned v) { sf[sflen++] = (unsigned char)v; }
static void put16(unsigned v) { put8(v & 255); put8(v >> 8); }
static void put32(unsigned long v) { put16(v & 0xFFFF); put16(v >> 16); }
static void putid(const char *id) { int i; for (i = 0; i < 4; i++) put8((unsigned char)id[i]); }
static void putname(const char *n) { int i, l = (int)strlen(n); for (i = 0; i < 20; i++) put8(i < l ? (unsigned char)n[i] : 0); }
static size_t open_chunk(const char *id) { size_t at; putid(id); at = sflen; put32(0); return at; }
static void close_chunk(size_t at)
{
	unsigned long n = (unsigned long)(sflen - at - 4);
	sf[at] = n & 255; sf[at + 1] = (n >> 8) & 255; sf[at + 2] = (n >> 16) & 255; sf[at + 3] = (n >> 24) & 255;
	if (n & 1) put8(0);
}

#define SINE_LEN 100      /* one cycle at 44100 Hz: 441 Hz; root 69 (440) with -4 cents is close */
#define NOISE_LEN 2000

/* Two presets: 0:0 "Sine" (looped sine, root 69, release 0.1 s, sustain
 * -6 dB) and 128:0 "Kit" with key 42 (closed hat, class 1) and key 46 (open
 * hat, class 1), each a one-shot click. And 0:1 "Quiet": the sine with
 * 20 dB initial attenuation (40 dB as 0.4 of the specification's 0.1 dB). */
static void make_sf2(void)
{
	size_t riff, list, ck;
	int i;
	sflen = 0;
	putid("RIFF"); riff = sflen; put32(0); putid("sfbk");
	putid("LIST"); list = sflen; put32(0); putid("INFO");
	ck = open_chunk("ifil"); put16(2); put16(4); close_chunk(ck);
	ck = open_chunk("INAM"); putname("gfopensynth test"); close_chunk(ck);
	close_chunk(list);
	putid("LIST"); list = sflen; put32(0); putid("sdta");
	ck = open_chunk("smpl");
	for (i = 0; i < SINE_LEN; i++) put16((unsigned)(short)lrint(16000 * sin(2 * M_PI * i / SINE_LEN)) & 0xFFFF);
	for (i = 0; i < 46; i++) put16(0);
	for (i = 0; i < NOISE_LEN; i++) put16((unsigned)(short)((i * 7919 % 2001) * 10 - 10000) & 0xFFFF);
	for (i = 0; i < 46; i++) put16(0);
	close_chunk(ck);
	close_chunk(list);
	putid("LIST"); list = sflen; put32(0); putid("pdta");
	/* presets: Sine (bag 0), Quiet (bag 1), Kit (bag 2), EOP (bag 3) */
	ck = open_chunk("phdr");
	putname("Sine"); put16(0); put16(0); put16(0); put32(0); put32(0); put32(0);
	putname("Quiet"); put16(1); put16(0); put16(1); put32(0); put32(0); put32(0);
	putname("Kit"); put16(0); put16(128); put16(2); put32(0); put32(0); put32(0);
	putname("EOP"); put16(0); put16(0); put16(3); put32(0); put32(0); put32(0);
	close_chunk(ck);
	ck = open_chunk("pbag"); put16(0); put16(0); put16(1); put16(0); put16(3); put16(0); put16(4); put16(0); close_chunk(ck);
	ck = open_chunk("pmod"); for (i = 0; i < 10; i++) put8(0); close_chunk(ck);
	ck = open_chunk("pgen");
	put16(GEN_instrument); put16(0);                     /* Sine -> inst 0 */
	put16(GEN_initialAttenuation); put16(500);           /* Quiet: 500 * 0.4 = 200 cB */
	put16(GEN_instrument); put16(0);
	put16(GEN_instrument); put16(1);                     /* Kit -> inst 1 */
	put16(0); put16(0);                                  /* terminal */
	close_chunk(ck);
	/* instruments: Tone (bags 0), Hats (bags 1-2), EOI (bag 3) */
	ck = open_chunk("inst"); putname("Tone"); put16(0); putname("Hats"); put16(1); putname("EOI"); put16(3); close_chunk(ck);
	ck = open_chunk("ibag"); put16(0); put16(0); put16(6); put16(0); put16(10); put16(0); put16(14); put16(0); close_chunk(ck);
	ck = open_chunk("imod"); for (i = 0; i < 10; i++) put8(0); close_chunk(ck);
	ck = open_chunk("igen");
	/* Tone: 6 */
	put16(GEN_overridingRootKey); put16(69);
	put16(GEN_releaseVolEnv); put16((unsigned)(short)-3986 & 0xFFFF);   /* 0.1 s */
	put16(GEN_sustainVolEnv); put16(60);                 /* 6 dB down */
	put16(GEN_decayVolEnv); put16((unsigned)(short)-7973 & 0xFFFF);     /* 0.01 s */
	put16(GEN_sampleModes); put16(1);
	put16(GEN_sampleID); put16(0);
	/* closed hat: 4 */
	put16(GEN_keyRange); put8(42); put8(42);
	put16(GEN_exclusiveClass); put16(1);
	put16(GEN_overridingRootKey); put16(42);
	put16(GEN_sampleID); put16(1);
	/* open hat: 4 */
	put16(GEN_keyRange); put8(46); put8(46);
	put16(GEN_exclusiveClass); put16(1);
	put16(GEN_overridingRootKey); put16(46);
	put16(GEN_sampleID); put16(1);
	put16(0); put16(0);
	close_chunk(ck);
	ck = open_chunk("shdr");
	putname("sine"); put32(0); put32(SINE_LEN); put32(0); put32(SINE_LEN); put32(44100); put8(69); put8(0); put16(0); put16(1);
	putname("click"); put32(SINE_LEN + 46); put32(SINE_LEN + 46 + NOISE_LEN); put32(0); put32(0); put32(22050); put8(60); put8(0); put16(0); put16(1);
	putname("EOS"); put32(0); put32(0); put32(0); put32(0); put32(0); put8(0); put8(0); put16(0); put16(0);
	close_chunk(ck);
	close_chunk(list);
	close_chunk(riff);
}

/* ---- helpers ---- */

#define RATE 22050
static short buf[RATE * 2 * 4];

static void msg(struct gs_synth *s, int st, int d1, int d2)
{
	struct gs_msg m;
	m.status = (unsigned char)st; m.d1 = (unsigned char)d1; m.d2 = (unsigned char)d2; m.len = 3;
	gs_synth_msg(s, &m);
}

/* rising zero crossings of one channel over frames */
static int crossings(const short *b, int frames, int ch)
{
	int i, n = 0;
	for (i = 1; i < frames; i++)
		if (b[(i - 1) * 2 + ch] < 0 && b[i * 2 + ch] >= 0)
			n++;
	return n;
}

static long peak(const short *b, int frames, int ch)
{
	long m = 0;
	int i;
	for (i = 0; i < frames; i++) {
		long v = labs((long)b[i * 2 + ch]);
		if (v > m) m = v;
	}
	return m;
}

/* ---- tests ---- */

static void test_tables(void)
{
	CHECK(gs_exp2_cents(0) == 65536);
	CHECK(gs_exp2_cents(1200) == 131072);
	CHECK(gs_exp2_cents(-1200) == 32768);
	CHECK(abs((int)gs_exp2_cents(700) - (int)lrint(65536 * pow(2, 700 / 1200.0))) <= 1);
	CHECK(abs((int)gs_exp2_cents(-500) - (int)lrint(65536 * pow(2, -500 / 1200.0))) <= 1);
	CHECK(gs_cb_gain_q15[0] == 32767 && gs_cb_gain_q15[60] == 16423 && gs_cb_gain_q15[1440] == 0);
	CHECK(gs_curve_cb[127] == 0 && gs_curve_cb[0] == 1440);
	CHECK(gs_pan_q15[0] == 0 && gs_pan_q15[128] == 32767);
}

static void test_parser(void)
{
	struct gs_midi_in p;
	struct gs_msg m;
	static const unsigned char run[] = { 0x90, 60, 100, 62, 101, 0xF8, 64, 0xFE, 102 };
	int i, n = 0, r;
	gs_midi_in_init(&p);
	for (i = 0; i < (int)sizeof run; i++) {
		r = gs_midi_in_byte(&p, run[i], &m);
		if (r == GS_IN_MSG) {
			n++;
			CHECK(m.status == 0x90 && m.d1 == 60 + (n - 1) * 2 && m.d2 == 99 + n);
		}
	}
	CHECK(n == 3);
	/* program change: one data byte, running */
	CHECK(gs_midi_in_byte(&p, 0xC3, &m) == GS_IN_NONE);
	CHECK(gs_midi_in_byte(&p, 5, &m) == GS_IN_MSG && m.status == 0xC3 && m.d1 == 5 && m.len == 2);
	CHECK(gs_midi_in_byte(&p, 6, &m) == GS_IN_MSG && m.d1 == 6);
	/* SysEx, with a clock inside */
	{
		static const unsigned char sx[] = { 0xF0, 0x7E, 0x7F, 0xF8, 0x09, 0x01, 0xF7 };
		r = 0;
		for (i = 0; i < (int)sizeof sx; i++)
			r = gs_midi_in_byte(&p, sx[i], &m);
		CHECK(r == GS_IN_SYSEX && p.sxlen == 6 && p.sysex[0] == 0xF0 && p.sysex[3] == 0x09 && p.sysex[5] == 0xF7);
	}
	/* SysEx clears running status: data after it is dropped */
	CHECK(gs_midi_in_byte(&p, 7, &m) == GS_IN_NONE);
	/* a SysEx cut short by a status byte is dropped, and that status works */
	gs_midi_in_byte(&p, 0xF0, &m);
	gs_midi_in_byte(&p, 0x41, &m);
	CHECK(gs_midi_in_byte(&p, 0x80, &m) == GS_IN_NONE);
	gs_midi_in_byte(&p, 60, &m);
	CHECK(gs_midi_in_byte(&p, 0, &m) == GS_IN_MSG && m.status == 0x80);
	/* a too-long SysEx is dropped whole */
	gs_midi_in_byte(&p, 0xF0, &m);
	for (i = 0; i < GS_SYSEX_MAX + 10; i++)
		gs_midi_in_byte(&p, 1, &m);
	CHECK(gs_midi_in_byte(&p, 0xF7, &m) == GS_IN_NONE);
	/* song position (system common) is parsed and not passed on */
	gs_midi_in_byte(&p, 0xF2, &m);
	gs_midi_in_byte(&p, 1, &m);
	CHECK(gs_midi_in_byte(&p, 2, &m) == GS_IN_NONE);
	CHECK(gs_midi_in_byte(&p, 3, &m) == GS_IN_NONE);   /* running status cleared */
}

/* a format 1 SMF: track 0 tempo 120 then 60 bpm at tick 96; track 1 note
 * on at 0, off at 96, on at 192 (running status), off at 288; division 96 */
static unsigned char smf[256];
static int smflen;

static void make_smf(void)
{
	static const unsigned char head[] = { 'M','T','h','d',0,0,0,6, 0,1, 0,2, 0,96 };
	static const unsigned char t0[] = { 0x00, 0xFF, 0x51, 3, 0x07, 0xA1, 0x20,   /* 500000 */
	                                    0x60, 0xFF, 0x51, 3, 0x0F, 0x42, 0x40,   /* 1000000 at 96 */
	                                    0x00, 0xFF, 0x2F, 0 };
	static const unsigned char t1[] = { 0x00, 0x90, 69, 100,
	                                    0x60, 0x80, 69, 0,
	                                    0x60, 0xF0, 0x05, 0x7E, 0x7F, 0x09, 0x01, 0xF7,
	                                    0x00, 0x90, 69, 100,
	                                    0x60, 0x90, 69, 0,
	                                    0x00, 0xFF, 0x2F, 0 };
	memcpy(smf, head, sizeof head); smflen = sizeof head;
	memcpy(smf + smflen, "MTrk\0\0\0", 7); smf[smflen + 7] = sizeof t0; smflen += 8;
	memcpy(smf + smflen, t0, sizeof t0); smflen += sizeof t0;
	memcpy(smf + smflen, "MTrk\0\0\0", 7); smf[smflen + 7] = sizeof t1; smflen += 8;
	memcpy(smf + smflen, t1, sizeof t1); smflen += sizeof t1;
}

static void test_smf(void)
{
	struct gs_smf s;
	struct gs_smf_event e;
	struct gs_seq q;
	unsigned long ticks[16];
	int types[16], n = 0;
	make_smf();
	CHECK(gs_smf_open(&s, smf, (unsigned)smflen) == 0);
	CHECK(s.ntracks == 2 && s.division == 96 && s.format == 1);
	for (;;) {
		gs_smf_next(&s, &e);
		if (e.type == GS_EV_END || n == 16) break;
		ticks[n] = e.tick; types[n] = e.type; n++;
	}
	CHECK(n == 7);
	CHECK(types[0] == GS_EV_TEMPO && ticks[0] == 0);
	CHECK(types[1] == GS_EV_MSG && ticks[1] == 0);
	CHECK(types[2] == GS_EV_TEMPO && ticks[2] == 96);   /* track 0 first at equal ticks */
	CHECK(types[3] == GS_EV_MSG && ticks[3] == 96);
	CHECK(types[4] == GS_EV_SYSEX && ticks[4] == 192);
	CHECK(types[5] == GS_EV_MSG && ticks[5] == 192);
	CHECK(types[6] == GS_EV_MSG && ticks[6] == 288);
	CHECK(gs_smf_open(&s, (const unsigned char *)"RIFF0000", 8) == -1);
	/* a cut file plays what it has */
	CHECK(gs_smf_open(&s, smf, (unsigned)smflen - 5) == 0);
	/* timing: 96 ticks at 120 bpm = 0.5 s, then 192 at 60 bpm = 2 s: 2.5 s */
	CHECK(gs_seq_open(&q, smf, (unsigned)smflen, RATE, 0) == 0);
	{
		unsigned long long len = gs_seq_length(&q);
		CHECK(len == (unsigned long long)RATE * 5 / 2);
	}
}

static void test_rom(void)
{
	static const unsigned char abc_sha1[20] = { 0xa9,0x99,0x3e,0x36,0x47,0x06,0x81,0x6a,0xba,0x3e,
	                                           0x25,0x71,0x78,0x50,0xc2,0x6c,0x9c,0xd0,0xd8,0x9d };
	static const unsigned char long_sha1[20] = { 0x84,0x98,0x3e,0x44,0x1c,0x3b,0xd2,0x6e,0xba,0xae,
	                                            0x4a,0xa1,0xf9,0x51,0x29,0xe5,0xe5,0x46,0x70,0xf1 };
	static const char *lng = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
	static unsigned char zero[65536], out[131072];
	unsigned char h[20];
	const struct gs_rom_info *c107, *c107a, *c107b, *pcm, *pcml, *pcmh, *cmh, *cm, *c207, *cm102;
	struct gs_romset set;
	struct gs_rom_pick pk;
	static const unsigned char s107[20] = { 0xb0,0x83,0x51,0x8f,0xff,0xb7,0xf6,0x6b,0x03,0xc2,0x3b,0x7e,0xb4,0xf8,0x68,0xe6,0x2d,0xc5,0xa9,0x87 };
	gs_sha1((const unsigned char *)"abc", 3, h);
	CHECK(memcmp(h, abc_sha1, 20) == 0);
	gs_sha1((const unsigned char *)lng, (unsigned)strlen(lng), h);
	CHECK(memcmp(h, long_sha1, 20) == 0);
	/* any other file of a ROM's size is not a ROM */
	CHECK(gs_rom_identify(zero, sizeof zero) == 0);
	CHECK(gs_rom_identify(zero, 1000) == 0);
	c107 = gs_rom_by_sha1(s107);
	CHECK(c107 && c107->kind == GS_ROM_CTRL && c107->model == GS_MODEL_MT32_OLD && c107->size == 65536);
	/* find the others by id through the table */
	{
		int i;
		const char *want[] = { "mt32_ctrl_1_07_a", "mt32_ctrl_1_07_b", "mt32_pcm", "mt32_pcm_l", "mt32_pcm_h",
		                       "cm32l_pcm_h", "cm32l_pcm", "mt32_ctrl_2_07", "cm32l_ctrl_1_02" };
		const struct gs_rom_info **got[] = { &c107a, &c107b, &pcm, &pcml, &pcmh, &cmh, &cm, &c207, &cm102 };
		
		for (i = 0; i < 9; i++) {
			*got[i] = gs_rom_by_id(want[i]);
			CHECK(*got[i] != 0);
		}
	}
	CHECK(gs_rom_whole(c107a) == c107 && gs_rom_whole(c107b) == c107);
	CHECK(gs_rom_whole(pcml) == pcm && gs_rom_whole(cmh) == cm);
	CHECK(c107a->part == GS_PART_EVEN && c107b->part == GS_PART_ODD);
	/* joining: even/odd bytes, in either order */
	{
		static unsigned char a[32768], b[32768];
		int i, ok = 1;
		for (i = 0; i < 32768; i++) { a[i] = (unsigned char)i; b[i] = (unsigned char)(255 - (i & 255)); }
		CHECK(gs_rom_join(c107b, b, c107a, a, out) == 0);
		for (i = 0; i < 32768; i++)
			if (out[i * 2] != a[i] || out[i * 2 + 1] != b[i]) ok = 0;
		CHECK(ok);
		CHECK(gs_rom_join(c107a, a, pcmh, b, out) == -1);
	}
	/* picking */
	gs_romset_init(&set);
	CHECK(gs_romset_pick(&set, 0, &pk) == -1);
	gs_romset_add(&set, c107a);
	gs_romset_add(&set, c107b);
	CHECK(gs_romset_pick(&set, 0, &pk) == -1);              /* no PCM */
	gs_romset_add(&set, pcml);
	gs_romset_add(&set, pcmh);
	CHECK(gs_romset_pick(&set, 0, &pk) == 0 && pk.model == GS_MODEL_MT32_OLD &&
	      pk.ctrl[0] == 0 && pk.ctrl[1] == 1 && pk.pcm[0] == 2 && pk.pcm[1] == 3);
	gs_romset_add(&set, c107);                               /* a whole file wins */
	gs_romset_add(&set, pcm);
	CHECK(gs_romset_pick(&set, GS_MODEL_MT32_OLD, &pk) == 0 && pk.ctrl[0] == 4 && pk.ctrl[1] == -1 && pk.pcm[0] == 5);
	CHECK(gs_romset_pick(&set, GS_MODEL_CM32L, &pk) == -1);
	gs_romset_add(&set, cm102);
	CHECK(gs_romset_pick(&set, GS_MODEL_CM32L, &pk) == -1);  /* needs the CM-32L PCM */
	gs_romset_add(&set, cmh);
	CHECK(gs_romset_pick(&set, GS_MODEL_CM32L, &pk) == 0 && pk.pcm[0] == 5 && pk.pcm[1] == 7);
	gs_romset_add(&set, c207);
	CHECK(gs_romset_pick(&set, GS_MODEL_MT32_NEW, &pk) == 0 && pk.ctrl_whole == c207);
	/* the MT-32 PCM followed by cm32l_pcm_h makes the CM-32L PCM */
	{
		static unsigned char lo[524288], hi[524288], whole[1048576];
		lo[0] = 1; hi[0] = 2;
		CHECK(gs_rom_join(cmh, hi, pcm, lo, whole) == 0 && whole[0] == 1 && whole[524288] == 2);
	}
}

static struct gs_sf2 font;
static struct gs_synth syn;

static void test_sf2(void)
{
	struct gs_zone z[8];
	int p;
	make_sf2();
	CHECK(gs_sf2_open(&font, sf, (unsigned)sflen) == 0);
	CHECK(font.nphdr == 4 && font.nshdr == 3);
	CHECK(font.smpl[SINE_LEN / 4] > 15000);                 /* the sine's peak, native order */
	p = gs_sf2_preset(&font, 0, 0);
	CHECK(p == 0 && strncmp(gs_sf2_preset_name(&font, p), "Sine", 4) == 0);
	CHECK(gs_sf2_preset(&font, 128, 0) == 2);
	CHECK(gs_sf2_preset(&font, 0, 5) == -1);
	CHECK(gs_sf2_zones(&font, 0, 60, 100, z, 8) == 1);
	CHECK(z[0].root == 69 && z[0].gen[GEN_sampleModes] == 1 && z[0].end == SINE_LEN && z[0].rate == 44100);
	CHECK(z[0].gen[GEN_releaseVolEnv] == -3986 && z[0].gen[GEN_scaleTuning] == 100);
	CHECK(gs_sf2_zones(&font, 1, 60, 100, z, 8) == 1 && z[0].gen[GEN_initialAttenuation] == 500);
	CHECK(gs_sf2_zones(&font, 2, 42, 100, z, 8) == 1 && z[0].gen[GEN_exclusiveClass] == 1);
	CHECK(gs_sf2_zones(&font, 2, 43, 100, z, 8) == 0);
	CHECK(gs_sf2_open(&font, (unsigned char *)"RIFF\4\0\0\0sfbk", 12) == -1);
	CHECK(gs_sf2_open(&font, sf, (unsigned)sflen) == 0);
}

static void test_synth(void)
{
	long a, b;
	int n;
	gs_synth_init(&syn, &font, RATE, 24, GS_MODE_GM);
	gs_synth_render(&syn, buf, RATE);
	CHECK(peak(buf, RATE, 0) == 0 && peak(buf, RATE, 1) == 0);
	/* A4 for a second: 441 Hz from the sample, 440 played (root 69, 0 cents) */
	msg(&syn, 0x90, 69, 127);
	gs_synth_render(&syn, buf, RATE);
	n = crossings(buf, RATE, 0);
	CHECK(n >= 438 && n <= 443);
	a = peak(buf + RATE, RATE / 2, 0);
	CHECK(a > 3000);
	/* an octave up */
	msg(&syn, 0x80, 69, 0);
	gs_synth_render(&syn, buf, RATE / 2);                    /* release: 0.1 s */
	CHECK(gs_synth_active(&syn) == 0);
	CHECK(peak(buf + RATE / 4, RATE / 4, 0) == 0);
	msg(&syn, 0x90, 81, 127);
	gs_synth_render(&syn, buf, RATE);
	n = crossings(buf, RATE, 0);
	CHECK(n >= 876 && n <= 886);
	/* bend +2 semitones (the default range): 81 sounds as 83 */
	msg(&syn, 0xE0, 0x7F, 0x7F);
	gs_synth_render(&syn, buf, RATE);
	n = crossings(buf, RATE, 0);
	CHECK(n >= 983 && n <= 993);
	/* bend range 12 by RPN 0, bend down full: 81 sounds as 69 */
	msg(&syn, 0xB0, 101, 0); msg(&syn, 0xB0, 100, 0); msg(&syn, 0xB0, 6, 12);
	msg(&syn, 0xE0, 0, 0);
	gs_synth_render(&syn, buf, RATE);
	n = crossings(buf, RATE, 0);
	CHECK(n >= 437 && n <= 443);
	msg(&syn, 0xE0, 0, 0x40);
	/* volume: CC7 64 is about 12 dB under 127 */
	msg(&syn, 0xB0, 7, 127);
	gs_synth_render(&syn, buf, RATE / 4);
	a = peak(buf, RATE / 4, 0);
	msg(&syn, 0xB0, 7, 64);
	gs_synth_render(&syn, buf, RATE / 4);
	b = peak(buf + 2 * GS_BLOCK, RATE / 4 - GS_BLOCK, 0);   /* gains change at the next block */
	CHECK(b * 10 > a * 2 && b * 10 < a * 3);
	msg(&syn, 0xB0, 7, 0);
	gs_synth_render(&syn, buf, RATE / 4);
	CHECK(peak(buf + 2 * GS_BLOCK, RATE / 4 - GS_BLOCK, 0) == 0);
	msg(&syn, 0xB0, 7, 100);
	/* pan hard left: nothing on the right */
	msg(&syn, 0xB0, 10, 0);
	gs_synth_render(&syn, buf, 2 * GS_BLOCK);
	gs_synth_render(&syn, buf, RATE / 4);
	CHECK(peak(buf, RATE / 4, 1) < 300 && peak(buf, RATE / 4, 0) > 2000);
	msg(&syn, 0xB0, 10, 64);
	gs_synth_render(&syn, buf, RATE / 4);
	a = peak(buf, RATE / 4, 0); b = peak(buf, RATE / 4, 1);
	CHECK(labs(a - b) < a / 20);
	/* sustain pedal holds a released note until it lifts */
	msg(&syn, 0xB0, 64, 127);
	msg(&syn, 0x80, 81, 0);
	gs_synth_render(&syn, buf, RATE / 2);
	CHECK(gs_synth_active(&syn) == 1);
	msg(&syn, 0xB0, 64, 0);
	gs_synth_render(&syn, buf, RATE / 2);
	CHECK(gs_synth_active(&syn) == 0);
	/* velocity: 64 is about 12 dB under 127 */
	msg(&syn, 0x90, 69, 127);
	gs_synth_render(&syn, buf, RATE / 2);
	a = peak(buf + RATE / 2, RATE / 4, 0);
	msg(&syn, 0x80, 69, 0); gs_synth_render(&syn, buf, RATE / 2);
	msg(&syn, 0x90, 69, 64);
	gs_synth_render(&syn, buf, RATE / 2);
	b = peak(buf + RATE / 2, RATE / 4, 0);
	CHECK(b * 10 > a * 2 && b * 10 < a * 3);
	msg(&syn, 0xB0, 123, 0); gs_synth_render(&syn, buf, RATE / 2);
	CHECK(gs_synth_active(&syn) == 0);
	/* the Quiet preset: 20 dB under (0.1 of the level) */
	msg(&syn, 0xC0, 1, 0);
	msg(&syn, 0x90, 69, 127);
	gs_synth_render(&syn, buf, RATE / 2);
	b = peak(buf + RATE / 2, RATE / 4, 0);
	CHECK(b * 100 > a * 8 && b * 100 < a * 12);
	msg(&syn, 0xB0, 120, 0);
	CHECK(gs_synth_active(&syn) == 0);
	/* drums on channel 10: the open hat cuts the closed one (class 1) */
	msg(&syn, 0x99, 42, 100);
	CHECK(gs_synth_active(&syn) == 1);
	msg(&syn, 0x99, 46, 100);
	CHECK(gs_synth_active(&syn) == 1 && syn.v[0].key == 46);
	gs_synth_render(&syn, buf, RATE / 2);                    /* one-shot: ends by itself */
	CHECK(gs_synth_active(&syn) == 0);
	/* a key with no zone is silent */
	msg(&syn, 0x99, 50, 100);
	CHECK(gs_synth_active(&syn) == 0);
	/* stealing: 30 notes on 24 voices keep 24 */
	{
		int k;
		msg(&syn, 0xC0, 0, 0);
		for (k = 0; k < 30; k++)
			msg(&syn, 0x90, 40 + k, 100);
		CHECK(gs_synth_active(&syn) == 24);
		gs_synth_render(&syn, buf, RATE);                    /* 24 voices: no overflow */
		CHECK(peak(buf, RATE, 0) <= 32767);
	}
	/* GM reset SysEx through the port */
	{
		struct gs_midi_in in;
		static const unsigned char gm_on[] = { 0xF0, 0x7E, 0x7F, 0x09, 0x01, 0xF7 };
		static const unsigned char mt32[] = { 0xF0, 0x41, 0x10, 0x16, 0x12, 0x10, 0x00, 0x16, 0x00, 0x5A, 0xF7 };
		gs_midi_in_init(&in);
		gs_synth_port(&syn, &in, gm_on, sizeof gm_on);
		CHECK(gs_synth_active(&syn) == 0);
		gs_synth_port(&syn, &in, mt32, sizeof mt32);
		CHECK(syn.sysex_ignored == 1);
		{
			static const unsigned char notes[] = { 0x90, 69, 100, 72, 100 };
			gs_synth_port(&syn, &in, notes, sizeof notes);
			CHECK(gs_synth_active(&syn) == 2);
		}
	}
}

static void test_mt32_mode(void)
{
	gs_synth_init(&syn, &font, RATE, 24, GS_MODE_MT32_GM);
	CHECK(gs_mt32_to_gm(0) == 0 && gs_mt32_to_gm(48) == 48 && gs_mt32_to_gm(88) == 56);
	msg(&syn, 0x90, 69, 100);                                 /* channel 1: an MT-32 ignores it */
	CHECK(gs_synth_active(&syn) == 0);
	msg(&syn, 0x91, 69, 100);                                 /* part 1 on channel 2 */
	CHECK(gs_synth_active(&syn) == 1);
	msg(&syn, 0x9A, 69, 100);                                 /* channel 11: off */
	CHECK(gs_synth_active(&syn) == 1);
	/* our engine switch: F0 7D 'G' 'S' 01 mode F7 */
	{
		static const unsigned char gm[] = { 0xF0, 0x7D, 'G', 'S', 0x01, 0x00, 0xF7 };
		static const unsigned char bad[] = { 0xF0, 0x7D, 'G', 'S', 0x01, 0x09, 0xF7 };
		gs_synth_sysex(&syn, gm, sizeof gm);
		CHECK(syn.mode == GS_MODE_GM && gs_synth_active(&syn) == 0);
		msg(&syn, 0x90, 69, 100);
		CHECK(gs_synth_active(&syn) == 1);
		gs_synth_sysex(&syn, bad, sizeof bad);
		CHECK(syn.mode == GS_MODE_GM && syn.sysex_ignored == 1);
	}
	/* universal master volume */
	{
		static const unsigned char half[] = { 0xF0, 0x7F, 0x7F, 0x04, 0x01, 0x00, 0x40, 0xF7 };
		static const unsigned char full[] = { 0xF0, 0x7F, 0x7F, 0x04, 0x01, 0x7F, 0x7F, 0xF7 };
		gs_synth_sysex(&syn, half, sizeof half);
		CHECK(syn.master == 128);
		gs_synth_sysex(&syn, full, sizeof full);
		CHECK(syn.master == 256);
	}
}

/* gs_seq_events: the song's events at millisecond steps, as a port player
 * sends them */
static int sunk[8], nsunk;
static unsigned long sunk_at[8], ms_now;
static void sink(void *ctx, const struct gs_smf_event *ev)
{
	(void)ctx;
	if (nsunk < 8) { sunk[nsunk] = ev->type; sunk_at[nsunk] = ms_now; nsunk++; }
}

static void test_seq_events(void)
{
	struct gs_seq q;
	unsigned r = 1;
	make_smf();
	CHECK(gs_seq_open(&q, smf, (unsigned)smflen, 1000, 0) == 0);
	nsunk = 0;
	for (ms_now = 0; ms_now < 4000 && r; ms_now += 10)
		r = gs_seq_events(&q, ms_now ? 10 : 0, sink, 0);
	CHECK(nsunk == 5);
	CHECK(sunk[0] == GS_EV_MSG && sunk_at[0] == 0);
	CHECK(sunk[1] == GS_EV_MSG && sunk_at[1] == 500);       /* off at 0.5 s */
	CHECK(sunk[2] == GS_EV_SYSEX && sunk_at[2] == 1500);    /* 192 ticks: 0.5 s + 1 s at 60 bpm */
	CHECK(sunk[3] == GS_EV_MSG && sunk_at[3] == 1500);
	CHECK(sunk[4] == GS_EV_MSG && sunk_at[4] == 2500);
	CHECK(r == 0 && q.ended);
	CHECK(gs_seq_open(&q, smf, (unsigned)smflen, 1000, 1) == 0);
	nsunk = 0;
	for (ms_now = 0, r = 1; ms_now < 2600 && r != 2; ms_now += 10)
		r = gs_seq_events(&q, ms_now ? 10 : 0, sink, 0);
	CHECK(r == 2 && q.loops == 1 && ms_now == 2510);
}

static void test_seq(void)
{
	struct gs_seq q;
	int i;
	gs_synth_init(&syn, &font, RATE, 24, GS_MODE_GM);
	make_smf();
	CHECK(gs_seq_open(&q, smf, (unsigned)smflen, RATE, 0) == 0);
	/* note on at 0, off at 0.5 s (+0.1 s release); on again at 1.5 s */
	gs_seq_render(&q, &syn, buf, RATE / 4);
	CHECK(gs_synth_active(&syn) == 1);
	gs_seq_render(&q, &syn, buf, RATE / 2);                   /* to 0.75 s */
	CHECK(gs_synth_active(&syn) == 0);
	gs_seq_render(&q, &syn, buf, RATE * 3 / 4 - 2);           /* to just before 1.5 s */
	CHECK(gs_synth_active(&syn) == 0);
	gs_seq_render(&q, &syn, buf, 4);
	CHECK(gs_synth_active(&syn) == 1);
	gs_seq_render(&q, &syn, buf, RATE * 2);                   /* past 2.5 s */
	CHECK(q.ended && gs_synth_active(&syn) == 0);
	/* looped: plays again */
	CHECK(gs_seq_open(&q, smf, (unsigned)smflen, RATE, 1) == 0);
	for (i = 0; i < 4; i++)
		gs_seq_render(&q, &syn, buf, RATE);
	CHECK(q.loops == 1 && !q.ended);
}

/* position and seek: 96 ticks a beat; beat 1 is at 0.5 s, beat 2 at 1.5 s */
static void test_seek(void)
{
	struct gs_seq q;
	unsigned r;
	gs_synth_init(&syn, &font, RATE, 24, GS_MODE_GM);
	make_smf();
	CHECK(gs_seq_open(&q, smf, (unsigned)smflen, RATE, 0) == 0);
	CHECK(gs_seq_tick(&q) == 0 && gs_seq_beat(&q) == 0);
	gs_seq_render(&q, &syn, buf, RATE / 4);                   /* 0.25 s: half a beat at 120 bpm */
	CHECK(gs_seq_tick(&q) >= 47 && gs_seq_tick(&q) <= 48 && gs_seq_beat(&q) == 0);
	gs_seq_render(&q, &syn, buf, RATE / 2);                   /* 0.75 s: beat 1 and a quarter at 60 bpm */
	CHECK(gs_seq_tick(&q) >= 119 && gs_seq_tick(&q) <= 120 && gs_seq_beat(&q) == 1);
	CHECK(gs_synth_active(&syn) == 0);
	gs_seq_seek_synth(&q, &syn, 2);
	CHECK(gs_seq_beat(&q) == 2 && gs_seq_tick(&q) == 192);
	CHECK((q.now >> 16) == (unsigned long long)RATE * 3 / 2); /* the tempo change on the way was kept */
	CHECK(gs_synth_active(&syn) == 0);                        /* the note before the beat was left out */
	gs_seq_render(&q, &syn, buf, 4);
	CHECK(gs_synth_active(&syn) == 1);                        /* the note on the beat plays */
	gs_seq_seek_synth(&q, &syn, 0);
	CHECK(gs_seq_tick(&q) == 0 && (q.now >> 16) == 0);
	gs_seq_seek_synth(&q, &syn, 99);                          /* past the end: the end */
	CHECK(gs_seq_tick(&q) == 288 && !q.ended);
	gs_seq_render(&q, &syn, buf, 4);
	CHECK(q.ended);
	/* the port's way: milliseconds, events to a sink */
	CHECK(gs_seq_open(&q, smf, (unsigned)smflen, 1000, 0) == 0);
	nsunk = 0; ms_now = 0;
	gs_seq_seek_beat(&q, 2, sink, 0);
	CHECK(nsunk == 0 && (q.now >> 16) >= 1499 && (q.now >> 16) <= 1500);
	r = gs_seq_events(&q, 0, sink, 0);
	CHECK(r == 1 && nsunk == 2 && sunk[0] == GS_EV_SYSEX && sunk[1] == GS_EV_MSG);
	CHECK(gs_seq_beat(&q) == 2);
}

int main(void)
{
	test_tables();
	test_parser();
	test_smf();
	test_rom();
	test_sf2();
	test_synth();
	test_mt32_mode();
	test_seq();
	test_seq_events();
	test_seek();
	printf("%s: %d checks, %d failed (long is %d bits)\n", fails ? "FAIL" : "PASS", checks, fails,
	       (int)sizeof(long) * 8);
	return fails != 0;
}
