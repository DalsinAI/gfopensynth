/* Checks step 4 against the user's own MT-32 control ROMs, on the machine that holds them: each file is known
 * by its SHA-1 (gs_rom.c), read by gs_mt32rom_read, and every group A and B timbre with no sampled partial is
 * played for a moment through our engine. It prints only names, counts and PASS or FAIL: no ROM bytes, so
 * nothing of the ROM leaves that machine.
 *   cc -O1 -Isrc -o mt32rom_check tools/mt32rom_check.c src/gs_mt32rom.c src/gs_mt32.c src/gs_la.c \
 *      src/gs_mt32mem.c src/gs_tables.c src/gs_rom.c -lm
 *   ./mt32rom_check DIR
 * MIT, Copyright (c) 2026 AmiCradle Team. */
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "gs_mt32.h"
#include "gs_mt32rom.h"
#include "gs_rom.h"

static struct gs_mt32rom r;
static struct gs_mt32 s;
static int32_t L[4096], R[4096];

static int sounds(int program)
{
	int i;
	long m = 0;
	gs_mt32_init(&s, 32000);
	gs_mt32_use_rom(&s, &r);
	gs_mt32_midi(&s, 0xC1, (uint8_t)program, 0);
	if (!gs_mt32_playable(&s, 1, 0))
		return -1;                                  /* a sampled partial is on: part two */
	gs_mt32_midi(&s, 0x91, 60, 100);
	for (i = 0; i < 8; i++) {                       /* a quarter of a second */
		int k;
		memset(L, 0, sizeof L); memset(R, 0, sizeof R);
		gs_mt32_render(&s, L, R, 1000);
		for (k = 0; k < 1000; k++) { long a = labs((long)L[k]) + labs((long)R[k]); if (a > m) m = a; }
	}
	return m > 0;
}

int main(int argc, char **argv)
{
	DIR *d;
	struct dirent *e;
	int files = 0, bad = 0;
	if (argc < 2 || !(d = opendir(argv[1]))) {
		fprintf(stderr, "usage: mt32rom_check DIR\n");
		return 2;
	}
	while ((e = readdir(d))) {
		char path[1024];
		FILE *f;
		static uint8_t buf[1 << 20];
		long n;
		const struct gs_rom_info *info;
		int layout, p, synth = 0, silent = 0, sampled = 0;
		snprintf(path, sizeof path, "%s/%s", argv[1], e->d_name);
		if (!(f = fopen(path, "rb")))
			continue;
		n = (long)fread(buf, 1, sizeof buf, f);
		fclose(f);
		info = n > 0 ? gs_rom_identify(buf, (uint32_t)n) : NULL;
		if (!info || info->kind != GS_ROM_CTRL || info->part != GS_PART_FULL)
			continue;
		files++;
		layout = gs_mt32rom_read(&r, buf, (uint32_t)n);
		if (!layout) {
			printf("FAIL %s: neither layout\n", info->name);
			bad++;
			continue;
		}
		for (p = 0; p < 128; p++) {
			int k = sounds(p);
			if (k < 0) sampled++;
			else if (k) synth++;
			else silent++;
		}
		printf("%s %s: %s layout, 128 timbres; %d play, %d wait for the PCM ROM, %d silent\n",
		       silent ? "FAIL" : "ok  ", info->name, layout == GS_MT32ROM_WHOLE ? "whole" : "packed", synth, sampled, silent);
		bad += silent != 0;
	}
	closedir(d);
	printf("%s: %d control ROMs\n", bad || !files ? "FAIL" : "PASS", files);
	return bad || !files;
}
