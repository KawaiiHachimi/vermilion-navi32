# 2026-11-18: running the mock tests on Linux

On Linux (bash 5, GNU coreutils) every suite in `tests/mock` fails at once, because 14 files create their work
directory with `mktemp -d /private/tmp/...`, a macOS-only path.

The same suites on a scratch copy with `/private/tmp` replaced by `${TMPDIR:-/tmp}` (repo unchanged):

| Suite | Result on Linux |
|---|---|
| `m7test.sh` | 62 passed, 0 failed |
| `dettest.sh` | 11 passed, 0 failed |
| `run.sh` (boot daemon) | 141 passed, 25 failed: `ws_crash_oldkext`, `gate_*`, `v4_*`, `dispovr_real*`, `one_acer`, all `b21_*` (login-window guard: `defaults` / PlistBuddy handling) |
| `b15test.sh` | 13 passed, 146 failed |
| `phxctl_serve_test.sh` | fails at "build" (it compiles `phxctl` against IOKit) |

The remaining failures are BSD/macOS differences in the mocks or the scripts (to investigate one by one). Likely
causes:

- `stat -f`;
- `date -j`;
- `sed -i ''`;
- PlistBuddy and `defaults` shims;
- `/usr/bin/...` absolute paths that the scripts call directly.

Fixing them must not change behaviour on macOS. Prefer making the mocks provide shims over editing the production
scripts. Where a script hardcodes `/usr/bin/x`, route it through a `PHXBOOT_*`-style override as the daemon already
does.

## 2026-11-23: the suites made portable (thread 00)

Every suite now sources `tests/mock/lib/portable.sh` ([README](../../tests/mock/README.md), "Running on Linux"). It
provides:
- the work-dir root `MOCKTMP`;
- `mock_sedi` (GNU / BSD `sed -i`);
- shims in `tests/mock/lib/bin/` for PlistBuddy, plutil, BSD `stat -f`, md5 and shasum.

Three production scripts got an override that is unset in production, so their behaviour doesn't change:
`DI_STAT` (`daemon_install.sh`), `ESPF_STAT` (`esp_flags.sh`) and `VERMILION_PLISTBUDDY` (`recovery_install.sh`).
`phxctl_serve_test.sh` prints SKIP off macOS. Nothing in `tools/phxctl` or `tests/mock` uses `date -j` / `-v`.

All 22 suites were run on the XTX machine (macOS 26.7.1) three ways:
- before, on plain macOS at `46bfd53`;
- after, on plain macOS;
- after, Linux-like: GNU coreutils / sed / grep / gawk / findutils (MacPorts) first on PATH, `sh` = dash,
  `MOCK_SHIMS=1`, `TMPDIR=/tmp`, `MOCK_SYSPATH` GNU-first.

All three runs gave exactly the same numbers for every suite:

| Suite | Result (all three runs) |
|---|---|
| `run.sh` | 225 passed, 0 failed |
| `b15test.sh` | 159 passed, 0 failed |
| `m7test.sh` | 62 passed, 0 failed |
| `dettest.sh` | 11 passed, 0 failed |
| `espflags_test.sh` | 10 passed, 0 failed |
| `daemon_install_test.sh` | 13 passed, 0 failed |
| `recovery_install_test.sh` | 6 passed, 0 failed |
| `perflog_test.sh` / `wifilog_test.sh` | 10 / 8 passed, 0 failed |
| `reboot_to_linux_test.sh` / `linux_pci_capture_test.sh` | 6 / 4 passed, 0 failed |
| `wm144_guard_test.sh` | 16 passed, 0 failed |
| `linux_display_capture_test.py` | 14/14 |
| `p0prefetch_test.sh` | SKIP (exit 77, no register dump) |
| `relight_mut.sh` / `watchsrv_mut.sh` / `hotplug0_mut.py` | 50 / 11 / 8 caught, 0 missed, 0 bad |
| `phxctl_serve_test.sh` | 8 passed, **1 failed** (failed before the change too) |
| `b21guard_mut.sh` | 14 caught, 0 missed, **1 bad** (before too) |
| `bootflags_mut.sh` | 14 caught, 0 missed, **2 bad** (before too) |
| `bootrate_mut.sh` | 51 caught, **4 missed, 1 bad** (before too) |
| `wsprefs_mut.sh` | 16 caught, 0 missed, **1 bad** (before too) |

Still open:
- A run on a real Linux machine is **(unverified)**. On the Mac, `/usr/bin/<tool>` called by full path stays BSD, and
  the off-macOS SKIP in `phxctl_serve_test.sh` hasn't run.
- The five failing results above are older than this change and are not investigated yet. They are in
  [TODO.md](../TODO.md), thread 12.
