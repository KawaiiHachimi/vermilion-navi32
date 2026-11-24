// SPDX-License-Identifier: MIT
/*
 * phxaudio_plan.h (thread 04, 2026-11-24): the display engine's DP audio enable for one DIG, computed from the
 * monitor's EDID the way Linux amdgpu DC does it on DCN 3.2 (Navi 31). Pure C, no IOKit: phxaudio.c writes the plan
 * through VermilionBoot, tests/kext/phxaudio_plan_test.c checks it on the host.
 *
 * Source of the sequence: Linux 7.2.15's DC on the XTX machine, ftrace amdgpu_dc_wreg of a 2560x1440@240 mode set on
 * the LG (findings/2026-11-23-dp-hdmi-audio-inventory.md, section 3; tools/capture/audio_replay_plan.py cuts it out).
 * The order and every fixed value below are that trace's. The values that come from the sink are computed as Linux
 * does (drivers/gpu/drm/amd/display: dm_helpers_parse_edid_caps, fill_audio_info, dce_aud_az_configure):
 *   - speaker allocation and SUPPORTS_AI: the first CTA speaker allocation block's byte 0 (bits 0-6, bit 7), else 5
 *   - the short audio descriptors, one register per format code 1-14 (9 and 13 skipped, as Linux does), the SAD with
 *     the most channels per format; byte 2 is the sample size for LPCM and 0 for the rest (amdgpu_dm leaves
 *     max_bit_rate / vendor_specific unset); none when the CTA revision is below 3
 *   - lipsync: the HDMI VSDB's progressive latencies when its latency bit is set, else 0
 *   - HBR capable 1: Linux skips the bandwidth check for single-stream 8b/10b links (check_audio_bandwidth_dp)
 *   - sink info: the EDID's manufacturer and product ID, the monitor name (up to 13 characters, as drm copies it),
 *     its length + 1 (Linux counts the terminator), the port ID constants of dc_stream.c
 *   - DTO1: module = DPREFCLK 716666 kHz x 10 (Linux's DCN 3.2 value; the XTX trace has it), phase = 24 MHz x 10
 * Only DIG1 / AFMT1 / DP1 with Azalia endpoint 1 (the XTX machine's head 0) and only an SST 8b/10b link: other
 * instances, HDMI and 128b/132b (UHBR) are refused, they have no trace yet. AC3 adds Linux's write of 5 to
 * AZALIA_F0_CODEC_FUNCTION_PARAMETER_STREAM_FORMATS (unverified: the LG has no AC3).
 *
 * Not done here, unlike Linux: the SMU PME workaround (dcn32_enable_pme_wa, only to wake the controller from D3; AppleGFXHDA
 * keeps it in D0) and the register 0x52 question (written by DC around the DTO, decodes to an Azalia stream 6 register
 * in the DCN 3.2 header; kept as traced).
 */
#ifndef PHXAUDIO_PLAN_H
#define PHXAUDIO_PLAN_H

#include <stddef.h>
#include <stdint.h>
#include <string.h>

struct pa_sad { uint8_t format, channels, rates, byte2; };    // channels = count (1-8), rates = byte 1 & 0x7f

struct pa_sink {
	uint16_t mfg, product;          // EDID bytes 8-9, 10-11, little-endian as Linux reads them
	char name[20];                  // monitor name descriptor 0xFC, NUL-terminated, at most 13 characters
	int cta_rev;                    // revision of the first CTA extension, 0 if none
	int nsad;
	struct pa_sad sad[32];
	int have_spk;
	uint8_t spk;                    // speaker allocation byte 0
	int lat_present;
	uint8_t vlat, alat;             // raw HDMI VSDB latency bytes (ms / 2 + 1)
};

struct pa_write { uint32_t reg, val; };

// AZ endpoint 1 index / data, the DIG1 audio blocks, the DTO (absolute dword offsets: DCN 3.2 segment base +
// register offset, the same numbers phxctl regs and the trace use).
enum {
	PA_AZ_INDEX = 0x384c, PA_AZ_DATA = 0x384d,
	PA_DTO_SOURCE = 0x016b, PA_DTO1_PHASE = 0x016e, PA_DTO1_MODULE = 0x016f,
	PA_DTBCLK_PHASE = 0x3521, PA_DTBCLK_MODULO = 0x3522, PA_REG52 = 0x0052,
	PA_AFMT_PKT2 = 0x5635, PA_AFMT_60958_0 = 0x5638, PA_AFMT_PKT = 0x5642, PA_AFMT_INFO0 = 0x5643,
	PA_AFMT_SRC = 0x5645, PA_AFMT_MEM_PWR = 0x5647, PA_DIG_AFMT_CNTL = 0x5670,
	PA_DP_SEC_AUD_N = 0x56f1, PA_DP_SEC_TIMESTAMP = 0x56f5, PA_STREAM_FORMATS = 0x38cc,
};

#define PA_DPREFCLK_KHZ 716666u
#define PA_PLAN_MAX 128

// 0 = ok; otherwise a short reason in err.
static inline int pa_parse_edid(const uint8_t *e, size_t n, struct pa_sink *s, const char **err)
{
	static const uint8_t hdr[8] = {0x00, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x00};
	memset(s, 0, sizeof(*s));
	if (n < 128 || memcmp(e, hdr, 8)) { *err = "not an EDID (no 00 ff .. ff 00 header)"; return 1; }
	s->mfg = (uint16_t)(e[8] | e[9] << 8);
	s->product = (uint16_t)(e[10] | e[11] << 8);
	for (int d = 0; d < 4; d++) {   // the four 18-byte descriptors at 54
		const uint8_t *p = e + 54 + 18 * d;
		if (p[0] || p[1] || p[3] != 0xfc) continue;
		int k = 0;
		while (k < 13 && p[5 + k] != '\n') { s->name[k] = (char)p[5 + k]; k++; }
		s->name[k] = 0;
		break;
	}
	size_t blocks = (size_t)e[126] + 1;
	if (blocks * 128 > n) blocks = n / 128;
	for (size_t b = 1; b < blocks; b++) {
		const uint8_t *c = e + 128 * b;
		if (c[0] != 0x02) continue;    // CTA-861 extension
		if (!s->cta_rev) s->cta_rev = c[1];
		uint8_t dtd = c[2];
		if (dtd < 4 || dtd > 127) dtd = 127;
		for (size_t i = 4; i < dtd;) {
			uint8_t tag = c[i] >> 5, len = c[i] & 0x1f;
			if (i + 1 + len > dtd) break;
			const uint8_t *db = c + i + 1;
			if (tag == 1) {
				for (int k = 0; k + 2 < len && s->nsad < 32; k += 3) {
					struct pa_sad *a = &s->sad[s->nsad++];
					a->format = (db[k] >> 3) & 0xf;
					a->channels = (uint8_t)((db[k] & 7) + 1);
					a->rates = db[k + 1] & 0x7f;
					a->byte2 = db[k + 2];
				}
			} else if (tag == 4 && len >= 1 && !s->have_spk) {
				s->have_spk = 1;
				s->spk = db[0];
			} else if (tag == 3 && len >= 5 && db[0] == 0x03 && db[1] == 0x0c && db[2] == 0x00) {
				// HDMI VSDB: drm's latency_present[0] = byte 8 bit 7 (counting the header as byte 0)
				if (len >= 8 && (db[7] & 0x80) && len >= 10) {
					s->lat_present = 1;
					s->vlat = db[8];
					s->alat = db[9];
				}
			}
			i += 1u + len;
		}
	}
	return 0;
}

static inline int pa_put(struct pa_write *w, int *n, uint32_t reg, uint32_t val)
{
	if (*n >= PA_PLAN_MAX) return 1;
	w[*n].reg = reg; w[*n].val = val; (*n)++;
	return 0;
}

static inline void pa_az(struct pa_write *w, int *n, uint32_t idx, uint32_t val)
{
	pa_put(w, n, PA_AZ_INDEX, idx);
	pa_put(w, n, PA_AZ_DATA, val);
}

static inline void pa_dto(struct pa_write *w, int *n)
{
	pa_put(w, n, PA_REG52, 0x110);
	pa_put(w, n, PA_DTBCLK_PHASE, 0);
	pa_put(w, n, PA_DTBCLK_MODULO, 0);
	pa_put(w, n, PA_DTO_SOURCE, 0x00100030);   // DTO_SEL 3 (no DTO), then 1 (DTO1)
	pa_put(w, n, PA_DTO_SOURCE, 0x00100010);
	pa_put(w, n, PA_DTO1_MODULE, PA_DPREFCLK_KHZ * 10u);
	pa_put(w, n, PA_DTO1_PHASE, 24000u * 10u);
	pa_put(w, n, PA_DTO_SOURCE, 0x00100010);
}

// The plan for DIG1 / endpoint 1, DP SST 8b/10b. Returns the number of writes.
static inline int pa_build_dp_dig1(const struct pa_sink *s, struct pa_write *w)
{
	int n = 0;
	// clock and AFMT1 source
	pa_dto(w, &n);
	pa_put(w, &n, PA_AFMT_SRC, 1);              // AFMT1 takes Azalia stream source 1
	pa_put(w, &n, PA_AFMT_PKT2, 0x300);
	pa_put(w, &n, PA_AFMT_MEM_PWR, 0);          // AFMT1 memory powered
	// dce_aud_az_configure
	const uint8_t spk = s->have_spk ? s->spk : 5;
	pa_az(w, &n, 0x54, 0x1);                    // HOT_PLUG_CONTROL: CLOCK_GATING_DISABLE
	pa_az(w, &n, 0x25, (uint32_t)(spk & 0x7f) | 0x00020000);   // speaker allocation, DP_CONNECTION
	pa_az(w, &n, 0x27, (uint32_t)(spk >> 7));   // ACP_DATA: SUPPORTS_AI
	int ac3 = 0;
	for (int f = 1; f <= 14; f++) {
		if (f == 9 || f == 13) continue;        // 1-bit audio and DST: Linux skips them
		uint32_t v = 0;
		int best = -1;
		if (s->cta_rev >= 3)
			for (int i = 0; i < s->nsad; i++)
				if (s->sad[i].format == f && (best < 0 || s->sad[i].channels > s->sad[best].channels)) best = i;
		if (best >= 0) {
			const struct pa_sad *a = &s->sad[best];
			uint32_t byte2 = 0;
			if (f == 1) { byte2 = a->byte2; v |= (uint32_t)a->rates << 24; }
			if (f == 2) ac3 = 1;
			v |= (uint32_t)(a->channels - 1) | (uint32_t)a->rates << 8 | byte2 << 16;
		}
		pa_az(w, &n, 0x28 + (uint32_t)(f - 1), v);
	}
	if (ac3) pa_put(w, &n, PA_STREAM_FORMATS, 0x05);
	pa_az(w, &n, 0x38, 1);                      // RESPONSE_HBR: HBR_CAPABLE
	const uint32_t vl = s->lat_present ? s->vlat : 0, al = s->lat_present ? s->alat : 0;
	pa_az(w, &n, 0x37, vl);                     // RESPONSE_LIPSYNC: video, then audio
	pa_az(w, &n, 0x37, vl | al << 8);
	pa_az(w, &n, 0x3a, (uint32_t)s->mfg | (uint32_t)s->product << 16);
	uint32_t len = 0;
	while (s->name[len++] != 0 && len < 18) {}
	pa_az(w, &n, 0x3b, len);                    // SINK_DESCRIPTION_LEN (Linux counts the terminator)
	pa_az(w, &n, 0x3c, 0x5558859e);             // PORT_ID0 / 1: dc_stream.c's constants
	pa_az(w, &n, 0x3d, 0x0d989449);
	char nm[20] = {0};
	memcpy(nm, s->name, sizeof(nm));
	for (int r = 0; r < 5; r++) {               // SINK_INFO4-8: the name, 4 characters a register (2 in the last)
		uint32_t v = 0;
		for (int k = 0; k < 4 && 4 * r + k < 18; k++) v |= (uint32_t)(uint8_t)nm[4 * r + k] << (8 * k);
		pa_az(w, &n, 0x3e + (uint32_t)r, v);
	}
	// dce_aud_az_enable, DIG1 AFMT on, the DP audio packets (enc3_dp_audio_enable / afmt3_setup_dp_audio)
	pa_az(w, &n, 0x54, 0x10);
	pa_put(w, &n, PA_DIG_AFMT_CNTL, 1);
	pa_az(w, &n, 0x54, 0x80000001);             // AUDIO_ENABLED + CLOCK_GATING_DISABLE, then gating back on
	pa_az(w, &n, 0x54, 0x80000000);
	pa_put(w, &n, PA_DIG_AFMT_CNTL, 0x101);
	pa_put(w, &n, PA_DP_SEC_AUD_N, 0x8000);
	pa_put(w, &n, PA_DP_SEC_TIMESTAMP, 1);
	pa_put(w, &n, PA_AFMT_PKT, 0x04000800);
	pa_put(w, &n, PA_AFMT_PKT2, 0x300);
	pa_put(w, &n, PA_AFMT_INFO0, 0x80);
	pa_put(w, &n, PA_AFMT_60958_0, 0);
	pa_put(w, &n, PA_AFMT_PKT, 0x04000801);
	pa_put(w, &n, PA_REG52, 0x10);
	// the second DTO pass of the same mode set (dce110_setup_audio_dto runs again from the stream enable)
	pa_dto(w, &n);
	pa_put(w, &n, PA_REG52, 0x10);
	return n;
}

#endif
