/* PlaySynth: plays a MIDI file through gfopensynth.library until it ends or
 * Ctrl-C.
 *   PlaySynth FILE/A,MT32/S,LOOP/S,OUTPUT/K,SOUNDFONT/K,VOLUME/N,RATE/N,VOICES/N
 * MT32 plays it as an MT-32 song; OUTPUT is PORT, AHI or AUTO (default:
 * AkikoPlus's MIDI port if there is one, else the 68k from SOUNDFONT).
 * MIT, Copyright (c) 2026 AmiCradle Team. */
#include <exec/types.h>
#include <dos/dos.h>
#include <dos/rdargs.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <gfopensynth.h>

static const char ver[] __attribute__((used)) = "$VER: PlaySynth 1.0 (17.11.2026) GFOpenSynth, AmiCradle Team";

int main(void)
{
	LONG args[8] = { 0 };
	struct RDArgs *rd;
	struct GSSynth *s;
	struct TagItem tags[8], loop[2];
	LONG rc = RETURN_OK;
	int n = 0;

	if (!(rd = ReadArgs((CONST_STRPTR)"FILE/A,MT32/S,LOOP/S,OUTPUT/K,SOUNDFONT/K,VOLUME/N,RATE/N,VOICES/N", args, NULL))) {
		PrintFault(IoErr(), (CONST_STRPTR)"PlaySynth");
		return RETURN_FAIL;
	}
	if (!gs_available()) {
		PutStr((CONST_STRPTR)"PlaySynth: needs gfopensynth.library 1\n");
		FreeArgs(rd);
		return RETURN_FAIL;
	}
	tags[n].ti_Tag = GSA_Engine; tags[n++].ti_Data = args[1] ? GSE_MT32 : GSE_GM;
	if (args[3]) {
		char c = *(char *)args[3] | 0x20;
		tags[n].ti_Tag = GSA_Output;
		tags[n++].ti_Data = c == 'p' ? GSOUT_PORT : (c == 'a' && (((char *)args[3])[1] | 0x20) == 'h') ? GSOUT_AHI : GSOUT_AUTO;
	}
	if (args[4]) { tags[n].ti_Tag = GSA_SoundFont; tags[n++].ti_Data = (ULONG)args[4]; }
	if (args[5]) { tags[n].ti_Tag = GSA_Volume; tags[n++].ti_Data = *(ULONG *)args[5]; }
	if (args[6]) { tags[n].ti_Tag = GSA_Rate; tags[n++].ti_Data = *(ULONG *)args[6]; }
	if (args[7]) { tags[n].ti_Tag = GSA_Voices; tags[n++].ti_Data = *(ULONG *)args[7]; }
	tags[n].ti_Tag = TAG_DONE;
	if (!(s = gs_open(tags))) {
		PrintFault(IoErr(), (CONST_STRPTR)"PlaySynth");
		FreeArgs(rd);
		return RETURN_ERROR;
	}
	loop[0].ti_Tag = GSA_Loop; loop[0].ti_Data = args[2] != 0;
	loop[1].ti_Tag = TAG_DONE;
	if (!gs_play_file(s, (const char *)args[0], loop)) {
		PrintFault(gs_get(s, GSA_Error), (CONST_STRPTR)args[0]);
		rc = RETURN_ERROR;
	} else {
		ULONG a[3];
		a[0] = gs_get(s, GSA_Length);
		a[1] = (ULONG)(gs_get(s, GSA_Output) == GSOUT_PORT ? "AkikoPlus MIDI port (the host plays it)" :
		               "the 68k, to AHI");
		a[2] = (ULONG)(gs_get(s, GSA_Engine) == GSE_MT32 ? "MT-32" : "General MIDI");
		VPrintf((CONST_STRPTR)"%lu ms on %s, as %s\n", a);
		while (gs_get(s, GSA_Playing)) {
			if (CheckSignal(SIGBREAKF_CTRL_C)) {
				gs_stop_file(s);
				PutStr((CONST_STRPTR)"***Break\n");
				rc = RETURN_WARN;
				break;
			}
			Delay(5);
		}
		Delay(25);                      /* the last notes' release */
	}
	gs_close(s);
	FreeArgs(rd);
	return rc;
}
