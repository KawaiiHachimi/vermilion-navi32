// SPDX-License-Identifier: MIT
/*
 * phxaudio.c (thread 04, 2026-11-24): program the display engine's DP audio for head 0 (DIG1, Azalia endpoint 1) from
 * the monitor's EDID, the way Linux DC does it (tools/phxctl/phxaudio_plan.h has the sequence and its sources).
 *
 *   phxaudio plan [<edid.bin>]   print the writes (reg value), from the file or from head 0's EDID in IOKit; no root
 *   phxaudio apply               root, phxdebug=1: check that DP1 carries a stream, write the plan through
 *                                VermilionBoot's register write, print each readback, check the end state
 *   phxaudio state               root: the registers the end-state check reads
 *
 * Run after the display is up and before AppleGFXHDA attaches (tools/phxctl/audio_match.sh), and again after anything
 * that redoes the display engine's audio blocks. Writes only to the registers of phxaudio_plan.h (all of them Linux DC
 * writes on this ASIC). The XTX machine's head 0 is DIG1 on DP; anything else is refused.
 * Build: scripts/build/tools.sh phxaudio. Install: ~/gpu/phxaudio.
 */
#include "phxaudio_plan.h"
#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/IOKitLib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define UC_TYPE 0x4E34384Eu
#define SEL_HELLO 0
#define SEL_READREGS 2
#define SEL_DBG_WRITE_REG 108
#define DP1_DP_SEC_CNTL 0x56ebu

static io_connect_t conn;

static int open_kext(void)
{
	io_service_t svc = IOServiceGetMatchingService(kIOMainPortDefault, IOServiceMatching("PhxBringup"));
	if (!svc) { fprintf(stderr, "phxaudio: PhxBringup service not found\n"); return -1; }
	kern_return_t kr = IOServiceOpen(svc, mach_task_self(), UC_TYPE, &conn);
	IOObjectRelease(svc);
	if (kr) { fprintf(stderr, "phxaudio: IOServiceOpen: 0x%08x (root?)\n", kr); return -1; }
	uint64_t in[2] = {1, 0}, out[4];
	uint32_t n = 4;
	kr = IOConnectCallScalarMethod(conn, SEL_HELLO, in, 2, out, &n);
	if (kr) { fprintf(stderr, "phxaudio: Hello: 0x%08x\n", kr); return -1; }
	return 0;
}

static int rreg(uint32_t reg, uint32_t *v)
{
	uint64_t in[3] = {reg, 1, 0xFFFFFFFFull};   // ReadRegs: first dword, count, mask (as phxctl regs)
	size_t sz = 4;
	*v = 0;
	return IOConnectCallMethod(conn, SEL_READREGS, in, 3, NULL, 0, NULL, NULL, v, &sz) ? -1 : 0;
}

static int wreg(uint32_t reg, uint32_t val, uint32_t *rb)
{
	uint64_t in[2] = {reg, val}, out[1] = {0};
	uint32_t n = 1;
	kern_return_t kr = IOConnectCallScalarMethod(conn, SEL_DBG_WRITE_REG, in, 2, out, &n);
	*rb = (uint32_t)out[0];
	return kr ? (int)kr : 0;
}

static long head_of(io_registry_entry_t fb)   // phx-head, absent = 0 (VermilionAudio's rule)
{
	long h = 0;
	CFTypeRef p = IORegistryEntryCreateCFProperty(fb, CFSTR("phx-head"), kCFAllocatorDefault, 0);
	if (p && CFGetTypeID(p) == CFNumberGetTypeID()) CFNumberGetValue((CFNumberRef)p, kCFNumberLongType, &h);
	else if (p && CFGetTypeID(p) == CFDataGetTypeID() && CFDataGetLength((CFDataRef)p) >= 1) h = CFDataGetBytePtr((CFDataRef)p)[0];
	if (p) CFRelease(p);
	return h;
}

// Head 0's EDID: IODisplayEDID below the PhxAMDFB with phx-head 0.
static size_t edid_from_iokit(uint8_t *buf, size_t max)
{
	io_iterator_t it;
	if (IOServiceGetMatchingServices(kIOMainPortDefault, IOServiceMatching("PhxAMDFB"), &it)) return 0;
	size_t n = 0;
	io_registry_entry_t fb;
	while (!n && (fb = IOIteratorNext(it))) {
		if (head_of(fb) == 0) {
			CFTypeRef p = IORegistryEntrySearchCFProperty(fb, kIOServicePlane, CFSTR("IODisplayEDID"), kCFAllocatorDefault,
			                                              kIORegistryIterateRecursively);
			if (p && CFGetTypeID(p) == CFDataGetTypeID()) {
				n = (size_t)CFDataGetLength((CFDataRef)p);
				if (n > max) n = max;
				memcpy(buf, CFDataGetBytePtr((CFDataRef)p), n);
			}
			if (p) CFRelease(p);
		}
		IOObjectRelease(fb);
	}
	IOObjectRelease(it);
	return n;
}

static int load_sink(const char *path, struct pa_sink *s)
{
	uint8_t e[2048];
	size_t n;
	if (path) {
		FILE *f = fopen(path, "rb");
		if (!f) { perror(path); return 1; }
		n = fread(e, 1, sizeof(e), f);
		fclose(f);
	} else if (!(n = edid_from_iokit(e, sizeof(e)))) {
		fprintf(stderr, "phxaudio: no IODisplayEDID below head 0's PhxAMDFB\n");
		return 1;
	}
	const char *err = "";
	if (pa_parse_edid(e, n, s, &err)) { fprintf(stderr, "phxaudio: %s\n", err); return 1; }
	return 0;
}

static void describe(const struct pa_sink *s)
{
	printf("phxaudio: sink \"%s\" %04x:%04x, CTA rev %d, %d SAD(s), speakers %s0x%02x%s\n", s->name, s->mfg, s->product,
	       s->cta_rev, s->nsad, s->have_spk ? "" : "default ", s->have_spk ? s->spk : 5, s->lat_present ? ", latency" : "");
	for (int i = 0; i < s->nsad; i++)
		printf("phxaudio:   SAD format %u, %u ch, rates 0x%02x, byte 2 0x%02x\n", s->sad[i].format, s->sad[i].channels,
		       s->sad[i].rates, s->sad[i].byte2);
}

// The end state Linux leaves: DTO1 selected with its module / phase, AFMT1 on stream source 1 and powered, DIG1 AFMT
// on, the endpoint's AUDIO_ENABLED set, DP1's audio packets on.
static int check_state(int verbose)
{
	struct { uint32_t reg, mask, want; const char *what; } c[] = {
		{PA_DTO_SOURCE, 0x00000070, 0x10, "DCCG_AUDIO_DTO_SOURCE DTO_SEL = 1 (DTO1)"},
		{PA_DTO1_MODULE, 0xffffffff, PA_DPREFCLK_KHZ * 10u, "DCCG_AUDIO_DTO1_MODULE"},
		{PA_DTO1_PHASE, 0xffffffff, 240000, "DCCG_AUDIO_DTO1_PHASE"},
		{PA_AFMT_SRC, 0x7, 1, "AFMT1_AFMT_AUDIO_SRC_CONTROL = 1"},
		{PA_AFMT_MEM_PWR, 0x1, 0, "AFMT1_AFMT_MEM_PWR forced off = 0"},
		{PA_DIG_AFMT_CNTL, 0x1, 1, "DIG1_AFMT_CNTL on"},
		{PA_AFMT_PKT, 0x1, 1, "AFMT1_AFMT_AUDIO_PACKET_CONTROL sample send"},
		{DP1_DP_SEC_CNTL, 0x1, 1, "DP1_DP_SEC_CNTL stream enable"},
	};
	int bad = 0;
	for (size_t i = 0; i < sizeof(c) / sizeof(c[0]); i++) {
		uint32_t v = 0;
		int ok = !rreg(c[i].reg, &v) && (v & c[i].mask) == c[i].want;
		if (verbose || !ok) printf("phxaudio: %s 0x%04x = 0x%08x %s\n", ok ? "ok  " : "BAD ", c[i].reg, v, c[i].what);
		bad += !ok;
	}
	uint32_t rb, v = 0;
	if (wreg(PA_AZ_INDEX, 0x54, &rb) || rreg(PA_AZ_DATA, &v) || !(v & 0x80000000)) {
		printf("phxaudio: BAD  endpoint 1 HOT_PLUG_CONTROL = 0x%08x (AUDIO_ENABLED not set)\n", v);
		bad++;
	} else if (verbose) printf("phxaudio: ok   endpoint 1 HOT_PLUG_CONTROL = 0x%08x AUDIO_ENABLED\n", v);
	return bad;
}

int main(int argc, char **argv)
{
	const char *cmd = argc > 1 ? argv[1] : "";
	struct pa_sink s;
	struct pa_write w[PA_PLAN_MAX];
	if (!strcmp(cmd, "plan")) {
		if (load_sink(argc > 2 ? argv[2] : NULL, &s)) return 1;
		describe(&s);
		int n = pa_build_dp_dig1(&s, w);
		for (int i = 0; i < n; i++) printf("0x%04x 0x%08x\n", w[i].reg, w[i].val);
		return 0;
	}
	if (!strcmp(cmd, "state")) {
		if (open_kext()) return 1;
		return check_state(1) ? 1 : 0;
	}
	if (!strcmp(cmd, "apply")) {
		if (load_sink(NULL, &s) || open_kext()) return 1;
		describe(&s);
		uint32_t sec = 0;
		if (rreg(DP1_DP_SEC_CNTL, &sec) || !(sec & 1)) {
			fprintf(stderr, "phxaudio: refused: DP1_DP_SEC_CNTL = 0x%08x, DIG1 carries no DP stream\n", sec);
			return 1;
		}
		uint32_t r52 = 0, src = 0, hp = 0, rb0 = 0;
		rreg(PA_REG52, &r52);
		rreg(PA_DTO_SOURCE, &src);
		if (!wreg(PA_AZ_INDEX, 0x54, &rb0)) rreg(PA_AZ_DATA, &hp);
		printf("phxaudio: before: 0x0052 = 0x%08x, DTO_SOURCE = 0x%08x, endpoint 1 HOT_PLUG_CONTROL = 0x%08x, DP1_DP_SEC_CNTL = 0x%08x\n",
		       r52, src, hp, sec);
		int n = pa_build_dp_dig1(&s, w), err = 0;
		for (int i = 0; i < n && !err; i++) {
			uint32_t rb = 0;
			err = wreg(w[i].reg, w[i].val, &rb);
			printf("phxaudio: %2d 0x%04x <- 0x%08x readback 0x%08x%s\n", i + 1, w[i].reg, w[i].val, rb,
			       err ? " FAILED" : "");
		}
		if (err) { fprintf(stderr, "phxaudio: write failed: 0x%08x (phxdebug=1?)\n", err); return 1; }
		int bad = check_state(0);
		printf("phxaudio: %d writes, end state %s\n", n, bad ? "WRONG" : "ok");
		return bad ? 1 : 0;
	}
	fprintf(stderr, "usage: phxaudio plan [<edid.bin>] | apply | state\n");
	return 2;
}
