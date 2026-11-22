// SPDX-License-Identifier: MIT
//
// audioprobe: what CoreAudio reports for every audio device, and how steady a device's I/O cycle runs.
//
//   audioprobe list                         every device: transport, streams, formats, rates, buffer, latency, jacks
//   audioprobe run <id|default|definput> [seconds] [frames] [tone]
//                                           run an IOProc for <seconds> (default 30) at a buffer of <frames>
//                                           (default: the device's current size) and report the callback period
//                                           (median, p99, max), sample-time gaps (dropped cycles) and the HAL's
//                                           overload count. Output is silence unless "tone" is given
//                                           (a quiet 440 Hz sine, -30 dBFS). The buffer size is restored afterwards.
//
// Build: clang -O2 -fobjc-arc -framework CoreAudio -framework Foundation audioprobe.m -o audioprobe
//
// No root needed. Device UIDs are not printed (USB UIDs carry the port location).

#import <Foundation/Foundation.h>
#import <CoreAudio/CoreAudio.h>
#include <mach/mach_time.h>
#include <math.h>
#include <stdatomic.h>

static AudioObjectPropertyAddress addr(AudioObjectPropertySelector s, AudioObjectPropertyScope sc) {
	AudioObjectPropertyAddress a = {s, sc, kAudioObjectPropertyElementMain};
	return a;
}

static UInt32 getU32(AudioObjectID o, AudioObjectPropertySelector s, AudioObjectPropertyScope sc, UInt32 dflt) {
	AudioObjectPropertyAddress a = addr(s, sc);
	UInt32 v = dflt, sz = sizeof(v);
	if (!AudioObjectHasProperty(o, &a) || AudioObjectGetPropertyData(o, &a, 0, NULL, &sz, &v) != noErr) return dflt;
	return v;
}

static NSString *getStr(AudioObjectID o, AudioObjectPropertySelector s, AudioObjectPropertyScope sc) {
	AudioObjectPropertyAddress a = addr(s, sc);
	CFStringRef v = NULL;
	UInt32 sz = sizeof(v);
	if (!AudioObjectHasProperty(o, &a) || AudioObjectGetPropertyData(o, &a, 0, NULL, &sz, &v) != noErr || !v) return @"?";
	return (__bridge_transfer NSString *)v;
}

static NSData *getData(AudioObjectID o, AudioObjectPropertySelector s, AudioObjectPropertyScope sc) {
	AudioObjectPropertyAddress a = addr(s, sc);
	UInt32 sz = 0;
	if (!AudioObjectHasProperty(o, &a) || AudioObjectGetPropertyDataSize(o, &a, 0, NULL, &sz) != noErr || sz == 0) return nil;
	NSMutableData *d = [NSMutableData dataWithLength:sz];
	if (AudioObjectGetPropertyData(o, &a, 0, NULL, &sz, d.mutableBytes) != noErr) return nil;
	d.length = sz;
	return d;
}

static const char *transportName(UInt32 t) {
	switch (t) {
	case kAudioDeviceTransportTypeBuiltIn: return "built-in";
	case kAudioDeviceTransportTypeUSB: return "USB";
	case kAudioDeviceTransportTypeBluetooth: return "Bluetooth";
	case kAudioDeviceTransportTypeBluetoothLE: return "Bluetooth LE";
	case kAudioDeviceTransportTypeHDMI: return "HDMI";
	case kAudioDeviceTransportTypeDisplayPort: return "DisplayPort";
	case kAudioDeviceTransportTypeAirPlay: return "AirPlay";
	case kAudioDeviceTransportTypeVirtual: return "virtual";
	case kAudioDeviceTransportTypeAggregate: return "aggregate";
	case kAudioDeviceTransportTypeThunderbolt: return "Thunderbolt";
	case kAudioDeviceTransportTypePCI: return "PCI";
	case kAudioDeviceTransportTypeContinuityCaptureWired:
	case kAudioDeviceTransportTypeContinuityCaptureWireless: return "Continuity";
	case 0: return "unknown";
	default: return "other";
	}
}

static NSString *fourcc(UInt32 v) {
	char c[5] = {(char)(v >> 24), (char)(v >> 16), (char)(v >> 8), (char)v, 0};
	for (int i = 0; i < 4; i++) if (c[i] < 32 || c[i] > 126) return [NSString stringWithFormat:@"0x%08x", v];
	return [NSString stringWithUTF8String:c];
}

static NSArray<NSNumber *> *devices(void) {
	NSData *d = getData(kAudioObjectSystemObject, kAudioHardwarePropertyDevices, kAudioObjectPropertyScopeGlobal);
	NSMutableArray *r = [NSMutableArray array];
	const AudioObjectID *ids = d.bytes;
	for (NSUInteger i = 0; i < d.length / sizeof(AudioObjectID); i++) [r addObject:@(ids[i])];
	return r;
}

static UInt32 channels(AudioObjectID dev, AudioObjectPropertyScope sc) {
	NSData *d = getData(dev, kAudioDevicePropertyStreamConfiguration, sc);
	if (!d) return 0;
	const AudioBufferList *bl = d.bytes;
	UInt32 n = 0;
	for (UInt32 i = 0; i < bl->mNumberBuffers; i++) n += bl->mBuffers[i].mNumberChannels;
	return n;
}

static void printScope(AudioObjectID dev, AudioObjectPropertyScope sc, const char *label) {
	UInt32 ch = channels(dev, sc);
	if (!ch) {
		NSData *st = getData(dev, kAudioDevicePropertyStreams, sc);
		if (st.length) printf("  %s: %lu streams but 0 channels\n", label, (unsigned long)(st.length / sizeof(AudioStreamID)));
		return;
	}
	UInt32 lat = getU32(dev, kAudioDevicePropertyLatency, sc, 0);
	UInt32 safety = getU32(dev, kAudioDevicePropertySafetyOffset, sc, 0);
	printf("  %s: %u channels, device latency %u frames, safety offset %u frames\n", label, ch, lat, safety);

	// data sources (which jack: "Line Out", "Headphones", ...)
	NSData *ds = getData(dev, kAudioDevicePropertyDataSources, sc);
	if (ds.length) {
		const UInt32 *ids = ds.bytes;
		UInt32 cur = getU32(dev, kAudioDevicePropertyDataSource, sc, 0);
		printf("    data sources:");
		for (NSUInteger i = 0; i < ds.length / 4; i++) {
			UInt32 id = ids[i];
			CFStringRef name = NULL;
			AudioValueTranslation t = {&id, sizeof(id), &name, sizeof(name)};
			AudioObjectPropertyAddress a = addr(kAudioDevicePropertyDataSourceNameForIDCFString, sc);
			UInt32 sz = sizeof(t);
			NSString *n = @"?";
			if (AudioObjectGetPropertyData(dev, &a, 0, NULL, &sz, &t) == noErr && name) n = (__bridge_transfer NSString *)name;
			printf(" [%s%s]", n.UTF8String, id == cur ? " *" : "");
		}
		printf("\n");
	}
	AudioObjectPropertyAddress ja = addr(kAudioDevicePropertyJackIsConnected, sc);
	if (AudioObjectHasProperty(dev, &ja)) printf("    jack connected: %u\n", getU32(dev, kAudioDevicePropertyJackIsConnected, sc, 0));

	// streams and their physical formats
	NSData *st = getData(dev, kAudioDevicePropertyStreams, sc);
	const AudioStreamID *sids = st.bytes;
	for (NSUInteger s = 0; s < st.length / sizeof(AudioStreamID); s++) {
		AudioStreamID sid = sids[s];
		UInt32 slat = getU32(sid, kAudioStreamPropertyLatency, kAudioObjectPropertyScopeGlobal, 0);
		UInt32 term = getU32(sid, kAudioStreamPropertyTerminalType, kAudioObjectPropertyScopeGlobal, 0);
		AudioStreamBasicDescription cur = {0};
		AudioObjectPropertyAddress pa = addr(kAudioStreamPropertyPhysicalFormat, kAudioObjectPropertyScopeGlobal);
		UInt32 sz = sizeof(cur);
		AudioObjectGetPropertyData(sid, &pa, 0, NULL, &sz, &cur);
		printf("    stream %lu: terminal %s, stream latency %u frames, physical format now %s %u-bit %u ch @ %.0f Hz\n",
		       (unsigned long)s, fourcc(term).UTF8String, slat, fourcc(cur.mFormatID).UTF8String, cur.mBitsPerChannel,
		       cur.mChannelsPerFrame, cur.mSampleRate);
		NSData *af = getData(sid, kAudioStreamPropertyAvailablePhysicalFormats, kAudioObjectPropertyScopeGlobal);
		const AudioStreamRangedDescription *rd = af.bytes;
		NSMutableOrderedSet *fmts = [NSMutableOrderedSet orderedSet];
		for (NSUInteger i = 0; i < af.length / sizeof(*rd); i++) {
			const AudioStreamBasicDescription *f = &rd[i].mFormat;
			[fmts addObject:[NSString stringWithFormat:@"%u-bit/%uch/%.0f", f->mBitsPerChannel, f->mChannelsPerFrame, f->mSampleRate]];
		}
		printf("      available physical formats: %s\n", [[fmts array] componentsJoinedByString:@", "].UTF8String);
	}
}

static int cmdList(void) {
	AudioObjectID defOut = getU32(kAudioObjectSystemObject, kAudioHardwarePropertyDefaultOutputDevice, kAudioObjectPropertyScopeGlobal, 0);
	AudioObjectID defIn = getU32(kAudioObjectSystemObject, kAudioHardwarePropertyDefaultInputDevice, kAudioObjectPropertyScopeGlobal, 0);
	for (NSNumber *n in devices()) {
		AudioObjectID dev = n.unsignedIntValue;
		printf("device %u: \"%s\" (%s), transport %s%s%s\n", dev, getStr(dev, kAudioObjectPropertyName, kAudioObjectPropertyScopeGlobal).UTF8String,
		       getStr(dev, kAudioObjectPropertyManufacturer, kAudioObjectPropertyScopeGlobal).UTF8String,
		       transportName(getU32(dev, kAudioDevicePropertyTransportType, kAudioObjectPropertyScopeGlobal, 0)),
		       dev == defOut ? ", DEFAULT OUTPUT" : "", dev == defIn ? ", DEFAULT INPUT" : "");
		Float64 rate = 0;
		AudioObjectPropertyAddress ra = addr(kAudioDevicePropertyNominalSampleRate, kAudioObjectPropertyScopeGlobal);
		UInt32 sz = sizeof(rate);
		AudioObjectGetPropertyData(dev, &ra, 0, NULL, &sz, &rate);
		NSData *rates = getData(dev, kAudioDevicePropertyAvailableNominalSampleRates, kAudioObjectPropertyScopeGlobal);
		const AudioValueRange *rr = rates.bytes;
		NSMutableArray *rs = [NSMutableArray array];
		for (NSUInteger i = 0; i < rates.length / sizeof(*rr); i++)
			[rs addObject:rr[i].mMinimum == rr[i].mMaximum ? [NSString stringWithFormat:@"%.0f", rr[i].mMinimum]
			                                               : [NSString stringWithFormat:@"%.0f-%.0f", rr[i].mMinimum, rr[i].mMaximum]];
		AudioValueRange br = {0};
		AudioObjectPropertyAddress bra = addr(kAudioDevicePropertyBufferFrameSizeRange, kAudioObjectPropertyScopeGlobal);
		sz = sizeof(br);
		AudioObjectGetPropertyData(dev, &bra, 0, NULL, &sz, &br);
		printf("  nominal rate %.0f Hz (available %s); buffer %u frames (range %.0f-%.0f); clock domain %u; alive %u, running %u\n",
		       rate, [rs componentsJoinedByString:@", "].UTF8String,
		       getU32(dev, kAudioDevicePropertyBufferFrameSize, kAudioObjectPropertyScopeGlobal, 0), br.mMinimum, br.mMaximum,
		       getU32(dev, kAudioDevicePropertyClockDomain, kAudioObjectPropertyScopeGlobal, 0),
		       getU32(dev, kAudioDevicePropertyDeviceIsAlive, kAudioObjectPropertyScopeGlobal, 0),
		       getU32(dev, kAudioDevicePropertyDeviceIsRunningSomewhere, kAudioObjectPropertyScopeGlobal, 0));
		printScope(dev, kAudioObjectPropertyScopeOutput, "output");
		printScope(dev, kAudioObjectPropertyScopeInput, "input");
	}
	return 0;
}

// ---- run: IOProc timing ----

#define MAXCB 400000
static uint64_t gHost[MAXCB];
static double gSample[MAXCB];
static _Atomic uint32_t gCount;
static _Atomic uint32_t gOverloads;
static int gTone;
static double gPhase, gRate;

static OSStatus ioproc(AudioObjectID dev, const AudioTimeStamp *now, const AudioBufferList *in, const AudioTimeStamp *inTime,
                       AudioBufferList *out, const AudioTimeStamp *outTime, void *ctx) {
	(void)dev; (void)in; (void)ctx;
	uint32_t i = atomic_load_explicit(&gCount, memory_order_relaxed);
	if (i < MAXCB) {
		gHost[i] = now->mHostTime;
		// the sample time of the cycle: output time if we have output, else input time
		gSample[i] = (out && out->mNumberBuffers) ? outTime->mSampleTime : inTime->mSampleTime;
		atomic_store_explicit(&gCount, i + 1, memory_order_release);
	}
	if (out) {
		for (UInt32 b = 0; b < out->mNumberBuffers; b++) {
			AudioBuffer *buf = &out->mBuffers[b];
			float *f = buf->mData;
			UInt32 nch = buf->mNumberChannels ? buf->mNumberChannels : 1;
			UInt32 frames = buf->mDataByteSize / (sizeof(float) * nch);
			if (!gTone) { memset(buf->mData, 0, buf->mDataByteSize); continue; }
			double ph = gPhase;
			for (UInt32 fr = 0; fr < frames; fr++) {
				float v = (float)(0.0316 * sin(ph)); // -30 dBFS
				ph += 2 * M_PI * 440.0 / gRate;
				for (UInt32 c = 0; c < nch; c++) f[fr * nch + c] = v;
			}
			if (b == out->mNumberBuffers - 1) gPhase = fmod(ph, 2 * M_PI);
		}
	}
	return noErr;
}

static OSStatus overloadListener(AudioObjectID o, UInt32 n, const AudioObjectPropertyAddress *a, void *ctx) {
	(void)o; (void)a; (void)ctx;
	for (UInt32 i = 0; i < n; i++) atomic_fetch_add(&gOverloads, 1);
	return noErr;
}

static int cmpd(const void *a, const void *b) {
	double x = *(const double *)a, y = *(const double *)b;
	return x < y ? -1 : x > y;
}

static int cmdRun(int argc, const char **argv) {
	if (argc < 1) { fprintf(stderr, "run: need a device id, default or definput\n"); return 2; }
	AudioObjectID dev;
	if (!strcmp(argv[0], "default"))
		dev = getU32(kAudioObjectSystemObject, kAudioHardwarePropertyDefaultOutputDevice, kAudioObjectPropertyScopeGlobal, 0);
	else if (!strcmp(argv[0], "definput"))
		dev = getU32(kAudioObjectSystemObject, kAudioHardwarePropertyDefaultInputDevice, kAudioObjectPropertyScopeGlobal, 0);
	else dev = (AudioObjectID)atoi(argv[0]);
	double seconds = argc > 1 ? atof(argv[1]) : 30;
	UInt32 frames = argc > 2 ? (UInt32)atoi(argv[2]) : 0;
	gTone = argc > 3 && !strcmp(argv[3], "tone");

	AudioObjectPropertyAddress ra = addr(kAudioDevicePropertyNominalSampleRate, kAudioObjectPropertyScopeGlobal);
	UInt32 sz = sizeof(gRate);
	if (AudioObjectGetPropertyData(dev, &ra, 0, NULL, &sz, &gRate) != noErr || gRate <= 0) {
		fprintf(stderr, "run: device %u has no sample rate\n", dev);
		return 1;
	}
	UInt32 oldFrames = getU32(dev, kAudioDevicePropertyBufferFrameSize, kAudioObjectPropertyScopeGlobal, 0);
	AudioObjectPropertyAddress ba = addr(kAudioDevicePropertyBufferFrameSize, kAudioObjectPropertyScopeGlobal);
	if (frames && frames != oldFrames) {
		OSStatus e = AudioObjectSetPropertyData(dev, &ba, 0, NULL, sizeof(frames), &frames);
		if (e) fprintf(stderr, "run: setting buffer %u failed (%d)\n", frames, (int)e);
	}
	UInt32 bufNow = getU32(dev, kAudioDevicePropertyBufferFrameSize, kAudioObjectPropertyScopeGlobal, 0);
	UInt32 outCh = channels(dev, kAudioObjectPropertyScopeOutput), inCh = channels(dev, kAudioObjectPropertyScopeInput);
	AudioObjectPropertyScope sc = outCh ? kAudioObjectPropertyScopeOutput : kAudioObjectPropertyScopeInput;
	UInt32 lat = getU32(dev, kAudioDevicePropertyLatency, sc, 0), safety = getU32(dev, kAudioDevicePropertySafetyOffset, sc, 0);
	NSData *st = getData(dev, kAudioDevicePropertyStreams, sc);
	UInt32 slat = st.length ? getU32(*(const AudioStreamID *)st.bytes, kAudioStreamPropertyLatency, kAudioObjectPropertyScopeGlobal, 0) : 0;

	printf("device %u \"%s\": %u out / %u in channels, %.0f Hz, buffer %u frames (%.2f ms), %s\n", dev,
	       getStr(dev, kAudioObjectPropertyName, kAudioObjectPropertyScopeGlobal).UTF8String, outCh, inCh, gRate, bufNow,
	       1000.0 * bufNow / gRate, gTone ? "440 Hz tone at -30 dBFS" : "silence");
	printf("reported %s latency: buffer %u + safety %u + device %u + stream %u = %u frames = %.2f ms\n",
	       outCh ? "output" : "input", bufNow, safety, lat, slat, bufNow + safety + lat + slat,
	       1000.0 * (bufNow + safety + lat + slat) / gRate);

	AudioObjectPropertyAddress oa = addr(kAudioDeviceProcessorOverload, kAudioObjectPropertyScopeGlobal);
	AudioObjectAddPropertyListener(dev, &oa, overloadListener, NULL);
	AudioDeviceIOProcID pid = NULL;
	OSStatus e = AudioDeviceCreateIOProcID(dev, ioproc, NULL, &pid);
	if (!e) e = AudioDeviceStart(dev, pid);
	if (e) { fprintf(stderr, "run: start failed (%d)\n", (int)e); return 1; }
	[NSThread sleepForTimeInterval:seconds];
	AudioDeviceStop(dev, pid);
	AudioDeviceDestroyIOProcID(dev, pid);
	AudioObjectRemovePropertyListener(dev, &oa, overloadListener, NULL);
	if (frames && frames != oldFrames) AudioObjectSetPropertyData(dev, &ba, 0, NULL, sizeof(oldFrames), &oldFrames);

	uint32_t n = atomic_load(&gCount);
	if (n < 3) { printf("only %u callbacks\n", n); return 1; }
	mach_timebase_info_data_t tb;
	mach_timebase_info(&tb);
	// skip the first 10 cycles (start-up)
	uint32_t skip = n > 20 ? 10 : 0, m = n - skip - 1;
	double *dt = malloc(sizeof(double) * m);
	double expect = 1e6 * bufNow / gRate, maxdev = 0;
	uint32_t gaps = 0, late2x = 0;
	double lostFrames = 0;
	for (uint32_t i = 0; i < m; i++) {
		uint32_t k = skip + i;
		dt[i] = (double)(gHost[k + 1] - gHost[k]) * tb.numer / tb.denom / 1000.0;
		double ds = gSample[k + 1] - gSample[k];
		if (fabs(ds - bufNow) > 0.5) { gaps++; if (ds > bufNow) lostFrames += ds - bufNow; }
		if (dt[i] > 2 * expect) late2x++;
		if (fabs(dt[i] - expect) > maxdev) maxdev = fabs(dt[i] - expect);
	}
	qsort(dt, m, sizeof(double), cmpd);
	double wall = (double)(gHost[n - 1] - gHost[skip]) * tb.numer / tb.denom / 1e9;
	double samples = gSample[n - 1] - gSample[skip];
	printf("callbacks %u in %.1f s; period expected %.1f us: median %.1f, p1 %.1f, p99 %.1f, p99.9 %.1f, max %.1f us\n",
	       n, wall, expect, dt[m / 2], dt[m / 100], dt[(uint32_t)(m * 0.99)], dt[(uint32_t)(m * 0.999)], dt[m - 1]);
	printf("cycles more than 2x late: %u; sample-time discontinuities: %u (%.0f frames skipped); HAL overloads: %u\n",
	       late2x, gaps, lostFrames, atomic_load(&gOverloads));
	printf("device clock vs host clock: %.1f Hz measured (%.0f ppm)\n", samples / wall, 1e6 * (samples / wall - gRate) / gRate);
	free(dt);
	return 0;
}

int main(int argc, const char **argv) {
	@autoreleasepool {
		if (argc >= 2 && !strcmp(argv[1], "list")) return cmdList();
		if (argc >= 2 && !strcmp(argv[1], "run")) return cmdRun(argc - 2, argv + 2);
		fprintf(stderr, "usage: audioprobe list | run <id|default|definput> [seconds] [frames] [tone]\n");
		return 2;
	}
}
