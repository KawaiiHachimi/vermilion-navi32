#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Print one line per fio job from fio's JSON output: bandwidth, IOPS, completion-latency percentiles, CPU."""
import json
import sys

for path in sys.argv[1:]:
    for j in json.load(open(path))["jobs"]:
        for k in ("read", "write"):
            x = j[k]
            if x["io_bytes"] == 0:
                continue
            p = x["clat_ns"]["percentile"]
            print(f'{j["jobname"]:24s} {k:5s} {x["bw_bytes"]/1e6:8.0f} MB/s {x["iops"]:8.0f} IOPS  '
                  f'clat p50 {p["50.000000"]/1e3:7.1f}  p99 {p["99.000000"]/1e3:7.1f}  '
                  f'p99.9 {p["99.900000"]/1e3:8.1f} us  usr/sys {j["usr_cpu"]:.0f}/{j["sys_cpu"]:.0f}%')
