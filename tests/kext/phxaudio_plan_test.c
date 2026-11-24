// SPDX-License-Identifier: MIT
// phxaudio_plan_test.c: host test of tools/phxctl/phxaudio_plan.h (the DP audio enable computed from the EDID).
//   cc -std=c11 -Wall -Wextra -Werror -I tools/phxctl -o /tmp/phxaudio_plan_test tests/kext/phxaudio_plan_test.c \
//     && /tmp/phxaudio_plan_test [<edid.bin> <plan.txt>]
// Without arguments: a synthetic EDID (no serial) with the audio block of the XTX machine's LG, checked field by field.
// With arguments: the computed plan must equal a plan cut from a Linux trace (tools/capture/audio_replay_plan.py),
// line by line. The real EDID and plan stay out of git (CLAUDE.md rule 3); 2026-11-24 the LG's: 90/90.
#include "phxaudio_plan.h"
#include <stdio.h>
#include <stdlib.h>

static int fails, checks;
#define CHECK(c) do { checks++; if (!(c)) { fails++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)

static uint32_t az(const struct pa_write *w, int n, uint32_t idx, int nth)   // the nth data write to endpoint index idx
{
	for (int i = 0; i + 1 < n; i++)
		if (w[i].reg == PA_AZ_INDEX && w[i].val == idx && w[i + 1].reg == PA_AZ_DATA && nth-- == 0) return w[i + 1].val;
	return 0xdeadbeef;
}

static void base_edid(uint8_t *e, const char *name, int with_audio, int cta_rev)
{
	memset(e, 0, 256);
	static const uint8_t hdr[8] = {0x00, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x00};
	memcpy(e, hdr, 8);
	e[8] = 0x1e; e[9] = 0x6d; e[10] = 0x7c; e[11] = 0x5c;    // manufacturer GSM, product 0x5c7c, serial 0
	uint8_t *d = e + 54 + 18;                                 // descriptor 2: monitor name
	d[3] = 0xfc;
	size_t k = strlen(name);
	memcpy(d + 5, name, k);
	if (k < 13) { d[5 + k] = '\n'; for (size_t j = k + 1; j < 13; j++) d[5 + j] = ' '; }
	e[126] = 1;
	uint8_t *c = e + 128;
	c[0] = 0x02; c[1] = (uint8_t)cta_rev;
	uint8_t *p = c + 4;
	if (with_audio) {
		*p++ = 0x20 | 3; *p++ = 0x0f; *p++ = 0x57; *p++ = 0x07;   // audio block: LPCM 8 ch, 32-192 kHz, 16/20/24
		*p++ = 0x80 | 3; *p++ = 0x01; *p++ = 0x00; *p++ = 0x00;   // speaker allocation FL/FR
	}
	c[2] = (uint8_t)(p - c);
}

static int compare_with(const char *edidp, const char *planp)
{
	uint8_t e[1024];
	FILE *f = fopen(edidp, "rb");
	if (!f) { perror(edidp); return 2; }
	size_t n = fread(e, 1, sizeof(e), f);
	fclose(f);
	struct pa_sink s;
	const char *err = "";
	if (pa_parse_edid(e, n, &s, &err)) { printf("parse: %s\n", err); return 1; }
	struct pa_write w[PA_PLAN_MAX];
	int m = pa_build_dp_dig1(&s, w);
	f = fopen(planp, "r");
	if (!f) { perror(planp); return 2; }
	unsigned r, v;
	int i = 0, bad = 0;
	while (fscanf(f, "%x %x", &r, &v) == 2) {
		if (i >= m || w[i].reg != r || w[i].val != v) {
			printf("line %d: plan 0x%04x 0x%08x, computed 0x%04x 0x%08x\n", i + 1, r, v, i < m ? w[i].reg : 0, i < m ? w[i].val : 0);
			bad++;
		}
		i++;
	}
	fclose(f);
	if (i != m) { printf("plan has %d writes, computed %d\n", i, m); bad++; }
	printf("phxaudio_plan_test: %s against %s: %d/%d writes equal\n", edidp, planp, m - bad < 0 ? 0 : m - bad, m);
	return bad != 0;
}

int main(int argc, char **argv)
{
	if (argc == 3) return compare_with(argv[1], argv[2]);
	uint8_t e[256];
	struct pa_sink s;
	struct pa_write w[PA_PLAN_MAX];
	const char *err = "";

	base_edid(e, "LG ULTRAGEAR+", 1, 3);
	CHECK(pa_parse_edid(e, sizeof(e), &s, &err) == 0);
	CHECK(!strcmp(s.name, "LG ULTRAGEAR+"));
	CHECK(s.nsad == 1 && s.sad[0].format == 1 && s.sad[0].channels == 8 && s.sad[0].rates == 0x57);
	int n = pa_build_dp_dig1(&s, w);
	CHECK(n == 90);
	CHECK(az(w, n, 0x25, 0) == 0x00020001);
	CHECK(az(w, n, 0x28, 0) == 0x57075707);
	for (uint32_t i = 0x29; i <= 0x35; i++) if (i != 0x30 && i != 0x34) CHECK(az(w, n, i, 0) == 0);
	CHECK(az(w, n, 0x30, 0) == 0xdeadbeef && az(w, n, 0x34, 0) == 0xdeadbeef);   // 1-bit audio, DST: not written
	CHECK(az(w, n, 0x3a, 0) == 0x5c7c6d1e);
	CHECK(az(w, n, 0x3b, 0) == 14);
	CHECK(az(w, n, 0x3e, 0) == 0x5520474c && az(w, n, 0x41, 0) == 0x2b && az(w, n, 0x42, 0) == 0);
	CHECK(w[5].reg == PA_DTO1_MODULE && w[5].val == 0x006d5ac4 && w[6].val == 0x0003a980);
	CHECK(w[n - 1].reg == PA_REG52 && w[n - 1].val == 0x10);

	base_edid(e, "LG ULTRAGEAR+", 1, 2);                       // CTA revision 2: no descriptors (fill_audio_info)
	pa_parse_edid(e, sizeof(e), &s, &err);
	n = pa_build_dp_dig1(&s, w);
	CHECK(az(w, n, 0x28, 0) == 0);

	base_edid(e, "TV", 0, 3);                                  // no audio block: default speakers 5, length 3
	pa_parse_edid(e, sizeof(e), &s, &err);
	n = pa_build_dp_dig1(&s, w);
	CHECK(az(w, n, 0x25, 0) == 0x00020005 && az(w, n, 0x27, 0) == 0 && az(w, n, 0x3b, 0) == 3);
	CHECK(az(w, n, 0x3e, 0) == 0x5654);

	base_edid(e, "X", 1, 3);                                   // AC3 6 ch + two LPCM SADs: the one with more channels
	uint8_t *c = e + 128, *p = c + c[2];
	*p++ = 0x20 | 9; *p++ = 0x15; *p++ = 0x07; *p++ = 0x50; *p++ = 0x09; *p++ = 0x07; *p++ = 0x07; *p++ = 0x01; *p++ = 0x07; *p++ = 0x01;
	c[2] = (uint8_t)(p - c);
	pa_parse_edid(e, sizeof(e), &s, &err);
	n = pa_build_dp_dig1(&s, w);
	CHECK(az(w, n, 0x28, 0) == 0x57075707);                    // the 8-channel LPCM, not the 2-channel one
	CHECK(az(w, n, 0x29, 0) == 0x00000705);                    // AC3: 6 ch, rates 0x07, byte 2 = 0
	int sf = 0;
	for (int i = 0; i < n; i++) if (w[i].reg == PA_STREAM_FORMATS && w[i].val == 5) sf = 1;
	CHECK(sf);

	CHECK(pa_parse_edid(e + 1, 200, &s, &err) != 0);           // no header

	printf("phxaudio_plan_test: %d/%d checks passed\n", checks - fails, checks);
	return fails != 0;
}
