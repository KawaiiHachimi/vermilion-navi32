# Handoff: one Linux boot for every reference number (written 2026-11-23, thread 00)

For the agent session on the Linux side (Omarchy) of the XTX machine, and for the owner. One boot collects everything
the series threads need from Linux:
- GPU power, clocks and SMU features (thread 02);
- CPU clock, idle power and C-states (02, the old thread 07);
- storage throughput and LUKS discards (old thread 03);
- the Super I/O chip for fan readings (02).

**All of it is read-only.** The rules of [runbooks.md](runbooks.md), "Linux sessions", apply:
- no register writes;
- no EFI or `efibootmgr` change, nothing under `/boot/EFI`;
- root only through T3's `request_secret` with file delivery, never in chat.

Results:
- Raw output stays on Linux, in `~/vermilion-captures/linux-ref-<date>/`. `dmesg` and `lspci` lines can carry serials
  and MACs: mask them before quoting.
- What goes to macOS is **one findings note** with the numbers, `.agents/findings/<date>-linux-reference.md`,
  committed on a branch `ownback/linux-reference` and pushed to `fork`. Thread 02 merges it and fills the Linux
  column of PROJECT_KNOWLEDGE's performance record.

Record for every block: the date and time, minutes since boot, the display mode (Hyprland `hyprctl monitors`: mode,
rate, HDR, VRR), and what else is running. Measure idle at the desktop with nothing else open.

## 0. Setup (no root)

```sh
cd ~/Projects/vermilion && git fetch fork && git checkout --detach fork/ownback/perf-power   # thread 02's branch (vkbench); fork/main once 02 merged
OUT=~/vermilion-captures/linux-ref-$(date +%Y%m%d-%H%M); mkdir -p "$OUT"
cc -O2 -o "$OUT/cpuclk" tools/sensors/cpuclk.c -lpthread
uname -r > "$OUT/kernel.txt"; cat /sys/class/dmi/id/bios_version >> "$OUT/kernel.txt"   # expect 4005; Linux must be booted directly, not through OpenCore
for c in /sys/devices/system/cpu/cpu*/cache/index3/shared_cpu_list; do echo "$c $(cat $c)"; done | sort -u -k2 > "$OUT/ccd.txt"
```

`ccd.txt` shows which CPUs share an L3, so which belong to which CCD. On the 7900X3D, one CCD has the 3D V-Cache and
the other is the frequency CCD (rated 5.6 GHz). Note which list is which: `lscpu -C` shows the L3 sizes, 96 MB vs
32 MB.

## 1. GPU: power, clocks, SMU features (thread 02)

The card is `card1` or `card0`: use the one whose `device/device` reads `0x744c`. Run this at 2560x1440 **240 Hz HDR**
idle, and again at **60 Hz SDR** idle. Switch the mode in Hyprland and wait 2 minutes after each switch:

```sh
D=$(for c in /sys/class/drm/card*/device; do [ "$(cat $c/device)" = 0x744c ] && echo $c; done | head -1)
for f in power_dpm_force_performance_level pp_dpm_sclk pp_dpm_mclk pp_dpm_fclk pp_dpm_socclk pp_features \
         hwmon/hwmon*/power1_average hwmon/hwmon*/temp*_input hwmon/hwmon*/fan1_input mem_busy_percent gpu_busy_percent; do
  echo "== $f"; cat $D/$f 2>&1; done > "$OUT/gpu-240hdr-idle.txt"       # name the file per mode
for i in $(seq 30); do cat $D/hwmon/hwmon*/power1_average; sleep 1; done > "$OUT/gpu-240hdr-power-30s.txt"
```

As root (debugfs), once per mode:

```sh
sudo -S -p '' sh -c 'cat /sys/kernel/debug/dri/*/amdgpu_pm_info' < "$file" > "$OUT/pm_info-240hdr.txt"
sudo -S -p '' sh -c 'cat /sys/kernel/debug/dri/*/amdgpu_gfxoff_residency 2>&1; cat /sys/kernel/debug/dri/*/amdgpu_gfxoff_count 2>&1' < "$file" > "$OUT/gfxoff.txt"
```

What the macOS side needs from it:
- Is the **memory clock (UCLK / `pp_dpm_mclk`) at its lowest level at idle**, or pinned, as macOS pins it at 456 MHz?
- Board power at idle. macOS reads 62-70 W at 240 Hz HDR.
- FCLK and SOCCLK at idle.
- GFXOFF residency.
- The `pp_features` mask. Compare it with macOS's SMU feature masks `0x71ffffff` / `0x0003ebb8`.

Also, once per mode (read-only, no root): `cat $D/pp_dpm_dcefclk $D/pp_od_clk_voltage $D/pp_power_profile_mode
2>&1 > "$OUT/gpu-extra-<mode>.txt"`, and every second for 30 s `grep -H . $D/pp_dpm_mclk $D/pp_dpm_fclk` (does UCLK move
at idle, or sit at one level?).

### 1b. The same shader through Vulkan (added 2026-11-23, thread 02)

`tools/vkbench/vkbench.c` is `metalbench`'s Mandelbrot loop as a Vulkan compute shader (the SPIR-V is checked in).
macOS ran the same binary on RADV directly, so these numbers compare the drivers, not the shader compilers:

```sh
ls /usr/include/vulkan/vulkan.h || git clone --depth 1 -b v1.3.296 https://github.com/KhronosGroup/Vulkan-Headers "$OUT/vkh"
VKH=$( [ -f /usr/include/vulkan/vulkan.h ] && echo /usr/include || echo "$OUT/vkh/include" )
cc -O2 -I"$VKH" -o "$OUT/vkbench" tools/vkbench/vkbench.c -ldl -lm
cd "$OUT"; for t in "compute 20 64" "compute 20 2000" "submit 10" "submit 10 200 512 512" "submit 10 2000 1024 1024" \
  "bandwidth 10 256" "bandwidth 10 16"; do echo "== vkbench $t"; ./vkbench $t; sleep 5; done > vkbench.txt 2>&1
```

At 1440p 240 Hz SDR, nothing else running. During `compute 20 2000` sample `power1_average`, `pp_dpm_sclk` and
`pp_dpm_mclk` every second into `vkbench-load.txt`. macOS (2026-11-23, b155, RADV directly): `compute 64` 0.41 ms GPU,
`compute 2000` 3.95 ms GPU, `submit` median 34 us, `submit 200 512 512` median 265 us, `bandwidth 256` 44.7 GB/s.

### 1c. The XTX's device profile for RADV (thread 02)

RADV on macOS runs a canned device description (`patches/mesa/0016`, the XT's). The XTX's own comes from Linux:

```sh
cc -Wall -I/usr/include/libdrm -o "$OUT/gpuinfo" tools/capture/gpuinfo.c -ldrm_amdgpu
for n in /dev/dri/renderD*; do readlink -f /sys/class/drm/${n##*/}/device; "$OUT/gpuinfo" $n; done > "$OUT/gpuinfo.txt" 2>&1
RADV_DEBUG=info vulkaninfo --summary > "$OUT/radv-info.txt" 2>&1 || true   # if vulkaninfo is installed
```

`gpuinfo.txt` has no serials. Copy it whole into the findings note.

## 2. CPU: clock, idle power, C-states (thread 02)

```sh
C0=<first CPU of the frequency CCD from ccd.txt>; C1=<first CPU of the V-Cache CCD>
taskset -c $C0 "$OUT/cpuclk" 1 5 > "$OUT/cpuclk-1t-freqccd.txt"
taskset -c $C1 "$OUT/cpuclk" 1 5 > "$OUT/cpuclk-1t-cacheccd.txt"
"$OUT/cpuclk" 24 15 > "$OUT/cpuclk-24t.txt"
cat /sys/devices/system/cpu/cpu0/cpufreq/scaling_driver /sys/devices/system/cpu/amd_pstate/status \
    /sys/devices/system/cpu/cpufreq/boost /sys/devices/system/cpu/cpu$C0/cpufreq/cpuinfo_max_freq > "$OUT/cpufreq.txt" 2>&1
for s in /sys/devices/system/cpu/cpu$C0/cpuidle/state*; do echo "$(cat $s/name) $(cat $s/desc) usage=$(cat $s/usage) time=$(cat $s/time)"; done > "$OUT/cpuidle.txt"
```

Idle package power and C-state residency, at an idle desktop, as root:

```sh
sudo -S -p '' turbostat --quiet --show Busy%,Bzy_MHz,C1%,C2%,C3%,CPU%c1,CPU%c6,PkgWatt,PkgTmp sleep 30 < "$file" > "$OUT/turbostat-idle.txt"
```

If `turbostat` is missing, install the package (`linux-tools` / `turbostat` on Arch). The decision this feeds:
- If Linux reaches about 5.5 GHz on the frequency CCD and idles 15+ W lower than macOS (40-50 W), the cause is that
  macOS never reaches CC6. The fix is then a minimal `_CST` idle kext (thread 02).
- If Linux tops out at about 5.15 GHz too, the limit is the BIOS settings or temperature. The owner's BIOS photos
  ([OWNER-TESTS.md](OWNER-TESTS.md), thread 02) answer that.

## 3. Storage reference and LUKS discards (old thread 03)

Read-only on the LUKS partition, `p2`: all of it is real data. **The variable goes before `fio`.** The command in
the old thread 03's report had it after; this is the corrected form:

```sh
sudo -S -p '' env FIODEV=/dev/nvme0n1p2 fio --readonly --thread tools/storage/raw-read.fio < "$file" > "$OUT/fio-raw-read.txt"
sudo -S -p '' fio --readonly --name=t --filename=/dev/nvme0n1p2 --rw=read --bs=1m --iodepth=32 --ioengine=io_uring \
     --direct=1 --runtime=20 --time_based --size=800g < "$file" > "$OUT/fio-ceiling.txt"
lsblk --discard /dev/nvme0n1 > "$OUT/discard.txt"                  # DISC-GRAN 0 on the crypt device = no discard
sudo -S -p '' cryptsetup status "$(lsblk -lno NAME,TYPE /dev/nvme0n1p2 | awk '$2=="crypt"{print $1}')" < "$file" >> "$OUT/discard.txt"
systemctl status fstrim.timer --no-pager >> "$OUT/discard.txt" 2>&1
```

macOS for comparison: raw sequential read 7.0 GB/s, APFS 9.1 GB/s, 4 KiB QD1 about 20 k IOPS. Turning discards on in
crypttab is the owner's Linux decision. It is a small security trade-off: it leaks which blocks are free. Don't
change it in this session.

## 4. Super I/O chip and sensors (thread 02)

```sh
for h in /sys/class/hwmon/hwmon*; do echo "$h $(cat $h/name)"; done > "$OUT/hwmon.txt"
sensors > "$OUT/sensors.txt" 2>&1
sudo -S -p '' sh -c 'dmesg | grep -i -E "nct|it87|superio|asus.*ec|asus_wmi_sensors"' < "$file" > "$OUT/superio.txt"
lsmod | grep -i -E 'nct|it87|asus' >> "$OUT/superio.txt"
```

The chip name (likely NCT6799D-R on ASUS AM5, which SMCSuperIO in VirtualSMC 1.3.8 supports; unverified) decides
whether fan readings on macOS are one EFI change away.

## 5. End

1. Write the findings note: one table per block above. Mask serials and MACs. Commit only the note and push it
   (`git switch -c ownback/linux-reference fork/main`, commit, `git push fork ownback/linux-reference`).
   Also copy the note and the raw files that carry no serials (everything except `dmesg` output) to the ESP, into
   `vermilion/results-02/` (on Linux the ESP is mounted at `/boot`), so the macOS side has them without git.
2. Tell the owner in one line: **shut down Linux (power off, not restart), power on, and macOS starts**. OpenCore
   is first in the boot order; pick Macintosh HD if the picker waits. Never a warm reboot into macOS: the GPU
   bring-up needs a cold GPU.
