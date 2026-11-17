/* MT-32 family ROMs known by SHA-1, joining halves and picking a set
 * (gs_rom.h). The table holds only checksums and names, never ROM data.
 * MIT, Copyright (c) 2026 AmiCradle Team. */
#include "gs_rom.h"

/* ---- SHA-1 (FIPS 180-4) ---- */

#define GSR_ROL(x, n) (((x) << (n)) | ((x) >> (32 - (n))))

static void gsr_sha1_block(uint32_t h[5], const uint8_t *p)
{
	uint32_t w[80], a, b, c, d, e, t;
	int i;
	for (i = 0; i < 16; i++)
		w[i] = (uint32_t)p[i * 4] << 24 | (uint32_t)p[i * 4 + 1] << 16 |
		       (uint32_t)p[i * 4 + 2] << 8 | p[i * 4 + 3];
	for (; i < 80; i++)
		w[i] = GSR_ROL(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
	a = h[0]; b = h[1]; c = h[2]; d = h[3]; e = h[4];
	for (i = 0; i < 80; i++) {
		if (i < 20) t = ((b & c) | (~b & d)) + 0x5A827999u;
		else if (i < 40) t = (b ^ c ^ d) + 0x6ED9EBA1u;
		else if (i < 60) t = ((b & c) | (b & d) | (c & d)) + 0x8F1BBCDCu;
		else t = (b ^ c ^ d) + 0xCA62C1D6u;
		t += GSR_ROL(a, 5) + e + w[i];
		e = d; d = c; c = GSR_ROL(b, 30); b = a; a = t;
	}
	h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e;
}

void gs_sha1(const uint8_t *data, uint32_t len, uint8_t out[20])
{
	uint32_t h[5] = { 0x67452301u, 0xEFCDAB89u, 0x98BADCFEu, 0x10325476u, 0xC3D2E1F0u };
	uint8_t tail[128];
	uint32_t full = len & ~63u, rest = len - full, i, n;
	uint32_t bitshi = len >> 29, bitslo = len << 3;
	for (i = 0; i < full; i += 64)
		gsr_sha1_block(h, data + i);
	for (i = 0; i < rest; i++)
		tail[i] = data[full + i];
	tail[rest] = 0x80;
	n = rest < 56 ? 64 : 128;
	for (i = rest + 1; i < n - 8; i++)
		tail[i] = 0;
	tail[n - 8] = (uint8_t)(bitshi >> 24); tail[n - 7] = (uint8_t)(bitshi >> 16);
	tail[n - 6] = (uint8_t)(bitshi >> 8);  tail[n - 5] = (uint8_t)bitshi;
	tail[n - 4] = (uint8_t)(bitslo >> 24); tail[n - 3] = (uint8_t)(bitslo >> 16);
	tail[n - 2] = (uint8_t)(bitslo >> 8);  tail[n - 1] = (uint8_t)bitslo;
	gsr_sha1_block(h, tail);
	if (n == 128)
		gsr_sha1_block(h, tail + 64);
	for (i = 0; i < 5; i++) {
		out[i * 4] = (uint8_t)(h[i] >> 24); out[i * 4 + 1] = (uint8_t)(h[i] >> 16);
		out[i * 4 + 2] = (uint8_t)(h[i] >> 8); out[i * 4 + 3] = (uint8_t)h[i];
	}
}

/* ---- the known ROMs (checksums as published by the Munt project and MAME) ---- */

static const struct gs_rom_info gsr_roms[] = {
	{ { 0x73, 0x68, 0x3d, 0x58, 0x5c, 0xd6, 0x94, 0x8c, 0xc1, 0x95, 0x47, 0x94, 0x2c, 0xa0, 0xe1, 0x4a, 0x03, 0x19, 0x45, 0x6d },
	  GS_ROM_CTRL, GS_PART_FULL, GS_MODEL_CM32L, 1, 65536u, 65536u, "CM-32L control 1.00", "cm32l_ctrl_1_00" },
	{ { 0xa4, 0x39, 0xfb, 0xb3, 0x90, 0xda, 0x38, 0xca, 0xda, 0x95, 0xa7, 0xcb, 0xb1, 0xd6, 0xca, 0x19, 0x9c, 0xd6, 0x6e, 0xf8 },
	  GS_ROM_CTRL, GS_PART_FULL, GS_MODEL_CM32L, 2, 65536u, 65536u, "CM-32L control 1.02", "cm32l_ctrl_1_02" },
	{ { 0x28, 0x9c, 0xc2, 0x98, 0xad, 0x53, 0x2b, 0x70, 0x24, 0x61, 0xbf, 0xc7, 0x38, 0x00, 0x9d, 0x9e, 0xbe, 0x80, 0x25, 0xea },
	  GS_ROM_PCM, GS_PART_FULL, GS_PCM_CM32L, 0, 1048576u, 1048576u, "CM-32L PCM", "cm32l_pcm" },
	{ { 0x3a, 0xd8, 0x89, 0xfd, 0xe5, 0xdb, 0x5b, 0x64, 0x37, 0xcb, 0xc2, 0xeb, 0x6e, 0x30, 0x53, 0x12, 0xfe, 0xc3, 0xdf, 0x93 },
	  GS_ROM_PCM, GS_PART_CM32L_HIGH, GS_PCM_CM32L, 0, 524288u, 1048576u, "CM-32L PCM (high half)", "cm32l_pcm_h" },
	{ { 0xdc, 0x1c, 0x5b, 0x1b, 0x90, 0xa4, 0x64, 0x6d, 0x00, 0xf7, 0xda, 0xf3, 0x67, 0x97, 0x33, 0xc7, 0xba, 0xdc, 0x70, 0x77 },
	  GS_ROM_CTRL, GS_PART_FULL, GS_MODEL_CM32LN, 1, 65536u, 65536u, "CM-32LN control 1.00", "cm32ln_ctrl_1_00" },
	{ { 0x5a, 0x5c, 0xb5, 0xa7, 0x7d, 0x7d, 0x55, 0xee, 0x69, 0x65, 0x7c, 0x2f, 0x87, 0x04, 0x16, 0xda, 0xed, 0x52, 0xde, 0xa7 },
	  GS_ROM_CTRL, GS_PART_FULL, GS_MODEL_MT32_OLD, 2, 65536u, 65536u, "MT-32 control 1.04", "mt32_ctrl_1_04" },
	{ { 0x9c, 0xd4, 0x85, 0x80, 0x14, 0xc4, 0xe8, 0xa9, 0xdf, 0xf9, 0x60, 0x53, 0xf7, 0x84, 0xbf, 0xaa, 0xc1, 0x09, 0x2a, 0x2e },
	  GS_ROM_CTRL, GS_PART_EVEN, GS_MODEL_MT32_OLD, 2, 32768u, 65536u, "MT-32 control 1.04 (even bytes)", "mt32_ctrl_1_04_a" },
	{ { 0xfe, 0x8d, 0xb4, 0x69, 0xb5, 0xbf, 0xeb, 0x37, 0xed, 0xb2, 0x69, 0xfd, 0x47, 0xe3, 0xce, 0x6d, 0x91, 0x01, 0x46, 0x52 },
	  GS_ROM_CTRL, GS_PART_ODD, GS_MODEL_MT32_OLD, 2, 32768u, 65536u, "MT-32 control 1.04 (odd bytes)", "mt32_ctrl_1_04_b" },
	{ { 0xe1, 0x7a, 0x3a, 0x6d, 0x26, 0x5b, 0xf1, 0xfa, 0x15, 0x03, 0x12, 0x06, 0x11, 0x34, 0x29, 0x3d, 0x2b, 0x58, 0x28, 0x8c },
	  GS_ROM_CTRL, GS_PART_FULL, GS_MODEL_MT32_OLD, 3, 65536u, 65536u, "MT-32 control 1.05", "mt32_ctrl_1_05" },
	{ { 0x57, 0xa0, 0x9d, 0x80, 0xd2, 0xf7, 0xca, 0x5b, 0x97, 0x34, 0xed, 0xbe, 0x96, 0x45, 0xe6, 0xe7, 0x00, 0xf8, 0x37, 0x01 },
	  GS_ROM_CTRL, GS_PART_EVEN, GS_MODEL_MT32_OLD, 3, 32768u, 65536u, "MT-32 control 1.05 (even bytes)", "mt32_ctrl_1_05_a" },
	{ { 0x52, 0xe3, 0xc6, 0x66, 0x6d, 0xb9, 0xef, 0x96, 0x25, 0x91, 0xa8, 0xee, 0x99, 0xbe, 0x0c, 0xde, 0x17, 0xf3, 0xa6, 0xb6 },
	  GS_ROM_CTRL, GS_PART_ODD, GS_MODEL_MT32_OLD, 3, 32768u, 65536u, "MT-32 control 1.05 (odd bytes)", "mt32_ctrl_1_05_b" },
	{ { 0xa5, 0x53, 0x48, 0x1f, 0x4e, 0x27, 0x94, 0xc1, 0x0c, 0xfe, 0x59, 0x7f, 0xef, 0x15, 0x4e, 0xef, 0x0d, 0x82, 0x57, 0xde },
	  GS_ROM_CTRL, GS_PART_FULL, GS_MODEL_MT32_OLD, 4, 65536u, 65536u, "MT-32 control 1.06", "mt32_ctrl_1_06" },
	{ { 0xcc, 0x83, 0xbf, 0x23, 0xce, 0xe5, 0x33, 0x09, 0x7f, 0xb4, 0xc7, 0xe2, 0xc1, 0x16, 0xe4, 0x3b, 0x50, 0xeb, 0xac, 0xc8 },
	  GS_ROM_CTRL, GS_PART_EVEN, GS_MODEL_MT32_OLD, 4, 32768u, 65536u, "MT-32 control 1.06 (even bytes)", "mt32_ctrl_1_06_a" },
	{ { 0xbf, 0x4f, 0x15, 0x66, 0x6b, 0xc4, 0x66, 0x79, 0x57, 0x94, 0x98, 0x38, 0x67, 0x04, 0x89, 0x3b, 0x63, 0x0c, 0x11, 0x71 },
	  GS_ROM_CTRL, GS_PART_ODD, GS_MODEL_MT32_OLD, 4, 32768u, 65536u, "MT-32 control 1.06 (odd bytes)", "mt32_ctrl_1_06_b" },
	{ { 0xb0, 0x83, 0x51, 0x8f, 0xff, 0xb7, 0xf6, 0x6b, 0x03, 0xc2, 0x3b, 0x7e, 0xb4, 0xf8, 0x68, 0xe6, 0x2d, 0xc5, 0xa9, 0x87 },
	  GS_ROM_CTRL, GS_PART_FULL, GS_MODEL_MT32_OLD, 5, 65536u, 65536u, "MT-32 control 1.07", "mt32_ctrl_1_07" },
	{ { 0x13, 0xf0, 0x6b, 0x38, 0xf0, 0xd9, 0xe0, 0xfc, 0x05, 0x0b, 0x65, 0x03, 0xab, 0x77, 0x7b, 0xb9, 0x38, 0x60, 0x32, 0x60 },
	  GS_ROM_CTRL, GS_PART_EVEN, GS_MODEL_MT32_OLD, 5, 32768u, 65536u, "MT-32 control 1.07 (even bytes)", "mt32_ctrl_1_07_a" },
	{ { 0xc5, 0x5e, 0x16, 0x54, 0x87, 0xd7, 0x1f, 0xa8, 0x8b, 0xd8, 0xc5, 0xe9, 0xc0, 0x83, 0xbc, 0x45, 0x6c, 0x1a, 0x89, 0xaa },
	  GS_ROM_CTRL, GS_PART_ODD, GS_MODEL_MT32_OLD, 5, 32768u, 65536u, "MT-32 control 1.07 (odd bytes)", "mt32_ctrl_1_07_b" },
	{ { 0x2c, 0x16, 0x43, 0x2b, 0x6c, 0x73, 0xdd, 0x2a, 0x39, 0x47, 0xcb, 0xa9, 0x50, 0xa0, 0xf4, 0xc1, 0x9d, 0x61, 0x80, 0xeb },
	  GS_ROM_CTRL, GS_PART_FULL, GS_MODEL_MT32_NEW, 6, 131072u, 131072u, "MT-32 control 2.04", "mt32_ctrl_2_04" },
	{ { 0x28, 0x69, 0xcf, 0x4c, 0x23, 0x5d, 0x67, 0x16, 0x68, 0xcf, 0xcb, 0x62, 0x41, 0x5e, 0x2c, 0xe8, 0x32, 0x3a, 0xd4, 0xed },
	  GS_ROM_CTRL, GS_PART_FULL, GS_MODEL_MT32_NEW, 7, 131072u, 131072u, "MT-32 control 2.06", "mt32_ctrl_2_06" },
	{ { 0x47, 0xb5, 0x2a, 0xde, 0xfe, 0xda, 0xec, 0x47, 0x5c, 0x92, 0x5e, 0x54, 0x34, 0x0e, 0x37, 0x67, 0x3c, 0x11, 0x70, 0x7c },
	  GS_ROM_CTRL, GS_PART_FULL, GS_MODEL_MT32_NEW, 8, 131072u, 131072u, "MT-32 control 2.07", "mt32_ctrl_2_07" },
	{ { 0x7b, 0x8c, 0x2a, 0x5d, 0xdb, 0x42, 0xfd, 0x07, 0x32, 0xe2, 0xf2, 0x2b, 0x33, 0x40, 0xdc, 0xf5, 0x36, 0x0e, 0xdf, 0x92 },
	  GS_ROM_CTRL, GS_PART_FULL, GS_MODEL_MT32_OLD, 1, 65536u, 65536u, "MT-32 control blue ribbon", "mt32_ctrl_bluer" },
	{ { 0x11, 0xa6, 0xae, 0x5d, 0x8b, 0x6e, 0xe3, 0x28, 0xb3, 0x71, 0xaf, 0x7f, 0x1e, 0x40, 0xb8, 0x21, 0x25, 0xaa, 0x6b, 0x4d },
	  GS_ROM_CTRL, GS_PART_EVEN, GS_MODEL_MT32_OLD, 1, 32768u, 65536u, "MT-32 control blue ribbon (even bytes)", "mt32_ctrl_bluer_a" },
	{ { 0xe0, 0x93, 0x43, 0x20, 0xd7, 0xcb, 0xb5, 0xed, 0xfa, 0xa2, 0x9e, 0x0d, 0x01, 0xae, 0x83, 0x5e, 0xf6, 0x20, 0x08, 0x5b },
	  GS_ROM_CTRL, GS_PART_ODD, GS_MODEL_MT32_OLD, 1, 32768u, 65536u, "MT-32 control blue ribbon (odd bytes)", "mt32_ctrl_bluer_b" },
	{ { 0xf6, 0xb1, 0xee, 0xbc, 0x4b, 0x2d, 0x20, 0x0e, 0xc6, 0xd3, 0xd2, 0x1d, 0x51, 0x32, 0x5d, 0x5b, 0x48, 0xc6, 0x02, 0x52 },
	  GS_ROM_PCM, GS_PART_FULL, GS_PCM_MT32, 0, 524288u, 524288u, "MT-32 PCM", "mt32_pcm" },
	{ { 0x2c, 0xad, 0xb9, 0x9d, 0x21, 0xa6, 0xa4, 0xa6, 0xf5, 0xb6, 0x1b, 0x62, 0x18, 0xd1, 0x6e, 0x9b, 0x43, 0xf6, 0x1d, 0x01 },
	  GS_ROM_PCM, GS_PART_HIGH, GS_PCM_MT32, 0, 262144u, 524288u, "MT-32 PCM (high half)", "mt32_pcm_h" },
	{ { 0x3a, 0x1e, 0x19, 0xb0, 0xcd, 0x40, 0x36, 0x62, 0x3f, 0xd1, 0xd1, 0xd1, 0x1f, 0x5f, 0x25, 0x99, 0x55, 0x85, 0x96, 0x2b },
	  GS_ROM_PCM, GS_PART_LOW, GS_PCM_MT32, 0, 262144u, 524288u, "MT-32 PCM (low half)", "mt32_pcm_l" },
};
#define gsr_NROMS ((int)(sizeof gsr_roms / sizeof gsr_roms[0]))

const struct gs_rom_info *gs_rom_by_sha1(const uint8_t sha1[20])
{
	int i, j;
	for (i = 0; i < gsr_NROMS; i++) {
		for (j = 0; j < 20 && gsr_roms[i].sha1[j] == sha1[j]; j++)
			;
		if (j == 20)
			return &gsr_roms[i];
	}
	return 0;
}

const struct gs_rom_info *gs_rom_by_id(const char *id)
{
	int i;
	for (i = 0; i < gsr_NROMS; i++) {
		const char *a = gsr_roms[i].id, *b = id;
		while (*a && *a == *b) { a++; b++; }
		if (!*a && !*b)
			return &gsr_roms[i];
	}
	return 0;
}

const struct gs_rom_info *gs_rom_identify(const uint8_t *data, uint32_t len)
{
	uint8_t h[20];
	const struct gs_rom_info *r;
	int i, ok = 0;
	/* only sizes a ROM can have are hashed */
	for (i = 0; i < gsr_NROMS; i++)
		if (gsr_roms[i].size == len)
			ok = 1;
	if (!ok)
		return 0;
	gs_sha1(data, len, h);
	r = gs_rom_by_sha1(h);
	return r && r->size == len ? r : 0;
}

/* id without its part suffix equals whole's id */
static int gsr_same_base(const char *part, const char *whole)
{
	while (*whole && *part == *whole) {
		part++;
		whole++;
	}
	return *whole == 0 && (*part == 0 || (part[0] == '_' && part[1] && part[2] == 0));
}

const struct gs_rom_info *gs_rom_whole(const struct gs_rom_info *p)
{
	int i;
	if (!p || p->part == GS_PART_FULL)
		return p;
	for (i = 0; i < gsr_NROMS; i++) {
		const struct gs_rom_info *w = &gsr_roms[i];
		if (w->part != GS_PART_FULL || w->kind != p->kind || w->model != p->model)
			continue;
		if (p->part == GS_PART_CM32L_HIGH || gsr_same_base(p->id, w->id))
			return w;
	}
	return 0;
}

static int gsr_is_mt32_pcm(const struct gs_rom_info *r)
{
	return r->kind == GS_ROM_PCM && r->part == GS_PART_FULL && r->model == GS_PCM_MT32;
}

int gs_rom_join(const struct gs_rom_info *a, const uint8_t *ad,
                const struct gs_rom_info *b, const uint8_t *bd, uint8_t *out)
{
	uint32_t i;
	const struct gs_rom_info *t;
	const uint8_t *td;
	if (!a || !b)
		return -1;
	/* put the first part first */
	if (b->part == GS_PART_EVEN || b->part == GS_PART_LOW || gsr_is_mt32_pcm(b)) {
		t = a; a = b; b = t;
		td = ad; ad = bd; bd = td;
	}
	if (a->part == GS_PART_EVEN && b->part == GS_PART_ODD && gs_rom_whole(a) == gs_rom_whole(b)) {
		for (i = 0; i < a->size; i++) {
			out[i * 2] = ad[i];
			out[i * 2 + 1] = bd[i];
		}
		return 0;
	}
	if ((a->part == GS_PART_LOW && b->part == GS_PART_HIGH && gs_rom_whole(a) == gs_rom_whole(b)) ||
	    (gsr_is_mt32_pcm(a) && b->part == GS_PART_CM32L_HIGH)) {
		for (i = 0; i < a->size; i++)
			out[i] = ad[i];
		for (i = 0; i < b->size; i++)
			out[a->size + i] = bd[i];
		return 0;
	}
	return -1;
}

void gs_romset_init(struct gs_romset *s)
{
	s->n = 0;
}

int gs_romset_add(struct gs_romset *s, const struct gs_rom_info *info)
{
	if (!info || s->n >= GS_ROMSET_MAX)
		return -1;
	s->info[s->n++] = info;
	return 0;
}

static int gsr_find(const struct gs_romset *s, const struct gs_rom_info *want, int part)
{
	int i;
	for (i = 0; i < s->n; i++)
		if (s->info[i]->part == part && gs_rom_whole(s->info[i]) == want &&
		    (part != GS_PART_FULL || s->info[i] == want))
			return i;
	return -1;
}

static int gsr_find_exact(const struct gs_romset *s, const struct gs_rom_info *want)
{
	int i;
	for (i = 0; i < s->n; i++)
		if (s->info[i] == want)
			return i;
	return -1;
}

static int gsr_pick_model(const struct gs_romset *s, int model, struct gs_rom_pick *p)
{
	int i, best = -1, bestrank = -1, pcmmodel;
	/* the control ROM: the highest version with a whole file or both parts */
	for (i = 0; i < gsr_NROMS; i++) {
		const struct gs_rom_info *w = &gsr_roms[i];
		if (w->kind != GS_ROM_CTRL || w->part != GS_PART_FULL || w->model != model ||
		    w->rank <= bestrank)
			continue;
		if (gsr_find_exact(s, w) >= 0 || (gsr_find(s, w, GS_PART_EVEN) >= 0 && gsr_find(s, w, GS_PART_ODD) >= 0)) {
			best = i;
			bestrank = w->rank;
		}
	}
	if (best < 0)
		return -1;
	p->model = model;
	p->ctrl_whole = &gsr_roms[best];
	p->ctrl[0] = gsr_find_exact(s, &gsr_roms[best]);
	p->ctrl[1] = -1;
	if (p->ctrl[0] < 0) {
		p->ctrl[0] = gsr_find(s, &gsr_roms[best], GS_PART_EVEN);
		p->ctrl[1] = gsr_find(s, &gsr_roms[best], GS_PART_ODD);
	}
	/* the PCM ROM */
	pcmmodel = (model == GS_MODEL_CM32L || model == GS_MODEL_CM32LN) ? GS_PCM_CM32L : GS_PCM_MT32;
	p->pcm[0] = p->pcm[1] = -1;
	p->pcm_whole = 0;
	for (i = 0; i < gsr_NROMS; i++)
		if (gsr_roms[i].kind == GS_ROM_PCM && gsr_roms[i].part == GS_PART_FULL && gsr_roms[i].model == pcmmodel)
			p->pcm_whole = &gsr_roms[i];
	if (!p->pcm_whole)
		return -1;
	p->pcm[0] = gsr_find_exact(s, p->pcm_whole);
	if (p->pcm[0] >= 0)
		return 0;
	if (pcmmodel == GS_PCM_MT32) {
		p->pcm[0] = gsr_find(s, p->pcm_whole, GS_PART_LOW);
		p->pcm[1] = gsr_find(s, p->pcm_whole, GS_PART_HIGH);
	} else {
		/* a whole MT-32 PCM and the CM-32L's high half */
		for (i = 0; i < s->n; i++)
			if (gsr_is_mt32_pcm(s->info[i]))
				p->pcm[0] = i;
		p->pcm[1] = gsr_find(s, p->pcm_whole, GS_PART_CM32L_HIGH);
	}
	if (p->pcm[0] < 0 || p->pcm[1] < 0)
		return -1;
	return 0;
}

int gs_romset_pick(const struct gs_romset *s, int want, struct gs_rom_pick *pick)
{
	static const int order[] = { GS_MODEL_MT32_OLD, GS_MODEL_MT32_NEW, GS_MODEL_CM32L, GS_MODEL_CM32LN };
	int i;
	if (want)
		return gsr_pick_model(s, want, pick);
	for (i = 0; i < 4; i++)
		if (gsr_pick_model(s, order[i], pick) == 0)
			return 0;
	return -1;
}
