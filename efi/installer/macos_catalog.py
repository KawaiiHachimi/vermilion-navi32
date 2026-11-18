#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""List the full macOS installers (InstallAssistant.pkg) in Apple's public software update catalog.

Recovery images (macrecovery) always serve the newest release; this finds a specific version, e.g. the 26.7.1 that
Vermilion was validated with. Standard library only; runs on Linux and macOS.

  efi/installer/macos_catalog.py                 # the last 15 installers: date, product, version, build, size
  efi/installer/macos_catalog.py --version 26.7.1 --url   # print only that installer's download URL
"""
import argparse
import gzip
import plistlib
import re
import sys
import urllib.request

CATALOG = ("https://swscan.apple.com/content/catalogs/others/index-26-15-14-13-12-10.16-10.15-10.14-10.13-10.12-"
           "10.11-10.10-10.9-mountainlion-lion-snowleopard-leopard.merged-1.sucatalog.gz")
UA = {"User-Agent": "Software%20Update"}


def get(url, timeout=60, tries=3):
    last = None
    for _ in range(tries):
        try:
            with urllib.request.urlopen(urllib.request.Request(url, headers=UA), timeout=timeout) as r:
                return r.read()
        except OSError as e:          # Apple's CDN resets connections now and then
            last = e
    raise last


def installers(limit):
    catalog = plistlib.loads(gzip.decompress(get(CATALOG)))
    rows = []
    for pid, p in catalog["Products"].items():
        pkg = [x for x in p.get("Packages", []) if x["URL"].endswith("InstallAssistant.pkg")]
        dist = p.get("Distributions", {})
        if pkg and dist:
            rows.append((p["PostDate"], pid, pkg[0], dist.get("English") or next(iter(dist.values()))))
    rows.sort(key=lambda r: r[0])
    for date, pid, pkg, dist in rows[-limit:]:
        text = get(dist).decode("utf-8", "replace")
        ver = re.search(r"<key>VERSION</key>\s*<string>([^<]+)", text)
        build = re.search(r"<key>BUILD</key>\s*<string>([^<]+)", text)
        yield {"date": date.date().isoformat(), "product": pid, "version": ver and ver.group(1),
               "build": build and build.group(1), "size": pkg["Size"], "url": pkg["URL"]}


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--version", help="only this macOS version (e.g. 26.7.1)")
    ap.add_argument("--url", action="store_true", help="print only the download URL (with --version)")
    ap.add_argument("--limit", type=int, default=15, help="how many of the newest installers to look at (default 15)")
    args = ap.parse_args()
    found = False
    for r in installers(args.limit if not args.version else 60):
        if args.version and r["version"] != args.version:
            continue
        found = True
        if args.url:
            print(r["url"])
            return
        print(f"{r['date']}  {r['product']:<10} {r['version']:<8} {r['build']:<9} {r['size'] / 1e9:5.1f} GB")
    if args.version and not found:
        sys.exit(f"macOS {args.version} is not in Apple's catalog (any more)")


if __name__ == "__main__":
    main()
