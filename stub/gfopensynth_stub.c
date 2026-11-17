/* libgfopensynth.a: <gfopensynth.h>'s calls, served by gfopensynth.library.
 * MIT, Copyright (c) 2026 AmiCradle Team. */
#include <exec/types.h>
#include <dos/dos.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <stabs.h>
#include <gfopensynth.h>
#define GFOPENSYNTH_BASE_NAME __gfopensynth_base
#include <inline/gfopensynth.h>

struct Library *__gfopensynth_base;
static int tried;

int gs_available(void)
{
	if (!__gfopensynth_base && !tried) {
		tried = 1;
		__gfopensynth_base = OpenLibrary((CONST_STRPTR)GFOPENSYNTHNAME, GFOPENSYNTH_VERSION);
	}
	return __gfopensynth_base != 0;
}

static void __attribute__((used)) gs_stub_close(void)
{
	if (__gfopensynth_base) {
		CloseLibrary(__gfopensynth_base);
		__gfopensynth_base = 0;
	}
}
ADD2EXIT(gs_stub_close, -50);

struct GSSynth *gs_open(const struct TagItem *tags)
{
	if (!gs_available()) {
		SetIoErr(ERROR_OBJECT_NOT_FOUND);
		return 0;
	}
	return GSOpenSynthA(tags);
}

void gs_close(struct GSSynth *s) { if (s && gs_available()) GSCloseSynth(s); }
void gs_short(struct GSSynth *s, unsigned long m) { if (s && gs_available()) GSShortMsg(s, m); }
void gs_sysex(struct GSSynth *s, const unsigned char *d, unsigned long n) { if (s && gs_available()) GSSysEx(s, d, n); }
int gs_play_file(struct GSSynth *s, const char *name, const struct TagItem *tags) { return s && gs_available() ? GSPlayFileA(s, (CONST_STRPTR)name, tags) : 0; }
void gs_stop_file(struct GSSynth *s) { if (s && gs_available()) GSStopFile(s); }
void gs_set_volume(struct GSSynth *s, unsigned long v) { if (s && gs_available()) GSSetVolume(s, v); }
void gs_reset(struct GSSynth *s) { if (s && gs_available()) GSReset(s); }
unsigned long gs_get(struct GSSynth *s, unsigned long a) { return s && gs_available() ? GSGetAttr(s, a) : 0; }
unsigned long gs_render(struct GSSynth *s, short *b, unsigned long f) { return s && gs_available() ? GSRenderS16(s, (WORD *)b, f) : 0; }
int gs_play_mem(struct GSSynth *s, const unsigned char *d, unsigned long n, const struct TagItem *tags) { return s && gs_available() ? GSPlayMemA(s, d, n, tags) : 0; }
void gs_pause_file(struct GSSynth *s, int pause) { if (s && gs_available()) GSPauseFile(s, (ULONG)(pause != 0)); }
void gs_seek_file(struct GSSynth *s, unsigned long beat) { if (s && gs_available()) GSSeekFile(s, beat); }
void gs_write(struct GSSynth *s, const unsigned char *d, unsigned long n) { if (s && gs_available()) GSWriteBytes(s, d, n); }
unsigned long gs_mem_length(const unsigned char *d, unsigned long n) { return gs_available() ? GSMemLength(d, n) : 0; }
