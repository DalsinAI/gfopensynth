/* Notes on our own MT-32 (LA) engine: step 3 of docs/MT32-ENGINE.md. It
 * takes MIDI and the MT-32's system exclusive messages, keeps the memory
 * (gs_mt32mem.h) and plays notes from the timbres in it: up to four
 * partials a note, joined in pairs by the timbre's two structures (mixed,
 * ring modulated, or both), 32 partials in all, shared out by the parts'
 * partial reserve.
 *
 * Without the user's ROM (gs_mt32_use_rom, step 4) only a timbre the song itself sent plays here (a write
 * to a part's timbre temporary area, or a patch that names timbre memory),
 * and of it only the partials of the synthesiser kind (gs_la.h); a sampled
 * (PCM) partial is silent until step 4 reads the user's ROMs. A part on a
 * ROM timbre is not playable: gs_mt32_playable says so, and the caller
 * plays that part some other way (the General MIDI map).
 *
 * Left for step 5: reverb, master tune and master volume.
 * MIT, Copyright (c) 2026 AmiCradle Team. */
#ifndef GS_MT32_H
#define GS_MT32_H

#include <stdint.h>
#include "gs_mt32mem.h"
#include "gs_la.h"
#include "gs_mt32rom.h"

#define GS_MT32_NOTES    32
#define GS_MT32_PARTIALS 32
#define GS_MT32_RHYTHM   8          /* the rhythm part's number */
#define GS_MT32_CHUNK    64         /* frames worked at a time */

struct gs_mt32_note {
	uint8_t active, part, key, released, held;
	uint8_t used;                   /* bit n: partial n is sounding */
	uint8_t structure[2];
	uint8_t level, pan;             /* 0-100, 0-14 */
	uint32_t age;
	struct gs_la_partial p[4];
};

struct gs_mt32_part {
	uint8_t known;                  /* its timbre is one we hold: the song's, or the user's ROM's (step 4) */
	uint8_t from_rom;               /* it is the ROM's: played only while it has no sampled partial on */
	uint8_t volume, expression, mod, hold;
	int16_t bend;                   /* -8192 to 8191 */
};

struct gs_mt32 {
	struct gs_mt32mem mem;
	uint32_t rate, age, resets_seen;
	struct gs_mt32_part part[GS_MT32_PARTS + 1];
	struct gs_mt32_note note[GS_MT32_NOTES];
	uint32_t dropped;               /* notes not started: no partial could be had */
	uint32_t stolen;                /* notes cut short to make room */
	const struct gs_mt32rom *rom;   /* the user's ROM timbres, or NULL (gs_mt32_use_rom) */
};

void gs_mt32_init(struct gs_mt32 *s, uint32_t rate);

/* takes groups A and B from the user's ROM (gs_mt32rom_read), or none with
 * NULL; the caller keeps rom alive. Parts on a group A or B patch take
 * their timbre from it at once and at each patch change. */
void gs_mt32_use_rom(struct gs_mt32 *s, const struct gs_mt32rom *rom);

/* a channel message: status with its channel, and its one or two data bytes */
void gs_mt32_midi(struct gs_mt32 *s, uint8_t status, uint8_t d1, uint8_t d2);

/* a system exclusive message, as gs_mt32mem_sysex takes and answers it */
int gs_mt32_sysex(struct gs_mt32 *s, const uint8_t *data, uint32_t len);

/* 1 when the part on this MIDI channel (0-15) has a timbre this engine can
 * sound: one the song sent, with a synthesiser partial switched on. The
 * rhythm part answers for a key (24-108) with d = that key, else pass 0. */
int gs_mt32_playable(const struct gs_mt32 *s, int channel, int key);

/* adds frames samples to left and right (16-bit scale, not clipped) */
void gs_mt32_render(struct gs_mt32 *s, int32_t *left, int32_t *right, uint32_t frames);

/* the partials sounding now */
int gs_mt32_partials(const struct gs_mt32 *s);

#endif
