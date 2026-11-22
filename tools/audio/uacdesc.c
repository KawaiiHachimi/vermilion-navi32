// SPDX-License-Identifier: MIT
//
// uacdesc: decode the USB Audio Class (UAC1 / UAC2) descriptors of a USB audio device, read-only.
//
//   uacdesc <vendorID> <productID>      e.g. uacdesc 0x0b05 0x1a5c
//
// Prints the clock entities, input / output terminals (with their terminal type: speaker, headphones, S/PDIF,
// microphone, line ...), and each streaming interface's alternate settings: the terminal it links to, channels,
// subslot size, bit resolution, and the isochronous endpoint (direction, sync type, max packet, interval).
// This tells which macOS audio device is which jack, and why CoreAudio might reject a stream.
//
// It reads the cached configuration descriptor through IOUSBLib without opening the device, so it does not
// disturb a running audio stream. No root needed. Serial numbers are not printed.
//
// Build: clang -O2 -Wall -framework IOKit -framework CoreFoundation uacdesc.c -o uacdesc

#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/IOCFPlugIn.h>
#include <IOKit/IOKitLib.h>
#include <IOKit/usb/IOUSBLib.h>
#include <stdio.h>
#include <stdlib.h>

static const char *termName(unsigned t) {
	switch (t) {
	case 0x0101: return "USB streaming";
	case 0x0201: return "microphone";
	case 0x0202: return "desktop microphone";
	case 0x0204: return "omni microphone";
	case 0x0205: return "microphone array";
	case 0x0301: return "speaker";
	case 0x0302: return "headphones";
	case 0x0304: return "desktop speaker";
	case 0x0305: return "room speaker";
	case 0x0402: return "headset";
	case 0x0601: return "analog connector";
	case 0x0602: return "digital audio interface";
	case 0x0603: return "line connector";
	case 0x0605: return "S/PDIF interface";
	case 0x0606: return "IEC1394 DA stream";
	case 0x0607: return "IEC1394 DV soundtrack";
	default: return "other";
	}
}

static void decode(const uint8_t *p, int len) {
	int uac2 = 0, ifnum = -1, alt = -1, ifclass = 0, ifsub = 0;
	for (int i = 0; i + 2 <= len && p[i] >= 2;) {
		int l = p[i], type = p[i + 1];
		const uint8_t *d = p + i;
		if (i + l > len) break;
		if (type == 0x04) { // interface
			ifnum = d[2], alt = d[3], ifclass = d[5], ifsub = d[6];
			if (ifclass == 1)
				printf("interface %d alt %d: audio %s, %d endpoint(s), protocol 0x%02x\n", ifnum, alt,
				       ifsub == 1 ? "control" : ifsub == 2 ? "streaming" : ifsub == 3 ? "MIDI" : "?", d[4], d[7]);
			else
				printf("interface %d alt %d: class 0x%02x subclass 0x%02x (not audio)\n", ifnum, alt, ifclass, ifsub);
			if (ifclass == 1) uac2 = d[7] == 0x20;
		} else if (type == 0x24 && ifclass == 1 && ifsub == 1) { // class-specific AC
			int sub = d[2];
			if (sub == 1) printf("  AC header: UAC %s, bcdADC %x.%02x\n", uac2 ? "2" : "1", d[4], d[3]);
			else if (sub == 2) { // input terminal
				unsigned tt = d[4] | d[5] << 8;
				if (uac2) printf("  input terminal %d: type 0x%04x (%s), assoc %d, clock %d, %d channels\n", d[3], tt, termName(tt), d[6], d[7], d[8]);
				else printf("  input terminal %d: type 0x%04x (%s), assoc %d, %d channels\n", d[3], tt, termName(tt), d[6], d[7]);
			} else if (sub == 3) { // output terminal
				unsigned tt = d[4] | d[5] << 8;
				if (uac2) printf("  output terminal %d: type 0x%04x (%s), assoc %d, source %d, clock %d\n", d[3], tt, termName(tt), d[6], d[7], d[8]);
				else printf("  output terminal %d: type 0x%04x (%s), assoc %d, source %d\n", d[3], tt, termName(tt), d[6], d[7]);
			} else if (sub == 6) printf("  feature unit %d: source %d\n", d[3], d[4]);
			else if (sub == 4) printf("  mixer unit %d\n", d[3]);
			else if (sub == 5) printf("  selector unit %d: %d inputs\n", d[3], d[4]);
			else if (uac2 && sub == 0x0a) printf("  clock source %d: attributes 0x%02x (%s), controls 0x%02x\n", d[3], d[4],
			                                     (d[4] & 3) == 0 ? "external" : (d[4] & 3) == 1 ? "internal fixed" : (d[4] & 3) == 2 ? "internal variable" : "internal programmable", d[5]);
			else if (uac2 && sub == 0x0b) printf("  clock selector %d: %d inputs\n", d[3], d[4]);
			else printf("  AC subtype 0x%02x id %d\n", sub, d[3]);
		} else if (type == 0x24 && ifclass == 1 && ifsub == 2) { // class-specific AS
			int sub = d[2];
			if (sub == 1) {
				if (uac2) printf("    AS general: terminal link %d, format type %d, formats 0x%08x, %d channels, channel config 0x%08x\n",
				                 d[3], d[5], d[6] | d[7] << 8 | d[8] << 16 | (unsigned)d[9] << 24, d[10], d[11] | d[12] << 8 | d[13] << 16 | (unsigned)d[14] << 24);
				else printf("    AS general: terminal link %d, delay %d, format tag 0x%04x\n", d[3], d[4], d[5] | d[6] << 8);
			} else if (sub == 2) {
				if (uac2) printf("    format type %d: subslot %d bytes, %d-bit\n", d[3], d[4], d[5]);
				else {
					printf("    format type %d: %d channels, subframe %d bytes, %d-bit, rates:", d[3], d[4], d[5], d[6]);
					int n = d[7];
					if (n == 0) printf(" %d-%d", d[8] | d[9] << 8 | d[10] << 16, d[11] | d[12] << 8 | d[13] << 16);
					for (int k = 0; k < n; k++) printf(" %d", d[8 + 3 * k] | d[9 + 3 * k] << 8 | d[10 + 3 * k] << 16);
					printf("\n");
				}
			}
		} else if (type == 0x05) { // endpoint
			int a = d[2], attr = d[3], mps = d[4] | d[5] << 8;
			const char *sync[] = {"no sync", "async", "adaptive", "sync"};
			const char *usage[] = {"data", "feedback", "implicit feedback", "?"};
			printf("    endpoint 0x%02x %s: %s, %s, %s, max packet %d (x%d), interval %d\n", a, a & 0x80 ? "IN" : "OUT",
			       (attr & 3) == 1 ? "isochronous" : (attr & 3) == 3 ? "interrupt" : (attr & 3) == 2 ? "bulk" : "control",
			       sync[(attr >> 2) & 3], usage[(attr >> 4) & 3], mps & 0x7ff, ((mps >> 11) & 3) + 1, d[6]);
		}
		i += l;
	}
}

int main(int argc, char **argv) {
	if (argc != 3) { fprintf(stderr, "usage: uacdesc <vendorID> <productID>\n"); return 2; }
	int vid = (int)strtol(argv[1], NULL, 0), pid = (int)strtol(argv[2], NULL, 0);
	CFMutableDictionaryRef m = IOServiceMatching("IOUSBHostDevice");
	CFNumberRef v = CFNumberCreate(NULL, kCFNumberIntType, &vid), q = CFNumberCreate(NULL, kCFNumberIntType, &pid);
	CFDictionarySetValue(m, CFSTR("idVendor"), v);
	CFDictionarySetValue(m, CFSTR("idProduct"), q);
	io_iterator_t it;
	if (IOServiceGetMatchingServices(kIOMainPortDefault, m, &it) != KERN_SUCCESS) return 1;
	io_service_t s;
	int found = 0;
	while ((s = IOIteratorNext(it))) {
		IOCFPlugInInterface **plug = NULL;
		SInt32 score;
		if (IOCreatePlugInInterfaceForService(s, kIOUSBDeviceUserClientTypeID, kIOCFPlugInInterfaceID, &plug, &score) || !plug) {
			fprintf(stderr, "no plug-in for the device\n");
			IOObjectRelease(s);
			continue;
		}
		IOUSBDeviceInterface **dev = NULL;
		(*plug)->QueryInterface(plug, CFUUIDGetUUIDBytes(kIOUSBDeviceInterfaceID), (LPVOID *)&dev);
		(*plug)->Release(plug);
		if (!dev) { IOObjectRelease(s); continue; }
		UInt8 n = 0;
		(*dev)->GetNumberOfConfigurations(dev, &n);
		for (UInt8 c = 0; c < n; c++) {
			IOUSBConfigurationDescriptorPtr cd = NULL;
			if ((*dev)->GetConfigurationDescriptorPtr(dev, c, &cd) == kIOReturnSuccess && cd) {
				printf("device %04x:%04x configuration %d: %d interfaces, %d bytes, max power %d mA\n", vid, pid, c,
				       cd->bNumInterfaces, USBToHostWord(cd->wTotalLength), cd->MaxPower * 2);
				decode((const uint8_t *)cd, USBToHostWord(cd->wTotalLength));
			}
		}
		(*dev)->Release(dev);
		IOObjectRelease(s);
		found++;
	}
	IOObjectRelease(it);
	if (!found) { fprintf(stderr, "no device %04x:%04x\n", vid, pid); return 1; }
	return 0;
}
