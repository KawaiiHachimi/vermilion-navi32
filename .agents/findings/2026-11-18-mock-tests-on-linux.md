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
