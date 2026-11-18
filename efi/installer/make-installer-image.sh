#!/bin/bash
# SPDX-License-Identifier: MIT
# Run on macOS (for example the macbox VM): turn Apple's InstallAssistant.pkg into a bootable macOS installer image.
# The image is a bare HFS+ volume (no partition map), so Linux can write it straight onto a partition
# (write-installer-partition.sh) and OpenCore (with HfsPlus.efi) boots it like a USB installer.
#
#   make-installer-image.sh <InstallAssistant.pkg> <out.dmg> [size, default 21g]
#
# Needs sudo (installer, createinstallmedia). Installs "Install macOS <name>.app" into /Applications.
set -euo pipefail
pkg=${1:?usage: make-installer-image.sh <InstallAssistant.pkg> <out.dmg> [size]}
out=${2:?usage: make-installer-image.sh <InstallAssistant.pkg> <out.dmg> [size]}
size=${3:-21g}
mnt=/Volumes/VermilionInstaller

say() { echo "== $*"; }

say "checking the package signature"
sig=$(pkgutil --check-signature "$pkg")
echo "$sig" | grep -qE "Status: signed (Apple Software|by a certificate trusted)" || { echo "$sig"; echo "not signed by Apple"; exit 1; }
echo "$sig" | grep -q "Software Update" || { echo "$sig"; echo "unexpected signer"; exit 1; }

say "installing the installer app into /Applications"
before=$(ls -d /Applications/Install\ macOS*.app 2>/dev/null || true)
sudo installer -pkg "$pkg" -target / >/dev/null
app=$(ls -dt /Applications/Install\ macOS*.app | head -1)
ver=$(/usr/libexec/PlistBuddy -c 'Print :DTPlatformVersion' "$app/Contents/Info.plist" 2>/dev/null || echo "?")
echo "app: $app (platform version $ver)"
[ -n "$before" ] && echo "note: other installer apps were already present: $before"

say "creating a $size HFS+ image without a partition map: $out"
rm -f "$out"
hdiutil create -size "$size" -layout NONE -fs HFS+J -volname VermilionInstaller "$out" >/dev/null
dev=$(hdiutil attach "$out" -nobrowse -mountpoint "$mnt" | awk '/\/dev\/disk/{print $1; exit}')
echo "attached as $dev"
cleanup() { hdiutil detach "$dev" -force >/dev/null 2>&1 || true; }
trap cleanup EXIT

say "createinstallmedia (copies about 18 GB)"
sudo "$app/Contents/Resources/createinstallmedia" --volume "$mnt" --nointeraction

say "result"
vol=$(diskutil info -plist "$dev" | plutil -extract VolumeName raw -o - - 2>/dev/null || echo "?")
used=$(df -k "/Volumes/$vol" 2>/dev/null | awk 'NR==2{printf "%.1f GiB used of %.1f GiB", $3/1048576, $2/1048576}')
echo "volume: $vol, $used"
cleanup
trap - EXIT
ls -l "$out"
echo "done: write it with efi/installer/write-installer-partition.sh"
