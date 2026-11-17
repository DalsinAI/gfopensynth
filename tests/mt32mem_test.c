/* gs_mt32mem: the MT-32's memory and its DT1 messages. No ROM is used.
 * MIT, Copyright (c) 2026 AmiCradle Team. */
#include <stdio.h>
#include <string.h>
#include "gs_mt32mem.h"

static int checks, fails;
#define CHECK(what, ok) do { checks++; if (!(ok)) { fails++; printf("FAIL: %s\n", what); } } while (0)

/* a DT1 for unit dev at a b c with n data bytes; returns its length (F0 ... F7) */
static uint32_t dt1(uint8_t *out, uint8_t dev, uint8_t a, uint8_t b, uint8_t c, const uint8_t *data, uint32_t n)
{
	uint32_t i = 0;
	out[i++] = 0xF0; out[i++] = 0x41; out[i++] = dev; out[i++] = 0x16; out[i++] = 0x12;
	out[i++] = a; out[i++] = b; out[i++] = c;
	memcpy(out + i, data, n); i += n;
	out[i] = gs_mt32_checksum(out + 5, 3 + n); i++;
	out[i++] = 0xF7;
	return i;
}

int main(void)
{
	static struct gs_mt32mem m;
	static uint8_t msg[600], data[512];
	struct gs_mt32_timbre t;
	uint32_t n, i;

	gs_mt32mem_init(&m);
	CHECK("power on: unit 0x10", m.device == 0x10);
	CHECK("power on: master volume 100, reverb mode 0 time 5 level 3", m.system[GS_MT32_S_VOLUME] == 100
	      && m.system[GS_MT32_S_REVMODE] == 0 && m.system[GS_MT32_S_REVTIME] == 5 && m.system[GS_MT32_S_REVLEVEL] == 3);
	CHECK("power on: parts 1-8 on channels 2-9, rhythm on 10", gs_mt32mem_part(&m, 1) == 0 && gs_mt32mem_part(&m, 8) == 7
	      && gs_mt32mem_part(&m, 9) == 8 && gs_mt32mem_part(&m, 0) == -1 && gs_mt32mem_part(&m, 10) == -1);
	CHECK("power on: patch 70 is group B number 6, no shift, bend 12", m.patch[70][GS_MT32_P_GROUP] == GS_MT32_GROUP_B
	      && m.patch[70][GS_MT32_P_NUMBER] == 6 && m.patch[70][GS_MT32_P_KEYSHIFT] == 24 && m.patch[70][GS_MT32_P_BEND] == 12);
	CHECK("power on: part 3 plays patch 2, pan in the middle", m.patch_temp[2][GS_MT32_P_NUMBER] == 2 && m.patch_temp[2][GS_MT32_P_PAN] == 7);
	CHECK("power on: partial reserve adds to 32", m.system[4] + m.system[5] + m.system[6] + m.system[7] + m.system[8]
	      + m.system[9] + m.system[10] + m.system[11] + m.system[12] == 32);

	/* the checksum: Roland's worked example, address 40 00 7F data 00 gives 41 */
	{ const uint8_t ex[4] = { 0x40, 0x00, 0x7F, 0x00 }; CHECK("checksum of 40 00 7F 00 is 41", gs_mt32_checksum(ex, 4) == 0x41); }
	{ const uint8_t ex[4] = { 0x10, 0x00, 0x16, 0x64 }; CHECK("checksum of 10 00 16 64 is 76", gs_mt32_checksum(ex, 4) == 0x76); }
	{ const uint8_t ex[3] = { 0, 0, 0 }; CHECK("checksum of zeros is 0", gs_mt32_checksum(ex, 3) == 0); }

	/* master volume: 10 00 16 = 50 */
	data[0] = 50;
	n = dt1(msg, 0x10, 0x10, 0x00, 0x16, data, 1);
	CHECK("master volume written", gs_mt32mem_sysex(&m, msg, n) == 1 && m.system[GS_MT32_S_VOLUME] == 50 && m.writes == 1);
	CHECK("the same without F0 and F7", gs_mt32mem_sysex(&m, msg + 1, n - 2) == 1 && m.writes == 2);
	data[0] = 127;
	n = dt1(msg, 0x10, 0x10, 0x00, 0x16, data, 1);
	CHECK("master volume held to 100", gs_mt32mem_sysex(&m, msg, n) == 1 && m.system[GS_MT32_S_VOLUME] == 100);

	/* not ours, or wrong */
	data[0] = 10;
	n = dt1(msg, 0x11, 0x10, 0x00, 0x16, data, 1);
	CHECK("another unit's message is left alone", gs_mt32mem_sysex(&m, msg, n) == 0 && m.system[GS_MT32_S_VOLUME] == 100);
	n = dt1(msg, 0x10, 0x10, 0x00, 0x16, data, 1);
	msg[3] = 0x42;
	CHECK("another model's (GS) is left alone", gs_mt32mem_sysex(&m, msg, n) == 0);
	msg[3] = 0x16; msg[1] = 0x43;
	CHECK("another maker's is left alone", gs_mt32mem_sysex(&m, msg, n) == 0);
	msg[1] = 0x41; msg[4] = 0x11;
	CHECK("a data request (RQ1) is left alone", gs_mt32mem_sysex(&m, msg, n) == 0 && m.rejected == 0);
	msg[4] = 0x12; msg[n - 2] ^= 1;
	CHECK("a bad checksum is refused, nothing written", gs_mt32mem_sysex(&m, msg, n) == -1 && m.system[GS_MT32_S_VOLUME] == 100 && m.rejected == 1);
	n = dt1(msg, 0x10, 0x10, 0x00, 0x16, data, 0);
	CHECK("a message with no data is refused", gs_mt32mem_sysex(&m, msg, n) == -1 && m.rejected == 2);
	n = dt1(msg, 0x10, 0x10, 0x00, 0x16, data, 1);
	msg[8] = 0x90; msg[9] = gs_mt32_checksum(msg + 5, 4);
	CHECK("a status byte inside is refused", gs_mt32mem_sysex(&m, msg, n) == -1);
	CHECK("an empty message is left alone", gs_mt32mem_sysex(&m, msg, 0) == 0);

	/* the system area in one message, running past its end */
	for (i = 0; i < 30; i++) data[i] = 1;
	n = dt1(msg, 0x10, 0x10, 0x00, 0x00, data, 30);
	CHECK("a write past the system area keeps the 23 bytes in it", gs_mt32mem_sysex(&m, msg, n) == 1 && m.system[0] == 1
	      && m.system[22] == 1 && gs_mt32mem_part(&m, 1) == 0);
	data[0] = 16;
	n = dt1(msg, 0x10, 0x10, 0x00, 0x0D, data, 1);
	gs_mt32mem_sysex(&m, msg, n);
	CHECK("part 1 switched off (channel 16)", m.system[GS_MT32_S_CHANNEL] == 16 && gs_mt32mem_part(&m, 1) == 1);

	/* a timbre into timbre memory 3 (08 04 00), 246 bytes in two messages as songs send them */
	for (i = 0; i < GS_MT32_TIMBRE_SIZE; i++) data[i] = 127;
	memcpy(data, "OurBrass 1", 10);
	n = dt1(msg, 0x10, 0x08, 0x04, 0x00, data, 128);
	CHECK("timbre memory, first half", gs_mt32mem_sysex(&m, msg, n) == 1);
	n = dt1(msg, 0x10, 0x08, 0x05, 0x00, data + 128, GS_MT32_TIMBRE_SIZE - 128);
	CHECK("timbre memory, second half (the address carries into the middle byte)", gs_mt32mem_sysex(&m, msg, n) == 1);
	gs_mt32_timbre_decode(m.timbre[2], &t);
	CHECK("its name", !strcmp(t.name, "OurBrass 1"));
	CHECK("its common part held: structures 12, mute 15, env mode 1", t.structure12 == 12 && t.structure34 == 12 && t.mute == 15 && t.no_sustain == 1);
	CHECK("partial 1 held: coarse 96, fine 100, keyfollow 16, bender 1, waveform 3, pcm 127, pulse width 100, velo 14",
	      t.partial[0].pitch_coarse == 96 && t.partial[0].pitch_fine == 100 && t.partial[0].pitch_keyfollow == 16
	      && t.partial[0].bender == 1 && t.partial[0].waveform == 3 && t.partial[0].pcm_wave == 127
	      && t.partial[0].pulse_width == 100 && t.partial[0].pw_velo == 14);
	CHECK("partial 4 held: resonance 30, filter keyfollow 14, amplifier bias levels 12, last envelope level 100",
	      t.partial[3].tvf_resonance == 30 && t.partial[3].tvf_keyfollow == 14 && t.partial[3].tva_bias_level1 == 12
	      && t.partial[3].tva_bias_level2 == 12 && t.partial[3].tva_env_level[3] == 100 && t.partial[3].tva_level == 100);
	CHECK("the timbres either side are untouched", m.timbre[1][0] == 0 && m.timbre[3][0] == 0);
	for (i = 0; i < 20; i++) data[i] = 5;
	CHECK("the 10 unused bytes after a timbre are dropped, the next timbre is not reached",
	      gs_mt32mem_write(&m, GS_MT32_ADDR(0x08, 0x05, 0x76), data, 10) == 0 && m.timbre[3][0] == 0);
	CHECK("timbre memory 64 is the last", gs_mt32mem_write(&m, GS_MT32_ADDR(0x08, 0x7E, 0x00), data, 1) == 1 && m.timbre[63][0] == 5
	      && gs_mt32mem_write(&m, GS_MT32_ADDR(0x09, 0x00, 0x00), data, 1) == 0);

	/* timbre temporary, part 2 (04 01 76), and a patch for part 2 choosing the memory timbre */
	data[0] = 'X';
	CHECK("timbre temporary of part 2 starts at 04 01 76", gs_mt32mem_write(&m, GS_MT32_ADDR(0x04, 0x01, 0x76), data, 1) == 1
	      && m.timbre_temp[1][0] == 'X' && m.timbre_temp[0][GS_MT32_TIMBRE_SIZE - 1] == 0);
	{
		const uint8_t p[10] = { GS_MT32_GROUP_MEMORY, 2, 30, 50, 99, 9, 9, 0, 127, 127 };
		n = dt1(msg, 0x10, 0x03, 0x00, 0x10, p, 10);
		CHECK("patch temporary of part 2", gs_mt32mem_sysex(&m, msg, n) == 1 && m.patch_temp[1][GS_MT32_P_GROUP] == GS_MT32_GROUP_MEMORY
		      && m.patch_temp[1][GS_MT32_P_NUMBER] == 2 && m.patch_temp[1][GS_MT32_P_KEYSHIFT] == 30);
		CHECK("its values held: bend 24, assign 3, reverb 1, level 100, pan 14", m.patch_temp[1][GS_MT32_P_BEND] == 24
		      && m.patch_temp[1][GS_MT32_P_ASSIGN] == 3 && m.patch_temp[1][GS_MT32_P_REVERB] == 1
		      && m.patch_temp[1][GS_MT32_P_LEVEL] == 100 && m.patch_temp[1][GS_MT32_P_PAN] == 14);
	}
	/* patch memory 128 (05 07 78) and rhythm key 24 (03 01 10) */
	{
		const uint8_t p[8] = { 1, 63, 48, 100, 24, 3, 1, 0 };
		CHECK("patch memory 128 is at 05 07 78", gs_mt32mem_write(&m, GS_MT32_ADDR(0x05, 0x07, 0x78), p, 8) == 8
		      && m.patch[127][GS_MT32_P_NUMBER] == 63 && m.patch[126][GS_MT32_P_NUMBER] == 62);
		CHECK("nothing after the last patch", gs_mt32mem_write(&m, GS_MT32_ADDR(0x05, 0x08, 0x00), p, 1) == 0);
	}
	{
		const uint8_t r[4] = { 64, 127, 127, 127 };
		CHECK("rhythm key 24: timbre 64, level, pan and reverb held", gs_mt32mem_write(&m, GS_MT32_ADDR(0x03, 0x01, 0x10), r, 4) == 4
		      && m.rhythm[0][0] == 64 && m.rhythm[0][1] == 100 && m.rhythm[0][2] == 14 && m.rhythm[0][3] == 1);
	}
	/* the display, then reset */
	n = dt1(msg, 0x10, 0x20, 0x00, 0x00, (const uint8_t *)"Hello from AmiCradle", 20);
	CHECK("the display", gs_mt32mem_sysex(&m, msg, n) == 1 && !strcmp(m.display, "Hello from AmiCradle") && m.display_set);
	data[0] = 0;
	n = dt1(msg, 0x10, 0x7F, 0x00, 0x00, data, 1);
	i = m.writes;
	CHECK("reset: power-on memory again, the counts kept", gs_mt32mem_sysex(&m, msg, n) == 1 && m.resets == 1 && m.writes == i + 1
	      && m.system[GS_MT32_S_VOLUME] == 100 && m.timbre[2][0] == 0 && !m.display_set && gs_mt32mem_part(&m, 1) == 0
	      && m.patch_temp[1][GS_MT32_P_GROUP] == GS_MT32_GROUP_A);
	CHECK("an address outside every area keeps nothing", gs_mt32mem_write(&m, GS_MT32_ADDR(0x01, 0x00, 0x00), data, 4) == 0
	      && gs_mt32mem_write(&m, GS_MT32_ADDR(0x30, 0x00, 0x00), data, 4) == 0);

	if (fails) { printf("FAIL: %d of %d MT-32 memory checks\n", fails, checks); return 1; }
	printf("PASS: %d MT-32 memory checks, 0 failed\n", checks);
	return 0;
}
