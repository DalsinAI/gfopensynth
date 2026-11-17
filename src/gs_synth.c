/* The SoundFont synth and the SMF player (gs_synth.h). Integer only: the
 * tables in gs_tables.c hold every curve.
 * MIT, Copyright (c) 2026 AmiCradle Team. */
#include "gs_synth.h"
#include "gs_tables.h"

#define ENV_DELAY   1
#define ENV_ATTACK  2
#define ENV_HOLD    3
#define ENV_DECAY   4
#define ENV_SUSTAIN 5
#define ENV_RELEASE 6

#define CB_SILENT 960           /* 96 dB: a voice this far down is done */
#define CB_MAX 1440

/* ---- small sums ---- */

uint32_t gs_exp2_cents(int32_t c)
{
	int32_t oct;
	uint32_t v;
	if (c < -14400) c = -14400;
	if (c > 12000) c = 12000;
	oct = c >= 0 ? c / 1200 : -((-c + 1199) / 1200);
	v = gs_exp2_q16[c - oct * 1200];
	return oct >= 0 ? v << oct : v >> -oct;
}

/* timecents to blocks of GS_BLOCK frames (at least 1 for a stage that has one) */
static uint32_t tc_blocks(int32_t tc, uint32_t rate)
{
	uint64_t frames;
	if (tc <= -12000)
		return 0;
	if (tc > 8000)
		tc = 8000;
	frames = ((uint64_t)rate * gs_exp2_cents(tc)) >> 16;
	return (uint32_t)(frames / GS_BLOCK);
}

static int clampi(int v, int lo, int hi)
{
	return v < lo ? lo : v > hi ? hi : v;
}

/* MT-32 programs to the nearest GM program (MT-32 owner's manual patch list,
 * GM level 1 sound set). */
static const uint8_t mt32_gm[128] = {
	0, 1, 0, 4, 5, 4, 5, 3,          /* AcouPiano 1-3, ElecPiano 1-4, Honkytonk */
	16, 17, 18, 16, 19, 19, 20, 21,  /* Elec Org 1-4, Pipe Org 1-3, Accordion */
	6, 6, 6, 7, 7, 7, 8, 8,          /* Harpsi 1-3, Clavi 1-3, Celesta 1-2 */
	62, 63, 62, 63, 38, 39, 38, 39,  /* SynBrass 1-4, SynBass 1-4 */
	88, 89, 52, 92, 97, 99, 14, 85,  /* Fantasy, Harmo Pan, Chorale, Glasses, Soundtrack, Atmosphere, Warm Bell, Funny Vox */
	98, 96, 68, 75, 80, 81, 53, 80,  /* Echo Bell, Ice Rain, Oboe 2001, Echo Pan, Doctor Solo, School Daze, Bell Singer, Square Wave */
	48, 49, 48, 45, 40, 40, 42, 42,  /* Str Sect 1-3, Pizzicato, Violin 1-2, Cello 1-2 */
	43, 46, 46, 24, 25, 26, 27, 104, /* Contrabass, Harp 1-2, Guitar 1-2, Elec Gtr 1-2, Sitar */
	32, 32, 33, 34, 36, 37, 35, 35,  /* Acou Bass 1-2, Elec Bass 1-2, Slap Bass 1-2, Fretless 1-2 */
	73, 73, 72, 72, 74, 75, 64, 65,  /* Flute 1-2, Piccolo 1-2, Recorder, Pan Pipes, Sax 1-2 */
	66, 67, 71, 71, 68, 69, 70, 22,  /* Sax 3-4, Clarinet 1-2, Oboe, Engl Horn, Bassoon, Harmonica */
	56, 56, 57, 57, 60, 60, 58, 61,  /* Trumpet 1-2, Trombone 1-2, Fr Horn 1-2, Tuba, Brs Sect 1 */
	61, 11, 11, 98, 9, 9, 14, 13,    /* Brs Sect 2, Vibe 1-2, Syn Mallet, Wind Bell, Glock, Tube Bell, Xylophone */
	12, 107, 111, 77, 78, 78, 76, 77,/* Marimba, Koto, Sho, Shakuhachi, Whistle 1-2, Bottle Blow, Breath Pipe */
	47, 117, 118, 118, 118, 116, 116, 119, /* Timpani, Melodic Tom, Deep Snare, Elec Perc 1-2, Taiko, Taiko Rim, Cymbal */
	115, 112, 55, 124, 123, 98, 114, 75    /* Castanets, Triangle, Orche Hit, Telephone, Bird Tweet, One Note Jam, Water Bells, Jungle Tune */
};

uint8_t gs_mt32_to_gm(uint8_t p)
{
	return mt32_gm[p & 127];
}

/* ---- channels ---- */

static int find_preset(struct gs_synth *s, int c)
{
	struct gs_channel *ch = &s->ch[c];
	int p, prog = ch->program;
	if (!s->sf)
		return -1;
	if (ch->drum) {
		p = gs_sf2_preset(s->sf, 128, prog);
		if (p < 0) p = gs_sf2_preset(s->sf, 128, 0);
		return p;
	}
	if (s->mode == GS_MODE_MT32_GM)
		prog = gs_mt32_to_gm((uint8_t)prog);
	p = gs_sf2_preset(s->sf, ch->bank, prog);
	if (p < 0) p = gs_sf2_preset(s->sf, 0, prog);
	if (p < 0) p = gs_sf2_preset(s->sf, 0, 0);
	return p;
}

static void reset_controllers(struct gs_channel *ch)
{
	ch->expression = 127;
	ch->sustain = 0;
	ch->bend = 8192;
	ch->rpn_msb = ch->rpn_lsb = 127;
}

void gs_synth_reset(struct gs_synth *s)
{
	int c;
	for (c = 0; c < GS_VOICES_MAX; c++)
		s->v[c].stage = 0;
	for (c = 0; c < 16; c++) {
		struct gs_channel *ch = &s->ch[c];
		ch->program = 0;
		ch->bank = 0;
		ch->drum = c == 9;
		ch->volume = 100;
		ch->pan = 64;
		ch->bend_range = 200;
		reset_controllers(ch);
		ch->preset = find_preset(s, c);
	}
}

void gs_synth_init(struct gs_synth *s, const struct gs_sf2 *sf, uint32_t rate, int voices, int mode)
{
	s->sf = sf;
	s->rate = rate ? rate : 22050;
	s->nvoices = clampi(voices, 1, GS_VOICES_MAX);
	s->mode = mode;
	s->master = 256;
	s->clock = 0;
	s->sysex_ignored = 0;
	s->block_left = 0;
	gs_synth_reset(s);
}

void gs_synth_set_master(struct gs_synth *s, uint32_t q8)
{
	s->master = q8 > 1024 ? 1024 : q8;
}

/* MT-32 mode listens on channels 2-10 only, as the MT-32 does from power on */
static int listening(const struct gs_synth *s, int c)
{
	return s->mode != GS_MODE_MT32_GM || (c >= 1 && c <= 9);
}

/* ---- voices ---- */

static void voice_pitch(struct gs_synth *s, struct gs_voice *v)
{
	const struct gs_channel *ch = &s->ch[v->chan];
	int32_t bend = ((int32_t)ch->bend - 8192) * (int32_t)ch->bend_range / 8192;
	v->inc = (uint32_t)(((uint64_t)v->ratio * gs_exp2_cents(v->cents + bend)) >> 16);
	if (v->inc > (64u << 16))
		v->inc = 64u << 16;
}

static void voice_release(struct gs_voice *v)
{
	if (v->released)
		return;
	v->released = 1;
	v->held = 0;
	v->stage = ENV_RELEASE;
	if (v->att_len && v->att_pos < v->att_len) {
		/* let go during the attack: release from the level reached */
		uint32_t lin = (v->att_pos << 15) / v->att_len, cb = 0;
		while (cb < CB_MAX && gs_cb_gain_q15[cb] > lin)
			cb += 8;
		v->env = (int32_t)cb << 8;
		v->att_len = 0;
	}
	v->env_step = v->release_step;
}

static struct gs_voice *voice_alloc(struct gs_synth *s)
{
	struct gs_voice *best = 0;
	int i;
	for (i = 0; i < s->nvoices; i++)
		if (!s->v[i].stage)
			return &s->v[i];
	/* the quietest released voice, else the oldest */
	for (i = 0; i < s->nvoices; i++) {
		struct gs_voice *v = &s->v[i];
		if (v->stage == ENV_RELEASE && (!best || v->env > best->env))
			best = v;
	}
	if (best)
		return best;
	for (i = 0; i < s->nvoices; i++)
		if (!best || s->v[i].age < best->age)
			best = &s->v[i];
	return best;
}

static void note_on(struct gs_synth *s, int c, int key, int vel)
{
	struct gs_channel *ch = &s->ch[c];
	struct gs_zone z[8];
	int n, i;
	if (ch->preset < 0)
		return;
	n = gs_sf2_zones(s->sf, ch->preset, key, vel, z, 8);
	for (i = 0; i < n; i++) {
		const int16_t *g = z[i].gen;
		struct gs_voice *v;
		int k = g[GEN_keynum] >= 0 ? g[GEN_keynum] : key;
		int vv = g[GEN_velocity] > 0 ? g[GEN_velocity] : vel;
		int32_t tc;
		/* a new note of an exclusive class (a hi-hat) cuts the others */
		if (g[GEN_exclusiveClass]) {
			int j;
			for (j = 0; j < s->nvoices; j++) {
				struct gs_voice *o = &s->v[j];
				if (o->stage && o->chan == c && o->excl == (uint8_t)g[GEN_exclusiveClass])
					o->stage = 0;
			}
		}
		v = voice_alloc(s);
		v->chan = (uint8_t)c;
		v->key = (uint8_t)key;
		v->vel = (uint8_t)vel;
		v->excl = (uint8_t)g[GEN_exclusiveClass];
		v->loop = (uint8_t)(g[GEN_sampleModes] & 3);
		if (v->loop == 2)
			v->loop = 0;
		v->released = v->held = 0;
		v->data = s->sf->smpl;
		v->pos = z[i].start;
		v->end = z[i].end;
		v->ls = z[i].loopstart;
		v->le = z[i].loopend;
		v->frac = 0;
		v->ratio = (uint32_t)(((uint64_t)z[i].rate << 16) / s->rate);
		v->cents = (int32_t)(k - z[i].root) * g[GEN_scaleTuning] + g[GEN_coarseTune] * 100 +
		           g[GEN_fineTune] + z[i].correction;
		voice_pitch(s, v);
		/* EMU's hardware, and the SoundFonts made on it, use 0.4 of the attenuation */
		v->atten = (int16_t)(clampi(g[GEN_initialAttenuation], 0, CB_MAX) * 2 / 5 + gs_curve_cb[vv & 127]);
		v->pan = (int16_t)clampi(g[GEN_pan], -500, 500);
		/* the volume envelope */
		v->env = 0;
		v->env_blocks = tc_blocks(g[GEN_delayVolEnv], s->rate);
		v->att_len = tc_blocks(g[GEN_attackVolEnv], s->rate);
		v->att_pos = 0;
		v->sustain = (int32_t)clampi(g[GEN_sustainVolEnv], 0, CB_SILENT) << 8;
		tc = g[GEN_decayVolEnv] + (60 - key) * g[GEN_keynumToVolEnvDecay];
		{
			uint32_t b = tc_blocks(tc, s->rate);
			v->decay_step = (int32_t)(((uint32_t)CB_SILENT << 8) / (b ? b : 1));
		}
		{
			uint32_t b = tc_blocks(g[GEN_releaseVolEnv], s->rate);
			v->release_step = (int32_t)(((uint32_t)CB_SILENT << 8) / (b ? b : 1));
		}
		v->stage = v->env_blocks ? ENV_DELAY : ENV_ATTACK;
		/* hold is kept for after the attack */
		tc = g[GEN_holdVolEnv] + (60 - key) * g[GEN_keynumToVolEnvHold];
		v->env_step = (int32_t)tc_blocks(tc, s->rate);   /* the hold, in blocks, until used */
		v->age = ++s->clock;
		v->gl = v->gr = 0;
	}
}

static void note_off(struct gs_synth *s, int c, int key)
{
	int i;
	for (i = 0; i < s->nvoices; i++) {
		struct gs_voice *v = &s->v[i];
		if (v->stage && v->chan == c && v->key == key && !v->released) {
			if (s->ch[c].sustain)
				v->held = 1;
			else
				voice_release(v);
		}
	}
}

static void all_off(struct gs_synth *s, int c, int now)
{
	int i;
	for (i = 0; i < s->nvoices; i++) {
		struct gs_voice *v = &s->v[i];
		if (v->stage && v->chan == c) {
			if (now)
				v->stage = 0;
			else
				voice_release(v);
		}
	}
}

static void control(struct gs_synth *s, int c, int cc, int val)
{
	struct gs_channel *ch = &s->ch[c];
	int i;
	switch (cc) {
	case 0: ch->bank = (uint8_t)val; break;          /* GS bank select */
	case 6:                                           /* data entry */
		if (ch->rpn_msb == 0 && ch->rpn_lsb == 0)
			ch->bend_range = (uint16_t)(val * 100);
		break;
	case 7: ch->volume = (uint8_t)val; break;
	case 10: ch->pan = (uint8_t)val; break;
	case 11: ch->expression = (uint8_t)val; break;
	case 64:
		ch->sustain = val >= 64;
		if (!ch->sustain)
			for (i = 0; i < s->nvoices; i++)
				if (s->v[i].stage && s->v[i].chan == c && s->v[i].held)
					voice_release(&s->v[i]);
		break;
	case 100: ch->rpn_lsb = (uint8_t)val; break;
	case 101: ch->rpn_msb = (uint8_t)val; break;
	case 98: case 99: ch->rpn_msb = ch->rpn_lsb = 127; break;   /* NRPN: not ours */
	case 120: all_off(s, c, 1); break;
	case 121: reset_controllers(ch); break;
	case 123: case 124: case 125: case 126: case 127: all_off(s, c, 0); break;
	}
}

void gs_synth_msg(struct gs_synth *s, const struct gs_msg *m)
{
	int c = m->status & 15, i;
	if (m->status < 0x80 || m->status >= 0xF0 || !listening(s, c))
		return;
	switch (m->status & 0xF0) {
	case 0x90:
		if (m->d2)
			note_on(s, c, m->d1 & 127, m->d2 & 127);
		else
			note_off(s, c, m->d1 & 127);     /* velocity 0 is a note off */
		break;
	case 0x80: note_off(s, c, m->d1 & 127); break;
	case 0xB0: control(s, c, m->d1 & 127, m->d2 & 127); break;
	case 0xC0:
		s->ch[c].program = m->d1 & 127;
		s->ch[c].preset = find_preset(s, c);
		break;
	case 0xE0:
		s->ch[c].bend = (uint16_t)((m->d1 & 127) | (m->d2 & 127) << 7);
		for (i = 0; i < s->nvoices; i++)
			if (s->v[i].stage && s->v[i].chan == c)
				voice_pitch(s, &s->v[i]);
		break;
	}
}

void gs_synth_sysex(struct gs_synth *s, const uint8_t *d, uint32_t len)
{
	/* data may start with F0 or not */
	if (len && d[0] == 0xF0) { d++; len--; }
	/* GM system on: 7E dev 09 01 */
	if (len >= 4 && d[0] == 0x7E && d[2] == 0x09 && d[3] == 0x01) {
		gs_synth_reset(s);
		return;
	}
	/* GS reset: 41 dev 42 12 40 00 7F 00 sum */
	if (len >= 9 && d[0] == 0x41 && d[2] == 0x42 && d[3] == 0x12 &&
	    d[4] == 0x40 && d[5] == 0x00 && d[6] == 0x7F && d[7] == 0x00) {
		gs_synth_reset(s);
		return;
	}
	s->sysex_ignored++;
}

void gs_synth_port(struct gs_synth *s, struct gs_midi_in *in, const uint8_t *bytes, uint32_t n)
{
	struct gs_msg m;
	uint32_t i;
	for (i = 0; i < n; i++) {
		int r = gs_midi_in_byte(in, bytes[i], &m);
		if (r == GS_IN_MSG)
			gs_synth_msg(s, &m);
		else if (r == GS_IN_SYSEX)
			gs_synth_sysex(s, in->sysex, in->sxlen);
	}
}

int gs_synth_active(const struct gs_synth *s)
{
	int i, n = 0;
	for (i = 0; i < s->nvoices; i++)
		if (s->v[i].stage)
			n++;
	return n;
}

/* one block's envelope step and gains; 0 when the voice ended */
static int voice_block(struct gs_synth *s, struct gs_voice *v)
{
	const struct gs_channel *ch = &s->ch[v->chan];
	int32_t cb;
	uint32_t g, lin = 32768;
	int p, idx;
	switch (v->stage) {
	case ENV_DELAY:
		if (v->env_blocks)
			v->env_blocks--;
		if (!v->env_blocks)
			v->stage = ENV_ATTACK;
		lin = 0;
		break;
	case ENV_ATTACK:
		if (v->att_pos < v->att_len) {
			lin = (v->att_pos << 15) / v->att_len;
			v->att_pos++;
			break;
		}
		v->att_len = 0;
		v->stage = ENV_HOLD;
		v->env_blocks = (uint32_t)v->env_step;
		/* fall through */ __attribute__((fallthrough));
	case ENV_HOLD:
		if (v->env_blocks) {
			v->env_blocks--;
			break;
		}
		v->stage = ENV_DECAY;
		__attribute__((fallthrough));
	case ENV_DECAY:
		v->env += v->decay_step;
		if (v->env >= v->sustain) {
			v->env = v->sustain;
			v->stage = ENV_SUSTAIN;
		}
		break;
	case ENV_SUSTAIN:
		break;
	case ENV_RELEASE:
		v->env += v->env_step;
		if (v->env >= (CB_SILENT << 8))
			return 0;
		break;
	}
	if (v->stage == ENV_SUSTAIN && v->sustain >= (CB_SILENT << 8))
		return 0;
	cb = v->atten + (v->env >> 8) + gs_curve_cb[ch->volume] + gs_curve_cb[ch->expression];
	if (cb >= CB_MAX)
		g = 0;
	else
		g = gs_cb_gain_q15[cb < 0 ? 0 : cb];
	g = (g * lin) >> 15;
	g = (g * s->master) >> 8;
	p = clampi(v->pan + ((int)ch->pan - 64) * 1000 / 128, -500, 500);
	idx = (p + 500) * 128 / 1000;
	v->gl = (int16_t)((g * gs_pan_q15[128 - idx]) >> 15);
	v->gr = (int16_t)((g * gs_pan_q15[idx]) >> 15);
	return 1;
}

/* mixes n frames of v into acc (interleaved stereo); 0 when it ended */
static int voice_mix(struct gs_voice *v, int32_t *acc, uint32_t n)
{
	const int16_t *d = v->data;
	uint32_t pos = v->pos, frac = v->frac, inc = v->inc;
	uint32_t ipart = inc >> 16, fpart = inc & 0xFFFF;
	int32_t gl = v->gl, gr = v->gr;
	int looping = v->loop == 1 || (v->loop == 3 && !v->released);
	uint32_t stop = looping ? v->le : v->end;
	uint32_t span = v->le - v->ls;
	while (n--) {
		int32_t s0 = d[pos], s1, x;
		uint32_t nx = pos + 1;
		if (nx >= stop)
			nx = looping ? v->ls : pos;
		s1 = d[nx];
		x = s0 + (((s1 - s0) * (int32_t)(frac >> 1)) >> 15);
		acc[0] += (x * gl) >> 15;
		acc[1] += (x * gr) >> 15;
		acc += 2;
		frac += fpart;
		pos += ipart + (frac >> 16);
		frac &= 0xFFFF;
		if (pos >= stop) {
			if (!looping)
				return 0;
			while (pos >= stop)
				pos -= span;
		}
	}
	v->pos = pos;
	v->frac = frac;
	return 1;
}

void gs_synth_render(struct gs_synth *s, int16_t *out, uint32_t frames)
{
	int32_t acc[GS_BLOCK * 2];
	while (frames) {
		uint32_t n, i;
		int k;
		if (!s->block_left) {
			for (k = 0; k < s->nvoices; k++)
				if (s->v[k].stage && !voice_block(s, &s->v[k]))
					s->v[k].stage = 0;
			s->block_left = GS_BLOCK;
		}
		n = frames < s->block_left ? frames : s->block_left;
		for (i = 0; i < n * 2; i++)
			acc[i] = 0;
		for (k = 0; k < s->nvoices; k++) {
			struct gs_voice *v = &s->v[k];
			if (v->stage && v->stage != ENV_DELAY && !voice_mix(v, acc, n))
				v->stage = 0;
		}
		for (i = 0; i < n * 2; i++) {
			int32_t x = acc[i];
			out[i] = (int16_t)(x > 32767 ? 32767 : x < -32768 ? -32768 : x);
		}
		out += n * 2;
		frames -= n;
		s->block_left -= n;
	}
}

/* ---- the SMF player ---- */

static void seq_tempo(struct gs_seq *q, uint32_t tempo)
{
	int16_t div = q->smf.division;
	if (div > 0)
		q->spt = (((uint64_t)q->rate * tempo) << 16) / ((uint64_t)div * 1000000u);
	else {
		uint32_t fps = (uint32_t)(-(div >> 8)) & 0x7F, tpf = (uint32_t)div & 0xFF;
		if (fps == 29) fps = 30;
		if (!fps) fps = 25;
		if (!tpf) tpf = 40;
		q->spt = ((uint64_t)q->rate << 16) / (fps * tpf);
	}
}

static uint64_t ev_time(const struct gs_seq *q)
{
	return q->base + (uint64_t)(q->ev.tick - q->base_tick) * q->spt;
}

void gs_seq_rewind(struct gs_seq *q)
{
	gs_smf_rewind(&q->smf);
	q->now = q->base = 0;
	q->base_tick = 0;
	seq_tempo(q, 500000);
	q->ended = 0;
	gs_smf_next(&q->smf, &q->ev);
}

int gs_seq_open(struct gs_seq *q, const uint8_t *data, uint32_t len, uint32_t rate, int loop)
{
	if (gs_smf_open(&q->smf, data, len))
		return -1;
	q->rate = rate ? rate : 22050;
	q->loop = loop;
	q->loops = 0;
	gs_seq_rewind(q);
	return 0;
}

/* plays every event due by now; 1 at the song's end */
static int seq_dispatch(struct gs_seq *q, struct gs_synth *s)
{
	while (q->ev.type != GS_EV_END && ev_time(q) <= q->now) {
		switch (q->ev.type) {
		case GS_EV_MSG: if (s) gs_synth_msg(s, &q->ev.msg); break;
		case GS_EV_SYSEX: if (s) gs_synth_sysex(s, q->ev.data, q->ev.len); break;
		case GS_EV_TEMPO:
			q->base = ev_time(q);
			q->base_tick = q->ev.tick;
			seq_tempo(q, q->ev.tempo);
			break;
		}
		gs_smf_next(&q->smf, &q->ev);
	}
	return q->ev.type == GS_EV_END;
}

void gs_seq_render(struct gs_seq *q, struct gs_synth *s, int16_t *out, uint32_t frames)
{
	while (frames) {
		uint32_t n = frames;
		if (!q->ended && seq_dispatch(q, s)) {
			if (q->loop && q->now) {      /* an empty song never loops */
				int c;
				for (c = 0; c < 16; c++)
					all_off(s, c, 0);
				gs_seq_rewind(q);
				q->loops++;
				continue;
			}
			q->ended = 1;
		}
		if (!q->ended) {
			uint64_t due = ev_time(q);
			uint64_t gap = due > q->now ? ((due - q->now + 0xFFFF) >> 16) : 1;
			if (gap < n)
				n = (uint32_t)gap;
		}
		gs_synth_render(s, out, n);
		out += n * 2;
		frames -= n;
		q->now += (uint64_t)n << 16;
	}
}

uint64_t gs_seq_length(struct gs_seq *q)
{
	uint64_t t;
	int loop = q->loop;
	gs_seq_rewind(q);
	while (q->ev.type != GS_EV_END) {
		q->now = ev_time(q);
		seq_dispatch(q, 0);
	}
	t = q->now >> 16;
	q->loop = loop;
	gs_seq_rewind(q);
	return t;
}
