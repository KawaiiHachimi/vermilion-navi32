# 2026-11-22: CPU temperature and power on the XTX machine (Ryzen 9 7900X3D, Zen 4)

Before: no AMD sensor kext; VirtualSMC exposes 65 public SMC keys, none for the CPU (`smcread -s`, VirtualSMC
1.3.8's tool). `powermetrics` CPU power and thermal figures are Intel counters and mean nothing here.

## Candidates (source and binaries read 2026-11-22)

| Kext | What it does at load, by default | Verdict |
|---|---|---|
| trulyspinach/SMCAMDProcessor 0.7.2f1: `AMDRyzenCPUPowerManagement` + `SMCAMDProcessor` (BSD-3, last release 2024-03) | Unregisters the kernel's `pmDispatch` and installs its own idle loop (`MachineIdle`) and `pmChooseCPU`; `pmRyzen_init_PState` **rewrites the P1 P-state definition MSR** (`C001_0065`) with a FID computed by the Zen 2/3 FID/DID formula; writes `C001_0073` (C-state I/O base) = `0xF0` and sets HWCR bit 30 on every core; switches P-states by load from then on; probes the SuperIO chip | **rejected**: it is a CPU power-management replacement, not a sensor; the P-state rewrite is a frequency/voltage table change computed for older CPUs |
| kodeaqua/SMCAMDProcessor-personal 3.34.x (the "Tahoe" fork) | All of the above, plus `AMDGPU.cpp`: maps every AMD GPU's register BAR and reads/writes its `PCIE_INDEX`/`PCIE_DATA` registers on a timer, unconditionally; fan-curve control | **rejected**: GPU register access races Vermilion's and breaks the GPU-side rule |
| **macos86/SMCProcessorAMD 1.0.1** (BSD-3, 2023-06; repo active 2026-07) | Attaches to ACPI `Processor` index 0 (our `SSDT-CPUR` `PR00`, matched like Apple's `AppleACPICPU`). Once a second: package temperature, package energy, every core's P-state status. Publishes VirtualSMC keys (`TC0E/F/P/T/p`, `PCPR`, `PCPT`, `PCTR`, `FCPE`, per-core `FCxE`) | **chosen**: read-only |

## SMCProcessorAMD 1.0.1 in detail

- Release binary `SMCProcessorAMD-1.0.1-RELEASE.zip`, sha256 `2622e54c...`, pinned in `efi/components.json`.
  Checked by disassembly (`otool -tV`): no `wrmsr`, no port I/O (`out*`); MSR access only through the kernel's
  `rdmsr_carefully` (which the Tahoe kernel exports: `nm /System/Library/Kernels/kernel`); exactly one PCI config
  write and one read.
- That write is the SMN index: host bridge `00:00.0` offset `0x60` = `0x00059800`, then the value from `0x64`. Linux
  does the same (`amd_smn_read`, `k10temp` `ZEN_REPORTED_TEMP_CTRL_BASE`). Nothing else on macOS uses that pair;
  Vermilion doesn't (grep of `src/` and `tools/` for SMN, `0x59800`, RAPL: nothing).
- Temperature = Tctl (bits 31:21 x 0.125 C, minus 49 C when the range bit is set). Its offset table has entries only
  for family 17h, so none applies to the 7900X3D, which is right for Zen 4 (Linux `k10temp` uses no offset there).
- Package power = delta of `C001_029B` (package energy) x 2^-16 J / time. The unit is hardcoded instead of read from
  `C001_0299` *(unverified that Zen 4's unit is 2^-16 J: sanity-check the readings)*. When the 32-bit counter wraps
  (about every 20 min at 50 W), one sample is garbage (the code subtracts from `UINT64_MAX`): ignore single spikes.
- Core clocks from `C001_0293` with FID/DID x 200 (Zen 5 has its own branch; Zen 4 uses FID/DID) *(unverified)*.
- Costs one `mp_rendezvous_no_intrs` (an IPI to all 24 threads) per second for the clocks.
- Tested by its author up to Sonoma; nobody reports Tahoe *(unverified)*. If it fails to link or start, the result
  is no sensors, not a changed CPU.

## What this does not change

CPU power management stays as it is: `DummyPowerManagement`, no OS P-state control; the CPU's own firmware (CPPC /
hardware P-states, boost) decides clocks and idle. The sensor makes that measurable for the first time; whether it
idles well is a question for thread 07 / 00.1 once there are numbers.

## Result (boot 2026-11-22 20:10, XTX machine)

- `as.lorys89.SMCProcessorAMD` 1.0.1 loaded; 10 new SMC keys (80 public keys, was 70): `TC0E/F/P/T/p` (Tctl),
  `TC0G/J` (0), `PCPR/PCPT/PCTR` (package power). The clock keys (`FCPE`, `FCxE`) are not there *(why not: unverified)*.
- Idle desktop, 1-4 min after boot, 1440p, GPU desktop up: **53-57 C, 39-41 W** package.
- 24 busy shell loops (all threads), 10 s: **85-87 C, 120-121 W**; back to ~40 W within seconds. 120 W all-core and
  a temperature close to the 7900X3D's 89 C limit are what this CPU does on Linux and Windows reviews too, so the
  hardcoded 2^-16 J energy unit looks right *(not compared with a Linux reading on this machine yet)*.
- Nothing else changed: USB map, Wi-Fi, the GPU daemon (`SUCCESS`, b150, 2560x1440, Metal).
- Read it with `python3 tools/sensors/cpusensors.py [-n N -i S]` (decodes `smcread -s`; VirtualSMC 1.3.8's
  `smcread` is in `~/.local/bin` on the machine).
- Note for thread 07 / 00.1: ~40 W package at an idle desktop is on the high side for a 7900X3D (the IO die and
  memory clocks dominate on Zen 4 desktop parts); compare with Linux before calling it a problem.
