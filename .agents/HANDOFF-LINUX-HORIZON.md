# Handoff: Horizon on Linux, the twin of the macOS run (written 2026-11-23, thread 02 part C)

For the agent session on the Linux side (Omarchy) of the XTX machine. A copy of this file and the two Horizon settings
files are on the ESP (`/boot/vermilion/HANDOFF-LINUX-HORIZON.md`, `/boot/vermilion/horizon-bench/`).

**Why:** on macOS, Horizon (Minecraft Java 26.3 in Rust, Metal through wgpu) runs at ~160 fps fullscreen at
2560x1440. The owner sees more than 3x that on Linux with the same setup, but it was never measured under the same
conditions. The macOS side found a fixed cost of ~4-5 ms per presented 1440p frame
([finding](findings/2026-11-23-horizon-present-cost.md)). This run gives the Linux number for the same world, view,
settings and measurement, plus GPU activity and power, so the gap and its cause can be pinned down.

Rules ([runbooks.md](runbooks.md), "Linux sessions"):
- read-only: no register writes, no EFI or `efibootmgr` change, nothing under `/boot/EFI`;
- root only through T3's `request_secret` with file delivery, never in chat (the sysfs reads below need no root);
- **ask the owner before the GPU load and say how long it runs** (about 5 minutes in total).

## 0. Context (record it)

- Date and time, minutes since boot; `hyprctl monitors` (mode, rate, HDR, VRR); `uname -r`;
  `vulkaninfo --summary` (driver name and Mesa version); what else is running.
- The Horizon build: the launcher keeps versions under `~/.local/share/horizon-launcher/versions/` (or ask the
  owner). macOS ran `nightly-2026-11-13-9f42064`. Use the same one if it is installed; otherwise note which.
- Horizon's log line `Horizon renderer: ... (Vulkan|Metal, DiscreteGpu)` and the present mode it reports.

## 1. Setup (no root)

```sh
V=~/.local/share/horizon-launcher/versions/nightly-2026-11-13-9f42064   # or the installed one
CTL="$V/horizon-ctl"; GAME="$V/horizon-client"
mkdir -p ~/hzbench/game ~/hzbench/home ~/hzbench/out
cp /boot/vermilion/horizon-bench/options.txt /boot/vermilion/horizon-bench/horizon-graphics.txt ~/hzbench/game/
# options.txt: fullscreen:true, renderDistance:12, maxFps:260 (= unlimited), enableVsync:false
# horizon-graphics.txt: resolutionScale:100, presentMode:auto, hdr:false
```

## 2. GPU sampler (run during each bench)

```sh
D=$(ls -d /sys/class/drm/card*/device | while read d; do [ "$(cat $d/vendor)" = 0x1002 ] && echo $d; done | head -1)
H=$(ls -d $D/hwmon/hwmon* | head -1)
sample(){ for i in $(seq 22); do echo "$(date +%T) busy $(cat $D/gpu_busy_percent)% power $(( $(cat $H/power1_average) / 1000000 )) W sclk $(grep '\*' $D/pp_dpm_sclk | tr -s ' ') mclk $(grep '\*' $D/pp_dpm_mclk | tr -s ' ')"; sleep 1; done; }
```

## 3. The runs (the same steps as macOS's `hzbench.sh`)

The world `Bench` is created from seed 1 on the first launch: do one warm-up run (discard it), then three measured
runs, then one in a 1280x720 window.

```sh
run(){  # name, extra game args
  "$CTL" --timeout 150 launch --game "$GAME" --world Bench --seed 1 --wait "${@:2}" \
      --game-dir ~/hzbench/game --home ~/hzbench/home > ~/hzbench/out/launch-$1.json
  "$CTL" tp 130 70 170; "$CTL" look 90 0; "$CTL" --timeout 150 wait-ready 120 >/dev/null; sleep 3
  "$CTL" status > ~/hzbench/out/status-$1-before.json
  sample > ~/hzbench/out/gpu-$1.txt & "$CTL" --timeout 60 bench 20 > ~/hzbench/out/bench-$1.json; wait
  "$CTL" status > ~/hzbench/out/status-$1-after.json
  "$CTL" screenshot ~/hzbench/out/shot-$1.png; "$CTL" quit; sleep 5
}
run warmup --fullscreen
run full-1 --fullscreen; run full-2 --fullscreen; run full-3 --fullscreen
sed -i 's/^fullscreen:true/fullscreen:false/' ~/hzbench/game/options.txt
run win720 --width 1280 --height 720
```

From each `bench-*.json`: `avg_fps`, `p50_ms`, `p95_ms`, `p99_ms`, `low_1pct_fps`, `cpu.avg_ms`; from `status-*`:
`renderer.size`, `renderer.present_mode`, `renderer.drawn_sections`. The player may drift (survival mode, water):
note `drawn_sections` so the views can be compared.

Also ask the owner which settings they normally play with on Linux (fps they see, fullscreen or windowed, render
distance) and note it next to the numbers.

## 4. Results

- Raw output stays in `~/hzbench/out/` (no serials in these files).
- One findings note `.agents/findings/2026-11-23-linux-horizon.md` with the table (Linux vs macOS: fullscreen
  158-166 fps, p50 6.0-6.3 ms, GPU 94 % at ~195 W board; 1280x720 window 328 fps), committed on a branch
  `ownback/linux-horizon` from `fork/main` and pushed to `fork`.
- Copy the note and the JSON / text files (not the screenshots) to `/boot/vermilion/results-02c/`, so the macOS
  side has them without git.
