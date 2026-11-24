#!/bin/bash
# SPDX-License-Identifier: MIT
# navi32_daemon_up.sh: what the boot daemon calls on a Navi 32 port.
#
# phxboot-navi31.sh runs $G/phxctl/navi31_up.sh for the bring-up. On this port that file is replaced by a three-line
# wrapper around navi32_up.sh, so the daemon runs the Navi 32 sequence without touching the daemon itself:
#
#   cp -p ~/gpu/phxctl/navi31_up.sh ~/gpu/phxctl/navi31_up.sh.xtx      # keep the Navi 31 one
#   cp -p navi32_daemon_up.sh ~/gpu/phxctl/navi31_up.sh               # what the daemon calls
#
# PHX_SKIP_VCN=1 because the daemon runs VCN itself after its own done line, and it owns /var/log/phxboot.log -
# navi32_up.sh would truncate that file and destroy the daemon's record and its vcn_up.sh pre-flight marker.
#
# docs/NAVI32.md ("Two things a port has to get right for the daemon") explains both.
set -u
HERE=$(cd "$(dirname "$0")" && pwd)
PHX_SKIP_VCN=1 exec bash "$HERE/navi32_up.sh" --no-test