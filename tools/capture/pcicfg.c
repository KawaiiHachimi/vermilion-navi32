// SPDX-License-Identifier: MIT
// pcicfg.c: read-only PCI config-space dump on macOS (x86_64), through IOPCIFamily's diagnostics user client
// (IOPCIDiagnosticsClient, type 0x99000001). It needs root and a non-zero `debug=` boot-arg (the client checks
// PE_i_can_has_debugger). Only the client's Read method (selector 0) is ever called; this tool cannot write.
//
//   cc -Wall -O2 -o pcicfg tools/capture/pcicfg.c -framework IOKit -framework CoreFoundation
//   sudo ./pcicfg 11:0.0 [bytes]        # bus:dev.fn in hex, default 256 bytes (up to 4096)
//
// The read goes through the bridge's configRead (ECAM), not IOPCIDevice::configAccess(), so it shows what the
// device itself answers. Struct layout from IOPCIFamily-726.100.6 IOKit/pci/IOPCIPrivate.h.
//
// WARNING (XTX machine, 2026-11-19): reading an endpoint that has stopped answering config requests FROZE THE
// WHOLE MACHINE (hard reset needed). That was the MT7922 or the I225-V behind the AMD 600-series chipset switch,
// whose IOPCIDevice shows an empty "Capability Offsets" dict. Read such devices' bridges only; never the device.
// Bus numbers are hex here, while IOPCIFamily's log prints them in decimal.
#include <IOKit/IOKitLib.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

struct diag_params {
	uint32_t options, spaceType, bitWidth, _resv;
	uint64_t value;
	union {
		uint64_t addr64;
		struct {
			unsigned int offset : 16, function : 3, device : 5, bus : 8, segment : 16, reserved : 16;
		} pci;
	} address;
};

enum { kDiagClientType = 0x99000001, kDiagRead = 0, kConfigSpace = 0 };

int main(int argc, char **argv)
{
	unsigned bus, dev, fn, len = 256;
	if (argc < 2 || sscanf(argv[1], "%x:%x.%x", &bus, &dev, &fn) != 3 || bus > 0xff || dev > 0x1f || fn > 7) {
		fprintf(stderr, "usage: %s <bus:dev.fn, hex> [bytes]\n", argv[0]);
		return 2;
	}
	if (argc > 2) len = (unsigned)strtoul(argv[2], NULL, 0);
	if (len == 0 || len > 4096 || len % 4) { fprintf(stderr, "bytes: 4..4096, multiple of 4\n"); return 2; }

	io_service_t svc = IOServiceGetMatchingService(kIOMainPortDefault, IOServiceMatching("IOPCIBridge"));
	if (!svc) { fprintf(stderr, "no IOPCIBridge\n"); return 1; }
	io_connect_t conn;
	kern_return_t kr = IOServiceOpen(svc, mach_task_self(), kDiagClientType, &conn);
	IOObjectRelease(svc);
	if (kr != KERN_SUCCESS) {
		fprintf(stderr, "IOServiceOpen failed 0x%x (root? non-zero debug= boot-arg?)\n", kr);
		return 1;
	}
	printf("%02x:%02x.%x", bus, dev, fn);
	for (unsigned off = 0; off < len; off += 4) {
		struct diag_params in = {0}, out = {0};
		in.spaceType = kConfigSpace;
		in.bitWidth = 32;
		in.address.pci.bus = bus;
		in.address.pci.device = dev;
		in.address.pci.function = fn;
		in.address.pci.offset = off;
		size_t outsz = sizeof(out);
		kr = IOConnectCallStructMethod(conn, kDiagRead, &in, sizeof(in), &out, &outsz);
		if (off % 16 == 0) printf("\n%03x:", off);
		if (kr != KERN_SUCCESS) printf(" ERR%05x", kr & 0xfffff);
		else printf(" %08x", (uint32_t)out.value);
	}
	printf("\n");
	IOServiceClose(conn);
	return 0;
}
