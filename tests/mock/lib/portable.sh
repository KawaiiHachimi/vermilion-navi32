# SPDX-License-Identifier: MIT
# tests/mock/lib/portable.sh: sourced by the mock suites so they also run on Linux (bash 5, GNU coreutils, no
# PlistBuddy / plutil / md5). POSIX sh: b15test.sh sources it under sh too.
#
# On macOS nothing changes: MOCKTMP is /private/tmp, `mock_sedi` is `sed -i ''`, MOCK_PB is /usr/libexec/PlistBuddy,
# MOCK_SHIMPATH is empty and PATH is left alone, so every suite runs exactly the commands it ran before.
#
# Elsewhere (or with MOCK_SHIMS=1, to try the Linux path on a Mac):
#   MOCKTMP        ${TMPDIR:-/tmp}, where the suites make their work dirs
#   mock_sedi      `sed -i` with GNU sed (`sed -i ''` with BSD sed, decided by the sed found on PATH)
#   MOCK_PB        tests/mock/lib/bin/PlistBuddy (Python plistlib, the subset of PlistBuddy the suites and scripts use)
#   MOCK_SHIMPATH  "tests/mock/lib/bin:", put in front of PATH here; run.sh also puts it in front of the fixed PATH it
#                  gives the daemon. It holds PlistBuddy, plutil, md5 and a `stat` that takes BSD `stat -f <fmt>`.
#   MOCK_STAT      that `stat` (for the production scripts that call /usr/bin/stat by path: ESPF_STAT, DI_STAT)
#   MOCK_DARWIN    1 on macOS (uname), 0 elsewhere: suites that need macOS itself (IOKit) print a SKIP line
# MOCKTMP=<dir> and MOCK_PB=<path> override the choice. MOCK_SYSPATH (default /usr/bin:/bin:/usr/sbin:/sbin) is the
# system part of the PATH run.sh gives the daemon (a GNU-first PATH for a Linux-like run on a Mac).
MOCK_LIB=${MOCK_LIB:-$(cd "$(dirname "${BASH_SOURCE:-$0}")" 2>/dev/null && pwd)}
[ -f "$MOCK_LIB/portable.sh" ] || MOCK_LIB=$HERE/lib   # sh has no BASH_SOURCE: the suites set HERE first
MOCK_DARWIN=0; [ "$(uname -s)" = Darwin ] && MOCK_DARWIN=1
if [ -z "${MOCK_SHIMS:-}" ]; then
  if [ $MOCK_DARWIN = 1 ]; then MOCK_SHIMS=0; else MOCK_SHIMS=1; fi
fi
if [ $MOCK_DARWIN = 1 ]; then MOCKTMP=${MOCKTMP:-/private/tmp}; else MOCKTMP=${MOCKTMP:-${TMPDIR:-/tmp}}; fi
MOCKTMP=${MOCKTMP%/}
MOCK_SYSPATH=${MOCK_SYSPATH:-/usr/bin:/bin:/usr/sbin:/sbin}
if [ "$MOCK_SHIMS" = 1 ]; then
  MOCK_SHIMPATH=$MOCK_LIB/bin:
  case ":$PATH:" in *":$MOCK_LIB/bin:"*) ;; *) PATH=$MOCK_LIB/bin:$PATH ;; esac
  MOCK_PB=${MOCK_PB:-$MOCK_LIB/bin/PlistBuddy}
  MOCK_STAT=$MOCK_LIB/bin/stat
else
  MOCK_SHIMPATH=
  MOCK_PB=${MOCK_PB:-/usr/libexec/PlistBuddy}
  MOCK_STAT=/usr/bin/stat
fi
if sed --version >/dev/null 2>&1; then MOCK_SED_GNU=1; else MOCK_SED_GNU=0; fi
mock_sedi() { if [ $MOCK_SED_GNU = 1 ]; then sed -i "$@"; else sed -i '' "$@"; fi; }
export PATH MOCK_LIB MOCK_DARWIN MOCK_SHIMS MOCKTMP MOCK_SYSPATH MOCK_SHIMPATH MOCK_PB MOCK_STAT MOCK_SED_GNU
