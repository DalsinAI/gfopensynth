/* The synth: General MIDI from a SoundFont, in integer arithmetic only, so
 * the same source runs on the host behind AkikoPlus's MIDI port, on other
 * platforms, and on a 68040 with no FPU. MIDI messages and SysEx in,
 * interleaved signed 16-bit stereo out.
 *
 * Modes: GS_MODE_GM plays every channel as General MIDI (channel 10 drums).
 * GS_MODE_MT32_GM plays an MT-32 song through the SoundFont: parts on MIDI
 * channels 2-9 with the MT-32's programs mapped to the nearest GM ones, and
 * rhythm on 10, as an MT-32 is set when it powers on. The SysEx
 * F0 7D 'G' 'S' 01 <mode> F7 (7D: the non-commercial id) switches the mode
 * and resets, as mt32-pi switches between its engines. (The MT-32 itself, from
 * its ROMs, is the LA engine, a later stage.)
 * MIT, Copyright (c) 2026 AmiCradle Team. */
#ifndef GS_SYNTH_H
#define GS_SYNTH_H

#include <stdint.h>
#include "gs_sf2.h"
#include "gs_midi.h"

#define GS_VOICES_MAX 64
#define GS_BLOCK 32             /* frames between envelope and gain updates */

#define GS_MODE_GM      0
#define GS_MODE_MT32_GM 1

struct gs_voice {
	uint8_t stage;              /* 0 free, else GS_ENV_* */
	uint8_t chan, key, vel;
	uint8_t loop;               /* 0 none, 1 always, 3 until released */
	uint8_t released, held;     /* held: let go while the sustain pedal is down */
	uint8_t excl;
	const int16_t *data;
	uint32_t pos, end, ls, le;
	uint32_t frac;              /* Q16 */
	uint32_t inc;               /* Q16 samples a frame */
	uint32_t ratio;             /* Q16 sample rate / output rate */
	int32_t cents;              /* pitch from key, root and tuning, before the bend */
	int32_t env;                /* the envelope's attenuation, cB Q8 */
	int32_t env_step;           /* its change a block, cB Q8 */
	uint32_t env_blocks;        /* blocks left in delay or hold */
	uint32_t att_pos, att_len;  /* attack, in blocks */
	int32_t sustain;            /* cB Q8 */
	int32_t decay_step, release_step;
	int16_t atten;              /* cB: initial attenuation and velocity */
	int16_t pan;                /* -500..500 */
	int16_t gl, gr;             /* this block's gains, Q15 */
	uint32_t age;
};

struct gs_channel {
	uint8_t program, bank, drum;
	uint8_t volume, expression, pan, sustain;
	uint8_t rpn_msb, rpn_lsb;
	uint16_t bend;              /* 0..16383, 8192 the centre */
	uint16_t bend_range;        /* cents */
	int preset;                 /* the SoundFont preset, -1 for none */
};

struct gs_synth {
	const struct gs_sf2 *sf;
	uint32_t rate;
	int nvoices;
	int mode;
	uint32_t master;            /* Q8: 256 full */
	uint32_t clock;             /* voice ages */
	uint32_t sysex_ignored;     /* SysEx not for this engine (MT-32 DT1 ...) */
	uint32_t block_left;        /* frames to the next block update */
	struct gs_channel ch[16];
	struct gs_voice v[GS_VOICES_MAX];
};

void gs_synth_init(struct gs_synth *s, const struct gs_sf2 *sf, uint32_t rate, int voices, int mode);
void gs_synth_reset(struct gs_synth *s);
void gs_synth_msg(struct gs_synth *s, const struct gs_msg *m);
void gs_synth_sysex(struct gs_synth *s, const uint8_t *data, uint32_t len);
/* interleaved stereo, frames of 2 samples */
void gs_synth_render(struct gs_synth *s, int16_t *out, uint32_t frames);
int gs_synth_active(const struct gs_synth *s);
void gs_synth_set_master(struct gs_synth *s, uint32_t q8);

/* The port: bytes as they arrive on a MIDI port, played. */
void gs_synth_port(struct gs_synth *s, struct gs_midi_in *in, const uint8_t *bytes, uint32_t n);

/* 2^(cents/1200) in Q16, cents clamped to -14400..12000 */
uint32_t gs_exp2_cents(int32_t cents);
/* the GM program an MT-32 program sounds most like */
uint8_t gs_mt32_to_gm(uint8_t program);

/* ---- the SMF player ---- */

struct gs_seq {
	struct gs_smf smf;
	uint32_t rate;
	uint64_t now;               /* frames played, Q16 */
	uint64_t base;              /* the frame time of base_tick, Q16 */
	uint32_t base_tick;
	uint64_t spt;               /* frames a tick, Q16 */
	struct gs_smf_event ev;     /* the next event */
	int loop;
	int ended;
	uint32_t loops;             /* times it has started again */
};

/* 0, or -1 for a file that is not an SMF */
int gs_seq_open(struct gs_seq *q, const uint8_t *data, uint32_t len, uint32_t rate, int loop);
void gs_seq_rewind(struct gs_seq *q);
/* For a player that sends the song elsewhere (a MIDI port): moves the song
 * on by frames (at q's rate: 1000 makes them milliseconds) and hands each
 * event due to cb (tempo changes are the player's own). 1 while it plays, 2 when it
 * has just started again (the player sends all notes off), 0 once it ended. */
typedef void (*gs_seq_sink)(void *ctx, const struct gs_smf_event *ev);
uint32_t gs_seq_events(struct gs_seq *q, uint32_t frames, gs_seq_sink cb, void *ctx);
/* plays frames of the song into out through s; at the end without loop
 * the rest is the synth's tail (releases), and q->ended is set */
void gs_seq_render(struct gs_seq *q, struct gs_synth *s, int16_t *out, uint32_t frames);
/* the song's length in frames (walks it once) */
uint64_t gs_seq_length(struct gs_seq *q);

#endif
