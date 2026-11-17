/* The engine as one translation unit, as a host that includes its sources
 * into its own file builds it (the runtime's a1200.c includes them as it
 * does acopl3.c): every file-local name is prefixed, so none collide.
 * MIT, Copyright (c) 2026 AmiCradle Team. */
#include "../src/gs_midi.c"
#include "../src/gs_mt32mem.c"
#include "../src/gs_la.c"
#include "../src/gs_rom.c"
#include "../src/gs_sf2.c"
#include "../src/gs_synth.c"
#include "../src/gs_tables.c"

int main(void)
{
	struct gs_synth s;
	int16_t out[64];
	gs_synth_init(&s, 0, 22050, 8, GS_MODE_GM);
	gs_synth_render(&s, out, 32);
	return out[0] != 0;
}
