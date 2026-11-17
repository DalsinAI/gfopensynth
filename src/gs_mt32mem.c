/* The MT-32's memory and the DT1 messages that write it (gs_mt32mem.h).
 * Written from the MIDI implementation in the MT-32 owner's manual.
 * MIT, Copyright (c) 2026 AmiCradle Team. */
#include <string.h>
#include "gs_mt32mem.h"

/* the highest value of each byte of a timbre's common part and of a partial */
static const uint8_t gsmm_common_max[GS_MT32_COMMON_SIZE] = {
	127, 127, 127, 127, 127, 127, 127, 127, 127, 127,   /* name */
	12, 12, 15, 1
};
static const uint8_t gsmm_partial_max[GS_MT32_PARTIAL_SIZE] = {
	96, 100, 16, 1, 3, 127, 100, 14,                    /* wave generator */
	10, 3, 4, 100, 100, 100, 100, 100, 100, 100, 100, 100,     /* pitch envelope */
	100, 100, 100,                                      /* pitch LFO */
	100, 30, 14, 127, 14,                               /* filter */
	100, 100, 4, 4, 100, 100, 100, 100, 100, 100, 100, 100, 100,   /* filter envelope */
	100, 100, 127, 12, 127, 12,                         /* amplifier */
	4, 4, 100, 100, 100, 100, 100, 100, 100, 100, 100   /* amplifier envelope */
};
static const uint8_t gsmm_patch_max[GS_MT32_PATCH_TEMP] = { 3, 63, 48, 100, 24, 3, 1, 0, 100, 14, 0, 0, 0, 0, 0, 0 };
static const uint8_t gsmm_rhythm_max[4] = { 127, 100, 14, 1 };
static const uint8_t gsmm_system_max[GS_MT32_SYSTEM_SIZE] = {
	127, 3, 7, 7, 32, 32, 32, 32, 32, 32, 32, 32, 32, 16, 16, 16, 16, 16, 16, 16, 16, 16, 100
};
static const uint8_t gsmm_system_default[GS_MT32_SYSTEM_SIZE] = {
	0x4A, 0, 5, 3, 3, 10, 6, 4, 3, 0, 0, 0, 6, 1, 2, 3, 4, 5, 6, 7, 8, 9, 100
};

/* a partial by name is its 58 bytes in order */
typedef char gsmm_partial_is_58[sizeof(struct gs_mt32_partial) == GS_MT32_PARTIAL_SIZE ? 1 : -1];

#define GSMM_PATCH_TEMP  GS_MT32_ADDR(0x03, 0x00, 0x00)
#define GSMM_RHYTHM      GS_MT32_ADDR(0x03, 0x01, 0x10)
#define GSMM_TIMBRE_TEMP GS_MT32_ADDR(0x04, 0x00, 0x00)
#define GSMM_PATCH       GS_MT32_ADDR(0x05, 0x00, 0x00)
#define GSMM_TIMBRE      GS_MT32_ADDR(0x08, 0x00, 0x00)
#define GSMM_SYSTEM      GS_MT32_ADDR(0x10, 0x00, 0x00)
#define GSMM_DISPLAY     GS_MT32_ADDR(0x20, 0x00, 0x00)
#define GSMM_RESET       GS_MT32_ADDR(0x7F, 0x00, 0x00)

static uint8_t gsmm_hold(uint8_t v, uint8_t max)
{
	return v > max ? max : v;
}

static uint8_t gsmm_timbre_max(uint32_t off)
{
	if (off < GS_MT32_COMMON_SIZE)
		return gsmm_common_max[off];
	return gsmm_partial_max[(off - GS_MT32_COMMON_SIZE) % GS_MT32_PARTIAL_SIZE];
}

void gs_mt32mem_init(struct gs_mt32mem *m)
{
	uint32_t resets = m->resets, writes = m->writes, rejected = m->rejected;
	uint8_t device = m->device;
	int i;
	memset(m, 0, sizeof *m);
	m->resets = resets;
	m->writes = writes;
	m->rejected = rejected;
	m->device = device ? device : 0x10;
	memcpy(m->system, gsmm_system_default, sizeof m->system);
	for (i = 0; i < GS_MT32_PATCHES; i++) {
		uint8_t *p = m->patch[i];
		p[GS_MT32_P_GROUP] = (uint8_t)(i / 64);
		p[GS_MT32_P_NUMBER] = (uint8_t)(i % 64);
		p[GS_MT32_P_KEYSHIFT] = 24;
		p[GS_MT32_P_FINETUNE] = 50;
		p[GS_MT32_P_BEND] = 12;
		p[GS_MT32_P_REVERB] = 1;
	}
	for (i = 0; i < GS_MT32_PARTS; i++) {
		memcpy(m->patch_temp[i], m->patch[i], GS_MT32_PATCH_SIZE);
		m->patch_temp[i][GS_MT32_P_LEVEL] = 80;
		m->patch_temp[i][GS_MT32_P_PAN] = 7;
	}
	for (i = 0; i < GS_MT32_RHYTHM_KEYS; i++) {
		m->rhythm[i][0] = 127;                      /* no timbre: a ROM's setup fills these (step 4) */
		m->rhythm[i][1] = 80;
		m->rhythm[i][2] = 7;
		m->rhythm[i][3] = 1;
	}
	memset(m->display, ' ', GS_MT32_DISPLAY_SIZE);
}

uint8_t gs_mt32_checksum(const uint8_t *b, uint32_t n)
{
	uint32_t sum = 0;
	while (n--)
		sum += *b++ & 127;
	return (uint8_t)((128 - (sum & 127)) & 127);
}

/* one byte at a linear address; 1 when an area took it */
static int gsmm_put(struct gs_mt32mem *m, uint32_t a, uint8_t v)
{
	uint32_t o;
	v &= 127;
	if (a >= GSMM_PATCH_TEMP && (o = a - GSMM_PATCH_TEMP) < GS_MT32_PARTS * GS_MT32_PATCH_TEMP) {
		m->patch_temp[o / GS_MT32_PATCH_TEMP][o % GS_MT32_PATCH_TEMP] = gsmm_hold(v, gsmm_patch_max[o % GS_MT32_PATCH_TEMP]);
		return 1;
	}
	if (a >= GSMM_RHYTHM && (o = a - GSMM_RHYTHM) < GS_MT32_RHYTHM_KEYS * 4) {
		m->rhythm[o / 4][o % 4] = gsmm_hold(v, gsmm_rhythm_max[o % 4]);
		return 1;
	}
	if (a >= GSMM_TIMBRE_TEMP && (o = a - GSMM_TIMBRE_TEMP) < GS_MT32_PARTS * GS_MT32_TIMBRE_SIZE) {
		m->timbre_temp[o / GS_MT32_TIMBRE_SIZE][o % GS_MT32_TIMBRE_SIZE] = gsmm_hold(v, gsmm_timbre_max(o % GS_MT32_TIMBRE_SIZE));
		return 1;
	}
	if (a >= GSMM_PATCH && (o = a - GSMM_PATCH) < GS_MT32_PATCHES * GS_MT32_PATCH_SIZE) {
		m->patch[o / GS_MT32_PATCH_SIZE][o % GS_MT32_PATCH_SIZE] = gsmm_hold(v, gsmm_patch_max[o % GS_MT32_PATCH_SIZE]);
		return 1;
	}
	if (a >= GSMM_TIMBRE && (o = a - GSMM_TIMBRE) < GS_MT32_TIMBRES * 256) {
		if (o % 256 >= GS_MT32_TIMBRE_SIZE)
			return 0;                               /* the 10 unused bytes after each timbre */
		m->timbre[o / 256][o % 256] = gsmm_hold(v, gsmm_timbre_max(o % 256));
		return 1;
	}
	if (a >= GSMM_SYSTEM && (o = a - GSMM_SYSTEM) < GS_MT32_SYSTEM_SIZE) {
		m->system[o] = gsmm_hold(v, gsmm_system_max[o]);
		return 1;
	}
	if (a >= GSMM_DISPLAY && (o = a - GSMM_DISPLAY) < GS_MT32_DISPLAY_SIZE) {
		m->display[o] = (char)(v >= 32 ? v : ' ');
		m->display_set = 1;
		return 1;
	}
	return 0;
}

uint32_t gs_mt32mem_write(struct gs_mt32mem *m, uint32_t addr, const uint8_t *data, uint32_t n)
{
	uint32_t i, kept = 0;
	if (addr >= GSMM_RESET) {
		gs_mt32mem_init(m);
		m->resets++;
		return n;
	}
	for (i = 0; i < n; i++)
		kept += (uint32_t)gsmm_put(m, addr + i, data[i]);
	return kept;
}

int gs_mt32mem_sysex(struct gs_mt32mem *m, const uint8_t *d, uint32_t len)
{
	uint32_t i;
	if (len && d[0] == 0xF0) { d++; len--; }
	if (len && d[len - 1] == 0xF7) len--;
	/* 41 unit 16 12 a b c data... sum: Roland, the unit, MT-32, data set */
	if (len < 4 || d[0] != 0x41 || d[2] != 0x16 || d[3] != 0x12 || d[1] != m->device)
		return 0;
	if (len < 9) {                                  /* an address, a byte and the sum at least */
		m->rejected++;
		return -1;
	}
	for (i = 4; i < len; i++)
		if (d[i] & 0x80) {
			m->rejected++;
			return -1;
		}
	if (gs_mt32_checksum(d + 4, len - 5) != d[len - 1]) {
		m->rejected++;
		return -1;
	}
	gs_mt32mem_write(m, GS_MT32_ADDR(d[4], d[5], d[6]), d + 7, len - 8);
	m->writes++;
	return 1;
}

void gs_mt32_timbre_decode(const uint8_t *b, struct gs_mt32_timbre *t)
{
	int i, p;
	for (i = 0; i < 10; i++)
		t->name[i] = (char)(b[i] >= 32 && b[i] < 127 ? b[i] : ' ');
	t->name[10] = 0;
	t->structure12 = b[10];
	t->structure34 = b[11];
	t->mute = b[12];
	t->no_sustain = b[13];
	for (p = 0; p < 4; p++) {
		/* struct gs_mt32_partial is 58 single bytes in the timbre's own order */
		memcpy(&t->partial[p], b + GS_MT32_COMMON_SIZE + p * GS_MT32_PARTIAL_SIZE, GS_MT32_PARTIAL_SIZE);
	}
}

int gs_mt32mem_part(const struct gs_mt32mem *m, int channel)
{
	int i;
	for (i = 0; i < 9; i++)
		if (m->system[GS_MT32_S_CHANNEL + i] == channel)
			return i;
	return -1;
}
