// SPDX-License-Identifier: MIT OR GPL-2.0-only
// mesboot: Phoenix (GC 11.0.1) GART + MES bring-up experiments — navi31 branch: RX 7900 XT (GC 11.0.0) values, driven through PhxBringup debug selectors.
// build: clang -O2 -o mesboot mesboot.c -framework IOKit -framework CoreFoundation ; run as root (phxdebug=1)
#include <IOKit/IOKitLib.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stddef.h>
#include <unistd.h>
#include "mes_v11_api_def.h"

#define UC_TYPE 0x4E34384Eu
#define SEL_HELLO 0
#define SEL_READREGS 2
#define SEL_DBG_VRAM_READ 102
#define SEL_DBG_WRITE_REG 108
#define SEL_DBG_VRAM_WRITE 109
#define SEL_DBG_DOORBELL 110
#define SEL_DBG_SYSMEM 111
#define SEL_DBG_ADOPTQ 112

static io_connect_t conn;

static int open_kext(void)
{
   io_service_t svc = IOServiceGetMatchingService(kIOMainPortDefault, IOServiceMatching("PhxBringup"));
   if (!svc) { fprintf(stderr, "no PhxBringup\n"); return -1; }
   kern_return_t kr = IOServiceOpen(svc, mach_task_self(), UC_TYPE, &conn);
   uint64_t hin[2] = {1, 0}, hout[4]; uint32_t hn = 4;
   if (kr == 0) kr = IOConnectCallScalarMethod(conn, SEL_HELLO, hin, 2, hout, &hn);
   if (kr) { fprintf(stderr, "open: 0x%x\n", kr); return -1; }
   return 0;
}
static uint32_t rd(uint32_t reg)
{
   uint64_t in[3] = {reg, 1, 0xFFFFFFFFull}; uint32_t v = 0; size_t sz = 4;
   IOConnectCallMethod(conn, SEL_READREGS, in, 3, NULL, 0, NULL, NULL, &v, &sz);
   return v;
}
static uint32_t wr(uint32_t reg, uint32_t val)
{
   uint64_t in[2] = {reg, val}, out[1] = {0}; uint32_t n = 1;
   IOConnectCallScalarMethod(conn, SEL_DBG_WRITE_REG, in, 2, out, &n);
   return (uint32_t)out[0];
}
static void hdp_flush(void);
static kern_return_t db(uint64_t byteOff, uint64_t v)
{
   uint64_t in[3] = {byteOff, v & 0xffffffffu, v >> 32};
   hdp_flush();   // navi31: CPU writes to VRAM through BAR0 sit in the HDP until flushed; the doorbell must follow it
   return IOConnectCallScalarMethod(conn, SEL_DBG_DOORBELL, in, 3, NULL, NULL);
}
static kern_return_t sm(uint64_t op, uint64_t a, uint64_t b, uint64_t *o0, uint64_t *o1)
{
   uint64_t in[3] = {op, a, b}, out[2] = {0, 0}; uint32_t n = 2;
   kern_return_t kr = IOConnectCallScalarMethod(conn, SEL_DBG_SYSMEM, in, 3, out, &n);
   if (o0) *o0 = out[0];
   if (o1) *o1 = out[1];
   return kr;
}
// navi31: HDP flush as Linux hdp_v6_0_flush_hdp: write 0 to the MMIO-remapped HDP_MEM_FLUSH_CNTL
// (BIF_BX0_REMAP_HDP_MEM_FLUSH_CNTL 0xE4D holds its BAR5 byte offset, 0x385C -> dword 0xE17), then read back.
static void hdp_flush(void)
{
   uint32_t remap = rd(0xE4D);
   if (remap && remap != 0xFFFFFFFFu && !(remap & 3)) { wr(remap >> 2, 0); (void)rd(remap >> 2); }
}
static void vw(uint64_t off, uint32_t v)
{
   uint64_t in[2] = {off, v};
   IOConnectCallScalarMethod(conn, SEL_DBG_VRAM_WRITE, in, 2, NULL, NULL);
}
static uint32_t vr(uint64_t off)
{
   uint64_t in[2] = {off, 4}; uint32_t buf[4] = {0}; size_t sz = sizeof(buf);
   IOConnectCallMethod(conn, SEL_DBG_VRAM_READ, in, 2, NULL, 0, NULL, NULL, buf, &sz);
   return buf[0];
}


#define VRAM_MC 0x8000000000ull          // MC address of VRAM offset 0
#define GART_VA 0x7FFF00000000ull            // VA = GART_VA + vram offset (first 128 MiB)
// navi31: dGPU — VRAM "physical" = VRAM offset (GCMC_VM_FB_OFFSET reads 0; Linux PT base 0x4_feb00001 = offset).
// Work area moved above the GOP framebuffer, the PSP fw_buf (128-144 MiB) and the SMU tables (144-146 MiB), and
// still inside the 256 MiB BAR0 macOS gives us: GART table at 160 MiB, rings/MQDs/test buffers from 161 MiB.
#define GART_PT 0xA000000ull             // flat PTE table (VRAM offset); 1 MiB at 512 MiB of window (rings start at 161 MiB)
// navi32: the window has to reach past the display's scan slots so a VRAM BO pool can live above them: 131072
// entries = 512 MiB, and the table then fills [160 MiB, 161 MiB) exactly, where the rings start. The kext's own
// submissions (rings, fences, the clear IB) stay below 256 MiB either way; this only lifts the ceiling.
#define GART_N 131072                    // 512 MiB window
#define FB_PA 0x0ull                     // navi31: no UMA carve-out
// The GPU physical address of VRAM offset 0 on a dGPU is the BAR0 aperture base, which the kext publishes as
// phx-fb-aper-phys (0x1080000000 on the Navi 32 board). The page tables must map VA -> that + offset; with 0
// they map VA -> system RAM, so every engine write lands somewhere the CPU never looks.
static uint64_t fb_pa_override(void)
{
   const char *e = getenv("FB_PA");
   return (e && *e) ? strtoull(e, NULL, 0) : FB_PA;
}

#define SEG0(o) (0x1260u + (o))
#define SEG1(o) (0xA000u + (o))
// VRAM layout of the experiment (offsets), all inside the GART window
#define KIQ_RING 0xA100000ull            // 4 KiB ring
#define KIQ_WB   0xA101000ull            // +0 rptr report, +16 wptr poll
#define KIQ_MQD  0xA102000ull            // 2 KiB
#define KIQ_EOP  0xA103000ull            // 2 KiB
#define SCH_RING 0xA110000ull            // 32 KiB ring, as Linux
#define SCH_RING_DW 8192                  // dwords in it (32 KiB)
#define SCH_WB   0xA118000ull
#define SCH_MQD  0xA119000ull
#define SCH_EOP  0xA11A000ull
#define TESTBUF  0xA120000ull
#define SAFE_PAGE 0xA3FF000ull           // navi31: default / fault page, inside our work area

// navi31: sudo drops the environment, so every MESBOOT_* switch may also be given as a command-line word.
static int g_argc; static char **g_argv;
static int env_on(const char *n)
{
   const char *v = getenv(n);
   if (v && *v && *v != '0') return 1;
   for (int i = 2; i < g_argc; i++) if (!strcmp(g_argv[i], n)) return 1;
   return 0;
}
static int use_mc;
// The GART VA base. On this die GCVM_CONTEXT0_END only keeps 24 bits of the page number, so the window is
// capped at 16 GiB and a ring at 0x7fff... (140 TB) can never be translated: GART_VA_BASE picks a base inside
// the window instead.
static uint64_t gGartVa = GART_VA;
static uint64_t gLowVa;   // 1 = plain VRAM offsets (a flat client table covers them)
#define VA(off) ((use_mc ? VRAM_MC : (gLowVa ? 0ull : gGartVa)) + (off))
#define MC(off) (VRAM_MC + (off))

// A real 4-level GCVM page table in VRAM: PML4, PDP, PD, then one leaf table per 2 MiB of the window. The
// flat table this function used to build was indexed by VA>>12 alone, which is not how the GC walks a
// table (translate_further is off, so it is a plain 4-level walk), so nothing the CP fetched through it
// could translate. The kext does the same thing in PhxVmTables.h.
#define PT_AREA 0x5000000ull          // 2 MiB of VRAM, below the 256 MiB BAR0 window so the CPU can fill it
#define PT_AREA_SIZE 0x200000ull
static void build_pt4(uint64_t fbpa, uint64_t vabase, uint64_t span)
{
   for (uint64_t o = 0; o < PT_AREA_SIZE; o += 4) vw(PT_AREA + o, 0);
   const uint64_t pml4 = PT_AREA, pdpt = PT_AREA + 0x1000, pdir = PT_AREA + 0x2000, leaf = PT_AREA + 0x3000;
   const uint64_t i4 = (vabase >> 39) & 0x1ff, i3 = (vabase >> 30) & 0x1ff, i2 = (vabase >> 21) & 0x1ff;
   const uint64_t npd = (span + (2ull << 20) - 1) >> 21;
   #define WR64(off, v) do { vw((off), (uint32_t)(v)); vw((off) + 4, (uint32_t)((v) >> 32)); } while (0)
   WR64(pml4 + i4 * 8, (fbpa + pdpt) | 1 | (1ull << 54));       // local (VRAM-resident) next level, as the kext's PDEs
   WR64(pdpt + i3 * 8, (fbpa + pdir) | 1 | (1ull << 54));
   for (uint64_t k = 0; k < npd; k++) {
      const uint64_t lt = leaf + k * 0x1000;
      WR64(pdir + (i2 + k) * 8, (fbpa + lt) | 1 | (1ull << 54));
      for (uint64_t j = 0; j < 512; j++)
         WR64(lt + j * 8, (fbpa + (k << 21) + (j << 12)) | 0x71);   // VALID | EXEC | READ | WRITE
   }
   #undef WR64
   printf("gart: 4-level table at VRAM 0x%llx: PML4[0x%llx] PDP[0x%llx] PD[0x%llx..], %llu leaves, window 0x%llx+0x%llx\n",
          (unsigned long long)PT_AREA, (unsigned long long)i4, (unsigned long long)i3,
          (unsigned long long)i2, (unsigned long long)npd, (unsigned long long)vabase, (unsigned long long)span);
}
// The 4-level table built in SYSTEM memory, the way Linux keeps page tables, with the SYSTEM bit on every
// level entry that points at a system-resident table. Used to A/B against the VRAM-resident table, which
// does not translate at all (ibtest with HIER_PT: nothing fetched, no fault).
static void build_pt4_sys(uint64_t fbpa, uint64_t vabase, uint64_t span, uint64_t *outPhys)
{
   uint64_t base = 0, len = 0, d0 = 0, d1 = 0;
   const uint64_t need = 0x4000ull + ((span + (2ull << 20) - 1) >> 21) * 0x1000ull;
   if (sm(1, need, 0, &base, &len) || !base) { printf("hier_sys: system table allocation failed\n"); return; }
   for (uint64_t o = 0; o < len; o += 4) sm(3, o, 0, &d0, &d1);
   const uint64_t pml4 = base, pdpt = base + 0x1000, pdir = base + 0x2000, leaf = base + 0x3000;
   const uint64_t i4 = (vabase >> 39) & 0x1ff, i3 = (vabase >> 30) & 0x1ff, i2 = (vabase >> 21) & 0x1ff;
   const uint64_t npd = (span + (2ull << 20) - 1) >> 21;
   #define SW64(off, v) do { sm(3, (off), (uint32_t)(v), &d0, &d1); sm(3, (off) + 4, (uint32_t)((v) >> 32), &d0, &d1); } while (0)
   SW64(pml4 - base + i4 * 8, pdpt | 7 | (1ull << 54));      // SYSTEM tables: bit1 set, as Linux's page-table BOs
   SW64(pdpt - base + i3 * 8, pdir | 7 | (1ull << 54));
   for (uint64_t k = 0; k < npd; k++) {
      const uint64_t lt = leaf + k * 0x1000;
      SW64(pdir - base + (i2 + k) * 8, lt | 7 | (1ull << 54));
      for (uint64_t j = 0; j < 512; j++)
         SW64(lt - base + j * 8, (fbpa + (k << 21) + (j << 12)) | 0x71);
   }
   #undef SW64
   *outPhys = base;
   printf("hier_sys: 4-level table in SYSTEM memory at 0x%llx (%llu bytes), %llu leaves\n",
          (unsigned long long)base, (unsigned long long)len, (unsigned long long)npd);
}
// The GCVM L2 (the page-table walker's cache) is never programmed by bringup: GCVM_L2_CNTL reads 0,
// where Linux sets ENABLE_L2_CACHE, ENABLE_DEFAULT_PAGE_OUT_TO_SYSTEM_MEMORY and
// CONTEXT1_IDENTITY_ACCESS_MODE. mmhub_v3_0_2_init sets all of these before any context is enabled.
// With the L2 unconfigured the walker mistranslates depth-3 (client) tables while the flat depth-0
// GART path still works, which is exactly the split observed on the card.
static void gcvm_l2_init(void)
{
   // Off by default: writing these GC-alias offsets does not read back what was written and it breaks the
   // working flat depth-0 configuration, so the offsets are not the GCVM L2 block. Kept behind L2INIT=1 as
   // the probe it is; the real offsets need to be found by scanning for a register that holds a value we
   // already know (context 0's page-table base, 0x0a000001).
   if (!env_on("L2INIT")) return;
   const uint32_t c = (uint32_t)(rd(SEG0(0x15bc) + 0x10) & 0xF8401FFFu);
   const uint32_t cntl = c | (1u << 0) | (1u << 11) | (1u << 19);   // L2 cache, default page out, identity mode 1
   wr(SEG0(0x15bc) + 0x10, cntl);
   wr(SEG0(0x15bd) + 0x10, 0x3u);                                   // invalidate L1 TLBs + L2 cache (self-clearing)
   uint32_t c3 = 0x80100007u;
   c3 = (c3 & ~0x3Fu) | 9u;                                          // BANK_SELECT 9 (translate_further off)
   c3 = (c3 & ~(0xFu << 15)) | (6u << 15);                            // L2_CACHE_BIGK_FRAGMENT_SIZE 6
   wr(SEG0(0x15be) + 0x10, c3);
   const uint32_t c4 = rd(SEG0(0x15d4) + 0x10) & ~0xC0u;             // VMC tap: physical requests, as Linux
   wr(SEG0(0x15d4) + 0x10, c4);
   wr(SEG0(0x15da) + 0x10, rd(SEG0(0x15da) + 0x10) & ~0x1Fu);        // SMALLK_FRAGMENT_SIZE 0
   printf("gcvm: L2_CNTL=0x%08x CNTL3=0x%08x CNTL4=0x%08x\n", rd(SEG0(0x15bc) + 0x10), rd(SEG0(0x15be) + 0x10), rd(SEG0(0x15d4) + 0x10));
}
// Find where a known value lives: scan a dword-index range and print every index holding it. Used to
// locate the real GCVM_CONTEXT0_PAGE_TABLE_BASE for this ASIC by searching for the page-table address the
// port just programmed, instead of trusting a segment base copied from another generation.
// Only the blocks this port already reads are safe: a blind scan over 0x1000..0x40000 hung the machine
// (unimplemented registers read as all-ones and the transaction never retires). Refuse anything else.
static int scan_ok(uint32_t lo, uint32_t hi)
{
   static const struct { uint32_t lo, hi; } kSafe[] = {
      {0x1200, 0x2C00},   // GC segment 0 (contexts, apertures, the port's own registers)
      {0x9000, 0xC200},   // GC segment 1 (scratch, EOP, PQ registers)
      {0x1A000, 0x1C000}, // MMHUB (MMVM block, base 0x1A080)
   };
   for (unsigned i = 0; i < sizeof(kSafe) / sizeof(kSafe[0]); i++)
      if (lo >= kSafe[i].lo && hi <= kSafe[i].hi) return 1;
   printf("scan refused: 0x%x..0x%x is not inside a known-safe block\n", lo, hi);
   return 0;
}
static void reg_scan(uint32_t lo, uint32_t hi, uint32_t want, uint32_t mask)
{
   if (!scan_ok(lo, hi)) return;
   for (uint32_t i = lo; i <= hi; i++) {
      const uint32_t v = rd(i);
      if ((v & mask) == want) printf("  0x%05x = 0x%08x\n", i, v);
   }
   printf("scan 0x%x..0x%x for 0x%08x/0x%08x done\n", lo, hi, want, mask);
}
static void gart(void)
{
   gcvm_l2_init();
   if (getenv("GART_VA_BASE") && *getenv("GART_VA_BASE"))
      gGartVa = strtoull(getenv("GART_VA_BASE"), NULL, 0);
   const uint64_t fbpa = fb_pa_override();
   // GC 11.0.3 moves the GCVM context/aperture block by +0x10 dwords (confirmed against a live Linux read of this
   // card). VM_SHIFT=0 restores the 11.0.0 layout.
   const uint32_t vsh = (getenv("VM_SHIFT") && *getenv("VM_SHIFT")) ? (uint32_t)strtoul(getenv("VM_SHIFT"), NULL, 0) : 0x10u;
   // The GCVM page table can live in system memory instead of VRAM (Linux keeps it there on this card: the live
   // reference shows context 0's page table at system physical 0x300000). PT_SYS=1 allocates a 1 MiB system
   // buffer through the kext and builds the same 256 MiB flat table there.
   const int pt_sys = getenv("PT_SYS") && *getenv("PT_SYS") == '1';
   uint64_t ptphys = fbpa + GART_PT;
   if (pt_sys) {
      uint64_t syspt = 0, len = 0, d0 = 0, d1 = 0;
      if (sm(1, 1u << 20, 0, &syspt, &len) == 0 && syspt) {
         for (uint64_t i = 0; i < GART_N; i++) {
            uint64_t pte = (fbpa + i * 4096) | 0x71;
            sm(3, i * 8, (uint32_t)pte, &d0, &d1);
            sm(3, i * 8 + 4, (uint32_t)(pte >> 32), &d0, &d1);
         }
         ptphys = syspt;
         printf("gart: page table in system memory at 0x%llx (%llu bytes)\n", (unsigned long long)ptphys, (unsigned long long)len);
      } else {
         printf("gart: system page table allocation failed; staying in VRAM\n");
      }
   }
   const uint64_t winspan_pt = env_on("WINDOW_NARROW") ? (128ull << 20) : (512ull << 20);
   if (!pt_sys) {
      if (env_on("HIER_SYS")) {  // 4-level table in system memory (Linux's layout, SYSTEM bits on the levels)
         build_pt4_sys(fbpa, gGartVa, winspan_pt, &ptphys);
      } else if (env_on("HIER_PT")) {   // 4-level walk, for A/B against context 0's flat table
         build_pt4(fbpa, gGartVa, winspan_pt);
         ptphys = fbpa + PT_AREA;
         // PT_ADD starts the walk one level down, so the same table can test depth 2 (PDP) and depth 1 (PD).
         if (getenv("PT_ADD") && *getenv("PT_ADD")) ptphys += strtoull(getenv("PT_ADD"), NULL, 0);
      } else {
         // Context 0's table is the flat GART PTE array indexed by (VA - window base) >> 12, matching
         // ENABLE_CONTEXT | PAGE_TABLE_DEPTH = 0 (gfxhub_v3_0_enable_system_domain).
         for (uint64_t i = 0; i < GART_N; i++) {
            uint64_t pte = (fbpa + i * 4096) | ((getenv("PTE_SYS") && *getenv("PTE_SYS")) ? 0x77 : 0x71);
            vw(GART_PT + i * 8, (uint32_t)pte);
            vw(GART_PT + i * 8 + 4, (uint32_t)(pte >> 32));
         }
         ptphys = fbpa + GART_PT;
      }
   }
   // These three are the *unshifted* context-0 registers (SEG0(0x1713) = 0x2973, SEG0(0x1733) = 0x2993,
   // SEG0(0x16f3) = 0x2953). On GC 11.0.3 the context block sits +0x10 higher, so 0x2973 and 0x2993 are
   // vmid 8's page-table base and window start - and vmid 8 is the first VMID the accelerator lends to
   // client contexts. Writing the GART window there clobbered the client VM, which is why the kext's own
   // submissions wedged while the port's VMID-0 queues kept working. The shifted writes below (0x2963,
   // 0x2983, 0x29a3) are the ones that actually apply on this die.
   if (!vsh) {
      wr(SEG0(0x1713), (uint32_t)(gGartVa >> 12)); wr(SEG0(0x1714), (uint32_t)(gGartVa >> 44));
      wr(SEG0(0x1733), (uint32_t)((gGartVa + (uint64_t)GART_N * 4096 - 1) >> 12)); wr(SEG0(0x1734), (uint32_t)((gGartVa + (uint64_t)GART_N * 4096 - 1) >> 44));
      wr(SEG0(0x16f3), (uint32_t)(ptphys | 1)); wr(SEG0(0x16f4), (uint32_t)(ptphys >> 32));
   }
   // system aperture / AGP / defaults exactly as the Linux trace (gfxhub_v3_0_init_system_aperture_regs)
   wr(SEG0(0x167c), 0); wr(SEG0(0x167b), 0x00ffffff); wr(SEG0(0x167a), 0);      // AGP_BASE/BOT/TOP (AGP off)
   // SYSTEM_APERTURE LOW/HIGH = the VRAM MC range, as gfxhub_v3_0_init_system_aperture_regs (vram_start >> 18,
   // vram_end >> 18), derived from GCMC_VM_FB_LOCATION_BASE/TOP (16 MiB units, set before we run) rather than
   // a fixed constant. The GC 11.0.3 die moves this whole block by +0x10 dwords (measured: 0x28ED/0x28EE hold
   // the live aperture, 0x28DD/0x28DE read 0), so both the read and the write carry vsh.
   {
      const uint32_t fbBase = rd(SEG0(0x1678) + vsh) & 0x00FFFFFF, fbTop = rd(SEG0(0x1679) + vsh) & 0x00FFFFFF;
      uint32_t apLo = 0x00200000, apHi = 0x0020ffff;   // this board's observed value
      if (fbBase == 0x8000 && fbTop > fbBase && fbTop - fbBase < 0x800) { apLo = fbBase << 6; apHi = (fbTop << 6) - 1; }
      else fprintf(stderr, "mesboot: GCMC_VM_FB_LOCATION base 0x%x top 0x%x not plausible: keeping 0x00200000-0x0020ffff\n", fbBase, fbTop);
      wr(SEG0(0x167d) + vsh, apLo); wr(SEG0(0x167e) + vsh, apHi);
      printf("system aperture 0x%08x-0x%08x (FB_LOCATION 0x%x-0x%x)\n", apLo, apHi, fbBase, fbTop);
   }
   wr(SEG0(0x15a8), (uint32_t)(SAFE_PAGE >> 12));   /* navi31: our own VRAM page */ wr(SEG0(0x15a9), 0);                           // SYSTEM_APERTURE_DEFAULT_ADDR LSB/MSB
   // GC 11.0.3 moves the GCVM_L2 block by +0x04 dwords against GC 11.0.0 (the kext's gGcL2Shift). The live
   // Linux block reads GCVM_L2_CNTL 0x00080e01 at 0x2820, CNTL2 3 at 0x2821, CNTL3 0x80130009 at 0x2822,
   // FAULT_CNTL 0x3ffffffc at 0x2828, FAULT_CNTL2 0x00060000 at 0x2829 and CNTL5 0x00003fe0 at 0x283e -
   // i.e. exactly these values, four dwords above where SEG0() puts them. Without the shift the whole L2/TLB
   // setup landed on unused registers, so translation and invalidation never behaved as configured.
   const uint32_t l2sh = (getenv("L2_SHIFT") && *getenv("L2_SHIFT")) ? (uint32_t)strtoul(getenv("L2_SHIFT"), NULL, 0) : 4u;
   wr(SEG0(0x15cb) + l2sh, (uint32_t)(SAFE_PAGE >> 12)); wr(SEG0(0x15cc) + l2sh, 0);   // L2 protection fault default addr
   wr(SEG0(0x15c5) + l2sh, 0x00060000);                                    // L2_PROTECTION_FAULT_CNTL2
   wr(SEG0(0x15c4) + l2sh, env_on("FAULT_DEFAULT") ? 0x3ffffffc : 0x00000002);   // L2_PROTECTION_FAULT_CNTL
   wr(SEG0(0x15ce) + l2sh, 0xffffffff); wr(SEG0(0x15cf) + l2sh, 0x0000000f);   // identity aperture LOW/HIGH
   wr(SEG0(0x167f) + vsh, 0x1859);           // GCMC_VM_MX_L1_TLB_CNTL (also in the shifted block)
   wr(SEG0(0x15bc) + l2sh, 0x00080e01);      // GCVM_L2_CNTL
   wr(SEG0(0x15bd) + l2sh, 0x00000003);      // GCVM_L2_CNTL2 (invalidate all L1 TLBs + L2)
   wr(SEG0(0x15be) + l2sh, 0x80130009);      // GCVM_L2_CNTL3
   wr(SEG0(0x15d4) + l2sh, 0x00000001);      // GCVM_L2_CNTL4
   wr(SEG0(0x15da) + l2sh, 0x00003fe0);      // GCVM_L2_CNTL5

   // GCVM_CONTEXT0_START/END hold 4 KiB PAGE NUMBERS. On the working Linux driver this card's vmid 0 reads
   //   START_LO/HI = 0xfff00000/0x00000007   -> 0x7fff00000 pages = 0x7fff00000000
   //   END_LO/HI   = 0xfff1ffff/0x00000007   -> 0x7fff1ffff pages = 0x7fff1ffff000
   // i.e. a 512 MiB window at GART_VA - exactly where the MES KIQ ring lives (0x7fff00800000). Writing byte
   // values here (or a 16 GiB window starting at 0) left the ring's 0x7fff... VA outside the window, so the
   // CP refused to translate it and every queue stayed idle. WINDOW_NARROW limits it to 128 MiB instead.
   const uint64_t win_lo = gGartVa;
   const uint64_t win_hi = gGartVa + winspan_pt - 1;
   // context 0: page table + the GART window it has to translate
   // Context 0 is amdgpu's "system domain": gfxhub_v3_0_enable_system_domain sets ENABLE_CONTEXT and
   // PAGE_TABLE_DEPTH = 0, and the table behind it is the flat GART PTE array. Asking for depth 3 here
   // (0x01fffe07, what vmids 1..15 use) makes the CP walk a flat table as if it were 4 levels, which is
   // exactly the translation fault the KIQ was taking on its own ring (GCVM_L2_PROTECTION_FAULT_ADDR
   // read back as the ring VA). Linux's live register reads 0x01fffe01.
   const uint32_t ctx0cntl = (getenv("CTX0_CNTL") && *getenv("CTX0_CNTL"))
                              ? (uint32_t)strtoul(getenv("CTX0_CNTL"), NULL, 0) : 0x01fffe01u;
   wr(SEG0(0x1688) + vsh, ctx0cntl);
   wr(0x2953 + vsh, (uint32_t)(ptphys | 1)); wr(0x2954 + vsh, (uint32_t)(ptphys >> 32));
   wr(0x2973 + vsh, (uint32_t)(win_lo >> 12)); wr(0x2974 + vsh, (uint32_t)((win_lo >> 12) >> 32));
   wr(0x2993 + vsh, (uint32_t)(win_hi >> 12)); wr(0x2994 + vsh, (uint32_t)((win_hi >> 12) >> 32));
   if (env_on("MMVM")) {   // the CP fetches through the MMHUB instance, so ctx0 there needs the same table
      wr(0x1A740, ctx0cntl);
      wr(0x1A7AB, (uint32_t)(ptphys | 1)); wr(0x1A7AC, (uint32_t)(ptphys >> 32));
      wr(0x1A7CB, (uint32_t)(win_lo >> 12)); wr(0x1A7CC, (uint32_t)((win_lo >> 12) >> 32));
      wr(0x1A7EB, (uint32_t)(win_hi >> 12)); wr(0x1A7EC, (uint32_t)((win_hi >> 12) >> 32));
      printf("gart: MMHUB ctx0 programmed (PT=0x%x)\n", rd(0x1A7AB));
   }
   // gfxhub_v3_0_program_invalidation: address range of every invalidation engine = everything (was 0 => flushes did nothing)
   // The per-engine invalidation range registers also moved: Linux has them at 0x293f+2e (0xffffffff / 0x1f).
   const uint32_t invsh = (getenv("INV_SHIFT") && *getenv("INV_SHIFT")) ? (uint32_t)strtoul(getenv("INV_SHIFT"), NULL, 0) : 0x10u;
   for (int e = 0; e < 18; e++) { wr(0x292f + invsh + 2 * e, 0xffffffff); wr(0x2930 + invsh + 2 * e, 0x0000001f); }
   // contexts 1..15 (VMIDs) as the Linux driver enables them
   for (int c = 1; c < 16; c++) {
      wr(0x28e8 + vsh + c, 0x01fffe07);
      wr(0x2973 + vsh + 2 * c, 0); wr(0x2974 + vsh + 2 * c, 0);
      wr(0x2993 + vsh + 2 * c, 0xffffffff); wr(0x2994 + vsh + 2 * c, 0x0000000f);
   }
   wr(SEG0(0x16bd + 17) + vsh, 1);            // GCVM_INVALIDATE_ENG17_REQ, now at the shifted address too
   // SH_MEM_CONFIG / SH_MEM_BASES per VMID as in gfx_v11_0_constants_init (trace 1.559162..)
   for (int v = 0; v < 16; v++) {
      wr(SEG1(0x900), v << 4);
      wr(0xa9e4, 0x0000c00c);
      if (v) wr(0xa9e3, v < 8 ? 0x20001000 : 0x00010002);
   }
   wr(SEG1(0x900), 0);
   wr(SEG0(0x1e1f), rd(SEG0(0x1e1f)) | 0x8000);   // CP_DEBUG.CPG_UTCL1_ERROR_HALT_DISABLE (linux sets it with the gfxhub)
   wr(SEG0(0x16ab + 17), 0x00F80001);          // GCVM_INVALIDATE_ENG17_REQ (the engine amdgpu uses for gmc flushes)
   // CP_PQ_STATUS.DOORBELL_ENABLE (bit 1). The working Linux driver has this set (0x00000002 at 0x30b8);
   // without it no doorbell is honoured, which is consistent with every queue staying idle while the
   // register state was otherwise identical. (0x3059 is *not* this register - that is where the port was
   // looking before, and it reads 8.)
   wr(0x30b8, rd(0x30b8) | 0x2);
   printf("gart: CP_PQ_STATUS=0x%x\n", rd(0x30b8));
   usleep(2000);
   printf("gart: ack17=0x%x ctx0=0x%x\n", rd(SEG0(0x16bd + 17)), rd(SEG0(0x1688)));
}
static void grbm_select(uint32_t me, uint32_t pipe, uint32_t queue, uint32_t vmid)
{
   wr(SEG1(0x900), pipe | (me << 2) | (vmid << 4) | (queue << 8));
}
static void mes_start(void)
{
   // uc start from fw headers: lo=0xF0005000? taken from the header words (same for sched + kiq)
   const uint64_t uc = 0xFFFFFFFFF0005000ull >> 2;
   wr(SEG1(0x2807), rd(SEG1(0x2807)) | (1u << 16) | (1u << 17));   // PIPE0/1 RESET (navi31: RMW as Linux, 0x40030000)
   for (int p = 0; p < 2; p++) {
      grbm_select(3, p, 0, 0);
      wr(SEG1(0x2800), (uint32_t)uc);
      wr(SEG1(0x289d), (uint32_t)(uc >> 32));
   }
   grbm_select(0, 0, 0, 0);
   wr(SEG1(0x2807), (1u << 26) | (1u << 27));                // PIPE0/1 ACTIVE
   usleep(2000);
   printf("mes: CP_MES_CNTL=0x%x\n", rd(SEG1(0x2807)));
}

// The MES block as the working Linux driver leaves it on this card (dumped 2026-11-23, MES block
// 0x0c7f0..0x0c8a0 read per pipe). Only the pipe-independent, non-zero registers are listed: they are the
// state amdgpu's gfx_v11_0_mes_init() sets up, and on the macOS side this block is essentially empty, which
// is why the MES never schedules a queue and no engine executes anything.
// CPC_MES_PRGRM_CNTR_START is 0xfc001400 on Linux against the 0xc0001400 mes_start() computes: the MES was
// being started at the wrong instruction address.
static void mes_full_init(void)
{

   static const struct { uint32_t off, val; } common[] = {
      {0x280e, 0x00001ffe}, {0x282c, 0x00000100}, {0x282d, 0x80000000},
      {0x282e, 0xcafebabe}, {0x282f, 0xdeadbeef},   // amdgpu's MES scratch test pattern
      {0x2830, 0x00000001}, {0x2831, 0x80000000}, {0x2832, 0x00000001},
      {0x2837, 0x00000002}, {0x283a, 0x00000008}, {0x283b, 0x00000008},
      {0x2842, 0xfc000000}, {0x2844, 0x7fffffff}, {0x2848, 0xffffffff},
      {0x2855, 0x0c000000}, {0x2856, 0x40000000},
      {0x2882, 0x00000800}, {0x2883, 0xf0100000}, {0x2884, 0x0000ffff},
      {0x2885, 0x00070000}, {0x2887, 0x00000007}, {0x2888, 0xf0000000},
      {0x2889, 0x0000ffff}, {0x288a, 0x00030000}, {0x288c, 0x00000007},
      {0x288d, 0x00000003}, {0x289b, 0x00000003},
   };
   for (size_t i = 0; i < sizeof common / sizeof common[0]; i++)
      wr(SEG1(common[i].off), common[i].val);
   // mes_v11_0_enable's order: raise the pipe RESET bits, program each pipe's program counter, then write
   // CP_MES_CNTL with only the ACTIVE bits. Writing ACTIVE straight away (what mes_start did) never resets
   // the pipes, so a firmware that was already running keeps its old state. Which bits are RESET is measured
   // here rather than assumed: both candidate pairs are raised and the read-back is printed.
   wr(SEG1(0x2807), 0x03030000u);
   printf("mesfull: reset probe CP_MES_CNTL=0x%x (bits 16/17 vs 24/25)\n", rd(SEG1(0x2807)));
   // per-pipe: program counter start, the second start, and their high words
   for (int p = 0; p < 2; p++) {
      grbm_select(3, p, 0, 0);
      wr(SEG1(0x2800), 0xfc001400u); wr(SEG1(0x2801), 0xfc000800u); wr(SEG1(0x2802), 0x3fffffffu);
      wr(SEG1(0x289d), 0x3fffffffu); wr(SEG1(0x289e), 0x3fffffffu);
   }
   grbm_select(0, 0, 0, 0);
   wr(SEG1(0x2808), 0x08081020u);
   wr(SEG1(0x2809), 2); wr(SEG1(0x280a), 2);
   wr(SEG1(0x2807), (1u << 26) | (1u << 27));   // PIPE0/1 ACTIVE only, as mes_v11_0_enable does
   usleep(2000);
   // mes_v11_0_load_microcode primes the instruction cache after pointing the pipes at their ucode:
   // CP_MES_IC_OP_CNTL invalidate (bit 0), then prime (bit 4), then wait for ICACHE_PRIMED (bit 5).
   // The port never ran this step, which leaves the MES executing from a cold instruction cache.
   wr(SEG1(0x2820), 1u);
   usleep(1000);
   wr(SEG1(0x2820), 1u << 4);
   for (int i = 0; i < 100 && !(rd(SEG1(0x2820)) & (1u << 5)); i++) usleep(1000);
   printf("mesfull: IC_OP_CNTL=0x%08x primed=%d\n", rd(SEG1(0x2820)), (rd(SEG1(0x2820)) >> 5) & 1);
   usleep(2000);
   printf("mesfull: MES_CNTL=0x%x PC_START(lo/hi)=0x%x/0x%x scratch=0x%x/0x%x\n",
          rd(SEG1(0x2807)), rd(SEG1(0x2800)), rd(SEG1(0x289d)), rd(SEG1(0x282e)), rd(SEG1(0x282f)));
}
static uint64_t gMqdEop;   // EOP buffer the last mqd_build described
static uint32_t gMqdPs;    // its cp_hqd_persistent_state
static uint32_t gMqdVmid;   // VMID the queue runs at (the accelerator lends VMIDs 8..15 to clients)
static void mqd_build(uint64_t mqd, uint64_t ring, uint64_t wb, uint64_t eop, int doorbell, uint32_t db_index)
{
   for (int i = 0; i < 512; i++) vw(mqd + i * 4, 0);
   #define M(i, v) vw(mqd + (i) * 4, (uint32_t)(v))
   M(0, 0xC0310800);
   M(11, 1);                                                  // compute_pipelinestat_enable
   M(23, 0xffffffff); M(24, 0xffffffff); M(26, 0xffffffff); M(27, 0xffffffff);
   M(32, 7);                                                  // compute_misc_reserved
   // The MQD base is an MC address, not a VA: Linux's live MES KIQ MQD reads 0xfeb2c000/0x00000083, i.e.
   // 0x83feb2c000 (VRAM at MC base + offset), while its PQ_BASE two words later is the VA 0x7fff00800000.
   // mesboot used the VA here, which the CP cannot resolve for an MQD fetch. MQD_VA=1 restores that.
   const uint64_t mqdBase = env_on("MQD_VA") ? VA(mqd) : MC(mqd);
   M(128, mqdBase & 0xfffffffc); M(129, mqdBase >> 32);
   M(130, (getenv("MQD_ACTIVE") && *getenv("MQD_ACTIVE")) ? (uint32_t)strtoul(getenv("MQD_ACTIVE"), NULL, 0) : 1u);   // hqd_active
   gMqdVmid = (getenv("MQD_VMID") && *getenv("MQD_VMID")) ? (uint32_t)strtoul(getenv("MQD_VMID"), NULL, 0) : 0u;
   M(131, gMqdVmid);                                          // hqd_vmid
   // Linux's live MES KIQ MQD (dumped from this card's VRAM) has persistent_state = 0x0bec5500 and
   // hqd_active = 0, against the 0x0be05501 / 1 the port builds. MQD_PS / MQD_ACTIVE override them so the
   // two can be A/B'd without rebuilding.
   M(132, (getenv("MQD_PS") && *getenv("MQD_PS")) ? (uint32_t)strtoul(getenv("MQD_PS"), NULL, 0) : 0x0bec5500u);
   M(135, 0);
   M(136, VA(ring) >> 8); M(137, VA(ring) >> 40);
   M(138, 0);
   M(139, VA(wb) & 0xfffffffc); M(140, (VA(wb) >> 32) & 0xffff);
   M(141, VA(wb + 16) & 0xfffffff8); M(142, (VA(wb + 16) >> 32) & 0xffff);
   uint32_t db = 0;
   if (doorbell) db = (db_index << 2) | (1u << 30);
   M(143, db);
   // PQ_CONTROL: default 0x00308509; QUEUE_SIZE=log2(4096/4)-1=9, RPTR_BLOCK_SIZE=(log2(4096/4)-1)<<8 (=9<<8), UNORD=1,PRIV=1,KMD=1,NO_UPDATE_RPTR=1
   uint32_t pq = 0x00308509;
   pq = (pq & ~0xffu) | 9u;
   pq = (pq & ~(0x3fu << 8)) | (9u << 8);
   pq |= (1u << 28) | (1u << 30) | (1u << 31) | (1u << 27);
   pq &= ~(1u << 29);
   M(145, pq);
   M(149, 0x00300000); M(150, 0);
   gMqdEop = eop;
   gMqdPs = (getenv("MQD_PS") && *getenv("MQD_PS")) ? (uint32_t)strtoul(getenv("MQD_PS"), NULL, 0) : 0x0bec5500u;
   M(162, 0x00000100);                                        // cp_mqd_control (VMID 0)
   M(165, VA(eop) >> 8); M(166, VA(eop) >> 40);
   M(167, 8);                                                 // EOP_SIZE = log2(2048/4)-1
   M(182, 0); M(183, 0);
   #undef M
}
// Reprogramming a running HQD wedges the CP: the second ibtest in a boot consumes nothing while the first
// executes. Stop the queue first (dequeue request, then poll ACTIVE clear, then force it) so a queue can be
// re-armed in place. QSTOP=0 disables it for A/B.
static void hqd_stop(uint32_t me, uint32_t pipe)
{
   if (getenv("QSTOP") && *getenv("QSTOP") == '0') return;
   grbm_select(me, pipe, 0, 0);
   if (!(rd(SEG0(0x1fab)) & 1)) { grbm_select(0, 0, 0, 0); return; }
   wr(SEG0(0x1fc1), 2);                                   // CP_HQD_DEQUEUE_REQUEST: unmap
   for (int i = 0; i < 100; i++) {
      if (!(rd(SEG0(0x1fab)) & 1)) break;
      usleep(1000);
   }
   if (rd(SEG0(0x1fab)) & 1) {
      wr(SEG0(0x1fab), 0);                                // force it down
      for (int i = 0; i < 100 && (rd(SEG0(0x1fab)) & 1); i++) usleep(1000);
   }
   wr(SEG0(0x1fc1), 0);
   printf("hqd_stop: me%u/pipe%u active now %u\n", me, pipe, rd(SEG0(0x1fab)) & 1);
   grbm_select(0, 0, 0, 0);
}
static void hqd_program(uint32_t me, uint32_t pipe, uint64_t mqd, uint64_t ring, uint64_t wb, uint32_t pq, uint32_t db)
{
   hqd_stop(me, pipe);
   grbm_select(me, pipe, 0, 0);
   wr(SEG0(0x1fab), 0);                                       // deactivate a previously programmed queue
   wr(SEG0(0x1fb3), 0); wr(SEG0(0x323f), 0); wr(SEG0(0x3240), 0);
   wr(SEG0(0x1fac), gMqdVmid);                                 // HQD_VMID: must match the VMID the submitter uses
   wr(SEG0(0x1fb8), 0);                                       // doorbell off while programming
   const uint64_t mqdmc = env_on("MQD_VA") ? VA(mqd) : MC(mqd);
   wr(SEG0(0x1fa9), (uint32_t)(mqdmc & 0xfffffffc)); wr(SEG0(0x1faa), (uint32_t)(mqdmc >> 32));
   wr(SEG0(0x1fcb), 0);
   wr(SEG0(0x1fb1), (uint32_t)(VA(ring) >> 8)); wr(SEG0(0x1fb2), (uint32_t)(VA(ring) >> 40));
   wr(SEG0(0x1fb4), (uint32_t)(VA(wb) & 0xfffffffc)); wr(SEG0(0x1fb5), (uint32_t)((VA(wb) >> 32) & 0xffff));
   // Linux's kiq_init_register also programs the EOP buffer and the persistent state; without them a
   // RELEASE_MEM (the fence every submission ends with) has nowhere to land and wedges the CP.
   wr(SEG0(0x1fad), gMqdPs);                                        // CP_HQD_PERSISTENT_STATE
   wr(SEG0(0x1fce), (uint32_t)(VA(gMqdEop) >> 8));                  // CP_HQD_EOP_BASE_ADDR
   wr(SEG0(0x1fcf), (uint32_t)(VA(gMqdEop) >> 40));                 // CP_HQD_EOP_BASE_ADDR_HI
   wr(SEG0(0x1fd0), 8u | (1u << 12) | (1u << 14));                  // CP_HQD_EOP_CONTROL: EOP_SIZE 8, process EOP + EOPIB
   wr(SEG0(0x1fba), pq);
   wr(SEG0(0x1fb6), (uint32_t)(VA(wb + 16) & 0xfffffff8)); wr(SEG0(0x1fb7), (uint32_t)((VA(wb + 16) >> 32) & 0xffff));
   wr(SEG0(0x1fb8), db);
   wr(SEG0(0x1fad), (0x0be05501u & ~(0xffu << 8)) | (0x55u << 8));
   wr(SEG0(0x1fab), env_on("HQD_OFF") ? 0u : 1u);                // HQD_ACTIVE (HQD_OFF=1 leaves it to the MES)
   grbm_select(0, 0, 0, 0);
}
static int use_db;
static void kiq_test(void)
{
   // RLC_CP_SCHEDULERS: KIQ = me3 pipe1 queue0
   uint32_t t = rd(SEG1(0x98a)) & 0xffffff00;
   t |= (3u << 5) | (1u << 3);
   wr(SEG1(0x98a), t); wr(SEG1(0x98a), t | 0x80);
   mqd_build(KIQ_MQD, KIQ_RING, KIQ_WB, KIQ_EOP, use_db, 0x18);
   uint32_t pq = 0x00308509; pq = (pq & ~0xffu) | 9u; pq = (pq & ~(0x3fu << 8)) | (9u << 8);
   pq |= (1u << 28) | (1u << 30) | (1u << 31) | (1u << 27); pq &= ~(1u << 29);
   hqd_program(3, 1, KIQ_MQD, KIQ_RING, KIQ_WB, pq, use_db ? ((0x18u << 2) | (1u << 30)) : 0);
   // ring: WRITE_DATA to TESTBUF (VA) magic
   for (int i = 0; i < 4; i++) vw(TESTBUF + i * 4, 0);
   vw(KIQ_RING + 0, 0xC0033700); vw(KIQ_RING + 4, 0x00100500);
   vw(KIQ_RING + 8, (uint32_t)VA(TESTBUF)); vw(KIQ_RING + 12, (uint32_t)(VA(TESTBUF) >> 32));
   vw(KIQ_RING + 16, 0x4B1D0001);
   // kick: MMIO wptr (doorbell disabled) under GRBM select me3/pipe1
   if (use_db) {
      vw(KIQ_WB + 16, 5); vw(KIQ_WB + 20, 0);
      kern_return_t kr = db(0x18 * 4, 5);
      printf("doorbell write kr=0x%x\n", kr);
   } else {
      grbm_select(3, 1, 0, 0);
      wr(SEG0(0x323f), 5); wr(SEG0(0x3240), 0);
   }
   usleep(100000);
   // second test: SET_UCONFIG_REG SCRATCH_REG0 = 0xDEADBEEF (MMIO side effect, no memory involved)
   wr(SEG1(0x2040), 0xCAFEDEAD);
   vw(KIQ_RING + 20, 0xC0017900); vw(KIQ_RING + 24, 0x40); vw(KIQ_RING + 28, 0xDEADBEEF);
   if (use_db) { vw(KIQ_WB + 16, 8); db(0x18 * 4, 8); }
   usleep(100000);
   printf("scratch0=0x%08x (expect deadbeef)\n", rd(SEG1(0x2040)));
   // map the SCHED pipe's queue through the KIQ: MAP_QUEUES(me=2 eng_sel=5 pipe0 queue0)
   mqd_build(SCH_MQD, SCH_RING, SCH_WB, SCH_EOP, 1, 0x16);
   for (int i = 0; i < 4; i++) { vw(SCH_WB + i * 4, 0); vw(SCH_WB + 16 + i * 4, 0); }
   vw(KIQ_RING + 32, 0xC0055E00);
   vw(KIQ_RING + 36, (2u << 18) | (5u << 26) | (1u << 29));
   vw(KIQ_RING + 40, 0x16u << 2);
   vw(KIQ_RING + 44, (uint32_t)MC(SCH_MQD)); vw(KIQ_RING + 48, (uint32_t)(MC(SCH_MQD) >> 32));   // MQD = MC
   vw(KIQ_RING + 52, (uint32_t)VA(SCH_WB + 16)); vw(KIQ_RING + 56, (uint32_t)(VA(SCH_WB + 16) >> 32));
   if (use_db) { vw(KIQ_WB + 16, 15); db(0x18 * 4, 15); }
   usleep(200000);
   grbm_select(3, 0, 0, 0);
   printf("sched hqd: active=%u pq_base=0x%x rptr=%u dbctl=0x%x\n", rd(SEG0(0x1fab)), rd(SEG0(0x1fb1)), rd(SEG0(0x1fb3)), rd(SEG0(0x1fb8)));
   grbm_select(3, 1, 0, 0);
   printf("kiq rptr after map=%u\n", rd(SEG0(0x1fb3)));
   grbm_select(3, 1, 0, 0);
   uint32_t rp = rd(SEG0(0x1fb3)), act = rd(SEG0(0x1fab)), wp = rd(SEG0(0x323f));
   grbm_select(0, 0, 0, 0);
   printf("kiq: active=%u rptr=%u wptr=%u target=0x%08x wb_rptr=0x%08x\n", act, rp, wp, vr(TESTBUF), vr(KIQ_WB));
}

static void mesdump(void)
{
   for (int p = 0; p < 2; p++) {
      grbm_select(3, p, 0, 0);
      printf("pipe%d: IC_BASE=%08x:%08x MDBASE=%08x:%08x PC_START=%08x INSTR_PNTR=%08x HDR=%08x GP0=%08x GP1=%08x MIBOUND=%08x\n", p,
             rd(SEG1(0x5851)), rd(SEG1(0x5850)), rd(SEG1(0x5855)), rd(SEG1(0x5854)), rd(SEG1(0x2800)),
             rd(SEG1(0x2813)), rd(SEG1(0x280d)), rd(SEG1(0x2843)), rd(SEG1(0x2845)), rd(SEG1(0x585b)));
   }
   grbm_select(0, 0, 0, 0);
   printf("CP_MES_CNTL=%08x RLC_CP_SCHEDULERS=%08x\n", rd(SEG1(0x2807)), rd(SEG1(0x98a)));
}

// ---- MES scheduler pipe (pipe0) driven by direct HQD programming + API ring ----
static uint32_t sch_wptr;      // in dwords
static uint64_t sch_seq;

static int skip_hqd;
// The MES reads the queue parameters from the MQD, not from the HQD registers. mqd_build still fills
// words 132/143/145 with the port's old hand-built values, so every path that builds a scheduler MQD has
// to patch them to the ones the live Linux scheduler ring uses.
static void sched_mqd_fixup(void)
{
   vw(SCH_MQD + 132 * 4, 0x0be05500u);      // cp_hqd_persistent_state
   vw(SCH_MQD + 143 * 4, 0x60000058u);      // cp_hqd_pq_doorbell_control
   vw(SCH_MQD + 145 * 4, 0xd830800cu);      // cp_hqd_pq_control: QUEUE_SIZE 0xc over the 32 KiB ring
}
static void sched_init(void)
{
   // Values taken from the live Linux scheduler ring on this card (me3/pipe0):
   //   MQD w132 = 0x0be05500, w143 doorbell control = 0x60000058, w145 PQ_CONTROL = 0xd830800c,
   //   ring 32 KiB (QUEUE_SIZE 0xc = order_base_2(32768/4) - 1).
   // The port was building PQ_CONTROL by hand (QUEUE_SIZE 0x11, i.e. a 1 MiB ring) over a 4 KiB ring, so
   // the MES was told the ring was 256x larger than it is - a good way to consume frames and never
   // complete anything.
   if (!skip_hqd) {
      mqd_build(SCH_MQD, SCH_RING, SCH_WB, SCH_EOP, 1, 0x16); sched_mqd_fixup();
   }
   for (int i = 0; i < SCH_RING_DW; i++) vw(SCH_RING + i * 4, 0);
   for (int i = 0; i < 32; i++) vw(SCH_WB + i * 4, 0);
   const uint32_t pq = 0xd830800cu;                 // Linux's scheduler PQ_CONTROL
   const uint32_t dbctl = 0x60000058u;              // Linux's scheduler doorbell control
   if (!skip_hqd) hqd_program(3, 0, SCH_MQD, SCH_RING, SCH_WB, pq, dbctl);
   sch_wptr = 0; sch_seq = 0;
   grbm_select(3, 0, 0, 0);
   printf("sched: hqd active=%u pq_base=0x%x pqctl=0x%x dbctl=0x%x\n", rd(SEG0(0x1fab)), rd(SEG0(0x1fb1)), rd(SEG0(0x1fba)), rd(SEG0(0x1fb8)));
   grbm_select(0, 0, 0, 0);
}
static int mes_submit(void *pkt, size_t size, size_t api_status_off, const char *what)
{
   uint32_t *dw = pkt;
   // The completion fence may have to be an MC address rather than a VA: the MES writes frames'
   // *_gpu_mc_ptr fields as MC (see MAP_QUEUES' MQD), and nothing proves the fence addresses are VA.
   // MES_FENCE_MC=1 switches both fence addresses to MC for an A/B run.
   // The completion fence MUST be an MC address. The MES resolves it as MC; with a VA the API still runs
   // (the firmware's PC advances, the work happens) but the completion value is written where nobody reads,
   // so every frame times out. MES_FENCE_VA=1 restores the old behaviour for A/B.
   const int fenceMc = !env_on("MES_FENCE_VA");
   uint64_t status_va = fenceMc ? MC(SCH_WB + 64) : VA(SCH_WB + 64);
   for (int i = 0; i < 4; i++) vw(SCH_WB + 64 + i * 4, 0);
   *(uint64_t *)((char *)pkt + api_status_off) = status_va;
   *(uint64_t *)((char *)pkt + api_status_off + 8) = 1;
   union MESAPI__QUERY_MES_STATUS q;
   memset(&q, 0, sizeof(q));
   q.header.type = MES_API_TYPE_SCHEDULER;
   q.header.opcode = MES_SCH_API_QUERY_SCHEDULER_STATUS;
   q.header.dwsize = API_FRAME_SIZE_IN_DWORDS;
   sch_seq++;
   q.api_status.api_completion_fence_addr = fenceMc ? MC(SCH_WB + 32) : VA(SCH_WB + 32);
   q.api_status.api_completion_fence_value = sch_seq;
   uint32_t *qd = (uint32_t *)&q;
   for (size_t i = 0; i < size / 4; i++) vw(SCH_RING + (sch_wptr + i) * 4, dw[i]);
   for (size_t i = 0; i < sizeof(q) / 4; i++) vw(SCH_RING + (sch_wptr + size / 4 + i) * 4, qd[i]);
   sch_wptr += (uint32_t)((size + sizeof(q)) / 4);
   vw(SCH_WB + 16, sch_wptr); vw(SCH_WB + 20, 0);          // wptr shadow (polled by MES) before the doorbell
   db(0x16 * 4, sch_wptr);
   for (int t = 0; t < 400; t++) {
      usleep(5000);
      if (vr(SCH_WB + 32) == (uint32_t)sch_seq) break;
   }
   grbm_select(3, 0, 0, 0);
   printf("  sched hqd: rptr=%u wptr_lo=%u dbctl=0x%08x active=%u\n", rd(SEG0(0x1fb3)), rd(SEG0(0x323f)), rd(SEG0(0x1fb8)), rd(SEG0(0x1fab)));
   grbm_select(0, 0, 0, 0);
   uint32_t f = vr(SCH_WB + 32), st = vr(SCH_WB + 64);
   printf("mes %s: fence=%u (want %llu) status=%u -> %s\n", what, f, (unsigned long long)sch_seq, st, (f == sch_seq && st) ? "OK" : "TIMEOUT");
   return !(f == sch_seq && st);
}
#define CMP_RING 0xA140000ull
#define CMP_WB   0xA141000ull
#define CMP_MQD  0xA142000ull
#define CMP_EOP  0xA143000ull
static void vm8_test(void);
static void gfx_via_mes(void);
static void compute_via_mes(void)
{
   for (int i = 0; i < 1024; i++) vw(CMP_RING + i * 4, 0);
   for (int i = 0; i < 32; i++) vw(CMP_WB + i * 4, 0);
   for (int i = 0; i < 4; i++) vw(TESTBUF + i * 4, 0);
   mqd_build(CMP_MQD, CMP_RING, CMP_WB, CMP_EOP, 1, 6);
   union MESAPI__ADD_QUEUE a;
   memset(&a, 0, sizeof(a));
   a.header.type = MES_API_TYPE_SCHEDULER;
   a.header.opcode = MES_SCH_API_ADD_QUEUE;
   a.header.dwsize = API_FRAME_SIZE_IN_DWORDS;
   a.pipe_id = 0; a.queue_id = 0;
   a.doorbell_offset = 6;
   a.mqd_addr = MC(CMP_MQD);   // ADD_QUEUE's mqd_addr is an MC address too
   a.wptr_addr = VA(CMP_WB + 16);
   a.queue_type = MES_QUEUE_TYPE_COMPUTE;
   a.map_legacy_kq = 1;
   if (mes_submit(&a, sizeof(a), offsetof(union MESAPI__ADD_QUEUE, api_status), "ADD_QUEUE(compute, map_legacy_kq)")) return;
   wr(SEG1(0x2040), 0xCAFEDEAD);
   vw(CMP_RING + 0, 0xC0017900); vw(CMP_RING + 4, 0x40); vw(CMP_RING + 8, 0xDEADBEEF);
   vw(CMP_RING + 12, 0xC0033700); vw(CMP_RING + 16, 0x00100500);
   vw(CMP_RING + 20, (uint32_t)VA(TESTBUF)); vw(CMP_RING + 24, (uint32_t)(VA(TESTBUF) >> 32)); vw(CMP_RING + 28, 0xFEEDF00D);
   vw(CMP_WB + 16, 8); vw(CMP_WB + 20, 0);
   db(6 * 4, 8);
   for (int i = 0; i < 50; i++) { usleep(10000); if (rd(SEG1(0x2040)) == 0xDEADBEEF && vr(TESTBUF) == 0xFEEDF00D) break; }
   grbm_select(1, 0, 0, 0);
   printf("compute: scratch0=0x%08x (want deadbeef) testbuf=0x%08x (want feedf00d) hqd_active=%u rptr=%u\n", rd(SEG1(0x2040)), vr(TESTBUF), rd(SEG0(0x1fab)), rd(SEG0(0x1fb3)));
   grbm_select(0, 0, 0, 0);
   if (env_on("ADOPT")) {   // hand the working compute queue to the kext: SUBMIT from RADV goes here from now on
      uint64_t in[6] = {CMP_RING, 1024, CMP_WB, 6 * 4, 8, 0xA150000};
      kern_return_t kr = IOConnectCallScalarMethod(conn, SEL_DBG_ADOPTQ, in, 6, NULL, NULL);
      printf("adopt queue -> kr 0x%x\n", kr);
      return;
   }
   if (env_on("VM8")) { vm8_test(); return; }
   if (getenv("MESBOOT_VMT") && *getenv("MESBOOT_VMT")) {
      // GPU write into SYSTEM memory through a context0 (GART) mapping, PTE SYSTEM|SNOOPED
      uint64_t phys = 0, len = 0, val = 0;
      kern_return_t kr = sm(1, 1 << 20, 0, &phys, &len);
      printf("sysmem alloc kr=0x%x phys=0x%llx len=0x%llx\n", kr, (unsigned long long)phys, (unsigned long long)len);
      if (kr) return;
      if (env_on("AGPTEST")) {   // can the GPU write system memory at all? identity AGP aperture MC==physical, no page tables
         wr(SEG0(0x167c), 0); wr(SEG0(0x167b), 0); wr(SEG0(0x167a), 0x000001ff);       // AGP BASE=0, BOT=0, TOP=0x1ff (8 GiB, 16 MiB units)
         if (env_on("CTX0_OFF")) { wr(SEG0(0x1688), 0x01fffe00); wr(SEG0(0x16ab + 17), 0x00F80001); usleep(2000); }   // MMHUB-like: no context0 translation
         uint64_t tgt = phys + 0x80000ull + 0x2000;                                     // inside the 1 MiB buffer
         uint32_t b = 8;
         vw(CMP_RING + b * 4 + 0, 0xC0033700); vw(CMP_RING + b * 4 + 4, 0x00100500);
         vw(CMP_RING + b * 4 + 8, (uint32_t)tgt); vw(CMP_RING + b * 4 + 12, (uint32_t)(tgt >> 32)); vw(CMP_RING + b * 4 + 16, 0xA6A6A6A6);
         b += 5;
         vw(CMP_RING + b * 4 + 0, 0xC0065800); vw(CMP_RING + b * 4 + 4, 0);                       // ACQUIRE_MEM: GL2 writeback+invalidate
         vw(CMP_RING + b * 4 + 8, 0xFFFFFFFF); vw(CMP_RING + b * 4 + 12, 0x00FFFFFF);
         vw(CMP_RING + b * 4 + 16, 0); vw(CMP_RING + b * 4 + 20, 0);
         vw(CMP_RING + b * 4 + 24, 0x0A); vw(CMP_RING + b * 4 + 28, 0xC000);
         b += 8;
         vw(CMP_WB + 16, b); vw(CMP_WB + 20, 0); db(6 * 4, b); usleep(300000);
         // GPU READ tests with COPY_DATA (src_sel=2 mem via L2, dst_sel=5 mem), first VRAM->VRAM to validate the encoding, then system->VRAM
         {
            uint64_t pp0, vv0; sm(3, 0x80000ull + 0x3000, 0x5A5A5A5A, &pp0, &vv0);
            vw(TESTBUF + 0x100, 0x77777777); vw(TESTBUF + 0x110, 0); vw(TESTBUF + 0x114, 0);
            struct { uint64_t src, dst; } cp[2] = { { MC(TESTBUF + 0x100), MC(TESTBUF + 0x110) }, { tgt + 0x1000, MC(TESTBUF + 0x114) } };
            for (int k = 0; k < 2; k++) {
               vw(CMP_RING + b * 4 + 0, 0xC0044000); vw(CMP_RING + b * 4 + 4, 2 | (5u << 8) | (1u << 20));
               vw(CMP_RING + b * 4 + 8, (uint32_t)cp[k].src); vw(CMP_RING + b * 4 + 12, (uint32_t)(cp[k].src >> 32));
               vw(CMP_RING + b * 4 + 16, (uint32_t)cp[k].dst); vw(CMP_RING + b * 4 + 20, (uint32_t)(cp[k].dst >> 32));
               b += 6; vw(CMP_WB + 16, b); db(6 * 4, b); usleep(300000);
               printf("COPY_DATA test %d (%s): dst=0x%08x\n", k, k ? "system->VRAM" : "VRAM->VRAM", vr(TESTBUF + (k ? 0x114 : 0x110)));
            }
         }
         uint64_t pp, v = 0; sm(2, 0x80000ull + 0x2000, 0, &pp, &v);
         printf("AGP identity write to system memory phys 0x%llx: value=0x%08x (want a6a6a6a6)\n", (unsigned long long)tgt, (uint32_t)v);
         return;
      }
      const uint64_t vaoff = 0x7F00000;
      const int vram_t = getenv("MESBOOT_VMT")[0] == '2';
      const int pt_sys = getenv("PT_SYS") && *getenv("PT_SYS") == '1';   // page table itself in system memory
      uint64_t sysphys = phys;
      if (vram_t) phys = FB_PA + 0xA200000ull;
      else if (pt_sys) phys = sysphys + 0x80000ull;                      // targets after the table region
      uint32_t wp = 8, hit = 0;
      static const uint64_t lowf[] = {0x77, 0x71, 0x73, 0x75, 0x7f};
      for (int mt = 0; mt < 5; mt++) {
         for (int fl = 0; fl < 5; fl++) {
            int idx = mt * 5 + fl;
            uint64_t pte = (phys + idx * 4096ull) | lowf[fl] | ((uint64_t)mt << 48) | (getenv("PTE_EXTRA") && *getenv("PTE_EXTRA") ? strtoull(getenv("PTE_EXTRA"), 0, 0) : 0);
            uint64_t slot = (vaoff >> 12) + idx;
            if (pt_sys) { uint64_t d0, d1; sm(3, slot * 8, (uint32_t)pte, &d0, &d1); sm(3, slot * 8 + 4, (uint32_t)(pte >> 32), &d0, &d1); }
            else { vw(GART_PT + slot * 8, (uint32_t)pte); vw(GART_PT + slot * 8 + 4, (uint32_t)(pte >> 32)); }
         }
      }
      if (pt_sys) {
         wr(SEG0(0x16f3), (uint32_t)((sysphys & ~0xfffull) | (getenv("ROOT_FL") && *getenv("ROOT_FL") ? strtoul(getenv("ROOT_FL"), 0, 0) : 3))); wr(SEG0(0x16f4), (uint32_t)(sysphys >> 32));   // PDB base in SYSTEM memory
         printf("PT in system memory at phys 0x%llx\n", (unsigned long long)sysphys);
      }
      wr(SEG0(0x16ab + 17), 0x00F80001); usleep(2000);       // invalidate (eng17)
      const char *vo = getenv("VARONLY"); const int only = (vo && *vo) ? atoi(vo) : -1;
      for (int idx = 0; idx < 25; idx++) {
         if (only >= 0 && idx != only) continue;
         uint64_t gva = GART_VA + vaoff + idx * 4096ull;
         vw(CMP_RING + wp * 4 + 0, 0xC0033700); vw(CMP_RING + wp * 4 + 4, 0x00100500);
         vw(CMP_RING + wp * 4 + 8, (uint32_t)gva); vw(CMP_RING + wp * 4 + 12, (uint32_t)(gva >> 32)); vw(CMP_RING + wp * 4 + 16, 0xABCD0000 + idx);
         wp += 5;
      }
      if (!env_on("NO_GCR")) {   // write back + invalidate the GPU L2 so cached (non-UC) writes reach memory
         vw(CMP_RING + wp * 4 + 0, 0xC0065800); vw(CMP_RING + wp * 4 + 4, 0);
         vw(CMP_RING + wp * 4 + 8, 0xFFFFFFFF); vw(CMP_RING + wp * 4 + 12, 0x00FFFFFF);
         vw(CMP_RING + wp * 4 + 16, 0); vw(CMP_RING + wp * 4 + 20, 0);
         vw(CMP_RING + wp * 4 + 24, 0x0A); vw(CMP_RING + wp * 4 + 28, 0xC000);
         wp += 8;
      }
      vw(CMP_WB + 16, wp); vw(CMP_WB + 20, 0);
      db(6 * 4, wp);
      usleep(300000);
      for (int idx = 0; idx < 25; idx++) {
         if (only >= 0 && idx != only) continue;
         uint64_t pp, val = 0; if (vram_t) val = vr(0xA200000 + idx * 4096ull); else sm(2, (pt_sys ? 0x80000ull : 0) + idx * 4096ull, 0, &pp, &val);
         if ((uint32_t)val == 0xABCD0000u + idx) { printf("  HIT: mtype=%d flags=0x%llx\n", idx / 5, (unsigned long long)lowf[idx % 5]); hit++; }
      }
      // read test: CPU puts a magic into system page 0, GPU COPY_DATA gart-VA -> MC testbuf
      if (env_on("COPYTEST")) {
         uint64_t pp, vv; if (vram_t) vw(0xA200000, 0xC0FFEE11); else sm(3, pt_sys ? 0x80000ull : 0, 0xC0FFEE11, &pp, &vv);
         vw(TESTBUF + 8, 0);
         uint64_t src = GART_VA + vaoff, dst = MC(TESTBUF + 8);
         uint32_t b = wp;
         vw(CMP_RING + b * 4 + 0, 0xC0044000); vw(CMP_RING + b * 4 + 4, 2 | (2u << 8) | (1u << 20));
         vw(CMP_RING + b * 4 + 8, (uint32_t)src); vw(CMP_RING + b * 4 + 12, (uint32_t)(src >> 32));
         vw(CMP_RING + b * 4 + 16, (uint32_t)dst); vw(CMP_RING + b * 4 + 20, (uint32_t)(dst >> 32));
         wp += 6; vw(CMP_WB + 16, wp); db(6 * 4, wp); usleep(200000);
         printf("GART read test: testbuf+8 = 0x%08x (want c0ffee11)\n", vr(TESTBUF + 8));
      }
      grbm_select(1, 0, 0, 0);
      printf("compute rptr=%u wptr_written=%u fault status=0x%x addr=0x%x:%x\n", rd(SEG0(0x1fb3)), wp, rd(SEG0(0x15c8)), rd(SEG0(0x15ca)), rd(SEG0(0x15c9)));
      grbm_select(0, 0, 0, 0);
      if (!vram_t) {   // where did the GPU writes go? scan the first dword of every page of the 1 MiB buffer
         int nz = 0;
         for (int pg = 0; pg < 256; pg++) { uint64_t pp, v = 0; sm(2, pg * 4096ull, 0, &pp, &v);
            if (pg >= 0x80 || !pt_sys) { if ((uint32_t)v) { printf("  page %d: 0x%08x\n", pg, (uint32_t)v); nz++; } } }
         printf("scan: %d non-zero target pages\n", nz);
      }
      printf("system-memory write via GART: %u hits of 25 variants (value=0x%08x)\n", hit, hit ? 0xabcd1234 : 0);
   }
}

// VMID 8 user context: 4-level page table in system memory, IB + target in VRAM pages, IB executed from the kernel compute ring
static void vm8_test(void)
{
   uint64_t phys = 0, len = 0, d0, d1;
   if (sm(1, 1 << 20, 0, &phys, &len)) { printf("vm8: sysmem alloc failed\n"); return; }
   // navi31: VM8_PTVRAM puts the 4 table levels in VRAM (offset == physical on this dGPU) with local PDEs, as
   // Linux keeps page tables; default keeps nullarc's system-memory tables (PDE VALID|SYSTEM|SNOOPED).
   const int ptv = env_on("VM8_PTVRAM");
   const uint64_t PTV = 0xA1C0000ull;
   const uint64_t T2 = ptv ? PTV : phys, T1 = T2 + 0x1000, T0 = T2 + 0x2000, TP = T2 + 0x3000;
   const uint64_t pde = ptv ? 1 : 7, root = ptv ? 1 : 3;
   if (ptv) for (int i = 0; i < 4 * 1024; i++) vw(PTV + i * 4, 0);
   const uint64_t VA_IB = env_on("VM8_HI") ? 0x800100000000ull : 0x100000000ull, VA_T = VA_IB + 0x1000;   // VM8_HI: upper half (bit 47), like RADV
   const uint64_t IBO = 0xA190000ull, TGO = 0xA191000ull;           // VRAM offsets backing the two pages (navi31: work area, clear of the GOP framebuffer)
   const unsigned mt = getenv("VM8_MT") ? (unsigned)atoi(getenv("VM8_MT")) : 4;
   const uint64_t flags = 0x71 | ((uint64_t)mt << 48);
   #define PUT(off, val) do { uint64_t v_ = (val); if (ptv) { vw(PTV + (off), (uint32_t)v_); vw(PTV + (off) + 4, (uint32_t)(v_ >> 32)); } \
                              else { sm(3, (off), (uint32_t)v_, &d0, &d1); sm(3, (off) + 4, (uint32_t)(v_ >> 32), &d0, &d1); } } while (0)
   const int V = 8;
   const int flat = env_on("VM8_FLAT");
   if (flat) {   // depth 0: one flat PTB covering 512 pages from VA_IB, like the working GART
      PUT(0x0000 + 0 * 8, (FB_PA + IBO) | flags);
      PUT(0x0000 + 1 * 8, (FB_PA + TGO) | flags);
      wr(SEG0(0x16f3 + 2 * V), (uint32_t)(T2 | root)); wr(SEG0(0x16f4 + 2 * V), (uint32_t)(T2 >> 32));
      wr(SEG0(0x1688 + V), 0x01fffe01);
      wr(SEG0(0x1713 + 2 * V), (uint32_t)(VA_IB >> 12)); wr(SEG0(0x1714 + 2 * V), (uint32_t)(VA_IB >> 44));
      wr(SEG0(0x1733 + 2 * V), (uint32_t)((VA_IB >> 12) + 511)); wr(SEG0(0x1734 + 2 * V), (uint32_t)(VA_IB >> 44));
   } else {
      PUT(0x0000 + ((VA_IB >> 39) & 0x1ff) * 8, T1 | pde);
      PUT(0x1000 + ((VA_IB >> 30) & 0x1ff) * 8, T0 | pde);                     // gmc_v11: translate_further=false -> plain PDE
      PUT(0x2000 + ((VA_IB >> 21) & 0x1ff) * 8, TP | pde);
      if (env_on("VM8_IBSYS")) PUT(0x3000 + ((VA_IB >> 12) & 0x1ff) * 8, (phys + 0x20000) | 0x77 | ((uint64_t)mt << 48));   // IB page in SYSTEM memory
      else PUT(0x3000 + ((VA_IB >> 12) & 0x1ff) * 8, (FB_PA + IBO) | flags);
      if (env_on("VM8_SYS")) PUT(0x3000 + ((VA_T >> 12) & 0x1ff) * 8, (phys + 0x10000) | 0x77 | ((uint64_t)mt << 48));   // target = SYSTEM page
      else PUT(0x3000 + ((VA_T >> 12) & 0x1ff) * 8, (FB_PA + TGO) | flags);
      wr(SEG0(0x16f3 + 2 * V), (uint32_t)(T2 | root)); wr(SEG0(0x16f4 + 2 * V), (uint32_t)(T2 >> 32));
      wr(SEG0(0x1688 + V), 0x01fffe07);
      wr(SEG0(0x1713 + 2 * V), 0); wr(SEG0(0x1714 + 2 * V), 0);
      wr(SEG0(0x1733 + 2 * V), 0xffffffff); wr(SEG0(0x1734 + 2 * V), 0xf);
   }
   #undef PUT
   wr(SEG0(0x16ab + 17), 0x00F80000 | (1u << V) | 1u); usleep(2000);
   // IB: WRITE_DATA to VA_T
   const int ib0 = env_on("IB0");                       // control: same IB packet but VMID 0 + MC addresses (no VM involved)
   // canonical (sign-extended) form for upper-half VAs, as amdgpu passes them: bit 47 replicated into 63:48
   #define SEXT(x) (((x) & (1ull << 47)) ? ((x) | 0xFFFF000000000000ull) : (x))
   const uint64_t ibva = ib0 ? MC(IBO) : SEXT(VA_IB), tva = ib0 ? MC(TGO) : SEXT(VA_T); const int vmid = ib0 ? 0 : V;
   vw(IBO + 0, 0xC0033700); vw(IBO + 4, 0x00100500); vw(IBO + 8, (uint32_t)tva); vw(IBO + 12, (uint32_t)(tva >> 32)); vw(IBO + 16, 0x1BADB002);
   if (env_on("VM8_IBSYS")) {   // same IB, but in the system page the PTE points at
      const uint32_t ibw[5] = {0xC0033700, 0x00100500, (uint32_t)tva, (uint32_t)(tva >> 32), 0x1BADB002};
      for (int i = 0; i < 5; i++) sm(3, 0x20000 + i * 4, ibw[i], &d0, &d1);
   }
   vw(TGO, 0);
   uint32_t b = vr(CMP_WB + 16);   // navi31: continue at the queue's wptr (was a fixed 8)
   printf("vm8: page tables in %s, ring at dw %u\n", ptv ? "VRAM" : "system memory", b);
   vw(CMP_RING + b * 4 + 0, 0xC0023F00); vw(CMP_RING + b * 4 + 4, (uint32_t)ibva); vw(CMP_RING + b * 4 + 8, (uint32_t)(ibva >> 32));
   vw(CMP_RING + b * 4 + 12, 5 | (1u << 23) | ((uint32_t)vmid << 24));   // IB_SIZE | VALID | VMID
   b += 4;
   // GL2 writeback+invalidate so the IB's write leaves the GPU cache (same ACQUIRE_MEM as the working GART test)
   vw(CMP_RING + b * 4 + 0, 0xC0065800); vw(CMP_RING + b * 4 + 4, 0);
   vw(CMP_RING + b * 4 + 8, 0xFFFFFFFF); vw(CMP_RING + b * 4 + 12, 0x00FFFFFF);
   vw(CMP_RING + b * 4 + 16, 0); vw(CMP_RING + b * 4 + 20, 0);
   vw(CMP_RING + b * 4 + 24, 0x0A); vw(CMP_RING + b * 4 + 28, 0xC000);
   b += 8;
   // RELEASE_MEM fence exactly as the kext emits it -> MC 0xA150000
   vw(0xA150000, 0); vw(0xA150004, 0);
   { const uint64_t fm = MC(0xA150000ull); const uint32_t rm[8] = {0xC0064900, 0x06603514, 0x40000000, (uint32_t)fm, (uint32_t)(fm >> 32), 0x77, 0, 0};
     for (int i = 0; i < 8; i++) vw(CMP_RING + (b + i) * 4, rm[i]); b += 8; }
   vw(CMP_WB + 16, b); vw(CMP_WB + 20, 0); db(6 * 4, b);
   const int tsys = env_on("VM8_SYS");
   uint64_t sv = 0;
   for (int i = 0; i < 50; i++) { usleep(10000); if (tsys) { uint64_t pp; sm(2, 0x10000, 0, &pp, &sv); if ((uint32_t)sv == 0x1BADB002) break; } else if (vr(TGO) == 0x1BADB002) break; }
   if (tsys) printf("vm8 SYSTEM target: 0x%08x (want 1badb002)\n", (uint32_t)sv);
   grbm_select(1, 0, 0, 0);
   printf("vm8 RELEASE_MEM fence = 0x%08x (want 77)\n", vr(0xA150000));
   printf("vm8 test (mtype %u): target=0x%08x (want 1badb002) rptr=%u fault=0x%x addr=0x%x:%x\n", mt, vr(TGO), rd(SEG0(0x1fb3)), rd(SEG0(0x15c8)), rd(SEG0(0x15ca)), rd(SEG0(0x15c9)));
   grbm_select(0, 0, 0, 0);
}

static void sched_test(void)
{
   sched_init();
   union MESAPI_SET_HW_RESOURCES r;
   memset(&r, 0, sizeof(r));
   r.header.type = MES_API_TYPE_SCHEDULER;
   r.header.opcode = MES_SCH_API_SET_HW_RSRC;
   r.header.dwsize = API_FRAME_SIZE_IN_DWORDS;
   // This card's own MES log prints "vmid_mask_mmhub 0x0000ff00, vmid_mask_gfxhub 0x0000ff00", i.e. VMIDs
   // 8..15 only. The port was sending 0xffffff00, which claims VMIDs 0..23 - an invalid mask that the
   // firmware starts executing against and then hangs on (the CP has to be power cycled afterwards).
   r.vmid_mask_mmhub = 0x0000ff00;
   r.vmid_mask_gfxhub = 0x0000ff00;
   r.gds_size = 0;
   r.paging_vmid = 0;
   // Fields named *_gpu_mc_ptr take an MC address, like MAP_QUEUES' MQD field - a VA is silently
   // unresolvable for the MES, which then processes the frame but never writes api_status.
   r.g_sch_ctx_gpu_mc_ptr = MC(SCH_WB + 128);
   r.query_status_fence_gpu_mc_ptr = MC(SCH_WB + 136);
   // The queue masks this card's own MES reported at init (dmesg: "MES: gfx_hqd_mask 0x00000002,
   // compute_hqd_mask 0x0000000c, sdma_hqd_mask 0x000000fc"). The port was sending 0xfffffffe for gfx
   // and 0xc in the first four compute slots, i.e. claiming queues that do not exist, which is a good
   // reason for the MES to reject the whole structure.
   r.compute_hqd_mask[0] = 0x0c;
   r.gfx_hqd_mask[0] = 0x02;
   r.sdma_hqd_mask[0] = 0xfc;
   // navi31: IP discovery bases of this card (its discovery table) — GC has a 4th segment (0x1C000),
   // MMHUB 3.0.0 starts at 0x1A000; there are 2 SDMA engines.
   const uint32_t gcb[5] = {0x1260, 0xA000, 0x1C000, 0x2402C00, 0}, mmb[5] = {0x1A000, 0x2408800, 0, 0, 0}, osb[5] = {0x10a0, 0x240a000, 0, 0, 0};
   for (int i = 0; i < 5; i++) { r.gc_base[i] = gcb[i]; r.mmhub_base[i] = mmb[i]; r.osssys_base[i] = osb[i]; }
   r.sdma_hqd_mask[1] = 0xfc;   // two SDMA engines on this die
   r.disable_reset = 1; r.disable_mes_log = 1; r.use_different_vmid_compute = 1;
   r.enable_reg_active_poll = 1; r.enable_level_process_quantum_check = 1;
   r.oversubscription_timer = 50;
   printf("sched: SET_HW_RESOURCES frame %u dwords (API says %u)\n",
          (unsigned)(sizeof(r) / 4), 64u);
   mes_submit(&r, sizeof(r), offsetof(union MESAPI_SET_HW_RESOURCES, api_status), "SET_HW_RESOURCES");
   // Newer MES firmware (sched_version >= 0x52) needs a second resource frame after SET_HW_RESOURCES:
   // mes_v11_0_hw_init sends SET_HW_RSRC_1 with enable_mes_info_ctx = 1 and a cleaner-shader fence address.
   union MESAPI_SET_HW_RESOURCES_1 r1;
   memset(&r1, 0, sizeof(r1));
   r1.header.type = MES_API_TYPE_SCHEDULER;
   r1.header.opcode = MES_SCH_API_SET_HW_RSRC_1;
   r1.header.dwsize = API_FRAME_SIZE_IN_DWORDS;
   r1.enable_mes_info_ctx = 1;
   r1.cleaner_shader_fence_mc_addr = MC(SCH_WB + 0x100);
   mes_submit(&r1, sizeof(r1), offsetof(union MESAPI_SET_HW_RESOURCES_1, api_status), "SET_HW_RESOURCES_1");
   union MESAPI__QUERY_MES_STATUS q;
   memset(&q, 0, sizeof(q));
   q.header.type = MES_API_TYPE_SCHEDULER;
   q.header.opcode = MES_SCH_API_QUERY_SCHEDULER_STATUS;
   q.header.dwsize = API_FRAME_SIZE_IN_DWORDS;
   mes_submit(&q, sizeof(q), offsetof(union MESAPI__QUERY_MES_STATUS, api_status), "QUERY_SCHED_STATUS");
   if (env_on("MESBOOT_CS")) compute_via_mes();
   if (env_on("MESBOOT_GFX")) gfx_via_mes();
}

#define MEC_RING 0xA130000ull
#define MEC_WB   0xA131000ull
#define MEC_MQD  0xA132000ull
#define MEC_EOP  0xA133000ull
static void mec_test(void)
{
   printf("MEC: CP_MEC_CNTL=0x%x before\n", rd(SEG1(0x802)));
   wr(SEG1(0x802), 0);                                  // unhalt ME1 + ME2
   usleep(5000);
   printf("MEC: CP_MEC_CNTL=0x%x after, RS64_CNTL=0x%x\n", rd(SEG1(0x802)), rd(SEG1(0x2904)));
   for (int i = 0; i < 1024; i++) vw(MEC_RING + i * 4, 0);
   for (int i = 0; i < 32; i++) vw(MEC_WB + i * 4, 0);
   mqd_build(MEC_MQD, MEC_RING, MEC_WB, MEC_EOP, 1, 6);
   uint32_t pq = 0x00308509; pq = (pq & ~0xffu) | 9u; pq = (pq & ~(0x3fu << 8)) | (9u << 8);
   pq |= (1u << 28) | (1u << 30) | (1u << 31) | (1u << 27); pq &= ~(1u << 29);
   hqd_program(1, 0, MEC_MQD, MEC_RING, MEC_WB, pq, (6u << 2) | (1u << 30));
   wr(SEG1(0x2040), 0xCAFEDEAD);
   vw(MEC_RING + 0, 0xC0017900); vw(MEC_RING + 4, 0x40); vw(MEC_RING + 8, 0xDEADBEEF);
   for (int i = 0; i < 4; i++) vw(TESTBUF + i * 4, 0);
   vw(MEC_RING + 12, 0xC0033700); vw(MEC_RING + 16, 0x00100500);
   vw(MEC_RING + 20, (uint32_t)VA(TESTBUF)); vw(MEC_RING + 24, (uint32_t)(VA(TESTBUF) >> 32)); vw(MEC_RING + 28, 0xFEEDF00D);
   db(6 * 4, 8);
   usleep(100000);
   grbm_select(1, 0, 0, 0);
   printf("MEC hqd: active=%u rptr=%u scratch0=0x%08x (expect deadbeef)\n", rd(SEG0(0x1fab)), rd(SEG0(0x1fb3)), rd(SEG1(0x2040)));
   printf("MEC testbuf=0x%08x (expect feedf00d)\n", vr(TESTBUF));
   grbm_select(0, 0, 0, 0);
}

// GFX (graphics) kernel queue: me0 pipe0 queue0 mapped by MES ADD_QUEUE(type GFX, map_legacy_kq), MQD per gfx_v11_0_gfx_mqd_init.
// Doorbell 0x116 (dword) inside the RB doorbell range Linux programs (LOWER 0x458 / UPPER 0x7f8, byte units).
#define GFX_RING 0xA170000ull            // 8 KiB ring (2048 dw: the clear-state preamble alone is 960)
#define GFX_RDW  2048u
#define GFX_WB   0xA174000ull            // +0 rptr report, +16 wptr poll
#define GFX_MQD  0xA175000ull            // v11_gfx_mqd, 2 KiB
#define GFX_CSB  0xA176000ull            // RLC clear-state buffer (RLC_CSIB_*), as gfx_v11_0_init_csb
#define GFX_DB   0x116u
#include "csb_gfx11.h"
static void gfx_via_mes(void)
{
   for (unsigned i = 0; i < GFX_RDW; i++) vw(GFX_RING + i * 4, 0);
   for (int i = 0; i < 32; i++) vw(GFX_WB + i * 4, 0);
   for (int i = 0; i < 512; i++) vw(GFX_MQD + i * 4, 0);
   const unsigned ncsb = sizeof(kCsbGfx11) / 4;
   for (unsigned i = 0; i < ncsb; i++) vw(GFX_CSB + i * 4, kCsbGfx11[i]);
   wr(SEG1(0x988), (uint32_t)(MC(GFX_CSB) >> 32)); wr(SEG1(0x987), (uint32_t)MC(GFX_CSB) & 0xfffffffc); wr(SEG1(0x989), ncsb);   // RLC_CSIB_ADDR_HI/LO/LENGTH
   vw(TESTBUF + 0x40, 0);
   wr(SEG0(0x1dfa), GFX_DB << 2); wr(SEG0(0x1dfb), 0x7f8);           // CP_RB_DOORBELL_RANGE_LOWER/UPPER
   const uint64_t ring = MC(GFX_RING), rb = ring >> 8, rptr = MC(GFX_WB), wpoll = MC(GFX_WB + 16), mqd = MC(GFX_MQD);
   struct { uint32_t off, val; } m[] = {
      {128, (uint32_t)mqd & 0xfffffffc}, {129, (uint32_t)(mqd >> 32)},   // cp_mqd_base_addr
      {162, 0x00000100},                                                // cp_gfx_mqd_control: PRIV_STATE 1, VMID 0
      {131, 0}, {134, 0}, {135, 0x00000a01},                            // hqd_vmid, queue_priority, quantum (QUANTUM_EN)
      {136, (uint32_t)rb}, {137, (uint32_t)(rb >> 32)},                 // cp_gfx_hqd_base = ring >> 8
      {139, (uint32_t)rptr & 0xfffffffc}, {140, (uint32_t)(rptr >> 32) & 0xffff},
      {141, (uint32_t)wpoll & 0xfffffffc}, {142, (uint32_t)(wpoll >> 32) & 0xffff},
      {145, 0x00a00000 | 10u | (8u << 8) | (1u << 29)},                 // hqd_cntl: RB_BUFSZ 10 (2048 dw), RB_BLKSZ 8, KMD_QUEUE
      {143, (GFX_DB << 2) | (1u << 30)},                                // cp_rb_doorbell_control: offset, DOORBELL_EN
      {138, 0}, {149, 0}, {150, 0},                                     // rptr, wptr, wptr_hi
      {130, 1},                                                         // cp_gfx_hqd_active
   };
   for (size_t i = 0; i < sizeof(m) / sizeof(m[0]); i++) vw(GFX_MQD + m[i].off * 4, m[i].val);
   union MESAPI__ADD_QUEUE a;
   memset(&a, 0, sizeof(a));
   a.header.type = MES_API_TYPE_SCHEDULER;
   a.header.opcode = MES_SCH_API_ADD_QUEUE;
   a.header.dwsize = API_FRAME_SIZE_IN_DWORDS;
   a.pipe_id = 0; a.queue_id = 0;
   a.doorbell_offset = GFX_DB;
   a.mqd_addr = mqd;
   a.wptr_addr = wpoll;
   a.queue_type = MES_QUEUE_TYPE_GFX;
   a.map_legacy_kq = 1;
   if (mes_submit(&a, sizeof(a), offsetof(union MESAPI__ADD_QUEUE, api_status), "ADD_QUEUE(gfx, map_legacy_kq)")) return;
   grbm_select(0, 0, 0, 0);
   printf("gfx hqd after map: active=%u base=0x%x cntl=0x%08x dbctl=0x%08x rptr=%u\n", rd(SEG0(0x1e80)), rd(SEG0(0x1e86)),
          rd(SEG0(0x1e8f)), rd(SEG0(0x1e8d)), rd(SEG0(0x1e88)));
   wr(SEG1(0x2041), 0xCAFEDEAD);
   uint32_t w = 0;
   if (!env_on("NO_CSB")) for (unsigned i = 0; i < ncsb; i++) vw(GFX_RING + w++ * 4, kCsbGfx11[i]);   // gfx_v11_0_cp_gfx_start
   vw(GFX_RING + w++ * 4, 0xC0017900); vw(GFX_RING + w++ * 4, 0x41); vw(GFX_RING + w++ * 4, 0xDEADBEEF);   // SET_UCONFIG_REG scratch1
   vw(GFX_RING + w++ * 4, 0xC0033700); vw(GFX_RING + w++ * 4, 0x00100500);                                   // WRITE_DATA mem, wr confirm
   vw(GFX_RING + w++ * 4, (uint32_t)MC(TESTBUF + 0x40)); vw(GFX_RING + w++ * 4, (uint32_t)(MC(TESTBUF + 0x40) >> 32));
   vw(GFX_RING + w++ * 4, 0x6F7A6F7A);
   vw(GFX_WB + 16, w); vw(GFX_WB + 20, 0);
   db(GFX_DB * 4, w);
   for (int i = 0; i < 50; i++) { usleep(10000); if (rd(SEG1(0x2041)) == 0xDEADBEEF && vr(TESTBUF + 0x40) == 0x6F7A6F7A) break; }
   grbm_select(0, 0, 0, 0);
   printf("gfx: scratch1=0x%08x (want deadbeef) testbuf=0x%08x (want 6f7a6f7a) rptr=%u rptr_wb=%u grbm=0x%08x cp_stat=0x%08x\n",
          rd(SEG1(0x2041)), vr(TESTBUF + 0x40), rd(SEG0(0x1e88)), vr(GFX_WB), rd(0x2004), rd(0x21a0));
   if (env_on("ADOPT_GFX")) {   // hand the gfx queue to the kext as its second queue
      uint64_t in[6] = {GFX_RING, GFX_RDW, GFX_WB, GFX_DB * 4, w, 0xA180000 | 1};
      kern_return_t kr = IOConnectCallScalarMethod(conn, SEL_DBG_ADOPTQ, in, 6, NULL, NULL);
      printf("adopt gfx queue -> kr 0x%x\n", kr);
   }
}

// The cleanest "does the MES execute anything at all" probe: a MISC/WRITE_REG command, whose effect is a
// register write we can read back (SCRATCH_REG0), independent of the queue machinery SET_HW_RESOURCES sets
// up. Frame layout (mes_api_def.h): header, misc opcode, api_status (4 dwords), then the payload;
// WRITE_REG carries struct MODIFY_REG { subcode, reg_offset, reg_value }.
static void mes_write_reg_test(void)
{
   sched_init();
   uint32_t pkt[64];
   for (int i = 0; i < 64; i++) pkt[i] = 0;
   pkt[0] = (1u << 0) | (14u << 4) | (64u << 12);   // type 1 (scheduler), opcode 14 (MISC), dwsize 64
   pkt[1] = 0;                                       // MISC sub-opcode 0 = WRITE_REG
   pkt[6] = 0;              // MODIFY_REG.subcode
   pkt[7] = 0x0000C040u;    // MODIFY_REG.reg_offset = SCRATCH_REG0
   pkt[8] = 0xBEEF1234u;    // MODIFY_REG.reg_value
   wr(SEG1(0x2040), 0xCAFEDEAD);
   mes_submit(pkt, sizeof pkt, 2 * 4, "MISC_WRITE_REG");
   printf("  scratch0 after MISC_WRITE_REG: 0x%08x (want beef1234)\n", rd(SEG1(0x2040)));
}
// Scan every ME/pipe for a live HQD: after the MES maps a queue it may land in a slot other than the one the
// port watches (gfx11_kiq_map_queues passes me = 2 for the MES ring, while the driver programs the scheduler
// HQD on me 3). Prints active/pq_base/rptr for each combination that reads as active.
static void hqd_scan(void)
{
   printf("HQD scan (me/pipe: active, pq_base, pq_base_hi, rptr, doorbell)\n");
   for (uint32_t me = 0; me <= 3; me++) {
      for (uint32_t pipe = 0; pipe <= 1; pipe++) {
         grbm_select(me, pipe, 0, 0);
         const uint32_t act = rd(SEG0(0x1fab));
         if (act) {
            printf("  me%u/pipe%u: active=%u pq_base=0x%08x hi=0x%08x rptr=%u dbctl=0x%08x mqd=0x%08x/0x%08x\n",
                   me, pipe, act, rd(SEG0(0x1fb1)), rd(SEG0(0x1fb2)), rd(SEG0(0x1fb3)),
                   rd(SEG0(0x1fb8)), rd(SEG0(0x1fa9)), rd(SEG0(0x1faa)));
         }
      }
   }
   grbm_select(0, 0, 0, 0);
}
// Program a MEC compute queue directly (me1/pipe0) and hand it to the kext, using the two constants that
// make the difference on this die:
//   - the compute ring's WRITE_DATA needs cmd = WR_CONFIRM (0x00100000); the KIQ needs cmd = 1 << 16.
//     A wrong cmd does not execute at all (and on me1 it wedges the CP).
//   - the queue parameters live in the MQD: dbctl 0x60000018, pqctl 0xf030890a, both read off the live
//     Linux compute queue on this card.
// Environment: ADOPT=1 hands the queue to the kext (debug.phxctl selector 112) so PhxAccel's submit path
// uses it; RINGTEST=1 (default) checks the queue executes before adopting.
static void compute_direct_setup(void)
{
   const uint32_t ringDw = 2048;                        // 8 KiB, matching QUEUE_SIZE 0xa
   for (uint32_t i = 0; i < ringDw; i++) vw(CMP_RING + i * 4, 0);
   for (int i = 0; i < 32; i++) vw(CMP_WB + i * 4, 0);
   mqd_build(CMP_MQD, CMP_RING, CMP_WB, CMP_EOP, 1, 6);
   vw(CMP_MQD + 132 * 4, 0x0be05500u);                  // cp_hqd_persistent_state
   vw(CMP_MQD + 143 * 4, 0x60000018u);                  // cp_hqd_pq_doorbell_control (index 6)
   vw(CMP_MQD + 145 * 4, 0xf030890au);                  // cp_hqd_pq_control (linux compute queue)
   // RTUPDATE=1 clears NO_UPDATE_RPTR (bit 29) so CP_HQD_PQ_RPTR tells us how far a client's stream got
   const uint32_t cq = env_on("RTUPDATE") ? 0xd030890au : 0xf030890au;
   vw(CMP_MQD + 145 * 4, cq);
   hqd_program(1, 0, CMP_MQD, CMP_RING, CMP_WB, cq, 0x60000018);
   grbm_select(1, 0, 0, 0);
   printf("computeq: active=%u pq_base=0x%x pqctl=0x%x dbctl=0x%x\n",
          rd(SEG0(0x1fab)), rd(SEG0(0x1fb1)), rd(SEG0(0x1fba)), rd(SEG0(0x1fb8)));
   grbm_select(0, 0, 0, 0);
}
static void compute_direct(void)
{
   compute_direct_setup();
   // Linux's ring test for a compute ring: WRITE_DATA with WR_CONFIRM to SCRATCH_REG0
   wr(SEG1(0x2040), 0xCAFEDEAD);
   vw(CMP_RING + 0, 0xC0033700); vw(CMP_RING + 4, 0x00100000); vw(CMP_RING + 8, 0x0000C040);
   vw(CMP_RING + 12, 0); vw(CMP_RING + 16, 0xDEADBEEF);
   vw(CMP_WB + 16, 5); vw(CMP_WB + 20, 0);
   db(6 * 4, 5);
   for (int i = 0; i < 100 && rd(SEG1(0x2040)) != 0xDEADBEEF; i++) usleep(10000);
   const int ok = rd(SEG1(0x2040)) == 0xDEADBEEF;
   printf("computeq: ring test scratch0=0x%08x -> %s\n", rd(SEG1(0x2040)), ok ? "EXECUTES" : "no execution");
   if (!ok) return;
   if (env_on("ADOPT")) {
      // Hand over a CLEAN ring. The ring test above advanced the CP's rptr, while the accelerator's own wptr
      // bookkeeping starts at 0, so its first submission would overwrite the packets the CP already consumed and
      // then fetch from the middle of its own sequence. Stop the queue, clear the ring, re-arm at rptr = wptr = 0.
      hqd_stop(1, 0);
      compute_direct_setup();
      // Clear-state preamble. amdgpu never brings a queue into use without one, and the accelerator's queue has
      // never run one, so a real client's command stream starts from whatever state the CP happened to have.
      if (env_on("PREAMBLE")) {   // note: PACKET3_CLEAR_STATE wedges a MEC queue (measured), so this is off by default
         static const uint32_t pre[] = {
            0xC0004A00u, 0x20000000u,   // PACKET3_PREAMBLE_CNTL, PREAMBLE_BEGIN_CLEAR_STATE
            0xC0001200u, 0x00000000u,   // PACKET3_CLEAR_STATE
            0xC0004A00u, 0x30000000u,   // PACKET3_PREAMBLE_CNTL, PREAMBLE_END_CLEAR_STATE
         };
         for (uint32_t i = 0; i < 6; i++) vw(0xA200000ull + i * 4, pre[i]);
         const uint64_t preva = gGartVa + 0xA200000ull;
         const uint64_t fenceMc = MC(0xA150000);
         vw(0xA150000, 0); vw(0xA150004, 0);
         uint32_t k = 0;
         vw(CMP_RING + (k++) * 4, 0xC0023F00);
         vw(CMP_RING + (k++) * 4, (uint32_t)preva & 0xFFFFFFFCu);
         vw(CMP_RING + (k++) * 4, (uint32_t)(preva >> 32));
         vw(CMP_RING + (k++) * 4, 6u | (1u << 23));
         vw(CMP_RING + (k++) * 4, 0xC0064900);
         vw(CMP_RING + (k++) * 4, 0x06603514);
         vw(CMP_RING + (k++) * 4, 0x40000000u);
         vw(CMP_RING + (k++) * 4, (uint32_t)fenceMc);
         vw(CMP_RING + (k++) * 4, (uint32_t)(fenceMc >> 32));
         vw(CMP_RING + (k++) * 4, 7); vw(CMP_RING + (k++) * 4, 0);
         vw(CMP_RING + (k++) * 4, 0);
         vw(CMP_WB + 16, k); vw(CMP_WB + 20, 0);
         db(6 * 4, k);
         for (int i = 0; i < 100 && vr(0xA150000) != 7; i++) usleep(10000);
         printf("preamble: clear-state fence=%u -> %s\n", vr(0xA150000), vr(0xA150000) == 7 ? "OK" : "no fence");
         hqd_stop(1, 0);
         compute_direct_setup();   // clean ring for the client
      }
      uint64_t in[6] = {CMP_RING, 2048u, CMP_WB, 6 * 4, 0, 0xA150000};
      kern_return_t kr = IOConnectCallScalarMethod(conn, SEL_DBG_ADOPTQ, in, 6, NULL, NULL);
      printf("computeq: adopt -> kr 0x%x\n", kr);
   }
}
// Does the compute queue fetch an indirect buffer at all? The ring test used direct PM4; the kext's submit
// path uses INDIRECT_BUFFER pointing at an IB in a client BO, so this isolates "IB fetch works" from "the
// client VM maps the IB". The IB lives in VRAM and is addressed through the GART window that context 0 (the
// queue's VMID) has mapped, so translation is the one we already proved.
#define IB_AREA 0xA200000ull
static void ib_test(void)
{
   static const uint32_t ib[] = {
      0xC0033700, 0x00100000, 0x0000C040, 0x00000000, 0xBEEF5678,   // WRITE_DATA WR_CONFIRM -> SCRATCH_REG0
   };
   const uint32_t ibdw = sizeof(ib) / 4;
   for (uint32_t i = 0; i < ibdw; i++) vw(IB_AREA + i * 4, ib[i]);
   const uint64_t ibva = gGartVa + IB_AREA;
   compute_direct_setup();             // programs the MEC queue without the ring test
   wr(SEG1(0x2040), 0xCAFEDEAD);
   uint32_t k = 0;
   vw(CMP_RING + (k++) * 4, 0xC0023F00);                                  // INDIRECT_BUFFER, 2 extra dwords
   vw(CMP_RING + (k++) * 4, (uint32_t)ibva & 0xFFFFFFFCu);
   vw(CMP_RING + (k++) * 4, (uint32_t)(ibva >> 32));
   vw(CMP_RING + (k++) * 4, ibdw | (1u << 23) | (0u << 24));              // size | VALID | VMID 0
   vw(CMP_WB + 16, k); vw(CMP_WB + 20, 0);
   db(6 * 4, k);
   for (int i = 0; i < 100 && rd(SEG1(0x2040)) != 0xBEEF5678; i++) usleep(10000);
   grbm_select(1, 0, 0, 0);
   printf("ibtest: scratch0=0x%08x (want beef5678) rptr=%u active=%u -> %s\n", rd(SEG1(0x2040)),
          rd(SEG0(0x1fb3)), rd(SEG0(0x1fab)), rd(SEG1(0x2040)) == 0xBEEF5678 ? "IB EXECUTES" : "no IB execution");
   grbm_select(0, 0, 0, 0);
}
// Same as ibtest but with the kext's full tail: INDIRECT_BUFFER, then RELEASE_MEM to a VRAM fence.
// The kext's submissions always end with a RELEASE_MEM; if that packet is what wedges the CP, the IB
// fetch itself is fine and the fence encoding is the suspect.
static void ib_test2(void)
{
   static const uint32_t ib[] = {
      0xC0033700, 0x00100000, 0x0000C040, 0x00000000, 0xFEED1234,
   };
   const uint32_t ibdw = sizeof(ib) / 4;
   for (uint32_t i = 0; i < ibdw; i++) vw(IB_AREA + i * 4, ib[i]);
   const uint64_t ibva = gGartVa + IB_AREA;
   const uint64_t fenceMc = MC(0xA150000);
   compute_direct_setup();
   wr(SEG1(0x2040), 0xCAFEDEAD);
   uint32_t k = 0;
   vw(CMP_RING + (k++) * 4, 0xC0023F00);
   vw(CMP_RING + (k++) * 4, (uint32_t)ibva & 0xFFFFFFFCu);
   vw(CMP_RING + (k++) * 4, (uint32_t)(ibva >> 32));
   vw(CMP_RING + (k++) * 4, ibdw | (1u << 23) | (0u << 24));
   vw(CMP_RING + (k++) * 4, 0xC0064900);                 // RELEASE_MEM, as gfx_v11_0_ring_emit_fence
   vw(CMP_RING + (k++) * 4, 0x06603514);
   vw(CMP_RING + (k++) * 4, 0x40000000u);
   vw(CMP_RING + (k++) * 4, (uint32_t)fenceMc);
   vw(CMP_RING + (k++) * 4, (uint32_t)(fenceMc >> 32));
   vw(CMP_RING + (k++) * 4, 1); vw(CMP_RING + (k++) * 4, 0);
   vw(CMP_RING + (k++) * 4, 0);
   vw(CMP_WB + 16, k); vw(CMP_WB + 20, 0);
   db(6 * 4, k);
   for (int i = 0; i < 100 && rd(SEG1(0x2040)) != 0xFEED1234; i++) usleep(10000);
   printf("ibtest2: scratch0=0x%08x (want feed1234) fence=0x%08x wb_rptr=0x%08x -> %s\n",
          rd(SEG1(0x2040)), vr(0xA150000), vr(CMP_WB), rd(SEG1(0x2040)) == 0xFEED1234 ? "IB + RELEASE_MEM OK" : "FAILED");
}
// RELEASE_MEM on its own: no IB, just the fence packet the kext appends to every submission. If this
// wedges the CP by itself, the packet is wrong; if the fence lands, the trouble is the combination.
static void ib_test3(void)
{
   const uint64_t fenceMc = MC(0xA150000);
   compute_direct_setup();
   vw(0xA150000, 0); vw(0xA150004, 0);
   uint32_t k = 0;
   vw(CMP_RING + (k++) * 4, 0xC0064900);
   vw(CMP_RING + (k++) * 4, (getenv("EOP_CTRL") && *getenv("EOP_CTRL")) ? (uint32_t)strtoul(getenv("EOP_CTRL"), NULL, 0) : 0x06603514u);
   vw(CMP_RING + (k++) * 4, (getenv("EOP_DATA") && *getenv("EOP_DATA")) ? (uint32_t)strtoul(getenv("EOP_DATA"), NULL, 0) : 0x40000000u);
   vw(CMP_RING + (k++) * 4, (uint32_t)fenceMc);
   vw(CMP_RING + (k++) * 4, (uint32_t)(fenceMc >> 32));
   vw(CMP_RING + (k++) * 4, 1); vw(CMP_RING + (k++) * 4, 0);
   vw(CMP_RING + (k++) * 4, 0);
   vw(CMP_WB + 16, k); vw(CMP_WB + 20, 0);
   db(6 * 4, k);
   for (int i = 0; i < 100 && vr(0xA150000) != 1; i++) usleep(10000);
   grbm_select(1, 0, 0, 0);
   printf("ibtest3: fence=0x%08x/%08x (want 1/0) rptr=%u active=%u -> %s\n",
          vr(0xA150000), vr(0xA150004), rd(SEG0(0x1fb3)), rd(SEG0(0x1fab)),
          vr(0xA150000) == 1 ? "FENCE WRITTEN" : "no fence");
   grbm_select(0, 0, 0, 0);
}
// Can the GPU read an indirect buffer that lives in SYSTEM memory? The client BOs the accelerator hands
// to RADV/NVMTL are GTT (system pages with SYSTEM|SNOOPED PTEs), so if this fails every client submission
// fails the same way, and the answer is to keep the client BOs in VRAM instead.
static void ib_test_sys(void)
{
   uint64_t phys = 0, len = 0, d0 = 0, d1 = 0;
   if (sm(1, 1u << 20, 0, &phys, &len)) { printf("ibtest_sys: system memory allocation failed\n"); return; }
   static const uint32_t ib[] = {0xC0033700, 0x00100000, 0x0000C040, 0x00000000, 0xABCD1234};
   for (int i = 0; i < 5; i++) sm(3, i * 4, ib[i], &d0, &d1);
   const uint64_t slot = 0x3000000ull >> 12;             // one PTE in the flat GART table
   const uint64_t pte = (phys & ~0xFFFull) | 0x77;       // VALID | SYSTEM | SNOOPED | EXEC | RD | WR
   vw(GART_PT + slot * 8, (uint32_t)pte); vw(GART_PT + slot * 8 + 4, (uint32_t)(pte >> 32));
   const uint64_t ibva = gGartVa + 0x3000000ull;
   hdp_flush();
   compute_direct_setup();
   wr(SEG1(0x2040), 0xCAFEDEAD);
   uint32_t k = 0;
   vw(CMP_RING + (k++) * 4, 0xC0023F00);
   vw(CMP_RING + (k++) * 4, (uint32_t)ibva & 0xFFFFFFFCu);
   vw(CMP_RING + (k++) * 4, (uint32_t)(ibva >> 32));
   vw(CMP_RING + (k++) * 4, 5u | (1u << 23));
   vw(CMP_WB + 16, k); vw(CMP_WB + 20, 0);
   db(6 * 4, k);
   for (int i = 0; i < 100 && rd(SEG1(0x2040)) != 0xABCD1234; i++) usleep(10000);
   printf("ibtest_sys: IB at system phys 0x%llx, scratch0=0x%08x -> %s\n", (unsigned long long)phys,
          rd(SEG1(0x2040)), rd(SEG1(0x2040)) == 0xABCD1234 ? "GPU READS SYSTEM MEMORY" : "GPU cannot read system memory");
}
// The decisive client-VM experiment, entirely from the port: give a *client* VMID (8) a flat depth-0 table -
// the only translation mode that is known to work on this die - and run a queue at that VMID with all of its
// addresses below the 512 MiB the table covers. If this executes, a flat client context is a viable path for
// the accelerator and the kext should build its client tables the same way.
static void ctx8_enable(void)
{
   const uint64_t span = 512ull << 20;                  // 512 MiB of client VA, 2 MiB of table
   const uint64_t tbl  = 0xA300000ull;
   const uint64_t fbpa = fb_pa_override();
   for (uint64_t i = 0; i < span / 4096; i++) {
      const uint64_t pte = (fbpa + i * 4096) | 0x71;    // VALID | WR | RD | EX
      vw(tbl + i * 8, (uint32_t)pte); vw(tbl + i * 8 + 4, (uint32_t)(pte >> 32));
   }
   hdp_flush();
   const uint32_t vmid = 8;
   const uint32_t vsh = 0x10;
   wr(0x28E8 + vsh + vmid, 0x01fffe01);                       // depth 0, flat, like context 0
   wr(0x2953 + vsh + 2 * vmid, (uint32_t)((fbpa + tbl) | 1));
   wr(0x2954 + vsh + 2 * vmid, (uint32_t)((fbpa + tbl) >> 32));
   wr(0x2973 + vsh + 2 * vmid, 0); wr(0x2974 + vsh + 2 * vmid, 0);
   wr(0x2993 + vsh + 2 * vmid, (uint32_t)((span >> 12) - 1)); wr(0x2994 + vsh + 2 * vmid, 0);
   wr(SEG0(0x16bd + 17) + vsh, 0x00F80000u | (1u << vmid) | 1u);    // invalidate this VMID
   usleep(2000);
   printf("ctx8: flat table at VRAM 0x%llx, CNTL=0x%08x PT=0x%08x window 0..0x%llx\n",
          (unsigned long long)tbl, rd(0x28E8 + vsh + vmid), rd(0x2953 + vsh + 2 * vmid),
          (unsigned long long)((span >> 12) - 1));
}
static void ctx8_flat(void)
{
   ctx8_enable();
   const uint32_t vmid = 8;
   gLowVa = 1; gMqdVmid = vmid;                        // every address below 512 MiB, translated by ctx 8
   compute_direct_setup();
   static const uint32_t ib[] = {0xC0033700, 0x00100000, 0x0000C040, 0x00000000, 0x8BADF00D};
   for (uint32_t i = 0; i < 5; i++) vw(IB_AREA + i * 4, ib[i]);
   const uint64_t ibva = IB_AREA;                       // low VA, mapped by the flat table
   wr(SEG1(0x2040), 0xCAFEDEAD);
   uint32_t k = 0;
   vw(CMP_RING + (k++) * 4, 0xC0023F00);
   vw(CMP_RING + (k++) * 4, (uint32_t)ibva & 0xFFFFFFFCu);
   vw(CMP_RING + (k++) * 4, (uint32_t)(ibva >> 32));
   vw(CMP_RING + (k++) * 4, 5u | (1u << 23) | (vmid << 24));
   vw(CMP_WB + 16, k); vw(CMP_WB + 20, 0);
   db(6 * 4, k);
   for (int i = 0; i < 100 && rd(SEG1(0x2040)) != 0x8BADF00D; i++) usleep(10000);
   grbm_select(1, 0, 0, 0);
   printf("ctx8: scratch0=0x%08x (want 8badf00d) rptr=%u active=%u -> %s\n", rd(SEG1(0x2040)),
          rd(SEG0(0x1fb3)), rd(SEG0(0x1fab)), rd(SEG1(0x2040)) == 0x8BADF00D ? "FLAT CLIENT VMID EXECUTES" : "no execution");
   grbm_select(0, 0, 0, 0);
}

// The queue stays at VMID 0 (the ring is in the GART window ctx 0 maps) and only the INDIRECT_BUFFER asks for
// VMID 8, so this isolates "does context 8 translate a fetch at all" from "can a queue live at VMID 8".
static void ib8(void)
{
   ctx8_enable();
   gLowVa = 0; gMqdVmid = 0;
   compute_direct_setup();
   static const uint32_t ib[] = {0xC0033700, 0x00100000, 0x0000C040, 0x00000000, 0x0D15EA5E};
   for (uint32_t i = 0; i < 5; i++) vw(IB_AREA + i * 4, ib[i]);
   const uint64_t ibva = IB_AREA;              // low VA covered by ctx 8's flat table
   wr(SEG1(0x2040), 0xCAFEDEAD);
   uint32_t k = 0;
   vw(CMP_RING + (k++) * 4, 0xC0023F00);
   vw(CMP_RING + (k++) * 4, (uint32_t)ibva & 0xFFFFFFFCu);
   vw(CMP_RING + (k++) * 4, (uint32_t)(ibva >> 32));
   vw(CMP_RING + (k++) * 4, 5u | (1u << 23) | (8u << 24));
   vw(CMP_WB + 16, k); vw(CMP_WB + 20, 0);
   db(6 * 4, k);
   for (int i = 0; i < 100 && rd(SEG1(0x2040)) != 0x0D15EA5E; i++) usleep(10000);
   grbm_select(1, 0, 0, 0);
   printf("ib8: scratch0=0x%08x (want 0d15ea5e) rptr=%u active=%u -> %s\n", rd(SEG1(0x2040)),
          rd(SEG0(0x1fb3)), rd(SEG0(0x1fab)), rd(SEG1(0x2040)) == 0x0D15EA5E ? "CTX-8 TRANSLATES" : "no translation");
   grbm_select(0, 0, 0, 0);
}

// ib8h: the same as ib8 but context 8 gets a FOUR-LEVEL table (depth 3) covering [0, 512 MiB) instead of a
// flat one, so the only variable is the walk depth. Table entries carry AMDGPU_PDE_PTE (bit 54) on every level,
// which is what amdgpu sets on all non-leaf entries (AMDGPU_PDE_PTE_FLAG in amdgpu_vm.h).
static void ib8h(void)
{
   const uint64_t span = 512ull << 20;
   const uint64_t fbpa = fb_pa_override();
   const uint32_t vmid = 8, vsh = 0x10;
   build_pt4(fbpa, 0, span);
   hdp_flush();
   // MMHUB=1 programs the MMHUB instance of the same block (base 0x1A080: the constants the port already has
   // are kMmCtx0Cntl 0x1A740, kMmCtx0BaseLo 0x1A7AB, kMmCtx0StartLo 0x1A7CB, kMmCtx0EndLo 0x1A7EB). Linux
   // only ever programs this instance - its GC-segment context registers read 0 - so this says which one the
   // CP's IB fetch actually goes through.
   const int mm = env_on("MMHUB");
   if (mm) {   // disable the GC instance's context 8 so only the MMHUB table can translate the fetch
      wr(0x28E8 + vsh + vmid, 0);
      wr(0x2953 + vsh + 2 * vmid, 0); wr(0x2954 + vsh + 2 * vmid, 0);
      printf("ib8h: GC ctx8 disabled (CNTL=0x%08x PT=0x%08x)\n", rd(0x28E8 + vsh + vmid), rd(0x2953 + vsh + 2 * vmid));
   }
   const uint32_t cBase = mm ? 0x1A7ABu : (0x2953u + vsh);
   const uint32_t cStart = mm ? 0x1A7CBu : (0x2973u + vsh);
   const uint32_t cEnd = mm ? 0x1A7EBu : (0x2993u + vsh);
   const uint32_t cCntl = mm ? 0x1A740u : (0x28E8u + vsh);
   wr(cBase + 2 * vmid, (uint32_t)((fbpa + PT_AREA) | 1));
   wr(cBase + 2 * vmid + 1, (uint32_t)((fbpa + PT_AREA) >> 32));
   wr(cStart + 2 * vmid, 0); wr(cStart + 2 * vmid + 1, 0);
   wr(cEnd + 2 * vmid, (uint32_t)((span >> 12) - 1)); wr(cEnd + 2 * vmid + 1, 0);
   wr(cCntl + vmid, 0x01fffe07);
   wr(SEG0(0x16bd + 17) + vsh, 0x00F80000u | (1u << vmid) | 1u);
   usleep(2000);
   printf("ib8h: 4-level table at VRAM 0x%llx for ctx8, CNTL=0x%08x PT=0x%08x\n",
          (unsigned long long)PT_AREA, rd(cCntl + vmid), rd(cBase + 2 * vmid));
   gLowVa = 0; gMqdVmid = 0;
   compute_direct_setup();
   static const uint32_t ib[] = {0xC0033700, 0x00100000, 0x0000C040, 0x00000000, 0x0BADF00D};
   const uint64_t ibArea = IB_AREA + 0x80000;   // 0xA280000: a VA the flat run never touched
   for (uint32_t i = 0; i < 5; i++) vw(ibArea + i * 4, ib[i]);
   const uint64_t ibva = ibArea;
   wr(SEG1(0x2040), 0xCAFEDEAD);
   uint32_t k = 0;
   vw(CMP_RING + (k++) * 4, 0xC0023F00);
   vw(CMP_RING + (k++) * 4, (uint32_t)ibva & 0xFFFFFFFCu);
   vw(CMP_RING + (k++) * 4, (uint32_t)(ibva >> 32));
   vw(CMP_RING + (k++) * 4, 5u | (1u << 23) | (8u << 24));
   vw(CMP_WB + 16, k); vw(CMP_WB + 20, 0);
   db(6 * 4, k);
   for (int i = 0; i < 100 && rd(SEG1(0x2040)) != 0x0BADF00D; i++) usleep(10000);
   grbm_select(1, 0, 0, 0);
   printf("ib8h: scratch0=0x%08x (want 0badf00d) -> %s\n", rd(SEG1(0x2040)),
          rd(SEG1(0x2040)) == 0x0BADF00D ? "4-LEVEL CLIENT WALK WORKS" : "no translation");
   grbm_select(0, 0, 0, 0);
}
// ib8hs: client VMID 8 with a FOUR-LEVEL table that lives in SYSTEM memory and carries SYSTEM|SNOOPED on every
// level entry (plus AMDGPU_PDE_PTE), which is the layout the accelerator's own comment describes as working on
// Navi 31. The only combination not yet tried.
static void ib8hs(void)
{
   const uint64_t span = 512ull << 20;
   const uint64_t fbpa = fb_pa_override();
   const uint32_t vmid = 8, vsh = 0x10;
   uint64_t ptphys = 0;
   build_pt4_sys(fbpa, 0, span, &ptphys);
   hdp_flush();
   wr(0x2953 + vsh + 2 * vmid, (uint32_t)(ptphys | 1));
   wr(0x2954 + vsh + 2 * vmid, (uint32_t)(ptphys >> 32));
   wr(0x2973 + vsh + 2 * vmid, 0); wr(0x2974 + vsh + 2 * vmid, 0);
   wr(0x2993 + vsh + 2 * vmid, (uint32_t)((span >> 12) - 1)); wr(0x2994 + vsh + 2 * vmid, 0);
   wr(0x28E8 + vsh + vmid, 0x01fffe07);
   wr(SEG0(0x16bd + 17) + vsh, 0x00F80000u | (1u << vmid) | 1u);
   usleep(2000);
   printf("ib8hs: system 4-level table at 0x%llx for ctx8, CNTL=0x%08x PT=0x%08x\n",
          (unsigned long long)ptphys, rd(0x28E8 + vsh + vmid), rd(0x2953 + vsh + 2 * vmid));
   gLowVa = 0; gMqdVmid = 0;
   compute_direct_setup();
   static const uint32_t ib[] = {0xC0033700, 0x00100000, 0x0000C040, 0x00000000, 0x0BADC0DE};
   for (uint32_t i = 0; i < 5; i++) vw(IB_AREA + i * 4, ib[i]);
   wr(SEG1(0x2040), 0xCAFEDEAD);
   uint32_t k = 0;
   vw(CMP_RING + (k++) * 4, 0xC0023F00);
   vw(CMP_RING + (k++) * 4, (uint32_t)IB_AREA & 0xFFFFFFFCu);
   vw(CMP_RING + (k++) * 4, 0u);
   vw(CMP_RING + (k++) * 4, 5u | (1u << 23) | (8u << 24));
   vw(CMP_WB + 16, k); vw(CMP_WB + 20, 0);
   db(6 * 4, k);
   for (int i = 0; i < 100 && rd(SEG1(0x2040)) != 0x0BADC0DE; i++) usleep(10000);
   grbm_select(1, 0, 0, 0);
   printf("ib8hs: scratch0=0x%08x (want 0badc0de) -> %s\n", rd(SEG1(0x2040)),
          rd(SEG1(0x2040)) == 0x0BADC0DE ? "SYSTEM 4-LEVEL CLIENT WALK WORKS" : "no translation");
   grbm_select(0, 0, 0, 0);
}
// RADV's nested IB starts with SET_SH_REG state programming and also emits SET_UCONFIG_REG for
// GB_ADDR_CONFIG (0x444). UConfig registers are graphics-level; this says whether a MEC compute queue
// tolerates them at all.
static void pm4_uconfig(void)
{
   static const uint32_t ib[] = {
      0xC0047900u, 0x00000444u, 0x00000444u, 0x00000000u, 0x00000000u,
   };
   const uint32_t ibdw = sizeof(ib) / 4;
   for (uint32_t i = 0; i < ibdw; i++) vw(0xA200000ull + i * 4, ib[i]);
   const uint64_t ibva = gGartVa + 0xA200000ull;
   const uint64_t fenceMc = MC(0xA150000);
   compute_direct_setup();
   vw(0xA150000, 0); vw(0xA150004, 0);
   uint32_t k = 0;
   vw(CMP_RING + (k++) * 4, 0xC0023F00);
   vw(CMP_RING + (k++) * 4, (uint32_t)ibva & 0xFFFFFFFCu);
   vw(CMP_RING + (k++) * 4, (uint32_t)(ibva >> 32));
   vw(CMP_RING + (k++) * 4, ibdw | (1u << 23));
   vw(CMP_RING + (k++) * 4, 0xC0064900);
   vw(CMP_RING + (k++) * 4, 0x06603514);
   vw(CMP_RING + (k++) * 4, 0x40000000u);
   vw(CMP_RING + (k++) * 4, (uint32_t)fenceMc);
   vw(CMP_RING + (k++) * 4, (uint32_t)(fenceMc >> 32));
   vw(CMP_RING + (k++) * 4, 9); vw(CMP_RING + (k++) * 4, 0);
   vw(CMP_RING + (k++) * 4, 0);
   vw(CMP_WB + 16, k); vw(CMP_WB + 20, 0);
   db(6 * 4, k);
   for (int i = 0; i < 100 && vr(0xA150000) != 9; i++) usleep(10000);
   grbm_select(1, 0, 0, 0);
   printf("uconfig: SET_UCONFIG_REG(0x444) on a MEC queue -> fence=%u %s\n", vr(0xA150000),
          vr(0xA150000) == 9 ? "ACCEPTED" : "WEDGED");
   grbm_select(0, 0, 0, 0);
}
// Bring up the GFX queue (ME0/pipe0). RADV's first submissions are gfx streams even for a compute-only
// workload - they program GB_ADDR_CONFIG and the gfx state through SET_UCONFIG_REG, which a MEC queue cannot
// execute (measured: a five-dword IB with that packet wedges the compute queue). The accelerator only has the
// compute queue ready, so gpuSubmit falls back and runs gfx work on it. This says whether ME0/pipe0 executes.
// The GFX queue, programmed through its own register block (CP_GFX_HQD_*, base register 0x1E80) instead of
// the CP_HQD_* block the MEC compute queue uses. The ring base lives inside the MQD, which CP_GFX_HQD_BASE
// points at, so mqd_build's ring/wb/eop fields are what matter and the registers only say where the MQD is.
// The gfx queue's MQD (struct v11_gfx_mqd, drivers/gpu/drm/amd/include/v11_structs.h). The field layout is
// its own: cp_gfx_hqd_base at word 136 holds the ring base >> 8, cp_gfx_hqd_cntl at word 145 is the
// CP_GFX_HQD_CNTL form (RB_BUFSZ/RB_BLKSZ/RB_NON_PRIV), not the MEC PQ_CONTROL the compute queue takes.
static void gfx_mqd_build(uint64_t mqd, uint64_t ring, uint64_t wb, uint32_t dbIdx)
{
   for (int i = 0; i < 512; i++) vw(mqd + i * 4, 0);
   #define G(i, v) vw(mqd + (i) * 4, (uint32_t)(v))
   const uint64_t mqdMc = MC(mqd);
   G(128, mqdMc & 0xfffffffc); G(129, mqdMc >> 32);
   G(130, 1);                                          // cp_gfx_hqd_active
   G(131, 0);                                          // cp_gfx_hqd_vmid
   G(134, 0);                                          // cp_gfx_hqd_queue_priority
   G(135, 0x00000a01);                                 // cp_gfx_hqd_quantum (QUANTUM_EN)
   G(136, VA(ring) >> 8); G(137, VA(ring) >> 40);       // cp_gfx_hqd_base: ring base >> 8
   G(138, 0);                                          // cp_gfx_hqd_rptr
   G(139, VA(wb) & 0xfffffffc); G(140, VA(wb) >> 32);   // cp_gfx_hqd_rptr_addr
   G(141, VA(wb + 16) & 0xfffffffc); G(142, VA(wb + 16) >> 32);   // cp_rb_wptr_poll_addr
   G(143, (dbIdx << 2) | (1u << 30));                  // cp_rb_doorbell_control
   G(144, 0);                                          // cp_gfx_hqd_offset
   G(145, 0x00a0880au);                                // cp_gfx_hqd_cntl: BUFSZ 10, BLKSZ 8, RB_NON_PRIV
   G(149, 0); G(150, 0);                               // cp_gfx_hqd_wptr / _hi
   #undef G
}
static void gfx_queue_test(void)
{
   const uint64_t RING = 0x6000000ull, WB = 0x6010000ull, MQD = 0x6020000ull, EOP = 0x6030000ull;
   const uint32_t dbIdx = 1;
   for (int i = 0; i < 2048; i++) vw(RING + i * 4, 0);
   for (int i = 0; i < 32; i++) vw(WB + i * 4, 0);
   gfx_mqd_build(MQD, RING, WB, dbIdx);
   hdp_flush();
   const uint64_t mqdMc = MC(MQD);
   grbm_select(0, 0, 0, 0);
   wr(SEG0(0x1e80), 0);                                            // ACTIVE off while programming
   wr(SEG0(0x1e81), 0);                                            // VMID 0
   wr(SEG0(0x1e84), 0);                                            // QUEUE_PRIORITY
   wr(SEG0(0x1e85), 0x00000a01u);                                  // QUANTUM (amdgpu default)
   wr(SEG0(0x1e86), (uint32_t)mqdMc);                              // BASE = the MQD in memory
   wr(SEG0(0x1e87), (uint32_t)(mqdMc >> 32));
   wr(SEG0(0x1e88), 0);                                            // RPTR
   wr(SEG0(0x1e89), (uint32_t)(VA(WB + 16) & 0xfffffffc));         // RPTR_ADDR (writeback)
   wr(SEG0(0x1e8a), (uint32_t)(VA(WB + 16) >> 32));
   wr(SEG0(0x1e8e), 0);                                            // OFFSET: RB_OFFSET 0
   wr(SEG0(0x1e8f), 0x00a0880au);                                  // CNTL: default + RB_BUFSZ 10, RB_BLKSZ 8, RB_NON_PRIV
   wr(SEG0(0x1e80), 1);                                            // ACTIVE
   printf("gfxq: active=%u base=0x%x cntl=0x%08x rptr=%u offset=%u\n", rd(SEG0(0x1e80)), rd(SEG0(0x1e86)),
          rd(SEG0(0x1e8f)), rd(SEG0(0x1e88)), rd(SEG0(0x1e8e)));
   wr(SEG1(0x2040), 0xCAFEDEAD);
   vw(RING + 0, 0xC0033700); vw(RING + 4, 0x40100000); vw(RING + 8, 0x0000C040);
   vw(RING + 12, 0); vw(RING + 16, 0xDEADBEEF);
   vw(WB + 16, 5); vw(WB + 20, 0);
   db(dbIdx * 4, 5);
   for (int i = 0; i < 100 && rd(SEG1(0x2040)) != 0xDEADBEEF; i++) usleep(10000);
   grbm_select(0, 0, 0, 0);
   printf("gfxq: scratch0=0x%08x (want deadbeef) rptr=%u -> %s\n", rd(SEG1(0x2040)), rd(SEG0(0x1e88)),
          rd(SEG1(0x2040)) == 0xDEADBEEF ? "GFX QUEUE EXECUTES" : "no execution");
}// The legacy (non-MES) gfx ring: CP_RB0_* registers, the path amdgpu's gfx_v11_0_cp_gfx_resume uses when the
// MES is not in charge. This is a global ring, not an HQD - which is why programming CP_GFX_HQD_* left a queue
// marked ACTIVE that never fetched anything.
static void gfx_rb0_test(void)
{
   const uint64_t RING = 0x6000000ull, WB = 0x6010000ull;   // 4 KiB ring, 4 KiB writeback
   for (int i = 0; i < 1024; i++) vw(RING + i * 4, 0);
   for (int i = 0; i < 32; i++) vw(WB + i * 4, 0);
   hdp_flush();
   const uint32_t rb_bufsz = 9;                    // order_base_2(4096 / 8)
   const uint32_t cntl = (rb_bufsz & 0x3f) | (((rb_bufsz - 2) & 0x3f) << 8) | (1u << 15);   // KMD_QUEUE
   // gfx_v11_0_config_gfx_rs64: point the PFP and ME at their microcode. The values come from the firmware
   // headers (gc_11_0_3_pfp.bin / _me.bin, gfx_firmware_header_v2_0): ucode_start_addr_lo = 0x00003000 and
   // ucode_start_addr_hi = 0x00070000, so the register takes (hi << 30) | (lo >> 2) = 0xC00, hi >> 2 = 0x1C000.
   // The port only ever programmed the MES entry point, which is why the gfx ME had no code to run.
   for (uint32_t pipe = 0; pipe < 2; pipe++) {
      grbm_select(0, pipe, 0, 0);
      wr(SEG0(0x1e44), 0x00000c00u);          // CP_PFP_PRGRM_CNTR_START
      wr(SEG0(0x1e59), 0x0001c000u);          // CP_PFP_PRGRM_CNTR_START_HI
      wr(SEG0(0x1e45), 0x00000c00u);          // CP_ME_PRGRM_CNTR_START
      wr(SEG0(0x1e79), 0x0001c000u);          // CP_ME_PRGRM_CNTR_START_HI
   }
   grbm_select(0, 0, 0, 0);
   {
      uint32_t t = rd(SEG1(0x803));
      wr(SEG1(0x803), t | (1u << 18) | (1u << 19));
      wr(SEG1(0x803), t & ~((1u << 18) | (1u << 19)));
      t = rd(SEG1(0x803));
      wr(SEG1(0x803), t | (1u << 20) | (1u << 21));
      wr(SEG1(0x803), t & ~((1u << 20) | (1u << 21)));
      printf("gfxme: PFP/ME start programmed, CP_ME_CNTL=0x%08x PFP_START=0x%x ME_START=0x%x\n",
             rd(SEG1(0x803)), rd(SEG0(0x1e44)), rd(SEG0(0x1e45)));
   }
   grbm_select(0, 0, 0, 0);
   // amdgpu's gfx_v11_0_cp_gfx_switch_pipe: select the CP's gfx pipe through GRBM_GFX_CNTL.PIPEID
   // (GRBM_GFX_INDEX is a different register: it selects register-block instances, not the CP pipe)
   wr(SEG0(0x0900), 0);                            // GRBM_GFX_CNTL: PIPEID 0
   wr(SEG0(0x0f61), 0);                            // CP_RB_WPTR_DELAY
   wr(SEG0(0x1df1), 0);                            // CP_RB_VMID
   wr(SEG0(0x1de1), cntl);                         // CP_RB0_CNTL
   wr(SEG0(0x1df4), 0); wr(SEG0(0x1df5), 0);       // CP_RB0_WPTR / _HI
   const uint64_t rptrAddr = env_on("RB0_MC") ? MC(WB) : VA(WB);
   wr(SEG0(0x1de3), (uint32_t)(rptrAddr & 0xfffffffc)); wr(SEG0(0x1de4), (uint32_t)(rptrAddr >> 32));
   const uint64_t pollAddr = env_on("RB0_MC") ? MC(WB + 16) : VA(WB + 16);
   wr(SEG0(0x1e8b), (uint32_t)(pollAddr & 0xfffffffc));   // CP_RB_WPTR_POLL_ADDR_LO
   wr(SEG0(0x1e8c), (uint32_t)(pollAddr >> 32));
   usleep(1000);
   wr(SEG0(0x1de1), cntl);                         // CP_RB0_CNTL again, as amdgpu does
   const uint64_t rb = (env_on("RB0_MC") ? MC(RING) : VA(RING)) >> 8;   // RB0_MC=1 tries an MC base
   wr(SEG0(0x1de0), (uint32_t)rb); wr(SEG0(0x1e51), (uint32_t)(rb >> 32));   // CP_RB0_BASE / _HI
   wr(SEG0(0x1f40), 1);                            // CP_RB_ACTIVE
   wr(SEG0(0x1dfa), 0); wr(SEG0(0x1dfb), 0xffffffffu);   // doorbell range
   wr(SEG0(0x1e8d), (1u << 30) | 2);               // CP_RB_DOORBELL_CONTROL: enable, offset 2
   printf("gfxq: active=%u cntl=0x%08x base=0x%x wptr=%u dbctl=0x%08x\n", rd(SEG0(0x1f40)), rd(SEG0(0x1de1)),
          rd(SEG0(0x1de0)), rd(SEG0(0x1df4)), rd(SEG0(0x1e8d)));
   wr(SEG1(0x2040), 0xCAFEDEAD);
   vw(RING + 0, 0xC0033700); vw(RING + 4, 0x40100000); vw(RING + 8, 0x0000C040);
   vw(RING + 12, 0); vw(RING + 16, 0xDEADBEEF);
   wr(SEG0(0x1df4), 5); wr(SEG0(0x1df5), 0);         // CP_RB0_WPTR / _HI
   db(0, 5); db(4, 5); db(8, 5); db(0x10, 5); db(0x18, 5);   // ring every plausible doorbell offset
   for (int i = 0; i < 100 && rd(SEG1(0x2040)) != 0xDEADBEEF; i++) usleep(10000);
   printf("gfxq: scratch0=0x%08x (want deadbeef) rptr=%u -> %s\n", rd(SEG1(0x2040)), rd(SEG0(0x0f60)),
          rd(SEG1(0x2040)) == 0xDEADBEEF ? "GFX QUEUE EXECUTES" : "no execution");
}
// The MES is a RISC-V core and the CP exposes its trap state: MEPC (where it stopped), MCAUSE (why),
// MBADADDR (the faulting address), MSTATUS and MSCRATCH. Reading them per pipe after a submission is the
// diagnostic the port never had: it says whether the firmware trapped, and where.
static void mes_trap_dump(void)
{
   for (uint32_t pipe = 0; pipe < 2; pipe++) {
      grbm_select(3, pipe, 0, 0);
      printf("mes pipe%u: MEPC=0x%08x%08x MCAUSE=0x%08x%08x MBADADDR=0x%08x%08x\n", pipe,
             rd(SEG1(0x2819)), rd(SEG1(0x2818)), rd(SEG1(0x281b)), rd(SEG1(0x281a)),
             rd(SEG1(0x281d)), rd(SEG1(0x281c)));
      printf("          MSTATUS=0x%08x%08x MSCRATCH=0x%08x%08x INSTR_PNTR=0x%08x MES_CNTL=0x%08x\n",
             rd(SEG1(0x2817)), rd(SEG1(0x2816)), rd(SEG1(0x2814)), rd(SEG1(0x2815)),
             rd(SEG1(0x2813)), rd(SEG1(0x2807)));
   }
   grbm_select(0, 0, 0, 0);
}
static void sched_test(void);
static uint32_t gKiqMe = 3, gKiqPipe = 1;   // which ME the KIQ test programs (3/1 = MES, 1/0 and 2/0 = MECs)
// KIQ_ATLINUX=1 puts the ring at the exact VA the working Linux driver uses (0x7fff00800000) instead of
// 0x7fff00a100000, in case the MES only accepts the window it was configured with.
static uint64_t gKiqRing = KIQ_RING, gKiqWb = KIQ_WB, gKiqMqd = KIQ_MQD;
static void kiq_init_only(void)
{
   if (env_on("KIQ_ATLINUX")) {
      gKiqRing = 0x800000ull; gKiqWb = 0x810000ull; gKiqMqd = 0x820000ull;
      printf("kiq: Linux ring placement, VA 0x%llx\n", (unsigned long long)(gGartVa + gKiqRing));
   }
   uint32_t t = rd(SEG1(0x98a)) & 0xffffff00;
   t |= (3u << 5) | (1u << 3);
   wr(SEG1(0x98a), t); wr(SEG1(0x98a), t | 0x80);
   mqd_build(gKiqMqd, gKiqRing, gKiqWb, KIQ_EOP, 1, 0x18);
   for (int i = 0; i < 1024; i++) vw(gKiqRing + i * 4, 0);
   for (int i = 0; i < 32; i++) vw(gKiqWb + i * 4, 0);
   // Which ME the queue is programmed on: 3/pipe1 is the MES (RS64) KIQ. 1 and 2 are the MECs, which is where a
   // plain compute queue lives; KIQ_ME / KIQ_PIPE select them for the direct (MES-free) execution test.
   gKiqMe = (getenv("KIQ_ME") && *getenv("KIQ_ME")) ? (uint32_t)strtoul(getenv("KIQ_ME"), NULL, 0) : 3u;
   gKiqPipe = (getenv("KIQ_PIPE") && *getenv("KIQ_PIPE")) ? (uint32_t)strtoul(getenv("KIQ_PIPE"), NULL, 0) : 1u;
   hqd_program(gKiqMe, gKiqPipe, gKiqMqd, gKiqRing, gKiqWb, 0xd8308011, (0x18u << 2) | (1u << 30));
   printf("kiq init: RLC_CP_SCHEDULERS=0x%x\n", rd(SEG1(0x98a)));
}
// navi31: the first packets Linux put on this card's KIQ ring (a decoded capture of that ring):
// SET_RESOURCES first, then its own ring test, a WRITE_DATA with dst_sel = register (SCRATCH_REG0 = 0xdeadbeef).
// The KIQ consumed but ignored every packet until it had seen SET_RESOURCES.
static void kiqres_test(void)
{
   kiq_init_only();
   static const uint32_t pkt[] = {
      0xC006A000, 0x000A0000, 0xFFFFFFFF, 0xFFFFFFFF, 0, 0, 0, 0,   // SET_RESOURCES, payload as Linux sent it
      0xC0033700, 0x00100100, 0x0000C040, 0x00000000, 0xDEADBEEF,   // WRITE_DATA -> SCRATCH_REG0
   };
   // KIQ_NORES=1 drops the SET_RESOURCES packet and sends only the WRITE_DATA. Its payload came from a capture on
   // another MEC build, so if the KIQ drains the ring without executing the marker, that packet is the suspect.
   static const uint32_t pkt_wr[] = {
      0xC0033700, 0x00100100, 0x0000C040, 0x00000000, 0xDEADBEEF,
   };
   // The WRITE_DATA control word: DST_SEL is bits 9:8 (1 = register, the value gfx_v11_0_ring_emit_wreg emits),
   // WR_CONFIRM is bit 20. The captured navi31 word 0x00010000 has DST_SEL = 0, i.e. a *memory* write to
   // 0x0000C040 - which is why the marker never reached SCRATCH_REG0. KIQ_CTRL overrides it for A/B runs.
   uint32_t ctrl = 0x00100100u;
   if (getenv("KIQ_CTRL") && *getenv("KIQ_CTRL")) ctrl = (uint32_t)strtoul(getenv("KIQ_CTRL"), NULL, 0);
   uint32_t pktv[sizeof(pkt) / 4], pktwv[sizeof(pkt_wr) / 4];
   memcpy(pktv, pkt, sizeof pkt); memcpy(pktwv, pkt_wr, sizeof pkt_wr);
   pktv[9] = ctrl; pktwv[1] = ctrl;
   const int nores = env_on("KIQ_NORES");
   const uint32_t *pktp = nores ? pktwv : pktv;
   const uint32_t n = nores ? (uint32_t)(sizeof(pkt_wr) / 4) : (uint32_t)(sizeof(pkt) / 4);
   const uint32_t *pkt_ = pktp;
   wr(SEG1(0x2040), 0xCAFEDEAD);
   for (uint32_t i = 0; i < n; i++) vw(gKiqRing + i * 4, pkt_[i]);
   vw(gKiqWb + 16, n); vw(gKiqWb + 20, 0);   // wptr poll shadow, then doorbell (db() flushes the HDP first)
   kern_return_t kr = db(0x18 * 4, n);
   for (int t = 0; t < 50 && rd(SEG1(0x2040)) != 0xDEADBEEF; t++) usleep(10000);
   grbm_select(gKiqMe, gKiqPipe, 0, 0);
   uint32_t rp = rd(SEG0(0x1fb3)), act = rd(SEG0(0x1fab));
   grbm_select(0, 0, 0, 0);
   printf("kiqres[me%u/pipe%u]: doorbell kr=0x%x rptr=%u/%u active=%u scratch0=0x%08x (want deadbeef) rptr_report=0x%08x\n",
          gKiqMe, gKiqPipe, kr, rp, n, act, rd(SEG1(0x2040)), vr(gKiqWb));
}
// navi31: submit packets on the KIQ ring at the wptr kept in its poll shadow (KIQ_WB+16), wait for rptr.
static int kiq_submit(const uint32_t *p, uint32_t n)
{
   uint32_t w = vr(KIQ_WB + 16);
   for (uint32_t i = 0; i < n; i++) vw(KIQ_RING + (w + i) * 4, p[i]);
   w += n;
   vw(KIQ_WB + 16, w); vw(KIQ_WB + 20, 0);
   db(0x18 * 4, w);
   uint32_t rp = 0;
   for (int t = 0; t < 100; t++) {
      grbm_select(3, 1, 0, 0); rp = rd(SEG0(0x1fb3)); grbm_select(0, 0, 0, 0);
      if (rp == w) break;
      usleep(2000);
   }
   printf("kiq submit: %u dw, rptr %u / wptr %u\n", n, rp, w);
   return rp == w ? 0 : -1;
}
// navi31: map the MES scheduler queue (me3 pipe0) through the KIQ exactly as Linux did (its KIQ ring dw 512:
// MAP_QUEUES 34080000 doorbell 0x58), then SET_HW_RESOURCES + QUERY_SCHEDULER_STATUS on the scheduler ring.
// Needs a KIQ that already got SET_RESOURCES (run `kiqres` first in the same boot).
static void schedkiq_test(void)
{
   mqd_build(SCH_MQD, SCH_RING, SCH_WB, SCH_EOP, 1, 0x16); sched_mqd_fixup();
   for (int i = 0; i < SCH_RING_DW; i++) vw(SCH_RING + i * 4, 0);
   for (int i = 0; i < 32; i++) vw(SCH_WB + i * 4, 0);
   // The MQD field of MAP_QUEUES is an MC address (gfx11_kiq_map_queues passes amdgpu_bo_gpu_offset), not a
   // VA. With the VA the MES stored a pointer it could not resolve, so it never created the scheduler HQD
   // (active=0, pq_base=0) and the SET_HW_RESOURCES that followed wedged the CP.
   const uint32_t map[] = {0xC005A200, (2u << 18) | (5u << 26) | (1u << 29), 0x16u << 2,
                           (uint32_t)MC(SCH_MQD), (uint32_t)(MC(SCH_MQD) >> 32),
                           (uint32_t)VA(SCH_WB + 16), (uint32_t)(VA(SCH_WB + 16) >> 32)};
   if (kiq_submit(map, 7)) { printf("schedkiq: KIQ did not consume MAP_QUEUES\n"); return; }
   usleep(20000);
   grbm_select(3, 0, 0, 0);
   printf("sched hqd after MAP_QUEUES: active=%u pq_base=0x%x dbctl=0x%x\n", rd(SEG0(0x1fab)), rd(SEG0(0x1fb1)), rd(SEG0(0x1fb8)));
   grbm_select(0, 0, 0, 0);
   skip_hqd = 1;
   sched_test();
}
static void kiqreg_test(void)
{
   uint32_t t = rd(SEG1(0x98a)) & 0xffffff00;
   t |= (3u << 5) | (1u << 3);
   wr(SEG1(0x98a), t); wr(SEG1(0x98a), t | 0x80);
   mqd_build(KIQ_MQD, KIQ_RING, KIQ_WB, KIQ_EOP, 1, 0x18);
   for (int i = 0; i < 1024; i++) vw(KIQ_RING + i * 4, 0);
   for (int i = 0; i < 32; i++) vw(KIQ_WB + i * 4, 0);
   uint32_t pq = 0xd8308011;
   hqd_program(3, 1, KIQ_MQD, KIQ_RING, KIQ_WB, pq, (0x18u << 2) | (1u << 30));
   wr(SEG1(0x2040), 0xCAFEDEAD);
   vw(KIQ_RING + 0, 0xC0017900); vw(KIQ_RING + 4, 0x40); vw(KIQ_RING + 8, 0xDEADBEEF);
   vw(KIQ_WB + 16, 3); vw(KIQ_WB + 20, 0);
   db(0x18 * 4, 3);
   for (int i = 0; i < 20; i++) { usleep(10000); if (rd(SEG1(0x2040)) == 0xDEADBEEF) break; }
   grbm_select(3, 1, 0, 0);
   printf("kiqreg: scratch0=0x%08x (expect deadbeef) rptr=%u wb_rptr=0x%x\n", rd(SEG1(0x2040)), rd(SEG0(0x1fb3)), vr(KIQ_WB));
   grbm_select(0, 0, 0, 0);
}

// SDMA0 queue0: WRITE_LINEAR of one dword to an arbitrary 64-bit address (ring in VRAM at 0x2000000, MC addressed)
static void sdma_write(uint64_t dst, uint32_t val)
{
   const uint64_t ring = 0x2000000ull;   // navi31 FIX: ring base MUST match RB_BASE (0x80020000 = VRAM off 0x2000000)
                                         // and the rptr/wptr-poll addrs at 0x2001000/0x2001010. (32MB, clear of the 8MB FB.)
   for (int i = 0; i < 8; i++) vw(ring + i * 4, 0);
   vw(ring + 0, 0x00000002); vw(ring + 4, (uint32_t)dst); vw(ring + 8, (uint32_t)(dst >> 32)); vw(ring + 12, 0); vw(ring + 16, val);
   wr(0x12E0, 0x00040800);
   wr(0x12E3, 0); wr(0x12E4, 0); wr(0x12E5, 0); wr(0x12E6, 0);
   wr(0x1313, 0x02001010); wr(0x1312, 0x80); wr(0x12E8, 0x80); wr(0x12E9, 0x02001000);
   wr(0x12E0, 0x00841814); wr(0x12E1, 0x80020000); wr(0x12E2, 0);
   wr(0x1315, 1); wr(0x12E5, 0); wr(0x12E6, 0); wr(0x1315, 0);
   wr(0xF89A, 0x08084000);
   wr(0x12E0, 0x00841815); wr(0x12EA, 0x101);
   vw(0x2001010, 0x14);
   wr(0x1315, 1); wr(0x12E5, 0x14); wr(0x12E6, 0); wr(0x1315, 0);
   usleep(200000);
}
static void sdma_fill(uint64_t dst, uint32_t val, uint32_t bytes)   // CONSTANT_FILL (dword) up to ~4MB/call; dst = MC addr
{
   const uint64_t ring = 0x2000000ull;
   for (int i = 0; i < 8; i++) vw(ring + i * 4, 0);
   vw(ring + 0, 0x8000000bu);          // op 0x0b CONSTANT_FILL, fillsize=2 (4-byte/dword)
   vw(ring + 4, (uint32_t)dst); vw(ring + 8, (uint32_t)(dst >> 32));
   vw(ring + 12, val);                 // fill data
   vw(ring + 16, bytes - 1);           // count = bytes - 1
   wr(0x12E0, 0x00040800);
   wr(0x12E3, 0); wr(0x12E4, 0); wr(0x12E5, 0); wr(0x12E6, 0);
   wr(0x1313, 0x02001010); wr(0x1312, 0x80); wr(0x12E8, 0x80); wr(0x12E9, 0x02001000);
   wr(0x12E0, 0x00841814); wr(0x12E1, 0x80020000); wr(0x12E2, 0);
   wr(0x1315, 1); wr(0x12E5, 0); wr(0x12E6, 0); wr(0x1315, 0);
   wr(0xF89A, 0x08084000);
   wr(0x12E0, 0x00841815); wr(0x12EA, 0x101);
   vw(0x2001010, 0x14);
   wr(0x1315, 1); wr(0x12E5, 0x14); wr(0x12E6, 0); wr(0x1315, 0);
   usleep(30000);
}
static void sdma_copy(uint64_t dst, uint64_t src, uint32_t bytes)   // COPY_LINEAR (7 dwords); dst/src = MC addrs, <=~1MB/call
{
   const uint64_t ring = 0x2000000ull;
   for (int i = 0; i < 12; i++) vw(ring + i * 4, 0);
   vw(ring + 0, 0x00000001u);          // op 0x01 COPY, sub_op 0 LINEAR
   vw(ring + 4, bytes - 1);            // count = bytes - 1
   vw(ring + 8, 0);                    // params
   vw(ring + 12, (uint32_t)src); vw(ring + 16, (uint32_t)(src >> 32));
   vw(ring + 20, (uint32_t)dst); vw(ring + 24, (uint32_t)(dst >> 32));
   wr(0x12E0, 0x00040800);
   wr(0x12E3, 0); wr(0x12E4, 0); wr(0x12E5, 0); wr(0x12E6, 0);
   wr(0x1313, 0x02001010); wr(0x1312, 0x80); wr(0x12E8, 0x80); wr(0x12E9, 0x02001000);
   wr(0x12E0, 0x00841814); wr(0x12E1, 0x80020000); wr(0x12E2, 0);
   wr(0x1315, 1); wr(0x12E5, 0); wr(0x12E6, 0); wr(0x1315, 0);
   wr(0xF89A, 0x08084000);
   wr(0x12E0, 0x00841815); wr(0x12EA, 0x101);
   vw(0x2001010, 0x1C);
   wr(0x1315, 1); wr(0x12E5, 0x1C); wr(0x12E6, 0); wr(0x1315, 0);
   usleep(30000);
}

static void sdma_sys_test(void)
{
   uint64_t phys = 0, len = 0, v = 0;
   if (sm(1, 1 << 20, 0, &phys, &len)) { printf("sysmem alloc failed\n"); return; }
   // MMHUB AGP aperture identity (BASE=0, BOT=0, TOP=0x1ff): MC address == system physical address for SDMA
   wr(0x1a8f0, 0); wr(0x1a8ef, 0); wr(0x1a8ee, 0x000001ff);
   uint64_t tgt = phys + 0x1000;
   sdma_write(tgt, 0x5D5D5D5D);
   uint64_t pp; sm(2, 0x1000, 0, &pp, &v);
   printf("SDMA write to system phys 0x%llx via MMHUB AGP: 0x%08x (want 5d5d5d5d)\n", (unsigned long long)tgt, (uint32_t)v);
   sdma_write(MC(TESTBUF + 0x200), 0x6E6E6E6E);
   printf("SDMA control write to VRAM: 0x%08x (want 6e6e6e6e)\n", vr(TESTBUF + 0x200));
}

int main(int argc, char **argv)
{
   g_argc = argc; g_argv = argv;
   if (open_kext()) return 1;
   if (argc < 2) { fprintf(stderr, "mesboot gart|mes|kiq|all\n"); return 2; }
   int all = !strcmp(argv[1], "all");
   if (getenv("MESBOOT_MC") && *getenv("MESBOOT_MC") == '1') use_mc = 1;
   // navi31: sudo drops the environment, so options can also follow the command, e.g. `mesboot kiqdb mc`
   for (int i = 2; i < argc; i++) if (!strcmp(argv[i], "mc")) use_mc = 1;
   if (!strcmp(argv[1], "db") && argc >= 4) { printf("db +0x%llx <- %llu kr=0x%x\n", strtoull(argv[2], NULL, 0), strtoull(argv[3], NULL, 0), db(strtoull(argv[2], NULL, 0), strtoull(argv[3], NULL, 0))); return 0; }
   if (!strcmp(argv[1], "dcn")) {   // navi31 display probe (READ-ONLY): OTG0 + HUBPREQ0 flip regs via DCN segment-2 base
      uint32_t seg2 = 0x34c0; for (int i = 2; i < argc; i++) if (!strncmp(argv[i], "seg=", 4)) seg2 = (uint32_t)strtoul(argv[i] + 4, NULL, 0);
      uint32_t ctl = rd(seg2 + 0x1b41), ht = rd(seg2 + 0x1b2a) + 1, vt = rd(seg2 + 0x1b2f) + 1;
      uint32_t c0 = rd(seg2 + 0x1b4c) & 0xffffff; usleep(200000); uint32_t c1 = rd(seg2 + 0x1b4c) & 0xffffff;
      uint32_t fr = (c1 - c0) & 0xffffff;
      printf("OTG0 (seg2=%#x): ctl=%#010x master_en=%u H_total=%u V_total=%u frames/200ms=%u (~%u Hz)\n", seg2, ctl, ctl & 1, ht, vt, fr, fr * 5);
      printf("HUBPREQ0 flip: SURFACE_ADDR=%#010x _HIGH=%#010x FLIP_CONTROL=%#010x FLIP_INT=%#010x\n", rd(seg2 + 0x060a), rd(seg2 + 0x060b), rd(seg2 + 0x061b), rd(seg2 + 0x061f));
      return 0;
   }
   if (!strcmp(argv[1], "dcnflip")) {   // navi31: NO-OP flip test -- rewrite the SAME scanout address (HIGH then LOW arms it). Invisible if correct.
      uint32_t seg2 = 0x34c0; for (int i = 2; i < argc; i++) if (!strncmp(argv[i], "seg=", 4)) seg2 = (uint32_t)strtoul(argv[i] + 4, NULL, 0);
      uint32_t hi = rd(seg2 + 0x060b), lo = rd(seg2 + 0x060a), fc = rd(seg2 + 0x061b);
      printf("before: ADDR=%#010x HIGH=%#010x FLIP_CONTROL=%#010x (lock=%u pending=%u)\n", lo, hi, fc, fc & 1, (fc >> 8) & 1);
      uint32_t rbh = wr(seg2 + 0x060b, hi);      // same high
      uint32_t rbl = wr(seg2 + 0x060a, lo);      // same low -> arms the (no-op) flip
      printf("wrote same addr: HIGH readback=%#010x  LOW readback=%#010x\n", rbh, rbl);
      uint32_t fc2 = 0; int ms;
      for (ms = 0; ms < 200; ms++) { fc2 = rd(seg2 + 0x061b); if (!((fc2 >> 8) & 1)) break; usleep(1000); }
      uint32_t fi = rd(seg2 + 0x061f);
      printf("after:  FLIP_CONTROL=%#010x pending=%u (cleared in %d ms)  FLIP_INT=%#010x occurred=%u\n", fc2, (fc2 >> 8) & 1, ms, fi, (fi >> 16) & 1);
      return 0;
   }
   if (!strcmp(argv[1], "fbtest")) {   // navi31: draw a colored band into the live GOP framebuffer (BAR0 off 0), then RESTORE. Reversible.
      static uint32_t save[300000];
      uint32_t n = 40000, color = 0xFFFF2020; uint32_t secs = 5;
      for (int i = 2; i < argc; i++) {
         if (!strncmp(argv[i], "n=", 2)) n = (uint32_t)strtoul(argv[i] + 2, NULL, 0);
         else if (!strncmp(argv[i], "color=", 6)) color = (uint32_t)strtoul(argv[i] + 6, NULL, 0);
         else if (!strncmp(argv[i], "secs=", 5)) secs = (uint32_t)strtoul(argv[i] + 5, NULL, 0);
      }
      if (n > 300000) n = 300000;
      printf("FB sample before: [0]=%#010x [4]=%#010x [%u]=%#010x  (SURFACE_PITCH reg=%#x)\n", vr(0), vr(4), (n/2)*4, vr((uint64_t)(n/2) * 4), rd(0x34c0 + 0x0607));
      for (uint32_t i = 0; i < n; i++) save[i] = vr((uint64_t)i * 4);
      for (uint32_t i = 0; i < n; i++) vw((uint64_t)i * 4, color);
      hdp_flush();
      printf("drew %u px color %#010x at top of framebuffer -- LOOK AT THE SCREEN -- restoring in %u s\n", n, color, secs);
      usleep(secs * 1000000u);
      for (uint32_t i = 0; i < n; i++) vw((uint64_t)i * 4, save[i]);
      hdp_flush();
      printf("restored original pixels.\n");
      return 0;
   }
   if (!strcmp(argv[1], "dump")) mesdump();
   if (all || !strcmp(argv[1], "gart")) gart();
   if (all || !strcmp(argv[1], "mes")) mes_start();
   if (!strcmp(argv[1], "mesfull")) mes_full_init();
   if (!strcmp(argv[1], "mesreg")) mes_write_reg_test();
   if (!strcmp(argv[1], "hqdscan")) hqd_scan();
   if (!strcmp(argv[1], "computeq")) compute_direct();
   if (!strcmp(argv[1], "ibtest")) ib_test();
   if (!strcmp(argv[1], "ctx8")) ctx8_flat();
   if (!strcmp(argv[1], "ib8")) ib8();
   if (!strcmp(argv[1], "ib8h")) ib8h();
   if (!strcmp(argv[1], "uconfig")) pm4_uconfig();
   if (!strcmp(argv[1], "me0q")) gfx_queue_test();   // ME0/pipe0 directly, no MES
   if (!strcmp(argv[1], "me0rb")) gfx_rb0_test();   // legacy CP_RB0 gfx ring
   if (!strcmp(argv[1], "mestrap")) mes_trap_dump();
   if (!strcmp(argv[1], "ib8hs")) ib8hs();
   if (!strcmp(argv[1], "ibtest2")) ib_test2();
   if (!strcmp(argv[1], "ibtest3")) ib_test3();
   if (!strcmp(argv[1], "ibsys")) ib_test_sys();
   if (!strcmp(argv[1], "scan") && argc >= 5) reg_scan((uint32_t)strtoul(argv[2],0,0), (uint32_t)strtoul(argv[3],0,0), (uint32_t)strtoul(argv[4],0,0), argc >= 6 ? (uint32_t)strtoul(argv[5],0,0) : 0xffffffffu);
   if (!strcmp(argv[1], "kiqdb")) { use_db = 1; kiq_test(); }
   if (!strcmp(argv[1], "kiqsched")) { kiq_init_only(); sched_test(); }
   if (!strcmp(argv[1], "claim")) {   // GPU is up: let PhxAccel (the Metal accelerator) start on it
      uint64_t in[1] = {1};
      printf("claim -> kr 0x%x\n", IOConnectCallScalarMethod(conn, 113, in, 1, NULL, NULL));
   }
   if (!strcmp(argv[1], "vm8")) vm8_test();   // navi31: reuse the compute queue `cs` left at dw 8
   if (!strcmp(argv[1], "gfxq")) { // navi31: MES already running (after schedkiq): ADD_QUEUE a GFX queue and adopt it
      sch_wptr = vr(SCH_WB + 16); sch_seq = vr(SCH_WB + 32);
      printf("sched ring: wptr=%u seq=%llu\n", sch_wptr, (unsigned long long)sch_seq);
      setenv("ADOPT_GFX", "1", 1);
      gfx_via_mes();
   }
   if (!strcmp(argv[1], "cs")) {    // navi31: MES already running (after schedkiq): ADD_QUEUE a compute queue on its ring
      sch_wptr = vr(SCH_WB + 16); sch_seq = vr(SCH_WB + 32);
      printf("sched ring: wptr=%u seq=%llu\n", sch_wptr, (unsigned long long)sch_seq);
      compute_via_mes();
   }
   if (!strcmp(argv[1], "gfx")) {   // MES already running (after kiqsched): continue its ring where the last run left it
      sch_wptr = vr(SCH_WB + 16); sch_seq = vr(SCH_WB + 32);
      printf("sched ring: wptr=%u seq=%llu\n", sch_wptr, (unsigned long long)sch_seq);
      gfx_via_mes();
   }
   if (!strcmp(argv[1], "sdmaw")) {   // navi31 SDMA validation: WRITE_LINEAR one dword to VRAM, read back
      uint64_t off = TESTBUF + 0x200; uint32_t val = 0x6e6e6e6e;
      if (argc >= 3) off = strtoull(argv[2], NULL, 0);
      if (argc >= 4) val = (uint32_t)strtoul(argv[3], NULL, 0);
      vw(off, 0x11111111);
      uint32_t before = vr(off);
      sdma_write(MC(off), val);
      uint32_t after = vr(off);
      printf("SDMA WRITE_LINEAR VRAM off 0x%llx: before=0x%08x after=0x%08x (want 0x%08x) -> %s\n",
             (unsigned long long)off, before, after, val, after == val ? "OK" : "FAIL");
      return 0;
   }
   if (!strcmp(argv[1], "sdmafill")) {   // navi31: CONSTANT_FILL a VRAM region (chunked). off=,bytes=,val=
      uint64_t off = 0xA120000ull, bytes = 0x1000ull; uint32_t val = 0x12345678u;
      for (int i = 2; i < argc; i++) {
         if (!strncmp(argv[i], "off=", 4)) off = strtoull(argv[i] + 4, NULL, 0);
         else if (!strncmp(argv[i], "bytes=", 6)) bytes = strtoull(argv[i] + 6, NULL, 0);
         else if (!strncmp(argv[i], "val=", 4)) val = (uint32_t)strtoul(argv[i] + 4, NULL, 0);
      }
      uint64_t done = 0; const uint32_t chunk = 0x100000;
      while (done < bytes) { uint32_t n = (bytes - done > chunk) ? chunk : (uint32_t)(bytes - done); sdma_fill(MC(off + done), val, n); done += n; }
      printf("SDMA CONSTANT_FILL: 0x%llx bytes @ VRAM off 0x%llx = 0x%08x. readback [0]=0x%08x [mid]=0x%08x [end]=0x%08x\n",
             (unsigned long long)bytes, (unsigned long long)off, val, vr(off), vr(off + bytes/2), vr(off + bytes - 4));
      return 0;
   }
   if (!strcmp(argv[1], "fbflip")) {   // navi31: SDMA-fill a framebuffer in VRAM, FLIP the scanout to it, then restore. VISIBLE.
      uint32_t seg2 = 0x34c0; uint64_t off = 0x4000000ull, fbytes = 0x900000ull; uint32_t color = 0xff2060ff; uint32_t secs = 4;
      for (int i = 2; i < argc; i++) {
         if (!strncmp(argv[i], "off=", 4)) off = strtoull(argv[i] + 4, NULL, 0);
         else if (!strncmp(argv[i], "color=", 6)) color = (uint32_t)strtoul(argv[i] + 6, NULL, 0);
         else if (!strncmp(argv[i], "secs=", 5)) secs = (uint32_t)strtoul(argv[i] + 5, NULL, 0);
      }
      uint32_t savLo = rd(seg2 + 0x060a), savHi = rd(seg2 + 0x060b);
      printf("saved scanout: HIGH=%#010x LOW=%#010x\n", savHi, savLo);
      uint64_t done = 0; const uint32_t chunk = 0x100000;
      while (done < fbytes) { uint32_t n = (fbytes - done > chunk) ? chunk : (uint32_t)(fbytes - done); sdma_fill(MC(off + done), color, n); done += n; }
      printf("filled 0x%llx bytes @ off 0x%llx = 0x%08x (readback [0]=0x%08x). FLIPPING now...\n", (unsigned long long)fbytes, (unsigned long long)off, color, vr(off));
      uint64_t mc = MC(off);
      wr(seg2 + 0x060b, (uint32_t)(mc >> 32));   // SURFACE_ADDRESS_HIGH
      wr(seg2 + 0x060a, (uint32_t)mc);           // SURFACE_ADDRESS (low last -> arms flip)
      printf(">>> LOOK AT THE SCREEN: solid color for %u s <<<\n", secs);
      usleep(secs * 1000000u);
      wr(seg2 + 0x060b, savHi);
      wr(seg2 + 0x060a, savLo);
      printf("restored original scanout (HIGH=%#010x LOW=%#010x).\n", savHi, savLo);
      return 0;
   }
   if (!strcmp(argv[1], "fbcomp")) {   // navi31: COPY live desktop -> our buffer, draw overlay band, FLIP, restore. Compositing demo.
      uint32_t seg2 = 0x34c0; uint64_t off = 0x4000000ull; uint32_t secs = 5, overlay = 0xff30ff30;
      uint32_t pitch = 1920u * 4u, h = 1080u; uint64_t fbytes = (uint64_t)pitch * h;
      for (int i = 2; i < argc; i++) { if (!strncmp(argv[i],"secs=",5)) secs=(uint32_t)strtoul(argv[i]+5,0,0); else if (!strncmp(argv[i],"overlay=",8)) overlay=(uint32_t)strtoul(argv[i]+8,0,0); }
      uint32_t savLo = rd(seg2 + 0x060a), savHi = rd(seg2 + 0x060b);
      printf("copying live desktop (0x%llx bytes) FB off0 -> buffer off 0x%llx via SDMA...\n", (unsigned long long)fbytes, (unsigned long long)off);
      uint64_t done = 0; const uint32_t chunk = 0x100000;
      while (done < fbytes) { uint32_t n=(fbytes-done>chunk)?chunk:(uint32_t)(fbytes-done); sdma_copy(MC(off+done), MC(0+done), n); done+=n; }
      // overlay: a green band over rows 440..520
      uint64_t band_off = off + (uint64_t)440 * pitch; sdma_fill(MC(band_off), overlay, 80u * pitch);
      printf("copied + drew overlay band. readback buf[0]=0x%08x (desktop) band=0x%08x. FLIPPING...\n", vr(off), vr(band_off));
      uint64_t mc = MC(off);
      wr(seg2 + 0x060b, (uint32_t)(mc >> 32)); wr(seg2 + 0x060a, (uint32_t)mc);
      printf(">>> LOOK: desktop + green band for %u s <<<\n", secs);
      usleep(secs * 1000000u);
      wr(seg2 + 0x060b, savHi); wr(seg2 + 0x060a, savLo);
      printf("restored.\n");
      return 0;
   }
   if (!strcmp(argv[1], "sdmasys")) sdma_sys_test();
   if (!strcmp(argv[1], "kiqreg")) kiqreg_test();
   if (!strcmp(argv[1], "kiqres")) kiqres_test();
   if (!strcmp(argv[1], "schedkiq")) schedkiq_test();
   if (!strcmp(argv[1], "mec")) mec_test();
   if (!strcmp(argv[1], "sched")) sched_test();
   if (!strcmp(argv[1], "viakiq")) { use_db = 1; kiq_test(); skip_hqd = 1; sched_test(); }
   if (all || !strcmp(argv[1], "kiq")) kiq_test();
   return 0;
}
