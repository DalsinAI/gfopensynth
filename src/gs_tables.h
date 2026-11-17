/* The synth's fixed-point tables (src/gs_tables.c, from tools/gen_tables.py).
 * MIT, Copyright (c) 2026 AmiCradle Team. */
#ifndef GS_TABLES_H
#define GS_TABLES_H

extern const unsigned long gs_exp2_q16[1200];
extern const unsigned short gs_cb_gain_q15[1441];
extern const unsigned short gs_curve_cb[128];
extern const unsigned short gs_pan_q15[129];

#endif
