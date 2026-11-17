/* MIDI byte stream parser and Standard MIDI File reader (gs_midi.h).
 * MIT, Copyright (c) 2026 AmiCradle Team. */
#include "gs_midi.h"

int gs_midi_data_len(uint8_t s)
{
	if (s < 0x80)
		return -1;
	if (s < 0xF0) {
		uint8_t hi = s & 0xF0;
		return (hi == 0xC0 || hi == 0xD0) ? 1 : 2;
	}
	switch (s) {
	case 0xF1: case 0xF3: return 1;
	case 0xF2: return 2;
	default: return 0;
	}
}

void gs_midi_in_init(struct gs_midi_in *p)
{
	p->running = 0;
	p->need = p->have = 0;
	p->in_sysex = 0;
	p->sxlen = 0;
	p->sxdrop = 0;
}

static int sysex_end(struct gs_midi_in *p)
{
	p->in_sysex = 0;
	if (p->sxdrop) {
		p->sxlen = 0;
		return GS_IN_NONE;
	}
	if (p->sxlen < GS_SYSEX_MAX)
		p->sysex[p->sxlen++] = 0xF7;
	else {
		p->sxlen = 0;
		return GS_IN_NONE;
	}
	return GS_IN_SYSEX;
}

int gs_midi_in_byte(struct gs_midi_in *p, uint8_t b, struct gs_msg *ev)
{
	if (b >= 0xF8)                  /* real time: passes through everything */
		return GS_IN_NONE;
	if (p->in_sysex) {
		if (b == 0xF7)
			return sysex_end(p);
		if (b < 0x80) {
			if (p->sxlen < GS_SYSEX_MAX)
				p->sysex[p->sxlen++] = b;
			else
				p->sxdrop = 1;
			return GS_IN_NONE;
		}
		/* a status byte ends a SysEx without F7: it is dropped */
		p->in_sysex = 0;
		p->sxlen = 0;
	}
	if (b == 0xF0) {
		p->in_sysex = 1;
		p->sxlen = 1;
		p->sxdrop = 0;
		p->sysex[0] = 0xF0;
		p->running = 0;
		return GS_IN_NONE;
	}
	if (b >= 0x80) {
		int n = gs_midi_data_len(b);
		if (b >= 0xF0) {
			p->running = 0;     /* system common clears running status */
			if (n == 0) {       /* F6 tune request, stray F7: nothing to play */
				p->need = 0;
				return GS_IN_NONE;
			}
		}
		p->running = b;
		p->need = (uint8_t)n;
		p->have = 0;
		return GS_IN_NONE;
	}
	/* a data byte */
	if (!p->running)
		return GS_IN_NONE;
	p->d[p->have++] = b;
	if (p->have < p->need)
		return GS_IN_NONE;
	ev->status = p->running;
	ev->d1 = p->d[0];
	ev->d2 = p->need > 1 ? p->d[1] : 0;
	ev->len = (uint8_t)(1 + p->need);
	p->have = 0;
	if (p->running >= 0xF0)
		p->running = 0;
	if (ev->status >= 0xF0)
		return GS_IN_NONE;      /* song position etc.: not for a synth */
	return GS_IN_MSG;
}

/* ---- SMF ---- */

static uint32_t be32(const uint8_t *p)
{
	return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
}

/* a variable-length number; 0 past the end */
static uint32_t vlq(const uint8_t **pp, const uint8_t *end)
{
	uint32_t v = 0;
	int i;
	const uint8_t *p = *pp;
	for (i = 0; i < 4 && p < end; i++) {
		uint8_t b = *p++;
		v = (v << 7) | (b & 0x7F);
		if (!(b & 0x80))
			break;
	}
	*pp = p;
	return v;
}

static void track_delta(struct gs_smf_track *t)
{
	if (t->p >= t->end) {
		t->done = 1;
		return;
	}
	t->next += vlq(&t->p, t->end);
	if (t->p >= t->end)
		t->done = 1;
}

int gs_smf_open(struct gs_smf *s, const uint8_t *data, uint32_t len)
{
	const uint8_t *p, *end = data + len;
	uint32_t hlen, n = 0, want;
	if (len < 14 || data[0] != 'M' || data[1] != 'T' || data[2] != 'h' || data[3] != 'd')
		return -1;
	hlen = be32(data + 4);
	if (hlen < 6 || hlen > len - 8)
		return -1;
	s->data = data;
	s->len = len;
	s->format = (uint16_t)(data[8] << 8 | data[9]);
	want = (uint32_t)(data[10] << 8 | data[11]);
	s->division = (int16_t)(data[12] << 8 | data[13]);
	if (s->format > 2 || s->division == 0)
		return -1;
	p = data + 8 + hlen;
	while (p + 8 <= end && n < GS_SMF_TRACKS && n < want) {
		uint32_t clen = be32(p + 4);
		const uint8_t *body = p + 8;
		uint32_t room = (uint32_t)(end - body);
		if (clen > room)
			clen = room;            /* a cut file: play what is there */
		if (p[0] == 'M' && p[1] == 'T' && p[2] == 'r' && p[3] == 'k') {
			s->tr[n].start = body;
			s->tr[n].end = body + clen;
			n++;
		}
		p = body + clen;
	}
	if (!n)
		return -1;
	/* format 2: the songs one after another; played as the first song */
	s->ntracks = (uint16_t)(s->format == 2 ? 1 : n);
	gs_smf_rewind(s);
	return 0;
}

void gs_smf_rewind(struct gs_smf *s)
{
	int i;
	for (i = 0; i < s->ntracks; i++) {
		struct gs_smf_track *t = &s->tr[i];
		t->p = t->start;
		t->next = 0;
		t->running = 0;
		t->done = 0;
		track_delta(t);
	}
}

void gs_smf_next(struct gs_smf *s, struct gs_smf_event *ev)
{
	for (;;) {
		struct gs_smf_track *t = 0;
		const uint8_t *end;
		uint8_t b;
		int i;
		for (i = 0; i < s->ntracks; i++)
			if (!s->tr[i].done && (!t || s->tr[i].next < t->next))
				t = &s->tr[i];
		if (!t) {
			ev->type = GS_EV_END;
			return;
		}
		ev->tick = t->next;
		end = t->end;
		b = *t->p;
		if (b == 0xFF) {                         /* meta */
			uint8_t type;
			uint32_t mlen;
			const uint8_t *body;
			t->p++;
			if (t->p >= end) { t->done = 1; continue; }
			type = *t->p++;
			mlen = vlq(&t->p, end);
			body = t->p;
			if (mlen > (uint32_t)(end - body)) { t->done = 1; continue; }
			t->p = body + mlen;
			if (type == 0x2F) { t->done = 1; continue; }
			track_delta(t);
			if (type == 0x51 && mlen == 3) {
				ev->type = GS_EV_TEMPO;
				ev->tempo = (uint32_t)body[0] << 16 | (uint32_t)body[1] << 8 | body[2];
				if (!ev->tempo) ev->tempo = 1;
				return;
			}
			continue;
		}
		if (b == 0xF0 || b == 0xF7) {            /* SysEx, or an escape */
			uint32_t slen;
			const uint8_t *body;
			t->p++;
			slen = vlq(&t->p, end);
			body = t->p;
			if (slen > (uint32_t)(end - body)) { t->done = 1; continue; }
			t->p = body + slen;
			t->running = 0;
			track_delta(t);
			if (b == 0xF7)
				continue;                        /* escaped bytes: not played */
			ev->type = GS_EV_SYSEX;
			ev->data = body;
			ev->len = slen;
			return;
		}
		{
			uint8_t status;
			int n;
			if (b & 0x80) {
				status = b;
				t->p++;
			} else if (t->running)
				status = t->running;
			else { t->done = 1; continue; }      /* data with no status: broken */
			n = gs_midi_data_len(status);
			if (status >= 0xF0 || n < 0 || (uint32_t)(end - t->p) < (uint32_t)n) {
				t->done = 1;
				continue;
			}
			t->running = status;
			ev->type = GS_EV_MSG;
			ev->msg.status = status;
			ev->msg.d1 = t->p[0] & 0x7F;
			ev->msg.d2 = n > 1 ? (t->p[1] & 0x7F) : 0;
			ev->msg.len = (uint8_t)(1 + n);
			t->p += n;
			track_delta(t);
			return;
		}
	}
}
