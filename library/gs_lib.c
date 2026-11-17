/* gfopensynth.library 1.0 (GFOpenSynth): MT-32 and General MIDI music.
 * The calls are in include/sfd/gfopensynth_lib.sfd's order. A synth sounds
 * on AkikoPlus's MIDI port (the host plays it), else on the 68k from a
 * SoundFont to ahi.device, else into the caller's buffer (GSRenderS16).
 * docs/gfopensynth.md is the design.
 * MIT, Copyright (c) 2026 AmiCradle Team. */
#include <exec/types.h>
#include <exec/resident.h>
#include <exec/libraries.h>
#include <exec/execbase.h>
#include <exec/memory.h>
#include <dos/dos.h>
#include <dos/var.h>
#include <utility/tagitem.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <libraries/gfopensynth.h>
#include "gs_priv.h"

#define REG(r, decl) register decl __asm(#r)
#define LIB_VERSION 1
#define LIB_REVISION 0

struct GSLibBase {
	struct Library lib;
	BPTR seglist;
};

struct ExecBase *SysBase;
struct DosLibrary *DOSBase;

/* Run as a program, the library does nothing. This must stay the first code
 * in the file (-fno-toplevel-reorder). */
int start(void) { return -1; }

static const char lib_name[] = GFOPENSYNTHNAME;
static const char lib_id[] =
	"gfopensynth.library 1.0 (17.11.2026) GFOpenSynth, AmiCradle Team\r\n";
static const char lib_ver[] __attribute__((used)) =
	"$VER: gfopensynth.library 1.0 (17.11.2026) GFOpenSynth, AmiCradle Team";

static struct Library *lib_init(REG(d0, struct GSLibBase *base), REG(a0, BPTR seglist),
                                REG(a6, struct ExecBase *sys));
static struct Library *lib_open(REG(a6, struct GSLibBase *base));
static BPTR lib_close(REG(a6, struct GSLibBase *base));
static BPTR lib_expunge(REG(a6, struct GSLibBase *base));
static ULONG lib_null(void);

static ULONG tag_data(const struct TagItem *t, ULONG tag, ULONG def)
{
	while (t) {
		switch (t->ti_Tag) {
		case TAG_DONE: return def;
		case TAG_IGNORE: t++; break;
		case TAG_MORE: t = (const struct TagItem *)t->ti_Data; break;
		case TAG_SKIP: t += t->ti_Data + 1; break;
		default:
			if (t->ti_Tag == tag)
				return t->ti_Data;
			t++;
		}
	}
	return def;
}

static ULONG env_num(CONST_STRPTR name, ULONG def)
{
	char v[16];
	LONG n = GetVar(name, (STRPTR)v, sizeof v, GVF_GLOBAL_ONLY), i;
	ULONG x = 0;
	if (n <= 0)
		return def;
	for (i = 0; i < n && v[i] >= '0' && v[i] <= '9'; i++)
		x = x * 10 + (ULONG)(v[i] - '0');
	return i ? x : def;
}

/* ENV:GFOpenSynth/Output: auto, port, ahi */
static ULONG env_output(void)
{
	char v[8];
	LONG n = GetVar((CONST_STRPTR)"GFOpenSynth/Output", (STRPTR)v, sizeof v, GVF_GLOBAL_ONLY);
	if (n <= 0)
		return GSOUT_AUTO;
	v[0] |= 0x20;
	if (v[0] == 'p') return GSOUT_PORT;
	if (v[0] == 'a' && n >= 2 && (v[1] | 0x20) == 'h') return GSOUT_AHI;
	return GSOUT_AUTO;
}

/* a whole file in memory (AllocVec: aligned, as the SoundFont reader needs) */
static UBYTE *load_file(CONST_STRPTR name, ULONG *len, LONG *err)
{
	BPTR fh;
	LONG size;
	UBYTE *buf = NULL;
	if (!(fh = Open(name, MODE_OLDFILE))) {
		*err = IoErr();
		return NULL;
	}
	if (Seek(fh, 0, OFFSET_END) < 0 || (size = Seek(fh, 0, OFFSET_BEGINNING)) <= 0) {
		*err = ERROR_OBJECT_WRONG_TYPE;
	} else if (!(buf = AllocVec((ULONG)size, MEMF_ANY))) {
		*err = ERROR_NO_FREE_STORE;
	} else if (Read(fh, buf, size) != size) {
		*err = IoErr() ? IoErr() : ERROR_SEEK_ERROR;
		FreeVec(buf);
		buf = NULL;
	} else
		*len = (ULONG)size;
	Close(fh);
	return buf;
}

static void port_bytes(struct GSSynth *s, const UBYTE *d, ULONG n)
{
	gs_player_send(s, GSC_WRITE, d, n);
}

/* engine and master volume, as SysEx to the port, or into the 68k engine */
static void send_setup(struct GSSynth *s)
{
	UBYTE eng[7] = { 0xF0, 0x7D, 'G', 'S', 0x01, 0, 0xF7 };
	UBYTE vol[8] = { 0xF0, 0x7F, 0x7F, 0x04, 0x01, 0, 0, 0xF7 };
	ULONG v = s->volume >= GS_VOLUME_FULL ? 16383 : (s->volume * 16383) / GS_VOLUME_FULL;
	eng[5] = s->engine == GSE_MT32 ? GS_MODE_MT32_GM : GS_MODE_GM;
	vol[5] = (UBYTE)(v & 127);
	vol[6] = (UBYTE)(v >> 7);
	if (s->output == GSOUT_PORT) {
		port_bytes(s, eng, sizeof eng);
		port_bytes(s, vol, sizeof vol);
	} else {
		ObtainSemaphore(&s->lock);
		gs_synth_sysex(&s->syn, eng, sizeof eng);
		gs_synth_sysex(&s->syn, vol, sizeof vol);
		ReleaseSemaphore(&s->lock);
	}
}

static void free_synth(struct GSSynth *s)
{
	if (s->sfdata) FreeVec(s->sfdata);
	if (s->song) FreeVec(s->song);
	FreeVec(s);
}

/* ---- The calls, in their registers (gfopensynth_lib.sfd) ------------------- */

static struct GSSynth *L_GSOpenSynthA(REG(a0, const struct TagItem *tags), REG(a6, struct GSLibBase *base))
{
	struct GSSynth *s;
	ULONG want;
	LONG err = 0;
	(void)base;
	if (!(s = AllocVec(sizeof *s, MEMF_PUBLIC | MEMF_CLEAR))) {
		SetIoErr(ERROR_NO_FREE_STORE);
		return NULL;
	}
	InitSemaphore(&s->lock);
	gs_midi_in_init(&s->in);
	s->engine = tag_data(tags, GSA_Engine, GSE_GM) == GSE_MT32 ? GSE_MT32 : GSE_GM;
	s->volume = tag_data(tags, GSA_Volume, GS_VOLUME_FULL);
	if (s->volume > GS_VOLUME_FULL) s->volume = GS_VOLUME_FULL;
	s->unit = tag_data(tags, GSA_Unit, 0);
	s->rate = tag_data(tags, GSA_Rate, 22050);
	if (s->rate < 8000 || s->rate > 48000) s->rate = 22050;
	s->voices = tag_data(tags, GSA_Voices, env_num((CONST_STRPTR)"GFOpenSynth/Voices", 24));
	if (s->voices < 1 || s->voices > GS_VOICES_MAX) s->voices = 24;
	if (GetVar((CONST_STRPTR)"GFOpenSynth/Port", (STRPTR)s->portName, sizeof s->portName, GVF_GLOBAL_ONLY) <= 0) {
		const char *d = GS_PORT_DEVICE;
		ULONG i;
		for (i = 0; d[i]; i++)
			s->portName[i] = d[i];
		s->portName[i] = 0;
	}
	want = tag_data(tags, GSA_Output, ~0UL);
	if (want > GSOUT_CALLER)
		want = env_output();

	/* the port first: the host plays it */
	if (want == GSOUT_PORT || (want == GSOUT_AUTO && !gs_port_probe((CONST_STRPTR)s->portName))) {
		s->output = GSOUT_PORT;
		if ((err = gs_player_start(s)) == 0) {
			send_setup(s);
			return s;
		}
		if (want == GSOUT_PORT) {
			free_synth(s);
			SetIoErr(err);
			return NULL;
		}
	}

	/* else the 68k engine from a SoundFont */
	{
		char name[256];
		CONST_STRPTR sf = (CONST_STRPTR)tag_data(tags, GSA_SoundFont, 0);
		if (!sf) {
			if (GetVar((CONST_STRPTR)"GFOpenSynth/SoundFont", (STRPTR)name, sizeof name, GVF_GLOBAL_ONLY) > 0)
				sf = (CONST_STRPTR)name;
			else
				sf = (CONST_STRPTR)"DEVS:SoundFonts/default.sf2";
		}
		if (!(s->sfdata = load_file(sf, &s->sflen, &err))) {
			free_synth(s);
			SetIoErr(err);
			return NULL;
		}
		if (gs_sf2_open(&s->sf, s->sfdata, s->sflen)) {
			free_synth(s);
			SetIoErr(ERROR_OBJECT_WRONG_TYPE);
			return NULL;
		}
	}
	gs_synth_init(&s->syn, &s->sf, s->rate, (int)s->voices,
	              s->engine == GSE_MT32 ? GS_MODE_MT32_GM : GS_MODE_GM);
	s->output = want == GSOUT_CALLER ? GSOUT_CALLER : GSOUT_AHI;
	send_setup(s);
	if (s->output == GSOUT_AHI && (err = gs_player_start(s)) != 0) {
		free_synth(s);
		SetIoErr(err);
		return NULL;
	}
	return s;
}

static VOID L_GSCloseSynth(REG(a0, struct GSSynth *s), REG(a6, struct GSLibBase *base))
{
	(void)base;
	if (!s)
		return;
	if (s->proc)
		gs_player_send(s, GSC_QUIT, NULL, 0);
	free_synth(s);
}

static VOID L_GSShortMsg(REG(a0, struct GSSynth *s), REG(d0, ULONG msg), REG(a6, struct GSLibBase *base))
{
	struct gs_msg m;
	UBYTE b[3];
	int n;
	(void)base;
	if (!s)
		return;
	m.status = (UBYTE)msg;
	m.d1 = (UBYTE)(msg >> 8) & 127;
	m.d2 = (UBYTE)(msg >> 16) & 127;
	n = gs_midi_data_len(m.status);
	if (m.status < 0x80 || m.status >= 0xF0 || n < 1)
		return;
	m.len = (UBYTE)(1 + n);
	if (s->output == GSOUT_PORT) {
		b[0] = m.status; b[1] = m.d1; b[2] = m.d2;
		port_bytes(s, b, m.len);
		return;
	}
	ObtainSemaphore(&s->lock);
	gs_synth_msg(&s->syn, &m);
	ReleaseSemaphore(&s->lock);
}

static VOID L_GSSysEx(REG(a0, struct GSSynth *s), REG(a1, const UBYTE *data), REG(d0, ULONG len),
                      REG(a6, struct GSLibBase *base))
{
	(void)base;
	if (!s || !data || len < 2 || data[0] != 0xF0)
		return;
	if (s->output == GSOUT_PORT) {
		port_bytes(s, data, len);
		return;
	}
	ObtainSemaphore(&s->lock);
	gs_synth_sysex(&s->syn, data, len);
	ReleaseSemaphore(&s->lock);
}

static VOID L_GSStopFile(REG(a0, struct GSSynth *s), REG(a6, struct GSLibBase *base));

/* every note let go, on the 68k engine (the caller holds the lock) */
static void notes_off(struct GSSynth *s)
{
	int c;
	for (c = 0; c < 16; c++) {
		struct gs_msg m;
		m.status = (UBYTE)(0xB0 | c); m.d1 = 123; m.d2 = 0; m.len = 3;
		gs_synth_msg(&s->syn, &m);
		m.d1 = 64;
		gs_synth_msg(&s->syn, &m);
	}
}

/* plays a MIDI file's bytes (an AllocVec the synth now owns) */
static BOOL play_data(struct GSSynth *s, UBYTE *data, ULONG len, const struct TagItem *tags,
                      struct GSLibBase *base)
{
	struct gs_seq probe;
	L_GSStopFile(s, base);
	if (gs_seq_open(&probe, data, len, 1000, 0)) {
		FreeVec(data);
		s->err = ERROR_OBJECT_WRONG_TYPE;
		return FALSE;
	}
	s->lengthMs = (ULONG)gs_seq_length(&probe);
	ObtainSemaphore(&s->lock);
	s->song = data;
	s->songlen = len;
	/* the port player counts milliseconds; the 68k engine frames */
	gs_seq_open(&s->seq, data, len, s->output == GSOUT_PORT ? 1000 : s->rate,
	            tag_data(tags, GSA_Loop, FALSE) != 0);
	s->paused = FALSE;
	s->playing = TRUE;
	s->err = 0;
	ReleaseSemaphore(&s->lock);
	if (s->output == GSOUT_PORT)
		gs_player_send(s, GSC_SONG, NULL, 0);
	return TRUE;
}

static BOOL L_GSPlayFileA(REG(a0, struct GSSynth *s), REG(a1, CONST_STRPTR name),
                          REG(a2, const struct TagItem *tags), REG(a6, struct GSLibBase *base))
{
	UBYTE *data;
	ULONG len = 0;
	LONG err = 0;
	if (!s || !name)
		return FALSE;
	if (!(data = load_file(name, &len, &err))) {
		L_GSStopFile(s, base);
		s->err = err;
		return FALSE;
	}
	return play_data(s, data, len, tags, base);
}

static VOID L_GSStopFile(REG(a0, struct GSSynth *s), REG(a6, struct GSLibBase *base))
{
	UBYTE *old;
	(void)base;
	if (!s || !s->song)
		return;
	ObtainSemaphore(&s->lock);
	s->playing = FALSE;
	old = s->song;
	s->song = NULL;
	s->paused = FALSE;
	if (s->output != GSOUT_PORT)
		notes_off(s);
	ReleaseSemaphore(&s->lock);
	if (s->output == GSOUT_PORT)
		gs_player_send(s, GSC_SONG, NULL, 0);
	FreeVec(old);
}

static VOID L_GSSetVolume(REG(a0, struct GSSynth *s), REG(d0, ULONG volume), REG(a6, struct GSLibBase *base))
{
	(void)base;
	if (!s)
		return;
	s->volume = volume > GS_VOLUME_FULL ? GS_VOLUME_FULL : volume;
	send_setup(s);
}

static VOID L_GSReset(REG(a0, struct GSSynth *s), REG(a6, struct GSLibBase *base))
{
	(void)base;
	if (s)
		send_setup(s);              /* the engine switch resets the synth */
}

static ULONG L_GSGetAttr(REG(a0, struct GSSynth *s), REG(d0, ULONG attr), REG(a6, struct GSLibBase *base))
{
	(void)base;
	if (!s)
		return 0;
	switch (attr) {
	case GSA_Output:  return s->output;
	case GSA_Engine:  return s->engine;
	case GSA_Rate:    return s->output == GSOUT_PORT ? 0 : s->rate;
	case GSA_Voices:  return s->output == GSOUT_PORT ? 0 : s->voices;
	case GSA_Unit:    return s->unit;
	case GSA_Volume:  return s->volume;
	case GSA_Playing: return s->playing;
	case GSA_Loops:   return s->seq.loops;
	case GSA_Active:  return s->output == GSOUT_PORT ? 0 : (ULONG)gs_synth_active(&s->syn);
	case GSA_Error:   return (ULONG)s->err;
	case GSA_Length:  return s->lengthMs;
	case GSA_Paused:  return s->paused;
	case GSA_Position:
	case GSA_Beat: {
		ULONG v = 0;
		ObtainSemaphore(&s->lock);
		if (s->song) {
			if (attr == GSA_Beat)
				v = gs_seq_beat(&s->seq);
			else {
				v = (ULONG)(s->seq.now >> 16);
				if (s->output != GSOUT_PORT)
					v = (ULONG)(((uint64_t)v * 1000u) / s->rate);
			}
		}
		ReleaseSemaphore(&s->lock);
		return v;
	}
	}
	return 0;
}

static ULONG L_GSRenderS16(REG(a0, struct GSSynth *s), REG(a1, WORD *buf), REG(d0, ULONG frames),
                           REG(a6, struct GSLibBase *base))
{
	(void)base;
	if (!s || !buf || s->output != GSOUT_CALLER)
		return 0;
	ObtainSemaphore(&s->lock);
	if (s->playing && !s->paused) {
		gs_seq_render(&s->seq, &s->syn, (int16_t *)buf, frames);
		if (s->seq.ended)
			s->playing = FALSE;
	} else
		gs_synth_render(&s->syn, (int16_t *)buf, frames);
	ReleaseSemaphore(&s->lock);
	return frames;
}

static BOOL L_GSPlayMemA(REG(a0, struct GSSynth *s), REG(a1, const UBYTE *data), REG(d0, ULONG len),
                         REG(a2, const struct TagItem *tags), REG(a6, struct GSLibBase *base))
{
	UBYTE *copy;
	if (!s || !data || !len)
		return FALSE;
	if (!(copy = AllocVec(len, MEMF_ANY))) {
		L_GSStopFile(s, base);
		s->err = ERROR_NO_FREE_STORE;
		return FALSE;
	}
	CopyMem((APTR)data, copy, len);
	return play_data(s, copy, len, tags, base);
}

static VOID L_GSPauseFile(REG(a0, struct GSSynth *s), REG(d0, ULONG pause), REG(a6, struct GSLibBase *base))
{
	(void)base;
	if (!s || !s->song || (s->paused != 0) == (pause != 0))
		return;
	ObtainSemaphore(&s->lock);
	s->paused = pause != 0;
	if (s->paused && s->output != GSOUT_PORT)
		notes_off(s);
	ReleaseSemaphore(&s->lock);
	if (s->output == GSOUT_PORT)
		gs_player_send(s, GSC_SONG, NULL, 0);
}

static VOID L_GSSeekFile(REG(a0, struct GSSynth *s), REG(d0, ULONG beat), REG(a6, struct GSLibBase *base))
{
	(void)base;
	if (!s || !s->song)
		return;
	if (s->output == GSOUT_PORT) {
		gs_player_send(s, GSC_SEEK, NULL, beat);
		return;
	}
	ObtainSemaphore(&s->lock);
	gs_seq_seek_synth(&s->seq, &s->syn, beat);
	s->playing = TRUE;
	ReleaseSemaphore(&s->lock);
}

static VOID L_GSWriteBytes(REG(a0, struct GSSynth *s), REG(a1, const UBYTE *data), REG(d0, ULONG len),
                           REG(a6, struct GSLibBase *base))
{
	(void)base;
	if (!s || !data || !len)
		return;
	if (s->output == GSOUT_PORT) {
		port_bytes(s, data, len);
		return;
	}
	ObtainSemaphore(&s->lock);
	gs_synth_port(&s->syn, &s->in, data, len);
	ReleaseSemaphore(&s->lock);
}

static ULONG L_GSMemLength(REG(a0, const UBYTE *data), REG(d0, ULONG len), REG(a6, struct GSLibBase *base))
{
	struct gs_seq *q;
	ULONG ms = 0;
	(void)base;
	if (!data || !len || !(q = AllocVec(sizeof *q, MEMF_ANY)))
		return 0;
	if (!gs_seq_open(q, data, len, 1000, 0))
		ms = (ULONG)gs_seq_length(q);
	FreeVec(q);
	return ms;
}

static const APTR lib_vectors[] = {
	(APTR)lib_open, (APTR)lib_close, (APTR)lib_expunge, (APTR)lib_null,
	(APTR)L_GSOpenSynthA, (APTR)L_GSCloseSynth, (APTR)L_GSShortMsg, (APTR)L_GSSysEx,
	(APTR)L_GSPlayFileA, (APTR)L_GSStopFile, (APTR)L_GSSetVolume, (APTR)L_GSReset,
	(APTR)L_GSGetAttr, (APTR)L_GSRenderS16,
	(APTR)L_GSPlayMemA, (APTR)L_GSPauseFile, (APTR)L_GSSeekFile, (APTR)L_GSWriteBytes,
	(APTR)L_GSMemLength,
	(APTR)-1
};

static const struct {
	ULONG size;
	const APTR *vectors;
	APTR data;
	APTR init;
} lib_inittable = {
	sizeof(struct GSLibBase), lib_vectors, NULL, (APTR)lib_init
};

const struct Resident lib_romtag = {
	RTC_MATCHWORD, (struct Resident *)&lib_romtag, (APTR)(&lib_romtag + 1),
	RTF_AUTOINIT, LIB_VERSION, NT_LIBRARY, 0,
	(char *)lib_name, (char *)lib_id, (APTR)&lib_inittable
};

/* ---- Library housekeeping ------------------------------------------------- */

static struct Library *lib_init(REG(d0, struct GSLibBase *base), REG(a0, BPTR seglist),
                                REG(a6, struct ExecBase *sys))
{
	SysBase = sys;
	base->seglist = seglist;
	base->lib.lib_Node.ln_Type = NT_LIBRARY;
	base->lib.lib_Node.ln_Name = (char *)lib_name;
	base->lib.lib_Flags = LIBF_SUMUSED | LIBF_CHANGED;
	base->lib.lib_Version = LIB_VERSION;
	base->lib.lib_Revision = LIB_REVISION;
	base->lib.lib_IdString = (APTR)lib_id;
	if (!(DOSBase = (struct DosLibrary *)OpenLibrary((CONST_STRPTR)"dos.library", 37))) {
		FreeMem((UBYTE *)base - base->lib.lib_NegSize, base->lib.lib_NegSize + base->lib.lib_PosSize);
		return NULL;
	}
	return &base->lib;
}

static struct Library *lib_open(REG(a6, struct GSLibBase *base))
{
	base->lib.lib_OpenCnt++;
	base->lib.lib_Flags &= ~LIBF_DELEXP;
	return &base->lib;
}

static BPTR lib_close(REG(a6, struct GSLibBase *base))
{
	if (base->lib.lib_OpenCnt)
		base->lib.lib_OpenCnt--;
	if (base->lib.lib_OpenCnt == 0 && (base->lib.lib_Flags & LIBF_DELEXP))
		return lib_expunge(base);
	return 0;
}

static BPTR lib_expunge(REG(a6, struct GSLibBase *base))
{
	BPTR seglist;
	if (base->lib.lib_OpenCnt) {
		base->lib.lib_Flags |= LIBF_DELEXP;
		return 0;
	}
	seglist = base->seglist;
	Remove(&base->lib.lib_Node);
	CloseLibrary((struct Library *)DOSBase);
	FreeMem((UBYTE *)base - base->lib.lib_NegSize,
	        base->lib.lib_NegSize + base->lib.lib_PosSize);
	return seglist;
}

static ULONG lib_null(void) { return 0; }
