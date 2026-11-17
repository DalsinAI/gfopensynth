/* MIDI bytes in, events out: the port's byte stream parser (running status,
 * SysEx, real-time bytes), and a Standard MIDI File reader that walks a
 * file's tracks in time order without copying them.
 * MIT, Copyright (c) 2026 AmiCradle Team. */
#ifndef GS_MIDI_H
#define GS_MIDI_H

#include <stdint.h>

#define GS_SYSEX_MAX 512

/* gs_midi_in_byte's answers */
#define GS_IN_NONE  0
#define GS_IN_MSG   1   /* a channel message in ev */
#define GS_IN_SYSEX 2   /* a whole SysEx (F0 .. F7) in the parser's buffer */

struct gs_msg {
	uint8_t status, d1, d2, len;   /* len: 1, 2 or 3 bytes */
};

struct gs_midi_in {
	uint8_t running;       /* running status, 0 for none */
	uint8_t need, have;    /* data bytes the message wants, and has */
	uint8_t d[2];
	uint8_t in_sysex;
	uint16_t sxlen;        /* SysEx bytes kept, F0 and F7 included */
	uint16_t sxdrop;       /* 1 when a SysEx was longer than GS_SYSEX_MAX */
	uint8_t sysex[GS_SYSEX_MAX];
};

void gs_midi_in_init(struct gs_midi_in *p);
int gs_midi_in_byte(struct gs_midi_in *p, uint8_t b, struct gs_msg *ev);
/* data bytes a status byte takes (0, 1 or 2) */
int gs_midi_data_len(uint8_t status);

/* ---- Standard MIDI Files ---- */

#define GS_SMF_TRACKS 64

#define GS_EV_MSG   1   /* a channel message */
#define GS_EV_SYSEX 2   /* data/len: the bytes after F0 (F7 included when the file has it) */
#define GS_EV_TEMPO 3   /* tempo: microseconds per quarter note */
#define GS_EV_END   4   /* every track ended */

struct gs_smf_event {
	uint32_t tick;
	int type;
	struct gs_msg msg;
	uint32_t tempo;
	const uint8_t *data;
	uint32_t len;
};

struct gs_smf_track {
	const uint8_t *start, *p, *end;
	uint32_t next;         /* the tick of the next event */
	uint8_t running;
	uint8_t done;
};

struct gs_smf {
	const uint8_t *data;
	uint32_t len;
	uint16_t format, ntracks;
	int16_t division;      /* ticks per quarter note, or < 0 SMPTE */
	struct gs_smf_track tr[GS_SMF_TRACKS];
};

/* 0, or -1 for a file that is not an SMF (or has no tracks); the file's
 * bytes must stay while it is used */
int gs_smf_open(struct gs_smf *s, const uint8_t *data, uint32_t len);
void gs_smf_rewind(struct gs_smf *s);
/* the next event in time order (tracks merged, equal ticks in track order) */
void gs_smf_next(struct gs_smf *s, struct gs_smf_event *ev);

#endif
