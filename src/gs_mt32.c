/* Notes on our own MT-32 engine (gs_mt32.h). Written from the MT-32 owner's
 * manual: its structure chart, patch and system settings and MIDI chart.
 * Where the manual leaves a choice open the reading is ours and is said so.
 * MIT, Copyright (c) 2026 AmiCradle Team. */
#include <string.h>
#include "gs_mt32.h"

/* The 13 structures, numbered from 0. Which of the pair is a sampled (PCM)
 * partial, and how the two are joined. */
#define GSM3_MIX  0
#define GSM3_BOTH 1             /* the first partial, and the two ring modulated */
#define GSM3_RING 2
static const uint8_t gsm3_pcm1[13] = { 0, 0, 1, 1, 0, 1, 1, 0, 1, 0, 1, 0, 1 };
static const uint8_t gsm3_pcm2[13] = { 0, 0, 0, 0, 1, 1, 1, 0, 1, 0, 0, 1, 1 };
static const uint8_t gsm3_join[13] = { GSM3_MIX, GSM3_BOTH, GSM3_MIX, GSM3_BOTH, GSM3_BOTH, GSM3_MIX, GSM3_BOTH,
                                       GSM3_MIX, GSM3_MIX, GSM3_RING, GSM3_RING, GSM3_RING, GSM3_RING };

#define GSM3_TIMBRE_TEMP GS_MT32_ADDR(0x04, 0x00, 0x00)
#define GSM3_PATCH_TEMP  GS_MT32_ADDR(0x03, 0x00, 0x00)
#define GSM3_RHYTHM      GS_MT32_ADDR(0x03, 0x01, 0x10)

/* bit n: partial n of this timbre can sound here (switched on, synthesiser kind) */
static int gsm3_sounding(const struct gs_mt32_timbre *t)
{
	int i, m = 0;
	for (i = 0; i < 4; i++) {
		int s = i < 2 ? t->structure12 : t->structure34;
		int pcm = (i & 1) ? gsm3_pcm2[s] : gsm3_pcm1[s];
		if ((t->mute & (1 << i)) && !pcm)
			m |= 1 << i;
	}
	return m;
}

static void gsm3_controllers(struct gs_mt32_part *p)
{
	p->volume = 100;
	p->expression = 127;
	p->mod = 0;
	p->hold = 0;
	p->bend = 0;
}

static void gsm3_power_on(struct gs_mt32 *s)
{
	int i;
	for (i = 0; i <= GS_MT32_PARTS; i++) {
		s->part[i].known = 0;
		s->part[i].from_rom = 0;
		gsm3_controllers(&s->part[i]);
	}
	for (i = 0; i < GS_MT32_NOTES; i++)
		s->note[i].active = 0;
	s->resets_seen = s->mem.resets;
}

void gs_mt32_init(struct gs_mt32 *s, uint32_t rate)
{
	memset(s, 0, sizeof *s);
	s->mem.device = 0x10;
	gs_mt32mem_init(&s->mem);
	s->rate = rate ? rate : 32000;
	gsm3_power_on(s);
}

/* a part's patch names its timbre: one in timbre memory, or in the user's
 * ROM (groups A and B, step 4), is copied to the part and is known; any
 * other is not */
static void gsm3_load_timbre(struct gs_mt32 *s, int part)
{
	const uint8_t *pt = s->mem.patch_temp[part], *rt;
	s->part[part].from_rom = 0;
	if (pt[GS_MT32_P_GROUP] == GS_MT32_GROUP_MEMORY) {
		memcpy(s->mem.timbre_temp[part], s->mem.timbre[pt[GS_MT32_P_NUMBER] & 63], GS_MT32_TIMBRE_SIZE);
		s->part[part].known = 1;
	} else if ((rt = gs_mt32rom_timbre(s->rom, pt[GS_MT32_P_GROUP], pt[GS_MT32_P_NUMBER] & 63)) != 0) {
		memcpy(s->mem.timbre_temp[part], rt, GS_MT32_TIMBRE_SIZE);
		s->part[part].known = 1;
		s->part[part].from_rom = 1;
	} else
		s->part[part].known = 0;
}

/* the parts' timbres again, from their patches (after a reset or a new ROM) */
static void gsm3_load_all(struct gs_mt32 *s)
{
	int i;
	for (i = 0; i < GS_MT32_PARTS; i++)
		gsm3_load_timbre(s, i);
}

void gs_mt32_use_rom(struct gs_mt32 *s, const struct gs_mt32rom *rom)
{
	s->rom = rom && rom->layout ? rom : 0;
	gsm3_load_all(s);
}

int gs_mt32_sysex(struct gs_mt32 *s, const uint8_t *data, uint32_t len)
{
	int r = gs_mt32mem_sysex(&s->mem, data, len);
	uint32_t addr, n, i;
	if (r != 1)
		return r;
	if (s->mem.resets != s->resets_seen) {
		gsm3_power_on(s);
		gsm3_load_all(s);
		return r;
	}
	if (len && data[0] == 0xF0) { data++; len--; }
	if (len && data[len - 1] == 0xF7) len--;
	if (len < 8)
		return r;
	addr = GS_MT32_ADDR(data[4], data[5], data[6]);
	n = len - 8;                                    /* data bytes: less the header, address and checksum */
	for (i = 0; i < GS_MT32_PARTS; i++) {
		uint32_t t0 = GSM3_TIMBRE_TEMP + i * GS_MT32_TIMBRE_SIZE, p0 = GSM3_PATCH_TEMP + i * GS_MT32_PATCH_TEMP;
		if (addr < t0 + GS_MT32_TIMBRE_SIZE && addr + n > t0) {
			s->part[i].known = 1;                   /* the song wrote this part's timbre itself */
			s->part[i].from_rom = 0;
		}
		if (addr < p0 + 2 && addr + n > p0)
			gsm3_load_timbre(s, (int)i);            /* it changed which timbre the part's patch names */
	}
	return r;
}

/* the timbre a note on this part and key plays, or 0 when it is not one we can sound */
static int gsm3_timbre(const struct gs_mt32 *s, int part, int key, struct gs_mt32_timbre *t)
{
	if (part == GS_MT32_RHYTHM) {
		const uint8_t *r;
		if (key < 24 || key > 108)
			return 0;
		r = s->mem.rhythm[key - 24];
		if (r[0] >= GS_MT32_TIMBRES)                /* 64 and up are the ROM's rhythm sounds */
			return 0;
		gs_mt32_timbre_decode(s->mem.timbre[r[0]], t);
	} else {
		if (!s->part[part].known)
			return 0;
		gs_mt32_timbre_decode(s->mem.timbre_temp[part], t);
		if (s->part[part].from_rom && gsm3_sounding(t) != (t->mute & 15))
			return 0;                               /* a sampled partial is on: not until the PCM ROM is read */
	}
	return gsm3_sounding(t) != 0;
}

int gs_mt32_playable(const struct gs_mt32 *s, int channel, int key)
{
	struct gs_mt32_timbre t;
	int part = gs_mt32mem_part(&s->mem, channel);
	return part >= 0 && gsm3_timbre(s, part, key, &t);
}

static int gsm3_count(uint8_t used)
{
	return (used & 1) + ((used >> 1) & 1) + ((used >> 2) & 1) + ((used >> 3) & 1);
}

int gs_mt32_partials(const struct gs_mt32 *s)
{
	int i, n = 0;
	for (i = 0; i < GS_MT32_NOTES; i++)
		if (s->note[i].active)
			n += gsm3_count(s->note[i].used);
	return n;
}

static int gsm3_part_partials(const struct gs_mt32 *s, int part)
{
	int i, n = 0;
	for (i = 0; i < GS_MT32_NOTES; i++)
		if (s->note[i].active && s->note[i].part == part)
			n += gsm3_count(s->note[i].used);
	return n;
}

/* The note to cut so another can start: the oldest whose key is up; else
 * the oldest on a part holding more partials than its reserve; else, when
 * the new note's part gives way to new notes, that part's oldest. -1: none. */
static int gsm3_victim(const struct gs_mt32 *s, int part, int give_way)
{
	int i, pass, best = -1;
	for (pass = 0; pass < 3 && best < 0; pass++)
		for (i = 0; i < GS_MT32_NOTES; i++) {
			const struct gs_mt32_note *n = &s->note[i];
			if (!n->active)
				continue;
			if (pass == 0 && !n->released)
				continue;
			if (pass == 1 && gsm3_part_partials(s, n->part) <= s->mem.system[GS_MT32_S_RESERVE + n->part])
				continue;
			if (pass == 2 && (!give_way || n->part != part))
				continue;
			if (best < 0 || n->age < s->note[best].age)
				best = i;
		}
	return best;
}

static int32_t gsm3_bend_cents(const struct gs_mt32 *s, int part)
{
	int range = part == GS_MT32_RHYTHM ? 0 : s->mem.patch_temp[part][GS_MT32_P_BEND];
	int32_t c = (int32_t)s->part[part].bend * range * 100 / 8192;
	if (part != GS_MT32_RHYTHM)
		c += (int32_t)s->mem.patch_temp[part][GS_MT32_P_FINETUNE] - 50;     /* the patch's fine tune, in cents */
	return c;
}

static void gsm3_control(struct gs_mt32 *s, int part)
{
	int i, k;
	int32_t c = gsm3_bend_cents(s, part);
	for (i = 0; i < GS_MT32_NOTES; i++)
		if (s->note[i].active && s->note[i].part == part)
			for (k = 0; k < 4; k++)
				if (s->note[i].used & (1 << k))
					gs_la_partial_control(&s->note[i].p[k], c, s->part[part].mod);
}

static void gsm3_release(struct gs_mt32_note *n)
{
	int k;
	if (n->released)
		return;
	n->released = 1;
	n->held = 0;
	for (k = 0; k < 4; k++)
		if (n->used & (1 << k))
			gs_la_partial_release(&n->p[k]);
}

static void gsm3_note_off(struct gs_mt32 *s, int part, int key)
{
	int i;
	for (i = 0; i < GS_MT32_NOTES; i++) {
		struct gs_mt32_note *n = &s->note[i];
		if (!n->active || n->part != part || n->key != key || n->released || n->held)
			continue;
		if (s->part[part].hold)
			n->held = 1;                            /* the pedal keeps it until it comes up */
		else
			gsm3_release(n);
	}
}

static void gsm3_note_on(struct gs_mt32 *s, int part, int key, int vel)
{
	struct gs_mt32_timbre t;
	struct gs_mt32_note *n = 0;
	int i, k, used, need, play, assign, level, pan;
	if (!gsm3_timbre(s, part, key, &t))
		return;
	used = gsm3_sounding(&t);
	need = gsm3_count((uint8_t)used);
	if (part == GS_MT32_RHYTHM) {
		assign = 2;                                 /* a drum struck again rings over itself */
		level = s->mem.rhythm[key - 24][1];
		pan = s->mem.rhythm[key - 24][2];
		play = 60;                                  /* a rhythm timbre sounds at its own pitch */
	} else {
		const uint8_t *pt = s->mem.patch_temp[part];
		assign = pt[GS_MT32_P_ASSIGN];
		level = pt[GS_MT32_P_LEVEL];
		pan = pt[GS_MT32_P_PAN];
		play = key + pt[GS_MT32_P_KEYSHIFT] - 24;
		if (play < 0) play = 0;
		if (play > 127) play = 127;
	}
	/* Assign modes, our reading of POLY 1-4: 1 and 2 give a key one note
	 * (struck again, the old one stops); 3 and 4 let it ring over itself.
	 * 1 and 3 give way to new notes when partials run out; 2 and 4 keep
	 * what is sounding and the new note is lost. */
	if (assign < 2)
		for (i = 0; i < GS_MT32_NOTES; i++)
			if (s->note[i].active && s->note[i].part == part && s->note[i].key == key)
				s->note[i].active = 0;
	while (gs_mt32_partials(s) + need > GS_MT32_PARTIALS) {
		int v = gsm3_victim(s, part, !(assign & 1));
		if (v < 0) {
			s->dropped++;
			return;
		}
		s->note[v].active = 0;
		s->stolen++;
	}
	for (i = 0; i < GS_MT32_NOTES && !n; i++)
		if (!s->note[i].active)
			n = &s->note[i];
	if (!n) {                                       /* 32 notes of one partial each fill both at once */
		s->dropped++;
		return;
	}
	n->active = 1;
	n->part = (uint8_t)part;
	n->key = (uint8_t)key;
	n->released = n->held = 0;
	n->used = (uint8_t)used;
	n->structure[0] = t.structure12;
	n->structure[1] = t.structure34;
	n->level = (uint8_t)level;
	n->pan = (uint8_t)pan;
	n->age = ++s->age;
	for (k = 0; k < 4; k++)
		if (used & (1 << k)) {
			gs_la_partial_start(&n->p[k], &t.partial[k], play, vel, t.no_sustain, s->rate);
			gs_la_partial_control(&n->p[k], gsm3_bend_cents(s, part), s->part[part].mod);
		}
}

void gs_mt32_midi(struct gs_mt32 *s, uint8_t status, uint8_t d1, uint8_t d2)
{
	int part = gs_mt32mem_part(&s->mem, status & 15), i;
	struct gs_mt32_part *p;
	if (part < 0)
		return;
	p = &s->part[part];
	d1 &= 127;
	d2 &= 127;
	switch (status & 0xF0) {
	case 0x80:
		gsm3_note_off(s, part, d1);
		break;
	case 0x90:
		if (d2) gsm3_note_on(s, part, d1, d2);
		else gsm3_note_off(s, part, d1);
		break;
	case 0xB0:
		switch (d1) {
		case 1: p->mod = d2; gsm3_control(s, part); break;
		case 7: p->volume = d2 > 100 ? 100 : d2; break;     /* the MT-32's part volume stops at 100 */
		case 10:                                    /* the MT-32 pans the other way round: 0 is right */
			if (part != GS_MT32_RHYTHM)
				s->mem.patch_temp[part][GS_MT32_P_PAN] = (uint8_t)(d2 * 14 / 127);
			break;
		case 11: p->expression = d2; break;
		case 64:
			p->hold = d2 >= 64;
			if (!p->hold)
				for (i = 0; i < GS_MT32_NOTES; i++)
					if (s->note[i].active && s->note[i].part == part && s->note[i].held)
						gsm3_release(&s->note[i]);
			break;
		case 121:
			gsm3_controllers(p);
			gsm3_control(s, part);
			break;
		case 123:
			for (i = 0; i < GS_MT32_NOTES; i++)
				if (s->note[i].active && s->note[i].part == part)
					gsm3_release(&s->note[i]);
			break;
		default: break;
		}
		break;
	case 0xC0:
		if (part != GS_MT32_RHYTHM) {
			memcpy(s->mem.patch_temp[part], s->mem.patch[d1], GS_MT32_PATCH_SIZE);
			gsm3_load_timbre(s, part);
		}
		break;
	case 0xE0:
		p->bend = (int16_t)((((int)d2 << 7) | d1) - 8192);
		gsm3_control(s, part);
		break;
	default: break;
	}
}

static int32_t gsm3_ring(int32_t a, int32_t b)
{
	int64_t r = ((int64_t)a * b) >> 15;
	if (r > 0x3FFFFF) r = 0x3FFFFF;
	if (r < -0x3FFFFF) r = -0x3FFFFF;
	return (int32_t)r;
}

void gs_mt32_render(struct gs_mt32 *s, int32_t *left, int32_t *right, uint32_t frames)
{
	int32_t a[GS_MT32_CHUNK], b[GS_MT32_CHUNK];
	while (frames) {
		uint32_t n = frames > GS_MT32_CHUNK ? GS_MT32_CHUNK : frames, f;
		int i, pair;
		for (i = 0; i < GS_MT32_NOTES; i++) {
			struct gs_mt32_note *nt = &s->note[i];
			const struct gs_mt32_part *pp;
			int32_t g, gl, gr;
			if (!nt->active)
				continue;
			pp = &s->part[nt->part];
			/* level 0-100, volume 0-100, expression 0-127, and a quarter for headroom: Q15 */
			g = (int32_t)(((int64_t)nt->level * pp->volume * pp->expression * 8192) / (100 * 100 * 127));
			gr = g * (14 - nt->pan) / 14;           /* pan 0 is right, 14 left */
			gl = g * nt->pan / 14;
			for (pair = 0; pair < 2; pair++) {
				int u1 = nt->used & (1 << (pair * 2)), u2 = nt->used & (2 << (pair * 2));
				int join = gsm3_join[nt->structure[pair]];
				if (!u1 && !u2)
					continue;
				memset(a, 0, n * sizeof a[0]);
				memset(b, 0, n * sizeof b[0]);
				if (u1 && !gs_la_partial_render(&nt->p[pair * 2], a, n))
					nt->used &= (uint8_t)~(1 << (pair * 2));
				if (u2 && !gs_la_partial_render(&nt->p[pair * 2 + 1], b, n))
					nt->used &= (uint8_t)~(2 << (pair * 2));
				for (f = 0; f < n; f++) {
					int32_t v = join == GSM3_MIX ? a[f] + b[f]
					          : join == GSM3_BOTH ? a[f] + gsm3_ring(a[f], b[f])
					          : gsm3_ring(a[f], b[f]);
					left[f] += (int32_t)(((int64_t)v * gl) >> 15);
					right[f] += (int32_t)(((int64_t)v * gr) >> 15);
				}
			}
			if (!nt->used)
				nt->active = 0;
		}
		left += n;
		right += n;
		frames -= n;
	}
}
