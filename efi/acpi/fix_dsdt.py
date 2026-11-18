#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Make a firmware DSDT loadable by macOS's ACPI interpreter (ACPICA 20160930).

The problem (seen on the ASUS B650E-I, BIOS 4005, and reported on other AM5 boards with newer firmware): the DSDT
defines devices inside module-level `If (...)` blocks and then, later and at the top level, opens
`Scope (\\path.to.that.device)`. Current ACPICA (Linux, Windows) executes module-level code while it loads the
table, so the device exists when the Scope is reached. macOS's ACPICA runs module-level code only after the whole
table is loaded, so the Scope fails ("Namespace lookup failure, AE_NOT_FOUND") and macOS throws away the entire
DSDT, and with it every SSDT that refers to it.

Making the DSDT load is not enough: the devices inside those blocks (on AM5, the whole chipset behind
GPP7.UP00: its USB, SATA, LAN and Wi-Fi) then still appear only through late module-level code, and macOS hangs
right after "CoreAnalyticsHub start completed" (B650E-I, 2026-11-18; the same hang and the same cure, blocks made
unconditional, on other ASUS AM5 boards: nullmoth/1401 issue 21).

Two fixes, best first:

1. resolve (with --namespace): the Linux ACPI namespace of the same machine and BIOS says which conditional blocks
   are really active. Active blocks (all their devices exist under Linux) are unwrapped, so their devices are
   defined while the table loads, as on Linux. Inactive blocks (none of their devices exist) are removed.
   Blocks that are partly present, or followed by Else, are left alone and reported.
2. wrap (always, after 1): every top-level Scope that still points into a device defined only inside a
   module-level If is wrapped in that same condition, so the table at least loads.

Works on iasl's disassembly (braces on their own lines). Usage:
  fix_dsdt.py DSDT.dsl -o DSDT-fixed.dsl [--namespace paths.txt]   # paths.txt: cat /sys/bus/acpi/devices/*/path
"""
import argparse
import re
import sys

class Block:
    def __init__(self, header, start, end, parent):
        self.header = header          # the statement text before '{' (multi-line headers joined)
        self.start = start            # index of the first header line
        self.end = end                # index of the closing '}' line
        self.parent = parent
        self.children = []

    @property
    def kind(self):
        m = re.match(r"\s*(\w+)", self.header)
        return m.group(1) if m else ""

    def arg(self):
        m = re.match(r"\s*\w+\s*\((.*)\)\s*$", self.header, re.S)
        return m.group(1).strip() if m else ""


def code_of(line):
    """The line without comments and string literals (iasl puts braces and quotes in data comments)."""
    line = re.sub(r'"(?:[^"\\]|\\.)*"', '""', line)
    line = re.sub(r"/\*.*?\*/", "", line)
    return line.split("//", 1)[0]


def parse(lines):
    """Build the block tree. A block header is the statement (possibly spanning lines) right before its '{'."""
    root = Block("Root", 0, len(lines) - 1, None)
    cur = root
    pending = []                                         # indexes of the lines of the current statement
    for i, line in enumerate(lines):
        code = code_of(line).strip()
        if not code:
            continue
        if code == "{":
            start = pending[0] if pending else i
            header = " ".join(code_of(lines[k]).strip() for k in pending)
            b = Block(header, start, None, cur)
            cur.children.append(b)
            cur = b
            pending = []
            continue
        if code.startswith("}"):
            cur.end = i
            cur = cur.parent
            if cur is None:
                raise ValueError(f"unbalanced '}}' at line {i + 1}")
            pending = []
            continue
        if pending and not balanced(" ".join(code_of(lines[k]) for k in pending)):
            pending.append(i)                            # continues an open parenthesis
        else:
            pending = [i]
    return root


def balanced(text):
    return text.count("(") == text.count(")")


def norm(path):
    p = path.strip().strip('"').lstrip("\\")
    parts = [x.rstrip("_") or x for x in p.split(".")]
    return ".".join(parts)


def walk(block, prefix, in_cond, top_if, out_devices, out_scopes, depth):
    """Collect devices defined under module-level If blocks, and top-level Scopes."""
    for b in block.children:
        kind = b.kind
        if kind == "Method":
            continue                                     # code inside methods runs later; not module-level
        path = prefix
        if kind in ("Scope", "Device"):
            arg = b.arg()
            path = norm(arg) if arg.startswith("\\") else ".".join(x for x in (prefix, norm(arg)) if x)
        cond = in_cond or kind in ("If", "Else", "ElseIf")
        tif = top_if
        if kind == "If" and top_if is None and depth == 1:
            tif = b
        if kind == "Device":
            out_devices.setdefault(path, []).append(tif if cond else None)
        if kind == "Scope" and depth == 1:
            out_scopes.append((b, path))
        walk(b, path, cond, tif, out_devices, out_scopes, depth + 1)


def devices_in(block, prefix):
    """Every Device defined (outside methods) inside a block, as normalised absolute paths."""
    out = []
    for c in block.children:
        if c.kind == "Method":
            continue
        path = prefix
        if c.kind in ("Scope", "Device"):
            arg = c.arg()
            path = norm(arg) if arg.startswith("\\") else ".".join(x for x in (prefix, norm(arg)) if x)
        if c.kind == "Device":
            out.append(path)
        out += devices_in(c, path)
    return out


def scope_targets_in(block, prefix):
    """Every Scope target (outside methods) inside a block, as normalised absolute paths."""
    out = []
    for c in block.children:
        if c.kind == "Method":
            continue
        path = prefix
        if c.kind in ("Scope", "Device"):
            arg = c.arg()
            path = norm(arg) if arg.startswith("\\") else ".".join(x for x in (prefix, norm(arg)) if x)
        if c.kind == "Scope":
            out.append(path)
        out += scope_targets_in(c, path)
    return out


def resolve(text, namespace):
    """Unwrap the module-level If blocks whose devices all exist in `namespace`; remove those with none."""
    lines = text.split("\n")
    root = parse(lines)
    top = root.children[0].children
    devmap, _ = {}, []
    walk(root.children[0], "", False, None, devmap, _, 1)
    conditional_only = {p for p, ifs in devmap.items() if all(x is not None for x in ifs)}
    plan = []
    for i, b in enumerate(top):
        if b.kind != "If":
            continue
        devs = devices_in(b, "")
        what = "devices"
        scope_only = not devs
        if scope_only:
            # A block that only extends devices defined elsewhere. Its targets say something about its condition
            # only if they are themselves defined conditionally; \_GPE, \_SB or unconditional devices say nothing.
            devs = [t for t in scope_targets_in(b, "") if t in conditional_only]
            what = "conditional Scope targets"
        if not devs:
            continue
        nxt = top[i + 1] if i + 1 < len(top) else None
        if nxt is not None and nxt.kind in ("Else", "ElseIf"):
            plan.append(("keep", b, f"followed by {nxt.kind}"))
            continue
        present = sum(1 for d in devs if d in namespace)
        if scope_only and present:
            plan.append(("keep", b, f"{present}/{len(devs)} {what} present; left to the firmware's condition"))
        elif present == len(devs):
            plan.append(("unwrap", b, f"{present}/{len(devs)} {what} present"))
        elif present == 0:
            plan.append(("remove", b, f"0/{len(devs)} {what} present"))
        else:
            plan.append(("keep", b, f"only {present}/{len(devs)} {what} present"))
    for action, b, why in sorted(plan, key=lambda p: p[1].start, reverse=True):
        if action == "unwrap":
            body_start = next(k for k in range(b.start, b.end) if code_of(lines[k]).strip() == "{") + 1
            body = [l[4:] if l.startswith("    ") else l for l in lines[body_start:b.end]]
            lines[b.start:b.end + 1] = body
        elif action == "remove":
            lines[b.start:b.end + 1] = []
    return "\n".join(lines), [(a, b.header.strip(), b.start + 1, why) for a, b, why in plan]


def fix(text):
    lines = text.split("\n")
    root = parse(lines)
    defblock = root.children[0]                          # DefinitionBlock (...)
    devices, scopes = {}, []
    walk(defblock, "", False, None, devices, scopes, 1)

    def defining_if(path):
        """The top-level If that defines the device a path points into, if it is only defined conditionally."""
        parts = path.split(".")
        for n in range(len(parts), 0, -1):
            p = ".".join(parts[:n])
            if p in devices:
                ifs = devices[p]
                if any(x is None for x in ifs):
                    return None                          # also defined unconditionally: no problem
                return ifs[0]
        return None

    edits = []
    for scope, path in scopes:
        cond_block = defining_if(path)
        if cond_block is None or cond_block.end > scope.start:
            continue                                     # target defined unconditionally, or after this Scope
        cond = cond_block.header.strip()
        edits.append((scope, cond, path))

    for scope, cond, path in sorted(edits, key=lambda e: e[0].start, reverse=True):
        indent = re.match(r"^(\s*)", lines[scope.start]).group(1)
        body = lines[scope.start:scope.end + 1]
        wrapped = [indent + cond, indent + "{"] + ["    " + l if l.strip() else l for l in body] + [indent + "}"]
        lines[scope.start:scope.end + 1] = wrapped
    return "\n".join(lines), [(path, cond, scope.start + 1) for scope, cond, path in edits]


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("dsl")
    ap.add_argument("-o", "--output", required=True)
    ap.add_argument("--namespace", help="file with the machine's ACPI device paths under Linux (resolve mode)")
    args = ap.parse_args()
    with open(args.dsl) as f:
        text = f.read()
    changed = False
    if args.namespace:
        with open(args.namespace) as f:
            namespace = {norm(p) for p in f.read().split()}
        text, plan = resolve(text, namespace)
        for action, cond, line, why in plan:
            print(f"  line {line}: {action:6} {cond}  ({why})")
            changed |= action != "keep"
    text, wraps = fix(text)
    for path, cond, line in wraps:
        print(f"  line {line}: Scope (\\{path}) wrapped in {cond}")
    with open(args.output, "w") as f:
        f.write(text)
    if not changed and not wraps:
        print("  nothing to fix")
        sys.exit(1)


if __name__ == "__main__":
    main()
