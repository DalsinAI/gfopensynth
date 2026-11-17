/* SoundFont 2 reader (SF2 2.01/2.04): the file stays where it was loaded
 * and is read in place; finding the zones that sound for a key and
 * velocity gives each one's generators, instrument and preset summed as
 * the specification says (8.5).
 * MIT, Copyright (c) 2026 AmiCradle Team. */
#ifndef GS_SF2_H
#define GS_SF2_H

#include <stdint.h>

/* generators the synth uses (SF2 8.1.2) */
enum {
	GEN_startAddrsOffset = 0, GEN_endAddrsOffset = 1, GEN_startloopAddrsOffset = 2,
	GEN_endloopAddrsOffset = 3, GEN_startAddrsCoarseOffset = 4,
	GEN_initialFilterFc = 8, GEN_endAddrsCoarseOffset = 12, GEN_pan = 17,
	GEN_delayVolEnv = 33, GEN_attackVolEnv = 34, GEN_holdVolEnv = 35, GEN_decayVolEnv = 36,
	GEN_sustainVolEnv = 37, GEN_releaseVolEnv = 38, GEN_keynumToVolEnvHold = 39,
	GEN_keynumToVolEnvDecay = 40, GEN_instrument = 41, GEN_keyRange = 43, GEN_velRange = 44,
	GEN_startloopAddrsCoarseOffset = 45, GEN_keynum = 46, GEN_velocity = 47,
	GEN_initialAttenuation = 48, GEN_endloopAddrsCoarseOffset = 50, GEN_coarseTune = 51,
	GEN_fineTune = 52, GEN_sampleID = 53, GEN_sampleModes = 54, GEN_scaleTuning = 56,
	GEN_exclusiveClass = 57, GEN_overridingRootKey = 58,
	GEN_COUNT = 61
};

struct gs_sf2 {
	const uint8_t *phdr, *pbag, *pgen, *inst, *ibag, *igen, *shdr;
	uint32_t nphdr, npbag, npgen, ninst, nibag, nigen, nshdr;
	int16_t *smpl;          /* 16-bit samples, made native-endian by gs_sf2_open */
	uint32_t nsmpl;
};

/* one sample zone that sounds: its sample and summed generators */
struct gs_zone {
	int16_t gen[GEN_COUNT];
	uint32_t start, end, loopstart, loopend;   /* in samples, offsets applied */
	uint32_t rate;
	uint8_t root;           /* the key the sample plays at its own pitch */
	int8_t correction;      /* cents */
};

/* Reads a file loaded at data (2-byte aligned, writable: the sample data is
 * byte-swapped in place on a big-endian machine). 0, or -1 for a file that
 * is not a usable SoundFont. */
int gs_sf2_open(struct gs_sf2 *f, uint8_t *data, uint32_t len);

/* the preset for bank:program, or -1 (no fallback here) */
int gs_sf2_preset(const struct gs_sf2 *f, int bank, int program);

/* Fills up to max zones for preset p, key, velocity; returns how many. */
int gs_sf2_zones(const struct gs_sf2 *f, int p, int key, int vel, struct gs_zone *out, int max);

/* the preset's name (20 bytes, not always 0-terminated) */
const char *gs_sf2_preset_name(const struct gs_sf2 *f, int p);

#endif
