"""Line up the MB/s column of two dispbench VERDICTS.md files.

Usage: vcmp.py off/VERDICTS.md on/VERDICTS.md [prefix]
Prints operation, off MB/s, on MB/s and on/off for every row present in
both, filtered by prefix (default "Gdi.").
"""
import sys


def rows(path):
    table = {}
    for line in open(path, encoding="utf-8"):
        cells = [c.strip() for c in line.strip().strip("|").split("|")]
        if len(cells) < 3 or cells[0] in ("Operation", "Ceiling", "Check"):
            continue
        name = cells[0]
        for value in cells[1:3]:
            try:
                table[name] = float(value.replace(",", ""))
                break
            except ValueError:
                continue
    return table


off = rows(sys.argv[1])
on = rows(sys.argv[2])
prefix = sys.argv[3] if len(sys.argv) > 3 else "Gdi."
print(f"| {'Operation':<44} | {'off':>10} | {'on':>10} | {'on/off':>7} |")
print(f"|{'-' * 46}|{'-' * 12}:|{'-' * 12}:|{'-' * 9}:|")
for name in sorted(off):
    if not name.startswith(prefix) or name not in on:
        continue
    ratio = on[name] / off[name] if off[name] else float("nan")
    print(f"| {name:<44} | {off[name]:>10,.1f} | {on[name]:>10,.1f} | {ratio:>7.2f} |")
