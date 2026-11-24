#!/usr/bin/env python3
# Dump the CP / MES / MEC / RLC block on the Linux side of the same card, so the port can be compared
# register by register. Run as root on Linux: the amdgpu driver has the GPU fully working there.
import os, struct
P = '/sys/kernel/debug/dri/0000:03:00.0/amdgpu_regs'
fd = os.open(P, os.O_RDWR)
def rd(r): return struct.unpack('<I', os.pread(fd, 4, r * 4))[0]
def wr(r, v): os.pwrite(fd, struct.pack('<I', v), r * 4)

# keep the GPU out of GFXOFF so the block is powered
g = '/sys/kernel/debug/dri/0000:03:00.0/amdgpu_gfxoff'
if os.path.exists(g):
    gf = os.open(g, os.O_RDWR); os.write(gf, struct.pack('<I', 0)); os.close(gf)

print('== CP block 0x0a800..0x0a810 (ME/PFP/CE control) ==')
for r in range(0x0a800, 0x0a811):
    print('  0x%05x 0x%08x' % (r, rd(r)))

print('== CP_PQ/CP_RB doorbell ranges ==')
for r in (0x0305a, 0x0305b, 0x0305c, 0x0305d, 0x0305e, 0x0305f):
    print('  0x%05x 0x%08x' % (r, rd(r)))

print('== MES block 0x0c7f0..0x0c8a0 ==')
for r in range(0x0c7f0, 0x0c8a1, 1):
    v = rd(r)
    if v: print('  0x%05x 0x%08x' % (r, v))

print('== MEC block 0x0c900..0x0c9a0 (non-zero only) ==')
for r in range(0x0c900, 0x0c9a1):
    v = rd(r)
    if v: print('  0x%05x 0x%08x' % (r, v))

print('== RLC 0x0ee00..0x0ee90 (non-zero only) ==')
for r in range(0x0ee00, 0x0ee91):
    v = rd(r)
    if v: print('  0x0%04x 0x%08x' % (r, v))

print('== status ==')
for n, r in (('CP_STAT', 0x021a0), ('GRBM_STATUS', 0x02004), ('GB_ADDR_CONFIG', 0x0263e),
             ('RLC_CP_SCHEDULERS(SEG1 0x98a)', 0x0a98a), ('SDMA0_VERSION', 0x01295)):
    print('  %-28s 0x%08x' % (n, rd(r)))

wr(0x0a900, 0x0d)   # GRBM me3/pipe1 = the MES KIQ
print('== KIQ HQD (me3/pipe1) ==')
for n, r in (('MQD_LO', 0x03209), ('MQD_HI', 0x0320a), ('ACTIVE', 0x0320b), ('VMID', 0x0320c),
             ('PQ_BASE', 0x03211), ('PQ_BASE_HI', 0x03212), ('PQ_RPTR', 0x03213),
             ('RPTR_REPORT', 0x03214), ('PQ_WPTR', 0x03219), ('DOORBELL_CTL', 0x03218),
             ('PQ_CONTROL', 0x0321a), ('PQ_WPTR_POLL', 0x0321b), ('IB_CNTL', 0x0321e)):
    print('  %-12s 0x%08x' % (n, rd(r)))
wr(0x0a900, 0)
os.close(fd)