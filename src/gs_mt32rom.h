/* Step 4 of our own MT-32 engine (docs/MT32-ENGINE.md), part one: the
 * timbres of groups A and B read from the user's own control ROM. The ROM
 * is never shipped: the caller reads the user's file (Emulation:Roms, then
 * DEVS:MT32, as gs_rom.h knows them) and hands the joined bytes here.
 *
 * Two layouts are known, both found by looking, not by a ROM's version:
 *   whole:  64 timbres of 246 bytes at 0x8080 (group A) and 64 at 0xC080
 *           (group B), as MT-32 control 1.04-1.07 and the blue ribbon keep
 *           them;
 *   packed: a table at 0x8000 of 128 16-bit little-endian offsets from
 *           0x8000, each to a timbre of 14 common bytes followed only by its
 *           switched-on partials (58 bytes each, in order), as MT-32 2.04-2.07
 *           and the CM-32L family keep them.
 * Every timbre is checked (name printable, structures 0-12, partial bytes
 * 0-127) before it is taken; a ROM that fails is refused as a whole.
 *
 * Not yet (step 4, part two): the rhythm timbres and setup, and the sampled
 * waves of the PCM ROM. Until then a ROM timbre with a sampled partial
 * switched on is not played (gs_mt32_playable says so).
 * MIT, Copyright (c) 2026 AmiCradle Team. */
#ifndef GS_MT32ROM_H
#define GS_MT32ROM_H

#include <stdint.h>
#include "gs_mt32mem.h"

#define GS_MT32ROM_TIMBRES 128          /* groups A (0-63) and B (64-127) */
#define GS_MT32ROM_WHOLE   1
#define GS_MT32ROM_PACKED  2

struct gs_mt32rom {
	uint8_t layout;                     /* 0: nothing read */
	uint8_t timbre[GS_MT32ROM_TIMBRES][GS_MT32_TIMBRE_SIZE];
};

/* reads groups A and B from a joined control ROM: the layout, or 0 when the
 * bytes hold neither layout (r is left empty) */
int gs_mt32rom_read(struct gs_mt32rom *r, const uint8_t *ctrl, uint32_t len);

/* a group A or B timbre (group 0 or 1, number 0-63), or NULL */
const uint8_t *gs_mt32rom_timbre(const struct gs_mt32rom *r, int group, int number);

#endif
