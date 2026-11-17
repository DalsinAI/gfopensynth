/* The MT-32's memory, as its MIDI implementation chart publishes it (the
 * owner's manual): the areas a song reaches with Roland data-set messages
 * (DT1), which is how a game sets up its own sounds before it plays.
 *
 *   address    area                         bytes
 *   03 00 00   patch temporary, parts 1-8   8 x 16
 *   03 01 10   rhythm setup, keys 24-108    85 x 4
 *   04 00 00   timbre temporary, parts 1-8  8 x 246
 *   05 00 00   patch memory                 128 x 8
 *   08 00 00   timbre memory                64 x 246 (one every 256)
 *   10 00 00   system                       23
 *   20 00 00   display                      20
 *   7F 00 00   reset (any write)
 *
 * Addresses are three 7-bit bytes. Every value is held to its range, as
 * the MT-32 does. This is step 1 of our own LA engine (docs/MT32-ENGINE.md):
 * the memory only, no sound. It needs no ROM: the timbres in a ROM (groups
 * A, B and R) are not here, only the 64 a song sends to timbre memory.
 * MIT, Copyright (c) 2026 AmiCradle Team. */
#ifndef GS_MT32MEM_H
#define GS_MT32MEM_H

#include <stdint.h>

#define GS_MT32_PARTS        8
#define GS_MT32_PATCH_TEMP   16
#define GS_MT32_RHYTHM_KEYS  85
#define GS_MT32_TIMBRE_SIZE  246
#define GS_MT32_COMMON_SIZE  14
#define GS_MT32_PARTIAL_SIZE 58
#define GS_MT32_PATCHES      128
#define GS_MT32_PATCH_SIZE   8
#define GS_MT32_TIMBRES      64
#define GS_MT32_SYSTEM_SIZE  23
#define GS_MT32_DISPLAY_SIZE 20

/* a patch's timbre group */
#define GS_MT32_GROUP_A      0
#define GS_MT32_GROUP_B      1
#define GS_MT32_GROUP_MEMORY 2
#define GS_MT32_GROUP_RHYTHM 3

/* offsets in a patch (temporary: 16 bytes, memory: the first 8) */
#define GS_MT32_P_GROUP   0
#define GS_MT32_P_NUMBER  1
#define GS_MT32_P_KEYSHIFT 2   /* 0-48, 24 is none */
#define GS_MT32_P_FINETUNE 3   /* 0-100, 50 is none */
#define GS_MT32_P_BEND    4    /* 0-24 semitones */
#define GS_MT32_P_ASSIGN  5    /* 0-3 */
#define GS_MT32_P_REVERB  6    /* 0-1 */
#define GS_MT32_P_LEVEL   8    /* temporary only: 0-100 */
#define GS_MT32_P_PAN     9    /* temporary only: 0-14, 7 is the middle */

/* offsets in the system area */
#define GS_MT32_S_TUNE     0   /* 0-127: 432.1 to 457.6 Hz */
#define GS_MT32_S_REVMODE  1   /* 0-3 */
#define GS_MT32_S_REVTIME  2   /* 0-7 */
#define GS_MT32_S_REVLEVEL 3   /* 0-7 */
#define GS_MT32_S_RESERVE  4   /* 9: partials kept for parts 1-8 and rhythm, 0-32 */
#define GS_MT32_S_CHANNEL  13  /* 9: MIDI channel of parts 1-8 and rhythm, 0-15, 16 off */
#define GS_MT32_S_VOLUME   22  /* 0-100 */

struct gs_mt32mem {
	uint8_t patch_temp[GS_MT32_PARTS][GS_MT32_PATCH_TEMP];
	uint8_t rhythm[GS_MT32_RHYTHM_KEYS][4];        /* timbre, level, pan, reverb */
	uint8_t timbre_temp[GS_MT32_PARTS][GS_MT32_TIMBRE_SIZE];
	uint8_t patch[GS_MT32_PATCHES][GS_MT32_PATCH_SIZE];
	uint8_t timbre[GS_MT32_TIMBRES][GS_MT32_TIMBRE_SIZE];
	uint8_t system[GS_MT32_SYSTEM_SIZE];
	char display[GS_MT32_DISPLAY_SIZE + 1];
	uint8_t device;             /* the unit number DT1 must carry (0x10) */
	uint8_t display_set;        /* a song wrote the display since the last reset */
	uint32_t resets;            /* writes to 7F 00 00 */
	uint32_t writes;            /* DT1 messages taken */
	uint32_t rejected;          /* DT1 for us with a bad checksum or shape */
};

/* one partial of a timbre, by name (the 58 bytes in order) */
struct gs_mt32_partial {
	uint8_t pitch_coarse, pitch_fine, pitch_keyfollow, bender, waveform, pcm_wave, pulse_width, pw_velo;
	uint8_t penv_depth, penv_velo, penv_time_keyfollow, penv_time[4], penv_level[5];
	uint8_t lfo_rate, lfo_depth, lfo_mod;
	uint8_t tvf_cutoff, tvf_resonance, tvf_keyfollow, tvf_bias_point, tvf_bias_level;
	uint8_t tvf_env_depth, tvf_env_velo, tvf_env_depth_keyfollow, tvf_env_time_keyfollow, tvf_env_time[5], tvf_env_level[4];
	uint8_t tva_level, tva_velo, tva_bias_point1, tva_bias_level1, tva_bias_point2, tva_bias_level2;
	uint8_t tva_env_time_keyfollow, tva_env_time_velo, tva_env_time[5], tva_env_level[4];
};

struct gs_mt32_timbre {
	char name[11];
	uint8_t structure12, structure34;   /* 0-12: how partials 1+2 and 3+4 are joined */
	uint8_t mute;                       /* bit n set: partial n+1 sounds */
	uint8_t no_sustain;                 /* env mode 1 */
	struct gs_mt32_partial partial[4];
};

/* power-on memory: system defaults, patch n on group n/64 number n%64, parts 1-8 on patches 0-7 */
void gs_mt32mem_init(struct gs_mt32mem *m);

/* the checksum a DT1's address and data bytes need */
uint8_t gs_mt32_checksum(const uint8_t *bytes, uint32_t n);

/* A system exclusive message, with or without its F0 and F7. Returns 1 when
 * it was an MT-32 DT1 for this unit and was written, 0 when it is not one
 * (another maker, model or unit: nothing changes), -1 when it is one but
 * its checksum or shape is wrong (nothing changes). */
int gs_mt32mem_sysex(struct gs_mt32mem *m, const uint8_t *data, uint32_t len);

/* writes n bytes at a 3-byte address, each held to its range; bytes that
 * fall outside every area are dropped. The number of bytes kept. */
uint32_t gs_mt32mem_write(struct gs_mt32mem *m, uint32_t addr, const uint8_t *data, uint32_t n);

/* a 3-byte address as one number, and back */
#define GS_MT32_ADDR(a, b, c) (((uint32_t)(a) << 14) | ((uint32_t)(b) << 7) | (uint32_t)(c))

/* the 246 bytes of a timbre by name */
void gs_mt32_timbre_decode(const uint8_t *bytes, struct gs_mt32_timbre *t);

/* the part (0-7) or rhythm (8) on a MIDI channel (0-15), or -1 */
int gs_mt32mem_part(const struct gs_mt32mem *m, int channel);

#endif
