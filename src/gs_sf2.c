/* SoundFont 2 reader (gs_sf2.h), from the SoundFont 2.04 specification.
 * MIT, Copyright (c) 2026 AmiCradle Team. */
#include "gs_sf2.h"

#define GSF_PHDR_SIZE 38
#define GSF_BAG_SIZE 4
#define GSF_GEN_SIZE 4
#define GSF_INST_SIZE 22
#define GSF_SHDR_SIZE 46

static uint32_t gsf_le32(const uint8_t *p)
{
	return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static uint16_t gsf_le16(const uint8_t *p)
{
	return (uint16_t)(p[0] | p[1] << 8);
}

static int gsf_id_is(const uint8_t *p, const char *id)
{
	return p[0] == (uint8_t)id[0] && p[1] == (uint8_t)id[1] && p[2] == (uint8_t)id[2] &&
	       p[3] == (uint8_t)id[3];
}

/* the chunk id inside a list [p, end), or 0 */
static const uint8_t *gsf_find_chunk(const uint8_t *p, const uint8_t *end, const char *id, uint32_t *len)
{
	while (p + 8 <= end) {
		uint32_t n = gsf_le32(p + 4);
		if (n > (uint32_t)(end - p) - 8)
			return 0;
		if (gsf_id_is(p, id)) {
			*len = n;
			return p + 8;
		}
		p += 8 + n + (n & 1);
	}
	return 0;
}

static const uint8_t *gsf_find_list(const uint8_t *p, const uint8_t *end, const char *type, uint32_t *len)
{
	while (p + 12 <= end) {
		uint32_t n = gsf_le32(p + 4);
		if (n > (uint32_t)(end - p) - 8)
			return 0;
		if (gsf_id_is(p, "LIST") && n >= 4 && gsf_id_is(p + 8, type)) {
			*len = n - 4;
			return p + 12;
		}
		p += 8 + n + (n & 1);
	}
	return 0;
}

static int gsf_table(const uint8_t *list, uint32_t llen, const char *id, uint32_t rec,
                 const uint8_t **out, uint32_t *count)
{
	uint32_t n;
	const uint8_t *c = gsf_find_chunk(list, list + llen, id, &n);
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
	if (len < 12 || !gsf_id_is(data, "RIFF") || !gsf_id_is(data + 8, "sfbk"))
		return -1;
	blen = gsf_le32(data + 4);
	if (blen > len - 8)
		blen = len - 8;
	body = data + 12;
	blen -= 4;
	list = gsf_find_list(body, body + blen, "sdta", &llen);
	if (!list)
		return -1;
	sm = gsf_find_chunk(list, list + llen, "smpl", &smlen);
	if (!sm || ((uintptr_t)sm & 1))
		return -1;
	f->smpl = (int16_t *)(uintptr_t)sm;
	f->nsmpl = smlen / 2;
	list = gsf_find_list(body, body + blen, "pdta", &llen);
	if (!list ||
	    gsf_table(list, llen, "phdr", GSF_PHDR_SIZE, &f->phdr, &f->nphdr) ||
	    gsf_table(list, llen, "pbag", GSF_BAG_SIZE, &f->pbag, &f->npbag) ||
	    gsf_table(list, llen, "pgen", GSF_GEN_SIZE, &f->pgen, &f->npgen) ||
	    gsf_table(list, llen, "inst", GSF_INST_SIZE, &f->inst, &f->ninst) ||
	    gsf_table(list, llen, "ibag", GSF_BAG_SIZE, &f->ibag, &f->nibag) ||
	    gsf_table(list, llen, "igen", GSF_GEN_SIZE, &f->igen, &f->nigen) ||
	    gsf_table(list, llen, "shdr", GSF_SHDR_SIZE, &f->shdr, &f->nshdr))
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
		const uint8_t *p = f->phdr + i * GSF_PHDR_SIZE;
		if (gsf_le16(p + 20) == program && gsf_le16(p + 22) == bank)
			return (int)i;
	}
	return -1;
}

const char *gs_sf2_preset_name(const struct gs_sf2 *f, int p)
{
	return (const char *)(f->phdr + (uint32_t)p * GSF_PHDR_SIZE);
}

static void gsf_set_defaults(int16_t *g)
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
static void gsf_apply(const uint8_t *gens, uint32_t from, uint32_t to, uint32_t ngen, int16_t *g, uint8_t *set)
{
	uint32_t i;
	if (to > ngen)
		to = ngen;
	for (i = from; i < to; i++) {
		const uint8_t *r = gens + i * GSF_GEN_SIZE;
		uint16_t op = gsf_le16(r);
		if (op < GEN_COUNT) {
			g[op] = (int16_t)gsf_le16(r + 2);
			if (set)
				set[op] = 1;
		}
	}
}

static int gsf_in_range(int16_t r, int v)
{
	int lo = r & 0xFF, hi = (r >> 8) & 0xFF;
	return v >= lo && v <= hi;
}

/* the bag's last generator is op (a zone that names an instrument/sample) */
static int gsf_names(const uint8_t *gens, uint32_t from, uint32_t to, uint32_t ngen, uint16_t op)
{
	if (to > ngen)
		to = ngen;
	return to > from && gsf_le16(gens + (to - 1) * GSF_GEN_SIZE) == op;
}

/* generators a preset zone may not add to (SF2 8.5) */
static int gsf_not_additive(int op)
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

static uint32_t gsf_clampu(int64_t v, uint32_t max)
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
	ph = f->phdr + (uint32_t)p * GSF_PHDR_SIZE;
	pb0 = gsf_le16(ph + 24);
	pb1 = gsf_le16(ph + GSF_PHDR_SIZE + 24);
	if (pb1 > f->npbag - 1)
		pb1 = f->npbag - 1;
	{
		int i;
		for (i = 0; i < GEN_COUNT; i++)
			pglob[i] = 0;
		pglob[GEN_keyRange] = pglob[GEN_velRange] = 0x7F00;
	}
	for (b = pb0; b < pb1; b++) {
		uint32_t g0 = gsf_le16(f->pbag + b * GSF_BAG_SIZE), g1 = gsf_le16(f->pbag + (b + 1) * GSF_BAG_SIZE);
		uint32_t inst, ib0, ib1, ib;
		int16_t iglob[GEN_COUNT];
		int i;
		if (!gsf_names(f->pgen, g0, g1, f->npgen, GEN_instrument)) {
			if (b == pb0)                    /* the preset's global zone */
				gsf_apply(f->pgen, g0, g1, f->npgen, pglob, 0);
			continue;
		}
		for (i = 0; i < GEN_COUNT; i++)
			pg[i] = pglob[i];
		gsf_apply(f->pgen, g0, g1, f->npgen, pg, 0);
		if (!gsf_in_range(pg[GEN_keyRange], key) || !gsf_in_range(pg[GEN_velRange], vel))
			continue;
		inst = (uint16_t)pg[GEN_instrument];
		if (inst + 1 >= f->ninst)
			continue;
		ib0 = gsf_le16(f->inst + inst * GSF_INST_SIZE + 20);
		ib1 = gsf_le16(f->inst + (inst + 1) * GSF_INST_SIZE + 20);
		if (ib1 > f->nibag - 1)
			ib1 = f->nibag - 1;
		gsf_set_defaults(iglob);
		for (ib = ib0; ib < ib1; ib++) {
			uint32_t h0 = gsf_le16(f->ibag + ib * GSF_BAG_SIZE), h1 = gsf_le16(f->ibag + (ib + 1) * GSF_BAG_SIZE);
			struct gs_zone *z;
			const uint8_t *sh;
			uint32_t sid;
			int j;
			if (!gsf_names(f->igen, h0, h1, f->nigen, GEN_sampleID)) {
				if (ib == ib0)
					gsf_apply(f->igen, h0, h1, f->nigen, iglob, 0);
				continue;
			}
			if (n >= (uint32_t)max)
				return (int)n;
			z = &out[n];
			for (j = 0; j < GEN_COUNT; j++)
				z->gen[j] = iglob[j];
			gsf_apply(f->igen, h0, h1, f->nigen, z->gen, 0);
			if (!gsf_in_range(z->gen[GEN_keyRange], key) || !gsf_in_range(z->gen[GEN_velRange], vel))
				continue;
			sid = (uint16_t)z->gen[GEN_sampleID];
			if (sid + 1 >= f->nshdr)
				continue;
			sh = f->shdr + sid * GSF_SHDR_SIZE;
			if (gsf_le16(sh + 44) & 0x8000)          /* a ROM sample: not in the file */
				continue;
			for (j = 0; j < GEN_COUNT; j++)
				if (!gsf_not_additive(j) && pg[j])
					z->gen[j] = (int16_t)(z->gen[j] + pg[j]);
			z->start = gsf_clampu((int64_t)gsf_le32(sh + 20) + z->gen[GEN_startAddrsOffset] +
			                  32768 * (int64_t)z->gen[GEN_startAddrsCoarseOffset], f->nsmpl);
			z->end = gsf_clampu((int64_t)gsf_le32(sh + 24) + z->gen[GEN_endAddrsOffset] +
			                32768 * (int64_t)z->gen[GEN_endAddrsCoarseOffset], f->nsmpl);
			z->loopstart = gsf_clampu((int64_t)gsf_le32(sh + 28) + z->gen[GEN_startloopAddrsOffset] +
			                      32768 * (int64_t)z->gen[GEN_startloopAddrsCoarseOffset], f->nsmpl);
			z->loopend = gsf_clampu((int64_t)gsf_le32(sh + 32) + z->gen[GEN_endloopAddrsOffset] +
			                    32768 * (int64_t)z->gen[GEN_endloopAddrsCoarseOffset], f->nsmpl);
			if (z->end <= z->start)
				continue;
			if (z->loopend <= z->loopstart || z->loopstart < z->start || z->loopend > z->end)
				z->gen[GEN_sampleModes] = 0;     /* a bad loop plays once */
			z->rate = gsf_le32(sh + 36);
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
