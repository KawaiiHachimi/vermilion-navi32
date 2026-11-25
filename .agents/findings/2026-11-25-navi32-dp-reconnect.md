# Navi 32 DP reconnect (2026-11-25)

A regular `phxPipe0SetMode` request for the current mode returns `kSame` before running the shell. The existing
round trip is the checked way to run that shell without changing timing. NullMoth follows the relight with its
registered connection interrupt rather than a dimensions event.

The new path needs a trained route. If a box test shows HPD high without a stream from OTG0, capture that case
separately: it did not reach pipe replay. See the test procedure in `docs/NAVI32.md`. No box access in this pass.
