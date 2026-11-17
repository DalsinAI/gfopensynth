/* MT-32 family ROMs, known by their SHA-1 (file names never matter, as in
 * mt32-pi). The library never ships or fetches a ROM: the user puts their
 * own in DEVS:MT32/ (or ENV:GFOpenSynth/RomPath). Halves are joined:
 * control ROMs split _a/_b are the even and odd bytes of the whole, PCM
 * ROMs split _l/_h are its first and second halves, and the CM-32L PCM is
 * the MT-32 PCM followed by cm32l_pcm_h.
 * MIT, Copyright (c) 2026 AmiCradle Team. */
#ifndef GS_ROM_H
#define GS_ROM_H

#include <stdint.h>

/* kind */
#define GS_ROM_CTRL 1
#define GS_ROM_PCM  2

/* part: what the file is of its whole */
#define GS_PART_FULL  0
#define GS_PART_EVEN  1   /* _a: bytes 0, 2, 4 ... */
#define GS_PART_ODD   2   /* _b: bytes 1, 3, 5 ... */
#define GS_PART_LOW   3   /* _l: the first half */
#define GS_PART_HIGH  4   /* _h: the second half */
#define GS_PART_CM32L_HIGH 5 /* cm32l_pcm_h: after a whole MT-32 PCM */

/* model (a control ROM's, or what a PCM ROM serves) */
#define GS_MODEL_MT32_OLD 1   /* control 1.04-1.07 and blue ribbon */
#define GS_MODEL_MT32_NEW 2   /* control 2.04-2.07 */
#define GS_MODEL_CM32L    3   /* CM-32L control 1.00, 1.02 */
#define GS_MODEL_CM32LN   4   /* CM-32LN control 1.00 */

/* the PCM each model plays */
#define GS_PCM_MT32  1
#define GS_PCM_CM32L 2

struct gs_rom_info {
	uint8_t sha1[20];
	uint8_t kind;           /* GS_ROM_* */
	uint8_t part;           /* GS_PART_* */
	uint8_t model;          /* GS_MODEL_* for a control ROM, GS_PCM_* for a PCM */
	uint8_t rank;           /* newer control ROM versions of a model rank higher */
	uint32_t size;          /* this file's bytes */
	uint32_t whole;         /* the joined ROM's bytes */
	const char *name;       /* "MT-32 control 1.07" */
	const char *id;         /* "mt32_ctrl_1_07" (the MAME name), with _a/_b etc. */
};

void gs_sha1(const uint8_t *data, uint32_t len, uint8_t out[20]);

/* the entry for a ROM's bytes, or NULL (any other file) */
const struct gs_rom_info *gs_rom_identify(const uint8_t *data, uint32_t len);
/* the entry for a SHA-1, or NULL */
const struct gs_rom_info *gs_rom_by_sha1(const uint8_t sha1[20]);
/* the entry for a MAME-style id ("mt32_ctrl_1_07_a"), or NULL */
const struct gs_rom_info *gs_rom_by_id(const char *id);
/* the whole ROM's entry for a part's (the same entry for GS_PART_FULL) */
const struct gs_rom_info *gs_rom_whole(const struct gs_rom_info *part);

/* joins two halves into out (whole bytes): even/odd interleave, or low/high
 * (and MT-32 PCM + cm32l_pcm_h) one after the other. 0, or -1 when the two
 * are not the halves of one whole. */
int gs_rom_join(const struct gs_rom_info *a, const uint8_t *adata,
                const struct gs_rom_info *b, const uint8_t *bdata, uint8_t *out);

/* A ROM folder's contents as found: up to GS_ROMSET_MAX identified files. */
#define GS_ROMSET_MAX 32
struct gs_romset {
	const struct gs_rom_info *info[GS_ROMSET_MAX];
	int n;
};

/* what to load for a model: one file, or two to join, for each ROM */
struct gs_rom_pick {
	int model;                       /* GS_MODEL_* picked */
	int ctrl[2], pcm[2];             /* indexes into the romset, -1 for none */
	const struct gs_rom_info *ctrl_whole, *pcm_whole;
};

void gs_romset_init(struct gs_romset *s);
/* adds a file already identified; 0, or -1 when the set is full */
int gs_romset_add(struct gs_romset *s, const struct gs_rom_info *info);
/* picks a complete set for want (GS_MODEL_*, or 0 for the first complete
 * one in the order MT-32 old, MT-32 new, CM-32L, CM-32LN), preferring the
 * newest version of that model's control ROM and whole files over parts.
 * 0, or -1 when no complete set is there. */
int gs_romset_pick(const struct gs_romset *s, int want, struct gs_rom_pick *pick);

#endif
