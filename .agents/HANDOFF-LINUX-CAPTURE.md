# Handoff: one Linux capture, then back to macOS (2026-11-19)

For the Claude session on the Linux side (Omarchy, T3 Code) of the XTX machine. The work itself happens in macOS
now; this Linux boot exists only to collect one comparison. **Do only what is below**, then hand back.

## Why

Wi-Fi (MT7922, `0b:00.0`) and Ethernet (I225-V, `0a:00.0`) don't work in macOS, with any driver. Both sit behind
the AMD 600-series chipset switch. In macOS they answer the PCI scan at 0.5 s, then stop answering config requests;
a direct config read of them froze the Mac. Their ports (`06:0b.0`, `06:0a.0`) look healthy: link up, ASPM L1
enabled, L1.2 off, AER clean. In Linux both work. We need Linux's view of the same path to see what differs.
Details: [findings/2026-11-19-wifi-mt7922.md](findings/2026-11-19-wifi-mt7922.md) (boots 2 and 3).

## Steps

1. Update the checkout (it exists on Linux at the same path, branch `ownback/phase1-wifi`):

   ```sh
   cd ~/Projects/worktrees/vermilion/ownback-phase1-wifi-13ed9a7b49bad2ec
   git fetch fork && git checkout ownback/phase1-wifi && git merge --ff-only fork/ownback/phase1-wifi
   ```

2. Run the capture as root. Get the password with T3's `request_secret` (file delivery), never in chat:

   ```sh
   sudo -S -p '' bash tools/capture/linux_pci_capture.sh /boot/vermilion/capture < "$file"
   ```

   It only reads (lspci, sysfs, dmesg) and writes `/boot/vermilion/capture/linux-pci-<date>.txt`, which macOS
   reads from the EFI partition. Tested with `tests/mock/linux_pci_capture_test.sh` (4/4) on macOS.

3. Check the file (as root): MT7922 bound to `mt7921e`, I225-V to `igc`, the `LnkCtl:`/`L1SubCtl1:` lines of
   `06:0b.0`, `06:0a.0`, `0b:00.0`, `0a:00.0` are there, and no MAC address or serial survived
   (`grep -iE '([0-9a-f]{2}:){5}[0-9a-f]{2}'` should only find `xx:xx:...`).

4. Optional, if quick: add a short "Linux side" section to the findings with the key differences (ASPM/L1SS state
   on both ends of the two links, ASPM policy, any `ASPM`/`AER`/quirk lines in dmesg), commit and push to
   `fork ownback/phase1-wifi`. The capture file itself stays out of git.

5. Tell the owner, in one line: **Shut Down Linux (power off, not restart), power on, and macOS starts by itself**
   (OpenCore is first in the boot order; pick Macintosh HD if the picker waits). Then they continue in the macOS
   thread, which reads the capture.

## Don't

- No EFI install, no `efibootmgr` change, no EFI variable change, no change to `/boot/EFI` (the EFI for the next
  macOS boot is already installed: `vermilion` stage, `OC.prev` = the pcilog EFI).
- No GPU work, no driver loading or unloading, no `setpci` writes, nothing under `/sys` written.
- Don't remove the Linux reboot guard (`vermilion-reboot-to-linux.service`).
