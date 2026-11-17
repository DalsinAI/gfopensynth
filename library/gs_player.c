/* gfopensynth.library's player: one process a synth, for the outputs that
 * need one.
 *
 * GSOUT_AHI: the 68k engine renders into two buffers, kept going on
 * ahi.device as two linked CMD_WRITEs (ahir_Link), each refilled when it
 * comes back, as gfopenmusic.library plays.
 *
 * GSOUT_PORT: the process owns AkikoPlus's MIDI port. Messages from any
 * task come here as GSC_WRITE and go out with CMD_WRITE; a playing file is
 * timed with timer.device (every 5 ms, the song moved on by the system
 * time that passed), its events written as MIDI bytes.
 * MIT, Copyright (c) 2026 AmiCradle Team. */
#include <exec/memory.h>
#include <exec/execbase.h>
#include <exec/errors.h>
#include <devices/timer.h>
#include <dos/dostags.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/timer.h>
#include <libraries/gfopensynth.h>
#include "gs_priv.h"
#include "gs_ahidev.h"

#define PORT_TICK_US 5000

struct Device *TimerBase;

static void clear(void *p, ULONG n)
{
	UBYTE *b = p;
	while (n--)
		*b++ = 0;
}

/* ---- AHI ------------------------------------------------------------------ */

struct ahi_out {
	struct MsgPort *port;
	struct AHIRequest *req[2];
	WORD *buf[2];
	BOOL busy[2];
	struct AHIRequest *last;
	ULONG frames;
	BOOL open;
};

static void ahi_send(struct GSSynth *s, struct ahi_out *o, int i)
{
	struct AHIRequest *r = o->req[i];
	ObtainSemaphore(&s->lock);
	if (s->playing) {
		gs_seq_render(&s->seq, &s->syn, (int16_t *)o->buf[i], o->frames);
		if (s->seq.ended)
			s->playing = FALSE;
	} else
		gs_synth_render(&s->syn, (int16_t *)o->buf[i], o->frames);
	ReleaseSemaphore(&s->lock);
	r->ahir_Std.io_Message.mn_Node.ln_Pri = 0;
	r->ahir_Std.io_Command = CMD_WRITE;
	r->ahir_Std.io_Data = o->buf[i];
	r->ahir_Std.io_Length = o->frames * 4;
	r->ahir_Std.io_Offset = 0;
	r->ahir_Type = AHIST_S16S;
	r->ahir_Frequency = s->rate;
	r->ahir_Volume = 0x10000;       /* the volume is the engine's master */
	r->ahir_Position = 0x8000;
	r->ahir_Link = (o->last && o->busy[o->last == o->req[0] ? 0 : 1]) ? o->last : NULL;
	SendIO((struct IORequest *)r);
	o->busy[i] = TRUE;
	o->last = r;
}

static void ahi_close(struct ahi_out *o)
{
	int i;
	if (o->open) {
		for (i = 0; i < 2; i++)
			if (o->busy[i]) {
				AbortIO((struct IORequest *)o->req[i]);
				WaitIO((struct IORequest *)o->req[i]);
			}
		CloseDevice((struct IORequest *)o->req[0]);
	}
	for (i = 0; i < 2; i++)
		if (o->buf[i]) FreeVec(o->buf[i]);
	if (o->req[1]) FreeVec(o->req[1]);
	if (o->req[0]) DeleteIORequest((struct IORequest *)o->req[0]);
	if (o->port) DeleteMsgPort(o->port);
}

static LONG ahi_open(struct GSSynth *s, struct ahi_out *o)
{
	int i;
	if (!(o->port = CreateMsgPort()))
		return ERROR_NO_FREE_STORE;
	if (!(o->req[0] = (struct AHIRequest *)CreateIORequest(o->port, sizeof(struct AHIRequest))))
		return ERROR_NO_FREE_STORE;
	o->req[0]->ahir_Version = 4;
	if (OpenDevice((CONST_STRPTR)AHINAME, s->unit, (struct IORequest *)o->req[0], 0))
		return ERROR_OBJECT_NOT_FOUND;
	o->open = TRUE;
	if (!(o->req[1] = AllocVec(sizeof(struct AHIRequest), MEMF_PUBLIC)))
		return ERROR_NO_FREE_STORE;
	CopyMem(o->req[0], o->req[1], sizeof(struct AHIRequest));
	o->frames = s->rate / 10;       /* 100 ms a buffer */
	o->frames = (o->frames + GS_BLOCK - 1) & ~(ULONG)(GS_BLOCK - 1);
	for (i = 0; i < 2; i++)
		if (!(o->buf[i] = AllocVec(o->frames * 4, MEMF_PUBLIC)))
			return ERROR_NO_FREE_STORE;
	return 0;
}

/* ---- the port ------------------------------------------------------------- */

struct port_out {
	struct MsgPort *port;
	struct IOStdReq *io;
	BOOL open;
	struct MsgPort *tport;
	struct timerequest *tr;
	BOOL topen, ticking;
	struct timeval last;
	ULONG carry_us;                 /* the part of a millisecond not yet given to the song */
};

static void port_write(struct port_out *p, const UBYTE *data, ULONG len)
{
	if (!p->open || !len)
		return;
	p->io->io_Command = CMD_WRITE;
	p->io->io_Data = (APTR)data;
	p->io->io_Length = len;
	DoIO((struct IORequest *)p->io);
}

static void to_port(void *ctx, const struct gs_smf_event *ev)
{
	struct port_out *p = ctx;
	UBYTE b[3];
	if (ev->type == GS_EV_MSG) {
		b[0] = ev->msg.status;
		b[1] = ev->msg.d1;
		b[2] = ev->msg.d2;
		port_write(p, b, ev->msg.len);
	} else if (ev->type == GS_EV_SYSEX) {
		b[0] = 0xF0;
		port_write(p, b, 1);
		port_write(p, ev->data, ev->len);
		if (!ev->len || ev->data[ev->len - 1] != 0xF7) {
			b[0] = 0xF7;
			port_write(p, b, 1);
		}
	}
}

static void port_notes_off(struct port_out *p)
{
	UBYTE b[3];
	int c;
	for (c = 0; c < 16; c++) {
		b[0] = (UBYTE)(0xB0 | c); b[1] = 123; b[2] = 0;
		port_write(p, b, 3);
		b[1] = 64;                  /* the pedal up too */
		port_write(p, b, 3);
	}
}

LONG gs_port_probe(CONST_STRPTR name)
{
	struct MsgPort *mp = CreateMsgPort();
	struct IOStdReq *io;
	LONG err = ERROR_OBJECT_NOT_FOUND;
	if (!mp)
		return ERROR_NO_FREE_STORE;
	if ((io = (struct IOStdReq *)CreateIORequest(mp, sizeof *io)) != NULL) {
		if (!OpenDevice(name, 0, (struct IORequest *)io, 0)) {
			CloseDevice((struct IORequest *)io);
			err = 0;
		}
		DeleteIORequest((struct IORequest *)io);
	}
	DeleteMsgPort(mp);
	return err;
}

static void port_close(struct port_out *p)
{
	if (p->topen) {
		if (p->ticking) {
			AbortIO((struct IORequest *)p->tr);
			WaitIO((struct IORequest *)p->tr);
		}
		CloseDevice((struct IORequest *)p->tr);
	}
	if (p->tr) DeleteIORequest((struct IORequest *)p->tr);
	if (p->tport) DeleteMsgPort(p->tport);
	if (p->open) {
		port_notes_off(p);
		CloseDevice((struct IORequest *)p->io);
	}
	if (p->io) DeleteIORequest((struct IORequest *)p->io);
	if (p->port) DeleteMsgPort(p->port);
}

static LONG port_open(struct GSSynth *s, struct port_out *p)
{
	if (!(p->port = CreateMsgPort()) || !(p->tport = CreateMsgPort()))
		return ERROR_NO_FREE_STORE;
	if (!(p->io = (struct IOStdReq *)CreateIORequest(p->port, sizeof *p->io)) ||
	    !(p->tr = (struct timerequest *)CreateIORequest(p->tport, sizeof *p->tr)))
		return ERROR_NO_FREE_STORE;
	if (OpenDevice((CONST_STRPTR)s->portName, 0, (struct IORequest *)p->io, 0))
		return ERROR_OBJECT_NOT_FOUND;
	p->open = TRUE;
	if (OpenDevice((CONST_STRPTR)TIMERNAME, UNIT_MICROHZ, (struct IORequest *)p->tr, 0))
		return ERROR_OBJECT_NOT_FOUND;
	p->topen = TRUE;
	TimerBase = p->tr->tr_node.io_Device;
	return 0;
}

static void port_tick(struct port_out *p)
{
	p->tr->tr_node.io_Command = TR_ADDREQUEST;
	p->tr->tr_time.tv_secs = 0;
	p->tr->tr_time.tv_micro = PORT_TICK_US;
	SendIO((struct IORequest *)p->tr);
	p->ticking = TRUE;
}

/* the song moved on by the time since the last look */
static void port_song(struct GSSynth *s, struct port_out *p)
{
	struct timeval now, d;
	ULONG us, ms;
	uint32_t r;
	GetSysTime(&now);
	d = now;
	SubTime(&d, &p->last);
	p->last = now;
	us = d.tv_secs > 4 ? 4000000 : d.tv_secs * 1000000 + d.tv_micro;
	us += p->carry_us;
	ms = us / 1000;
	p->carry_us = us - ms * 1000;
	ObtainSemaphore(&s->lock);
	r = gs_seq_events(&s->seq, ms, to_port, p);
	if (r == 2)
		port_notes_off(p);
	if (r == 0) {
		s->playing = FALSE;
		port_notes_off(p);
	}
	ReleaseSemaphore(&s->lock);
}

/* ---- the process ------------------------------------------------------------ */

static void player(void)
{
	struct Process *me = (struct Process *)FindTask(NULL);
	struct GSCmd *c;
	struct GSSynth *s;
	struct ahi_out ahi;
	struct port_out pt;
	ULONG portSig, outSig = 0;
	BOOL quit = FALSE;
	LONG err;

	WaitPort(&me->pr_MsgPort);
	c = (struct GSCmd *)GetMsg(&me->pr_MsgPort);
	s = c->synth;
	clear(&ahi, sizeof ahi);
	clear(&pt, sizeof pt);
	if (!(s->port = CreateMsgPort()))
		err = ERROR_NO_FREE_STORE;
	else if (s->output == GSOUT_PORT)
		err = port_open(s, &pt);
	else
		err = ahi_open(s, &ahi);
	c->result = err;
	if (err) {
		port_close(&pt);
		ahi_close(&ahi);
		if (s->port) DeleteMsgPort(s->port);
		s->port = NULL;
		Forbid();
		ReplyMsg(&c->msg);
		return;
	}
	ReplyMsg(&c->msg);
	portSig = 1UL << s->port->mp_SigBit;
	if (s->output == GSOUT_PORT)
		outSig = 1UL << pt.tport->mp_SigBit;
	else {
		outSig = 1UL << ahi.port->mp_SigBit;
		ahi_send(s, &ahi, 0);
		ahi_send(s, &ahi, 1);
	}

	while (!quit) {
		ULONG got = Wait(portSig | outSig);
		if (got & outSig) {
			if (s->output == GSOUT_PORT) {
				if (pt.ticking && CheckIO((struct IORequest *)pt.tr)) {
					WaitIO((struct IORequest *)pt.tr);
					pt.ticking = FALSE;
					if (s->playing) {
						port_song(s, &pt);
						if (s->playing)
							port_tick(&pt);
					}
				}
			} else {
				struct AHIRequest *r;
				while ((r = (struct AHIRequest *)GetMsg(ahi.port)) != NULL) {
					int i = r == ahi.req[0] ? 0 : 1;
					ahi.busy[i] = FALSE;
					if (ahi.last == r)
						ahi.last = NULL;
					ahi_send(s, &ahi, i);
				}
			}
		}
		while ((c = (struct GSCmd *)GetMsg(s->port)) != NULL) {
			c->result = 0;
			switch (c->cmd) {
			case GSC_WRITE:
				port_write(&pt, c->data, c->len);
				break;
			case GSC_SONG:              /* a file started or stopped (sent without the lock held) */
				if (s->output == GSOUT_PORT) {
					port_notes_off(&pt);
					if (s->playing) {
						GetSysTime(&pt.last);
						pt.carry_us = 0;
						port_song(s, &pt);
						if (s->playing && !pt.ticking)
							port_tick(&pt);
					}
				}
				break;
			case GSC_QUIT:
				quit = TRUE;
				break;
			}
			if (quit)
				break;
			ReplyMsg(&c->msg);
		}
	}
	port_close(&pt);
	ahi_close(&ahi);
	DeleteMsgPort(s->port);
	s->port = NULL;
	/* the library may be expunged once the reply is seen: reply under
	 * Forbid(), which lasts until this process has gone */
	Forbid();
	ReplyMsg(&c->msg);
}

LONG gs_player_send(struct GSSynth *s, ULONG cmd, const UBYTE *data, ULONG len)
{
	struct GSCmd c;
	struct MsgPort *reply;
	if (!s || !s->port)
		return -1;
	if (!(reply = CreateMsgPort()))
		return -1;
	clear(&c, sizeof c);
	c.msg.mn_ReplyPort = reply;
	c.msg.mn_Length = sizeof c;
	c.cmd = cmd;
	c.data = data;
	c.len = len;
	c.synth = s;
	PutMsg(s->port, &c.msg);
	WaitPort(reply);
	GetMsg(reply);
	DeleteMsgPort(reply);
	return c.result;
}

LONG gs_player_start(struct GSSynth *s)
{
	struct TagItem tags[5];
	struct GSCmd c;
	struct MsgPort *reply;

	if (!(reply = CreateMsgPort()))
		return ERROR_NO_FREE_STORE;
	tags[0].ti_Tag = NP_Entry;     tags[0].ti_Data = (ULONG)player;
	tags[1].ti_Tag = NP_Name;      tags[1].ti_Data = (ULONG)"gfopensynth player";
	tags[2].ti_Tag = NP_Priority;  tags[2].ti_Data = 5;
	tags[3].ti_Tag = NP_StackSize; tags[3].ti_Data = 16384;
	tags[4].ti_Tag = TAG_DONE;     tags[4].ti_Data = 0;
	if (!(s->proc = CreateNewProc(tags))) {
		DeleteMsgPort(reply);
		return ERROR_NO_FREE_STORE;
	}
	clear(&c, sizeof c);
	c.msg.mn_ReplyPort = reply;
	c.msg.mn_Length = sizeof c;
	c.cmd = GSC_START;
	c.synth = s;
	PutMsg(&s->proc->pr_MsgPort, &c.msg);
	WaitPort(reply);
	GetMsg(reply);
	DeleteMsgPort(reply);
	if (c.result)
		s->proc = NULL;
	return c.result;
}
