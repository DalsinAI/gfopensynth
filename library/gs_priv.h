/* gfopensynth.library inside: a synth, its player process and its output.
 * MIT, Copyright (c) 2026 AmiCradle Team. */
#ifndef GS_PRIV_H
#define GS_PRIV_H

#include <exec/types.h>
#include <exec/ports.h>
#include <exec/semaphores.h>
#include <dos/dosextens.h>
#include "../src/gs_synth.h"

/* AkikoPlus's MIDI port as an exec device: CMD_WRITE sends MIDI bytes, as
 * serial.device would to a MIDI interface. The name can be changed with
 * ENV:GFOpenSynth/Port (the port's own PR names the device). */
#define GS_PORT_DEVICE "akikomidi.device"

enum { GSC_START = 1, GSC_WRITE, GSC_SONG, GSC_QUIT };

struct GSCmd {
	struct Message msg;
	ULONG cmd;
	const UBYTE *data;
	ULONG len;
	LONG result;
	struct GSSynth *synth;
};

struct GSSynth {
	struct SignalSemaphore lock;    /* the engine and the song */
	ULONG output;                   /* GSOUT_PORT, GSOUT_AHI or GSOUT_CALLER, as opened */
	ULONG engine;                   /* GSE_ */
	ULONG volume;
	ULONG unit, rate, voices;
	LONG err;
	/* the 68k engine */
	UBYTE *sfdata;
	ULONG sflen;
	struct gs_sf2 sf;
	struct gs_synth syn;
	/* the file playing */
	UBYTE *song;
	ULONG songlen;
	struct gs_seq seq;
	volatile BOOL playing;
	ULONG lengthMs;
	/* the player process (AHI and the port) */
	struct Process *proc;
	struct MsgPort *port;
	char portName[40];
};

/* gs_player.c */
LONG gs_player_start(struct GSSynth *s);
LONG gs_player_send(struct GSSynth *s, ULONG cmd, const UBYTE *data, ULONG len);
/* 0 when the port device opens (it is closed again), else an error */
LONG gs_port_probe(CONST_STRPTR name);

extern struct ExecBase *SysBase;
extern struct DosLibrary *DOSBase;

#endif
