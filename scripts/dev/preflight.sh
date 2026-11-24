#!/bin/sh
# SPDX-License-Identifier: MIT
# preflight.sh <kext> [--opencore|--auxkc]: everything that can be checked about a built kext before it costs a boot.
# Read-only; exit 0 = all checks passed, 1 = a check failed, 2 = a check could not run.
#
#   1. Info.plist parses, CFBundleExecutable exists, is a thin x86_64 Mach-O kext binary.
#   2. Link gate (--auxkc): every imported symbol is exported by a library in OSBundleLibraries, the way kmutil binds an
#      AuxKC kext (scripts/build/linkgate.py, exports from scripts/build/linkgate_exports.sh in build/linkgate or
#      $LINKGATE_EXPORTS). OpenCore links the kexts it injects itself, against the whole kernel, so for --opencore the
#      result is only shown (the booted VermilionBoot b160 imports private symbols the KPI lists don't have).
#   3. OpenCore-injected kexts (--opencore, the default) load with the boot kernel collection: every OSBundleLibraries
#      entry must be a kext inside BootKernelExtensions.kc or another kext OpenCore injects (EFI/OC/Kexts, $OC_KEXTS).
#      A library that only lives in the System KC (IOGraphicsFamily, for one) fails at boot (finding 2026-11-24, 04's
#      change 31).
#   4. kmutil print-diagnostics: what kmutil itself says about loading the bundle on this macOS.
#   5. VermilionBoot only: PHX_KEXT_BUILD was bumped against fork/main and CHANGELOG.md has a line for it.
set -u
KEXT=${1:?usage: preflight.sh <kext bundle> [--opencore|--auxkc]}
KEXT=${KEXT%/}
MODE=${2:---opencore}
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
rc=0
ok() { echo "  ok    $*"; }
bad() { echo "  FAIL  $*"; rc=1; }
skip() { echo "  skip  $*"; [ "$rc" = 1 ] || rc=2; }
echo "preflight $(basename "$KEXT") ($MODE)"

INFO="$KEXT/Contents/Info.plist"
plutil -lint "$INFO" >/dev/null 2>&1 && ok "Info.plist parses" || { bad "Info.plist does not parse"; exit 1; }
EXE=$(/usr/libexec/PlistBuddy -c 'Print :CFBundleExecutable' "$INFO" 2>/dev/null)
BIN="$KEXT/Contents/MacOS/$EXE"
if [ -n "$EXE" ] && [ -f "$BIN" ]; then
  arch=$(lipo -archs "$BIN" 2>/dev/null)
  [ "$arch" = x86_64 ] && ok "binary $EXE is thin x86_64" || bad "binary $EXE architectures: '$arch' (want x86_64 only)"
else
  [ -z "$EXE" ] && ok "codeless kext (no CFBundleExecutable)" || bad "CFBundleExecutable $EXE missing in Contents/MacOS"
fi

if [ -n "$EXE" ] && [ -f "$BIN" ]; then
  python3 -I "$ROOT/scripts/build/linkgate.py" "$KEXT" ${LINKGATE_EXPORTS:+"$LINKGATE_EXPORTS"} > /tmp/preflight.$$ 2>&1
  case $? in
    0) ok "link gate: every import is exported by a listed library" ;;
    2) skip "link gate could not run: $(tail -1 /tmp/preflight.$$)" ;;
    *) if [ "$MODE" = --auxkc ]; then bad "link gate:"; else echo "  note  link gate (informational for OpenCore-injected kexts):"; fi
       sed 's/^/        /' /tmp/preflight.$$ ;;
  esac
  rm -f /tmp/preflight.$$
fi

if [ "$MODE" = --opencore ]; then
  libs=$(python3 -I -c 'import plistlib,sys; print("\n".join(sorted(plistlib.load(open(sys.argv[1],"rb")).get("OSBundleLibraries",{}))))' "$INFO")
  bootkc=$(kmutil inspect -B /System/Library/KernelCollections/BootKernelExtensions.kc --no-header 2>/dev/null | awk '{for(i=1;i<=NF;i++) if ($i ~ /^com\.|^as\.|^org\./) print $i}' | sort -u)
  ockexts=""
  for d in "${OC_KEXTS:-/Volumes/EFI/EFI/OC/Kexts}" "$ROOT/build/efi/EFI/OC/Kexts"; do
    [ -d "$d" ] || continue
    for k in "$d"/*.kext; do
      [ -f "$k/Contents/Info.plist" ] && ockexts="$ockexts $(/usr/libexec/PlistBuddy -c 'Print :CFBundleIdentifier' "$k/Contents/Info.plist" 2>/dev/null)"
    done
  done
  if [ -z "$bootkc" ]; then
    skip "boot-KC rule: could not read BootKernelExtensions.kc"
  else
    for l in $libs; do
      case $l in com.apple.kpi.*) continue ;; esac
      if echo "$bootkc" | grep -qx "$l"; then :
      elif echo " $ockexts " | grep -q " $l "; then :
      else bad "boot-KC rule: $l is neither in the boot KC nor an OpenCore-injected kext"; fi
    done
    [ "$rc" = 1 ] || ok "boot-KC rule: every library is in the boot KC or injected by OpenCore"
  fi
fi

if kmutil print-diagnostics -p "$KEXT" > /tmp/preflight.$$ 2>&1; then
  ok "kmutil print-diagnostics: no problems"
else
  # kmutil reports unapproved third-party kexts and missing OpenCore-only dependencies as errors too: show, don't judge
  echo "  note  kmutil print-diagnostics:"; sed -n '1,15p' /tmp/preflight.$$ | sed 's/^/        /'
fi
rm -f /tmp/preflight.$$

if [ "$EXE" = VermilionBoot ]; then
  cur=$(grep -o -E 'PHX_KEXT_BUILD[[:space:]]+[0-9]+' "$ROOT/src/vermilion-boot/PhxBringup.cpp" | grep -o -E '[0-9]+$' | head -1)
  base=$(git -C "$ROOT" show fork/main:src/vermilion-boot/PhxBringup.cpp 2>/dev/null | grep -o -E 'PHX_KEXT_BUILD[[:space:]]+[0-9]+' | grep -o -E '[0-9]+$' | head -1)
  if [ -n "$cur" ] && [ -n "$base" ] && [ "$cur" -gt "$base" ]; then ok "PHX_KEXT_BUILD $cur > fork/main's $base"
  elif [ -n "$cur" ] && [ "$cur" = "$base" ] && git -C "$ROOT" diff --quiet fork/main -- src/vermilion-boot; then ok "VermilionBoot unchanged against fork/main ($cur)"
  else bad "PHX_KEXT_BUILD not bumped (now '$cur', fork/main '$base')"; fi
  grep -q -E "(^|[^0-9])b?$cur([^0-9]|$)" "$ROOT/src/vermilion-boot/CHANGELOG.md" 2>/dev/null && ok "CHANGELOG.md mentions build $cur" || bad "CHANGELOG.md has no line for build $cur"
fi
echo "preflight: $( [ $rc = 0 ] && echo passed || { [ $rc = 2 ] && echo 'incomplete (a check could not run)' || echo FAILED; } )"
exit $rc
