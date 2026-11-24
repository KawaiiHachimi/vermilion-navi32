#!/usr/bin/env python3
"""navi32_kms_hold.py: set one of the P27QBA-RX modes on a DRM card and hold DRM master.

modetest sets a mode and exits; the kernel then restores the fbdev/preferred mode on master drop
(observed: 2560x1440@100 reverts to 60 within a second), so the mode is gone before a register read
can be taken. This keeps master and the mode while it runs.

The mode table is the P27QBA-RX as Linux enumerates it (modetest -M amdgpu -c), so no GETCONNECTOR is
needed: only GETRESOURCES, CREATE_DUMB, MAP_DUMB, ADDFB and SETCRTC.

usage: navi32_kms_hold.py [device] [connector_id] [crtc_id] <rate: 60|100|120|144|165> [seconds]
"""
import ctypes, fcntl, mmap, os, sys, time

DRM_IOCTL_SET_MASTER = 0x641e
FLAG_PHSYNC, FLAG_NHSYNC, FLAG_PVSYNC, FLAG_NVSYNC = 1, 2, 4, 8


def iowr(nr, size):
    # _IOWR: direction 3, size, type 0x64 at bits 8-15, ordinal at bits 0-7
    return (3 << 30) | (size << 16) | (0x64 << 8) | nr


class CardRes(ctypes.Structure):
    _fields_ = [("fb_id_ptr", ctypes.c_uint64), ("crtc_id_ptr", ctypes.c_uint64),
                ("connector_id_ptr", ctypes.c_uint64), ("encoder_id_ptr", ctypes.c_uint64),
                ("count_fbs", ctypes.c_uint32), ("count_crtcs", ctypes.c_uint32),
                ("count_connectors", ctypes.c_uint32), ("count_encoders", ctypes.c_uint32),
                ("min_width", ctypes.c_uint32), ("max_width", ctypes.c_uint32),
                ("min_height", ctypes.c_uint32), ("max_height", ctypes.c_uint32)]


class ModeInfo(ctypes.Structure):
    _fields_ = [("clock", ctypes.c_uint32),
                ("hdisplay", ctypes.c_uint16), ("hsync_start", ctypes.c_uint16),
                ("hsync_end", ctypes.c_uint16), ("htotal", ctypes.c_uint16), ("hskew", ctypes.c_uint16),
                ("vdisplay", ctypes.c_uint16), ("vsync_start", ctypes.c_uint16),
                ("vsync_end", ctypes.c_uint16), ("vtotal", ctypes.c_uint16), ("vscan", ctypes.c_uint16),
                ("vrefresh", ctypes.c_uint32), ("flags", ctypes.c_uint32), ("type", ctypes.c_uint32),
                ("name", ctypes.c_char * 32)]


class CreateDumb(ctypes.Structure):
    _fields_ = [("height", ctypes.c_uint32), ("width", ctypes.c_uint32), ("bpp", ctypes.c_uint32),
                ("flags", ctypes.c_uint32), ("handle", ctypes.c_uint32), ("pitch", ctypes.c_uint32),
                ("size", ctypes.c_uint64)]


class MapDumb(ctypes.Structure):
    _fields_ = [("handle", ctypes.c_uint32), ("pad", ctypes.c_uint32), ("offset", ctypes.c_uint64)]


class FbCmd(ctypes.Structure):
    _fields_ = [("fb_id", ctypes.c_uint32), ("width", ctypes.c_uint32), ("height", ctypes.c_uint32),
                ("pitch", ctypes.c_uint32), ("bpp", ctypes.c_uint32), ("depth", ctypes.c_uint32),
                ("handle", ctypes.c_uint32)]


class SetCrtc(ctypes.Structure):
    _fields_ = [("set_connectors_ptr", ctypes.c_uint64), ("count_connectors", ctypes.c_uint32),
                ("crtc_id", ctypes.c_uint32), ("fb_id", ctypes.c_uint32),
                ("x", ctypes.c_uint32), ("y", ctypes.c_uint32), ("gamma_size", ctypes.c_uint32),
                ("mode_valid", ctypes.c_uint32), ("mode", ModeInfo)]


# P27QBA-RX (EDID base DTD + the CTA/DisplayID rates), as modetest lists them on this board.
# h/v: active, sync start, sync end, total. clock in kHz, flags as DRM_MODE_FLAG_*.
MODES = {
    60:  (241700, 2560, 2608, 2640, 2720, 1440, 1443, 1448, 1481, FLAG_PHSYNC | FLAG_NVSYNC),
    100: (402840, 2560, 2608, 2640, 2720, 1440, 1443, 1448, 1481, FLAG_PHSYNC | FLAG_NVSYNC),
    120: (497760, 2560, 2608, 2640, 2720, 1440, 1443, 1448, 1525, FLAG_PHSYNC | FLAG_NVSYNC),
    144: (597290, 2560, 2608, 2640, 2720, 1440, 1443, 1448, 1525, FLAG_PHSYNC | FLAG_NVSYNC),
    165: (657500, 2560, 2608, 2640, 2720, 1440, 1443, 1448, 1465, FLAG_PHSYNC | FLAG_PVSYNC),
    1080: (148500, 1920, 2008, 2052, 2200, 1080, 1084, 1089, 1125, FLAG_NHSYNC | FLAG_NVSYNC),
}


def main():
    dev = sys.argv[1] if len(sys.argv) > 1 else "/dev/dri/card1"
    conn_id = int(sys.argv[2]) if len(sys.argv) > 2 else 378
    crtc_id = int(sys.argv[3]) if len(sys.argv) > 3 else 364
    want = sys.argv[4] if len(sys.argv) > 4 else "60"
    secs = float(sys.argv[5]) if len(sys.argv) > 5 else 120.0
    rate = int(want)
    if rate not in MODES:
        sys.exit("no mode for %r (have %s)" % (want, sorted(MODES)))
    clk, ha, hs, he, ht, va, vs, ve, vt, flags = MODES[rate]
    fd = os.open(dev, os.O_RDWR)
    fcntl.ioctl(fd, DRM_IOCTL_SET_MASTER, 0)
    res = CardRes()
    fcntl.ioctl(fd, iowr(0xa0, ctypes.sizeof(res)), res)
    fbs = (ctypes.c_uint32 * max(res.count_fbs, 1))()
    crtcs = (ctypes.c_uint32 * max(res.count_crtcs, 1))()
    conns = (ctypes.c_uint32 * max(res.count_connectors, 1))()
    encs = (ctypes.c_uint32 * max(res.count_encoders, 1))()
    res.fb_id_ptr = ctypes.addressof(fbs)
    res.crtc_id_ptr = ctypes.addressof(crtcs)
    res.connector_id_ptr = ctypes.addressof(conns)
    res.encoder_id_ptr = ctypes.addressof(encs)
    fcntl.ioctl(fd, iowr(0xa0, ctypes.sizeof(res)), res)
    if crtc_id not in list(crtcs):
        crtc_id = int(crtcs[0])
    dumb = CreateDumb(height=va, width=ha, bpp=32)
    fcntl.ioctl(fd, iowr(0xb2, ctypes.sizeof(dumb)), dumb)
    md = MapDumb(handle=dumb.handle)
    fcntl.ioctl(fd, iowr(0xb3, ctypes.sizeof(md)), md)
    buf = mmap.mmap(fd, dumb.size, mmap.MAP_SHARED, mmap.PROT_READ | mmap.PROT_WRITE, offset=md.offset)
    for row in range(va):
        v = 40 + (row * 160) // max(va - 1, 1)
        px = bytes((v // 2, v, v, 255))
        buf[row * dumb.pitch:(row + 1) * dumb.pitch] = px * ha
    fb = FbCmd(width=ha, height=va, pitch=dumb.pitch, bpp=32, depth=24, handle=dumb.handle)
    fcntl.ioctl(fd, iowr(0xae, ctypes.sizeof(fb)), fb)
    con = (ctypes.c_uint32 * 1)(conn_id)
    sc = SetCrtc(set_connectors_ptr=ctypes.addressof(con), count_connectors=1, crtc_id=crtc_id,
                 fb_id=fb.fb_id, mode_valid=1)
    sc.mode = ModeInfo(clock=clk, hdisplay=ha, hsync_start=hs, hsync_end=he, htotal=ht,
                       vdisplay=va, vsync_start=vs, vsync_end=ve, vtotal=vt, vrefresh=rate,
                       flags=flags, name=("%dx%d" % (ha, va)).encode())
    fcntl.ioctl(fd, iowr(0xa2, ctypes.sizeof(sc)), sc)
    print("holding %dx%d@%d on %s connector %d crtc %d (clock %d kHz, totals %dx%d)" %
          (ha, va, rate, dev, conn_id, crtc_id, clk, ht, vt), flush=True)
    time.sleep(secs)
    return 0


if __name__ == "__main__":
    sys.exit(main())
