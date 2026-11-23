# GPU hang under clpeak's MPS / CoreML pass (2026-11-23 09:15, XTX machine, b156)

What happened, from the unified log, `/var/log/phxboot.log` and the daemon's hang dump
(`/var/log/phxhang-20261123-091740/`, not in git: it holds process lists).

## Timeline

- 09:12-09:13: the benchmark set's first pass (VRAM BOs **off**): vkbench compute / bandwidth / submit, then metalbench
  render / compute / bandwidth (09:13:42, :48, :53). All finished.
- 09:13:58: clpeak (`--devices metal:0,coreml:0`, pid 79284) starts; the plugin allow-lists it (60 s GPU wait budget).
- 09:14:03-09:14:50: clpeak's MPS kernels are translated and run (`reduce_multiple_passes_axes_max`,
  `ndArrayLUTGEMV_xReduce`; "translation refuses it" for one unresolved linked-function variant; "align guard: built
  SAFE").
- 09:14:26: WindowServer's slowest submit 2118 ms; 09:15:44 2488 ms; 09:15:46 2651 ms.
- 09:15:47: clpeak's last line (compiling `MPSNDArrayI4Quantized::NDArrayDequantizeInt4`, the int4 GEMV path).
- 09:15:58 onwards: WindowServer's fence "still pending after 10000 / 20000 ... ms", 50004 ms at 09:16:38.
- 09:16:52 and 09:17:32: WindowServer killed by its userspace watchdog (two `.ips`); the daemon reverted to GOP
  ("WindowServer hung 2x on GPU") and the machine came back at 09:18:43 on the safe desktop (`~/gpu/phxboot-off`,
  `~/gpu/phxboot-failed`).

## State at the dump (09:17:41)

- seq emitted 347436, retired 347422; the oldest unretired submission is clpeak's (VMID 10, last seq 347423). Every
  other context (WindowServer x3, T3 Code) waits behind it on the one gfx ring.
- GRBM_STATUS 0xa840302c (busy), CP_STAT 0x8003a600. **No VM fault** (fault status 0, the page-table walk has nothing
  to report), so this is a shader that does not finish, not a bad address.
- unmap/free timeouts 4; TLB invalidation timeouts 0.

## Reading

- **VRAM BOs were off** during the hang (the benchmark set runs off first, and only one clpeak ran; the 09:10:57 clpeak
  was a device listing that exited at once). So the hang is not the b156 VRAM path (one sample; the "on" pass never ran).
- The hang is in the MPS / CoreML kernels clpeak drives (Apple's MPSNDArray metallibs, translated to SPIR-V). A kernel
  that runs for tens of seconds, or never ends, holds the only gfx ring, and with no compute preemption WindowServer
  starves. Which kernel: unknown (no per-dispatch trace). Leading guess: the int4 / LUT GEMV kernels, where the
  translator left linked functions unresolved (unverified).
- Before this run, MPS was not part of any test here. metalbench and vkbench were fine in the same pass.

## What follows

- Benchmarks: clpeak is out of the set for now. Its `metal:0` device also runs MPS (`src/metal/mtl_blas.mm`:
  MPSMatrixMultiplication; the MPSMatrix metallib was loaded at 09:14:03), so the hanging kernel may be there too.
  `~/src/bench/benchset.sh` runs vkbench and metalbench only.
- Plugin / translator (thread 03 or a stability item): find the MPS kernel that does not finish; a GPU job that
  exceeds a budget should be lost for that process only, not take WindowServer down (today the ring has no reset path).
