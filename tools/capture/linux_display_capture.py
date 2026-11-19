#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# linux_display_capture.py: the Linux side of "1440p at 120/144/240 Hz and HDR on the XTX" (findings
# 2026-11-19-refresh-and-hdr.md, plan step 2). Run as root on Linux (Omarchy / Hyprland), booted directly.
#
# For each step it switches the monitor with hyprctl (Linux's own amdgpu DC does the mode set and link training), traces
# every register write DC makes during the switch (ftrace amdgpu_dm:amdgpu_dc_wreg), lets it settle, then READS:
# amdgpu_dm_dtn_log, the connector's debugfs state (link_settings, output_bpc, DSC), DPCD over /dev/drm_dp_auxN, the
# steady value of every register DC wrote plus a fixed list (debugfs amdgpu_regs), clocks, DMUB trace and firmware info.
# Nothing is written to the GPU by this script itself except through amdgpu: hyprctl mode sets, and in the "hbr2"
# steps the debugfs link_settings preference (Linux retrains its own link; undone by a power-off).
#
# Steps (each can be picked with --steps):
#   base            what is running now (no switch)
#   60 120 144 240  2560x1440 at that rate, 8 bpc, VRR off, Linux's own link choice (HBR3 most likely)
#   hbr2-60 hbr2-120 hbr2-144   the same with the link forced to HBR2 x4 (what the GOP leaves macOS on) [--hbr2]
#   hbr2-120-10     120 Hz at 10 bpc on HBR2 x4 [--hbr2]
#   hdr-60 hdr-120  HDR on (Hyprland cm,hdr, 10 bpc) [--hdr]: the HDR infoframe / colorimetry / regamma writes
# At the end the monitor goes back to the config file (hyprctl reload). Forced link settings stay until power-off.
#
# Output: <outdir>/display-<date>/<step>/... and a summary.txt. Monitor serials are stripped from the EDID copy (base
# block serial + 0xFF descriptor + DisplayID product block serial). Register dumps carry no serials. Never git it.
#
# Usage:  sudo python3 tools/capture/linux_display_capture.py /boot/vermilion/capture [--hbr2] [--hdr] [--steps a,b]
#         --dry-run lists what would run, touches nothing (no root needed).
import argparse, datetime, glob, json, os, re, shutil, subprocess, sys, time

# Never read: reads with side effects. MMVM_INVALIDATE_ENG*_SEM (a read takes the semaphore), the DC_I2C engine and the
# DP AUX engines (reading SW_DATA advances the FIFO index). Absolute dwords (DCN 3.2, segment 2 = 0x34C0).
DENY = [(0x1A751, 0x1A762), (0x34C0 + 0x1E98, 0x34C0 + 0x1EB8), (0x34C0 + 0x1F50, 0x34C0 + 0x1F50 + 5 * 0x1C)]
# Always read (beyond what DC wrote): the registers macOS read on 2026-11-19 and the pipe-0 engine's fingerprint.
FIXED = {
    0x056C8: "DP1_DP_LINK_CNTL", 0x056C9: "DP1_DP_PIXEL_FORMAT", 0x056CA: "DP1_DP_MSA_COLORIMETRY", 0x056CB: "DP1_DP_CONFIG",
    0x056CC: "DP1_DP_VID_STREAM_CNTL", 0x056CE: "DP1_DP_MSA_MISC", 0x056D0: "DP1_DP_VID_TIMING", 0x056D1: "DP1_DP_VID_N",
    0x056D2: "DP1_DP_VID_M", 0x056EB: "DP1_DP_SEC_CNTL", 0x0564B: "DIG1_DIG_FE_CNTL", 0x05651: "DIG1_DIG_FIFO_CTRL0",
    0x05671: "DIG1_DIG_BE_CNTL", 0x05672: "DIG1_DIG_BE_EN_CNTL", 0x04FEA: "OTG0_OTG_H_TOTAL", 0x04FEF: "OTG0_OTG_V_TOTAL",
    0x05001: "OTG0_OTG_CONTROL", 0x0500C: "OTG0_OTG_STATUS_FRAME_COUNT", 0x00141: "DP_DTO0_PHASE", 0x00142: "DP_DTO0_MODULO",
    0x00124: "DENTIST_DISPCLK_CNTL", 0x0397B: "DCHUBBUB_DET0_CTRL", 0x0397C: "DCHUBBUB_DET1_CTRL", 0x0397D: "DCHUBBUB_DET2_CTRL",
    0x0397E: "DCHUBBUB_DET3_CTRL", 0x039BC: "DCHUBBUB_ARB_DRAM_STATE_CNTL", 0x039BE: "ARB_DATA_URGENCY_WATERMARK_A",
    0x039C1: "ARB_ALLOW_SR_ENTER_WATERMARK_A", 0x039C2: "ARB_ALLOW_SR_EXIT_WATERMARK_A", 0x039C3: "ARB_UCLK_PSTATE_WATERMARK_A",
    0x039C4: "ARB_FCLK_PSTATE_WATERMARK_A", 0x036A4: "DMCUB_SCRATCH1", 0x03AA5: "HUBP0_DCSURF_SURFACE_CONFIG",
    0x03AB3: "HUBP0_DCHUBP_CNTL", 0x04F8A: "ODM0_OPTC_INPUT_GLOBAL_CONTROL", 0x039EE: "VTG0_CONTROL",
}
STEPS = {   # name: (refresh Hz or None, bpc, link "auto"/"hbr2", hdr)
    "base": (None, None, "auto", False),
    "60": (60, 8, "auto", False), "120": (120, 8, "auto", False), "144": (144, 8, "auto", False), "240": (240, 8, "auto", False),
    "hbr2-60": (60, 8, "hbr2", False), "hbr2-120": (120, 8, "hbr2", False), "hbr2-144": (144, 8, "hbr2", False),
    "hbr2-120-10": (120, 10, "hbr2", False),
    "hdr-60": (60, 10, "auto", True), "hdr-120": (120, 10, "auto", True),
}
DEFAULT = ["base", "60", "120", "144", "240"]
HBR2 = ["hbr2-60", "hbr2-120", "hbr2-144", "hbr2-120-10"]
HDR = ["hdr-60", "hdr-120"]
DPCD_RANGES = [(0x0000, 0x100), (0x0100, 0x100), (0x0200, 0x100), (0x0300, 0x10), (0x0600, 0x1), (0x2200, 0x100)]
TRACE = "/sys/kernel/tracing"
EVENT = "amdgpu_dm/amdgpu_dc_wreg"


def denied(r):
    return any(a <= r <= b for a, b in DENY)


def strip_edid(e):
    """The EDID with the monitor's serials blanked: base block bytes 12-15, any 0xFF (serial) descriptor, and the
    serial of a DisplayID 1.x/2.x product identification block (tags 0x00 / 0x20). Checksums recomputed."""
    e = bytearray(e)
    if len(e) < 128:
        return bytes(e)
    e[12:16] = b"\0\0\0\0"
    for o in (54, 72, 90, 108):
        if e[o] == 0 and e[o + 1] == 0 and e[o + 3] == 0xFF:
            e[o + 5:o + 18] = b"\x0a" + b"\x20" * 12
    for b in range(128, len(e) - 127, 128):
        x = e[b:b + 128]
        if x[0] != 0x70:
            continue
        end = min(5 + x[2], 127)   # DisplayID section: header 1..4, data blocks from 5, section checksum at 5 + length
        i = 5
        while i + 3 <= end:
            tag, n = x[i], x[i + 2]
            if tag in (0x00, 0x20) and n >= 9:   # product identification: OUI 3, product code 2, serial 4, ...
                x[i + 3 + 5:i + 3 + 9] = b"\0\0\0\0"
            i += 3 + n
        if end < 127:
            x[end] = (-sum(x[1:end])) & 0xFF
        e[b:b + 128] = x
    for b in range(0, len(e) - 127, 128):
        e[b + 127] = (-sum(e[b:b + 127])) & 0xFF
    return bytes(e)


class Cap:
    def __init__(self, a):
        self.a = a
        self.dry = a.dry_run
        self.log = []

    def say(self, s):
        print(s, flush=True)
        self.log.append(s)

    # ---- discovery ----
    def find(self):
        self.card = self.dri = None
        for v in glob.glob("/sys/class/drm/card[0-9]*/device/vendor"):
            d = os.path.dirname(v)
            if open(v).read().strip() == "0x1002" and open(d + "/device").read().strip() == "0x744c":
                self.card = os.path.basename(os.path.dirname(d))
                self.pci = os.path.basename(os.path.realpath(d))
        if not self.card:
            sys.exit("no RX 7900 (1002:744c) card found")
        for n in glob.glob("/sys/kernel/debug/dri/*/name"):
            if self.pci in open(n).read():
                self.dri = os.path.dirname(n)
        self.rev = open(f"/sys/class/drm/{self.card}/device/revision").read().strip()
        self.conn = None
        for c in sorted(glob.glob(f"/sys/class/drm/{self.card}-DP-*")):
            if open(c + "/status").read().strip() == "connected":
                self.conn = c
                break
        if not self.conn:
            sys.exit("no connected DP connector on the card")
        self.cname = os.path.basename(self.conn).split("-", 1)[1]   # DP-1
        aux = glob.glob(self.conn + "/drm_dp_aux*")
        self.aux = "/dev/" + os.path.basename(aux[0]) if aux else None
        self.say(f"card {self.card} pci {self.pci} rev {self.rev} debugfs {self.dri} connector {self.cname} aux {self.aux}")

    # ---- hyprland ----
    def hypr(self, *args):
        user = os.environ.get("SUDO_USER") or os.environ.get("USER")
        uid = int(subprocess.run(["id", "-u", user], capture_output=True, text=True).stdout.strip() or 0)
        rd = f"/run/user/{uid}"
        sigs = sorted(glob.glob(rd + "/hypr/*/.socket.sock"), key=os.path.getmtime)
        if not sigs:
            raise RuntimeError("no Hyprland instance found")
        env = dict(os.environ, XDG_RUNTIME_DIR=rd, HYPRLAND_INSTANCE_SIGNATURE=os.path.basename(os.path.dirname(sigs[-1])))
        cmd = ["hyprctl", *args] if os.geteuid() == uid else ["sudo", "-u", user, "-E", "hyprctl", *args]
        return subprocess.run(cmd, capture_output=True, text=True, env=env, timeout=20).stdout

    def monitor(self):
        for m in json.loads(self.hypr("-j", "monitors") or "[]"):
            if m.get("name") == self.cname:
                return m
        return {}

    # ---- reads ----
    def readregs(self, regs):
        out = {}
        with open(self.dri + "/amdgpu_regs", "rb", buffering=0) as f:
            for r in sorted(regs):
                if denied(r):
                    continue
                try:
                    out[r] = int.from_bytes(os.pread(f.fileno(), 4, r * 4), "little")
                except OSError as e:
                    out[r] = f"err {e.errno}"
        return out

    def dpcd(self):
        res = {}
        if not self.aux:
            return res
        fd = os.open(self.aux, os.O_RDONLY)
        try:
            for base, n in DPCD_RANGES:
                b = bytearray()
                for o in range(base, base + n, 16):
                    try:
                        b += os.pread(fd, min(16, base + n - o), o)
                    except OSError:
                        b += b"\xee" * min(16, base + n - o)
                res[base] = bytes(b)
        finally:
            os.close(fd)
        return res

    def cat(self, path, dst):
        try:
            with open(path, "rb") as f:
                data = f.read(4 << 20)
            open(dst, "wb").write(data)
            return data
        except OSError as e:
            open(dst, "w").write(f"(unreadable: {e})\n")
            return b""

    # ---- ftrace ----
    def trace_start(self):
        w = lambda p, v: open(f"{TRACE}/{p}", "w").write(v)
        w("tracing_on", "0"); w("trace", ""); w("buffer_size_kb", "65536")
        w(f"events/{EVENT}/enable", "1"); w("tracing_on", "1")

    def trace_stop(self, dst):
        open(f"{TRACE}/tracing_on", "w").write("0")
        open(f"{TRACE}/events/{EVENT}/enable", "w").write("0")
        shutil.copyfile(f"{TRACE}/trace", dst)
        regs, n = set(), 0
        for line in open(dst, errors="replace"):
            m = re.search(r"reg=0x([0-9a-fA-F]+)", line) or re.search(r"address=0x([0-9a-fA-F]+)", line)
            if m:
                regs.add(int(m.group(1), 16)); n += 1
        return regs, n

    # ---- one step ----
    def step(self, name, out):
        hz, bpc, link, hdr = STEPS[name]
        d = os.path.join(out, name)
        rule = None
        if hz:
            rule = f"{self.cname},2560x1440@{hz},0x0,1,vrr,0" + (f",bitdepth,{bpc}" if bpc and bpc > 8 else "") + (",cm,hdr" if hdr else "")
        self.say(f"== {name}: {rule or 'no switch'}{' (link forced HBR2 x4)' if link == 'hbr2' else ''}")
        if self.dry:
            return
        os.makedirs(d, exist_ok=True)
        if link == "hbr2" and not self.forced:
            self.trace_start()
            try:
                open(f"{self.dri}/{self.cname}/link_settings", "w").write("4 0x14")
            except OSError as e:
                self.say(f"   link_settings write failed: {e}")
            time.sleep(3)
            self.trace_stop(os.path.join(d, "trace-linkforce.txt"))
            self.forced = True
        written, n = set(), 0
        if rule:
            self.trace_start()
            r = self.hypr("keyword", "monitor", rule)
            time.sleep(4)
            written, n = self.trace_stop(os.path.join(d, "trace-modeset.txt"))
            self.say(f"   hyprctl: {r.strip()[:80]}; {n} register writes traced, {len(written)} distinct registers")
        time.sleep(2)
        m = self.monitor()
        json.dump(m, open(os.path.join(d, "hyprctl-monitor.json"), "w"), indent=1)
        f0 = self.readregs([0x0500C]).get(0x0500C, 0)
        time.sleep(1.0)
        f1 = self.readregs([0x0500C]).get(0x0500C, 0)
        regs = self.readregs(set(FIXED) | written)
        with open(os.path.join(d, "regs.txt"), "w") as f:
            f.write("# dword value name (steady state after the switch; names only for the fixed list)\n")
            for r, v in regs.items():
                f.write(f"0x{r:05X} {v if isinstance(v, str) else f'0x{v:08x}'} {FIXED.get(r, '')}\n")
        for p in ("amdgpu_dm_dtn_log", "amdgpu_dm_dmub_tracebuffer", "amdgpu_firmware_info", "amdgpu_pm_info", "amdgpu_dm_capabilities"):
            self.cat(f"{self.dri}/{p}", os.path.join(d, p + ".txt"))
        cd = f"{self.dri}/{self.cname}"
        with open(os.path.join(d, "connector.txt"), "w") as f:
            for p in sorted(glob.glob(cd + "/*")):
                b = os.path.basename(p)
                if b in ("dp_sdp_message", "test_pattern", "trigger_hotplug", "aux_dpcd_address", "aux_dpcd_size", "aux_dpcd_data") or not os.path.isfile(p):
                    continue
                try:
                    f.write(f"--- {b}\n{open(p, errors='replace').read(65536)}\n")
                except OSError as e:
                    f.write(f"--- {b}: {e}\n")
        for base, b in self.dpcd().items():
            open(os.path.join(d, f"dpcd-{base:04x}.bin"), "wb").write(b)
        for p in glob.glob(f"/sys/class/drm/{self.card}/device/pp_dpm_*"):
            self.cat(p, os.path.join(d, os.path.basename(p) + ".txt"))
        mv = regs.get(0x056D2, 0); nv = regs.get(0x056D1, 0); cfg = regs.get(0x056CB, 0); pf = regs.get(0x056C9, 0)
        self.say(f"   now: {m.get('width')}x{m.get('height')} @ {m.get('refreshRate')} Hz; OTG0 frames/s {(f1 - f0) & 0xffffffff}; "
                 f"lanes {(cfg & 3) + 1 if isinstance(cfg, int) else '?'}; VID_M/N 0x{mv:x}/0x{nv:x}; DP_PIXEL_FORMAT 0x{pf:08x}")

    def run(self):
        steps = self.a.steps.split(",") if self.a.steps else DEFAULT + (HBR2 if self.a.hbr2 else []) + (HDR if self.a.hdr else [])
        for s in steps:
            if s not in STEPS:
                sys.exit(f"unknown step {s}; known: {', '.join(STEPS)}")
        if self.a.hdr and self.a.hbr2:   # HDR steps run before the link is forced
            steps = [s for s in steps if not s.startswith("hbr2")] + [s for s in steps if s.startswith("hbr2")]
        self.forced = False
        if self.dry:
            self.cname = "DP-?"
            for s in steps:
                self.step(s, "/nonexistent")
            return
        if os.geteuid() != 0:
            sys.exit("run as root (sudo)")
        self.find()
        need = 64 << 20
        st = shutil.disk_usage(self.a.outdir if os.path.isdir(self.a.outdir) else os.path.dirname(self.a.outdir.rstrip("/")) or "/")
        if st.free < need:
            sys.exit(f"{self.a.outdir}: less than 64 MiB free")
        if not os.path.exists(f"{TRACE}/events/{EVENT}/enable"):
            sys.exit(f"no ftrace event {EVENT} (tracefs at {TRACE}?)")
        out = os.path.join(self.a.outdir, "display-" + datetime.datetime.now().strftime("%Y%m%d-%H%M%S"))
        os.makedirs(out)
        self.say(f"out {out}; kernel {os.uname().release}; steps {','.join(steps)}")
        try:
            e = open(self.conn + "/edid", "rb").read()
            open(os.path.join(out, "edid-noserial.bin"), "wb").write(strip_edid(e))
            self.say(f"EDID {len(e)} bytes (serials stripped in the copy)")
        except OSError as e:
            self.say(f"EDID unreadable: {e}")
        try:
            for s in steps:
                try:
                    self.step(s, out)
                except Exception as ex:   # keep going: one failed step must not leave the monitor half-switched
                    self.say(f"   step {s} failed: {ex!r}")
        finally:
            try:
                self.say(f"restore: hyprctl reload -> {self.hypr('reload').strip()[:60]}")
            except Exception as ex:
                self.say(f"restore failed: {ex!r} (run 'hyprctl reload' by hand)")
            if self.forced:
                self.say("the link stays forced to HBR2 x4 until power-off (Linux only; nothing persists)")
            open(os.path.join(out, "summary.txt"), "w").write("\n".join(self.log) + "\n")


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[1] if __doc__ else "")
    ap.add_argument("outdir")
    ap.add_argument("--hbr2", action="store_true", help="add the steps with the link forced to HBR2 x4")
    ap.add_argument("--hdr", action="store_true", help="add the HDR steps (Hyprland cm,hdr)")
    ap.add_argument("--steps", help="comma list, overrides the default set")
    ap.add_argument("--dry-run", action="store_true")
    Cap(ap.parse_args()).run()


if __name__ == "__main__":
    main()
