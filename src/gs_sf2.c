/* SoundFont 2 reader (gs_sf2.h), from the SoundFont 2.04 specification.
 * MIT, Copyright (c) 2026 AmiCradle Team. */
#include "gs_sf2.h"

#define PHDR_SIZE 38
#define BAG_SIZE 4
#define GEN_SIZE 4
#define INST_SIZE 22
#define SHDR_SIZE 46

static uint32_t le32(const uint8_t *p)
{
	return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static uint16_t le16(const uint8_t *p)
{
	return (uint16_t)(p[0] | p[1] << 8);
}

static int id_is(const uint8_t *p, const char *id)
{
	return p[0] == (uint8_t)id[0] && p[1] == (uint8_t)id[1] && p[2] == (uint8_t)id[2] &&
	       p[3] == (uint8_t)id[3];
}

/* the chunk id inside a list [p, end), or 0 */
static const uint8_t *find_chunk(const uint8_t *p, const uint8_t *end, const char *id, uint32_t *len)
{
	while (p + 8 <= end) {
		uint32_t n = le32(p + 4);
		if (n > (uint32_t)(end - p) - 8)
			return 0;
		if (id_is(p, id)) {
			*len = n;
			return p + 8;
		}
		p += 8 + n + (n & 1);
	}
	return 0;
}

static const uint8_t *find_list(const uint8_t *p, const uint8_t *end, const char *type, uint32_t *len)
{
	while (p + 12 <= end) {
		uint32_t n = le32(p + 4);
		if (n > (uint32_t)(end - p) - 8)
			return 0;
		if (id_is(p, "LIST") && n >= 4 && id_is(p + 8, type)) {
			*len = n - 4;
			return p + 12;
		}
		p += 8 + n + (n & 1);
	}
	return 0;
}

static int table(const uint8_t *list, uint32_t llen, const char *id, uint32_t rec,
                 const uint8_t **out, uint32_t *count)
{
	uint32_t n;
	const uint8_t *c = find_chunk(list, list + llen, id, &n);
	if (!c || n % rec || n / rec < 2)        /* each table ends with a terminal record */
		return -1;
	*out = c;
	*count = n / rec;
	return 0;
}

int gs_sf2_open(struct gs_sf2 *f, uint8_t *data, uint32_t len)
{
	const uint8_t *body, *list, *sm;
	uint32_t blen, llen, smlen, i;
	union { uint16_t u; uint8_t b[2]; } probe;
	if (len < 12 || !id_is(data, "RIFF") || !id_is(data + 8, "sfbk"))
		return -1;
	blen = le32(data + 4);
	if (blen > len - 8)
		blen = len - 8;
	body = data + 12;
	blen -= 4;
	list = find_list(body, body + blen, "sdta", &llen);
	if (!list)
		return -1;
	sm = find_chunk(list, list + llen, "smpl", &smlen);
	if (!sm || ((uintptr_t)sm & 1))
		return -1;
	f->smpl = (int16_t *)(uintptr_t)sm;
	f->nsmpl = smlen / 2;
	list = find_list(body, body + blen, "pdta", &llen);
	if (!list ||
	    table(list, llen, "phdr", PHDR_SIZE, &f->phdr, &f->nphdr) ||
	    table(list, llen, "pbag", BAG_SIZE, &f->pbag, &f->npbag) ||
	    table(list, llen, "pgen", GEN_SIZE, &f->pgen, &f->npgen) ||
	    table(list, llen, "inst", INST_SIZE, &f->inst, &f->ninst) ||
	    table(list, llen, "ibag", BAG_SIZE, &f->ibag, &f->nibag) ||
	    table(list, llen, "igen", GEN_SIZE, &f->igen, &f->nigen) ||
	    table(list, llen, "shdr", SHDR_SIZE, &f->shdr, &f->nshdr))
		return -1;
	/* the samples are little-endian in the file */
	probe.u = 1;
	if (!probe.b[0]) {
		uint8_t *s = (uint8_t *)f->smpl, t;
		for (i = 0; i < f->nsmpl; i++, s += 2) {
			t = s[0]; s[0] = s[1]; s[1] = t;
		}
	}
	return 0;
}

int gs_sf2_preset(const struct gs_sf2 *f, int bank, int program)
{
	uint32_t i;
	for (i = 0; i + 1 < f->nphdr; i++) {
		const uint8_t *p = f->phdr + i * PHDR_SIZE;
		if (le16(p + 20) == program && le16(p + 22) == bank)
			return (int)i;
	}
	return -1;
}

const char *gs_sf2_preset_name(const struct gs_sf2 *f, int p)
{
	return (const char *)(f->phdr + (uint32_t)p * PHDR_SIZE);
}

static void set_defaults(int16_t *g)
{
	int i;
	for (i = 0; i < GEN_COUNT; i++)
		g[i] = 0;
	g[GEN_initialFilterFc] = 13500;
	g[GEN_delayVolEnv] = g[GEN_attackVolEnv] = g[GEN_holdVolEnv] = -12000;
	g[GEN_decayVolEnv] = g[GEN_releaseVolEnv] = -12000;
	g[GEN_keyRange] = g[GEN_velRange] = 0x7F00;    /* lo 0, hi 127 */
	g[GEN_keynum] = g[GEN_velocity] = -1;
	g[GEN_scaleTuning] = 100;
	g[GEN_overridingRootKey] = -1;
}

/* applies a bag's generators onto g; set[] marks those present */
static void apply(const uint8_t *gens, uint32_t from, uint32_t to, uint32_t ngen, int16_t *g, uint8_t *set)
{
	uint32_t i;
	if (to > ngen)
		to = ngen;
	for (i = from; i < to; i++) {
		const uint8_t *r = gens + i * GEN_SIZE;
		uint16_t op = le16(r);
		if (op < GEN_COUNT) {
			g[op] = (int16_t)le16(r + 2);
			if (set)
				set[op] = 1;
		}
	}
}

static int in_range(int16_t r, int v)
{
	int lo = r & 0xFF, hi = (r >> 8) & 0xFF;
	return v >= lo && v <= hi;
}

/* the bag's last generator is op (a zone that names an instrument/sample) */
static int names(const uint8_t *gens, uint32_t from, uint32_t to, uint32_t ngen, uint16_t op)
{
	if (to > ngen)
		to = ngen;
	return to > from && le16(gens + (to - 1) * GEN_SIZE) == op;
}

/* generators a preset zone may not add to (SF2 8.5) */
static int not_additive(int op)
{
	switch (op) {
	case GEN_startAddrsOffset: case GEN_endAddrsOffset: case GEN_startloopAddrsOffset:
	case GEN_endloopAddrsOffset: case GEN_startAddrsCoarseOffset: case GEN_endAddrsCoarseOffset:
	case GEN_startloopAddrsCoarseOffset: case GEN_endloopAddrsCoarseOffset:
	case GEN_instrument: case GEN_keyRange: case GEN_velRange: case GEN_keynum: case GEN_velocity:
	case GEN_sampleID: case GEN_sampleModes: case GEN_exclusiveClass: case GEN_overridingRootKey:
		return 1;
	}
	return 0;
}

static uint32_t clampu(int64_t v, uint32_t max)
{
	return v < 0 ? 0 : v > max ? max : (uint32_t)v;
}

int gs_sf2_zones(const struct gs_sf2 *f, int p, int key, int vel, struct gs_zone *out, int max)
{
	const uint8_t *ph;
	uint32_t pb0, pb1, b, n = 0;
	int16_t pglob[GEN_COUNT], pg[GEN_COUNT];
	if (p < 0 || (uint32_t)p + 1 >= f->nphdr)
		return 0;
	ph = f->phdr + (uint32_t)p * PHDR_SIZE;
	pb0 = le16(ph + 24);
	pb1 = le16(ph + PHDR_SIZE + 24);
	if (pb1 > f->npbag - 1)
		pb1 = f->npbag - 1;
	{
		int i;
		for (i = 0; i < GEN_COUNT; i++)
			pglob[i] = 0;
		pglob[GEN_keyRange] = pglob[GEN_velRange] = 0x7F00;
	}
	for (b = pb0; b < pb1; b++) {
		uint32_t g0 = le16(f->pbag + b * BAG_SIZE), g1 = le16(f->pbag + (b + 1) * BAG_SIZE);
		uint32_t inst, ib0, ib1, ib;
		int16_t iglob[GEN_COUNT];
		int i;
		if (!names(f->pgen, g0, g1, f->npgen, GEN_instrument)) {
			if (b == pb0)                    /* the preset's global zone */
				apply(f->pgen, g0, g1, f->npgen, pglob, 0);
			continue;
		}
		for (i = 0; i < GEN_COUNT; i++)
			pg[i] = pglob[i];
		apply(f->pgen, g0, g1, f->npgen, pg, 0);
		if (!in_range(pg[GEN_keyRange], key) || !in_range(pg[GEN_velRange], vel))
			continue;
		inst = (uint16_t)pg[GEN_instrument];
		if (inst + 1 >= f->ninst)
			continue;
		ib0 = le16(f->inst + inst * INST_SIZE + 20);
		ib1 = le16(f->inst + (inst + 1) * INST_SIZE + 20);
		if (ib1 > f->nibag - 1)
			ib1 = f->nibag - 1;
		set_defaults(iglob);
		for (ib = ib0; ib < ib1; ib++) {
			uint32_t h0 = le16(f->ibag + ib * BAG_SIZE), h1 = le16(f->ibag + (ib + 1) * BAG_SIZE);
			struct gs_zone *z;
			const uint8_t *sh;
			uint32_t sid;
			int j;
			if (!names(f->igen, h0, h1, f->nigen, GEN_sampleID)) {
				if (ib == ib0)
					apply(f->igen, h0, h1, f->nigen, iglob, 0);
				continue;
			}
			if (n >= (uint32_t)max)
				return (int)n;
			z = &out[n];
			for (j = 0; j < GEN_COUNT; j++)
				z->gen[j] = iglob[j];
			apply(f->igen, h0, h1, f->nigen, z->gen, 0);
			if (!in_range(z->gen[GEN_keyRange], key) || !in_range(z->gen[GEN_velRange], vel))
				continue;
			sid = (uint16_t)z->gen[GEN_sampleID];
			if (sid + 1 >= f->nshdr)
				continue;
			sh = f->shdr + sid * SHDR_SIZE;
			if (le16(sh + 44) & 0x8000)          /* a ROM sample: not in the file */
				continue;
			for (j = 0; j < GEN_COUNT; j++)
				if (!not_additive(j) && pg[j])
					z->gen[j] = (int16_t)(z->gen[j] + pg[j]);
			z->start = clampu((int64_t)le32(sh + 20) + z->gen[GEN_startAddrsOffset] +
			                  32768 * (int64_t)z->gen[GEN_startAddrsCoarseOffset], f->nsmpl);
			z->end = clampu((int64_t)le32(sh + 24) + z->gen[GEN_endAddrsOffset] +
			                32768 * (int64_t)z->gen[GEN_endAddrsCoarseOffset], f->nsmpl);
			z->loopstart = clampu((int64_t)le32(sh + 28) + z->gen[GEN_startloopAddrsOffset] +
			                      32768 * (int64_t)z->gen[GEN_startloopAddrsCoarseOffset], f->nsmpl);
			z->loopend = clampu((int64_t)le32(sh + 32) + z->gen[GEN_endloopAddrsOffset] +
			                    32768 * (int64_t)z->gen[GEN_endloopAddrsCoarseOffset], f->nsmpl);
			if (z->end <= z->start)
				continue;
			if (z->loopend <= z->loopstart || z->loopstart < z->start || z->loopend > z->end)
				z->gen[GEN_sampleModes] = 0;     /* a bad loop plays once */
			z->rate = le32(sh + 36);
			if (z->rate < 400 || z->rate > 200000)
				z->rate = 44100;
			z->root = sh[40] <= 127 ? sh[40] : 60;
			if (z->gen[GEN_overridingRootKey] >= 0 && z->gen[GEN_overridingRootKey] <= 127)
				z->root = (uint8_t)z->gen[GEN_overridingRootKey];
			z->correction = (int8_t)sh[41];
			n++;
		}
	}
	return (int)n;
}
