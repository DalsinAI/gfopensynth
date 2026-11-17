/* The timbres of a user's MT-32 control ROM (gs_mt32rom.h).
 * MIT, Copyright (c) 2026 AmiCradle Team. */
#include <string.h>
#include "gs_mt32rom.h"

#define GSMR_WHOLE_A  0x8080u
#define GSMR_WHOLE_B  0xC080u
#define GSMR_TABLE    0x8000u

/* a timbre's 14 common bytes look right: printable name, structures 0-12,
 * partial switches 0-15, sustain switch 0-1 */
static int gsmr_common_ok(const uint8_t *t)
{
	int i;
	for (i = 0; i < 10; i++)
		if (t[i] < 32 || t[i] > 126)
			return 0;
	return t[10] <= 12 && t[11] <= 12 && t[12] <= 15 && t[13] <= 1;
}

static int gsmr_bytes_ok(const uint8_t *p, uint32_t n)
{
	uint32_t i;
	for (i = 0; i < n; i++)
		if (p[i] > 127)
			return 0;
	return 1;
}

static int gsmr_count(uint8_t m)
{
	return (m & 1) + ((m >> 1) & 1) + ((m >> 2) & 1) + ((m >> 3) & 1);
}

static int gsmr_whole(struct gs_mt32rom *r, const uint8_t *c, uint32_t len)
{
	int g, i;
	if (len < GSMR_WHOLE_B + 64u * GS_MT32_TIMBRE_SIZE)
		return 0;
	for (g = 0; g < 2; g++)
		for (i = 0; i < 64; i++) {
			const uint8_t *t = c + (g ? GSMR_WHOLE_B : GSMR_WHOLE_A) + (uint32_t)i * GS_MT32_TIMBRE_SIZE;
			if (!gsmr_common_ok(t) || !gsmr_bytes_ok(t + 10, GS_MT32_TIMBRE_SIZE - 10))
				return 0;
			memcpy(r->timbre[g * 64 + i], t, GS_MT32_TIMBRE_SIZE);
		}
	return GS_MT32ROM_WHOLE;
}

static int gsmr_packed(struct gs_mt32rom *r, const uint8_t *c, uint32_t len)
{
	int i, k;
	if (len < GSMR_TABLE + 2u * GS_MT32ROM_TIMBRES)
		return 0;
	for (i = 0; i < GS_MT32ROM_TIMBRES; i++) {
		uint32_t at = GSMR_TABLE + (uint32_t)(c[GSMR_TABLE + 2 * i] | c[GSMR_TABLE + 2 * i + 1] << 8), n;
		const uint8_t *t;
		uint8_t *out = r->timbre[i];
		if (at + GS_MT32_COMMON_SIZE > len || !gsmr_common_ok(c + at))
			return 0;
		t = c + at;
		n = (uint32_t)gsmr_count(t[12]) * GS_MT32_PARTIAL_SIZE;
		if (at + GS_MT32_COMMON_SIZE + n > len || !gsmr_bytes_ok(t + 10, GS_MT32_COMMON_SIZE - 10 + n))
			return 0;
		memset(out, 0, GS_MT32_TIMBRE_SIZE);
		memcpy(out, t, GS_MT32_COMMON_SIZE);
		t += GS_MT32_COMMON_SIZE;
		for (k = 0; k < 4; k++)                      /* the switched-on partials, in order, to their own places */
			if (out[12] & (1 << k)) {
				memcpy(out + GS_MT32_COMMON_SIZE + k * GS_MT32_PARTIAL_SIZE, t, GS_MT32_PARTIAL_SIZE);
				t += GS_MT32_PARTIAL_SIZE;
			}
	}
	return GS_MT32ROM_PACKED;
}

int gs_mt32rom_read(struct gs_mt32rom *r, const uint8_t *ctrl, uint32_t len)
{
	int layout;
	memset(r, 0, sizeof *r);
	if (!ctrl)
		return 0;
	layout = gsmr_whole(r, ctrl, len);
	if (!layout) {
		memset(r, 0, sizeof *r);
		layout = gsmr_packed(r, ctrl, len);
	}
	if (!layout)
		memset(r, 0, sizeof *r);
	r->layout = (uint8_t)layout;
	return layout;
}

const uint8_t *gs_mt32rom_timbre(const struct gs_mt32rom *r, int group, int number)
{
	if (!r || !r->layout || group < 0 || group > 1 || number < 0 || number > 63)
		return 0;
	return r->timbre[group * 64 + number];
}
