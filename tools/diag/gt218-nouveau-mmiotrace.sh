#!/bin/bash
# Trace Linux nouveau bringing up a GT218, for step 1 of
# docs/plans/nvidia-gt218-linux-trace.md.
#
# Run as root on the BringupKit target, on a boot where nouveau is NOT
# loaded (blacklisted), so its whole load is inside the trace:
#
#     sudo bash gt218-nouveau-mmiotrace.sh
#
# It turns the kernel's mmiotrace tracer on, loads nouveau, waits for its
# init, turns the tracer off and leaves three files in ./gt218-trace/:
# the trace, the kernel log of the load, and lspci -vvv of the card and
# its root port. mmiotrace takes every CPU but one offline while it runs
# and brings them back afterwards (its documented behaviour).
#
# The console must not be on the card: /proc/fb must not list nouveau
# before this runs. The script refuses otherwise.
set -eu

OUT=${1:-./gt218-trace}
TRACING=/sys/kernel/tracing
SETTLE_SECONDS=20
BUFFER_KB=262144

if [ "$(id -u)" != 0 ]; then
    echo "Run this as root (sudo bash $0)." >&2
    exit 1
fi
if lsmod | grep -q '^nouveau '; then
    echo "nouveau is already loaded; blacklist it and reboot first." >&2
    exit 1
fi
if ! grep -q '^CONFIG_MMIOTRACE=y' "/boot/config-$(uname -r)"; then
    echo "This kernel has no CONFIG_MMIOTRACE." >&2
    exit 1
fi
[ -d "$TRACING" ] || mount -t tracefs nodev "$TRACING"

mkdir -p "$OUT"
lspci -nn > "$OUT/lspci.txt"
lspci -vvv -s 00:02.0 > "$OUT/lspci-rootport.txt"
lspci -vvv -xxxx -d 10de: > "$OUT/lspci-card.txt"
cat /proc/fb > "$OUT/proc-fb-before.txt"
dmesg > "$OUT/dmesg-before.txt"

# One large buffer: the tracer runs on a single CPU, and a lost event is
# a hole in the comparison.
echo "$BUFFER_KB" > "$TRACING/buffer_size_kb"
echo mmiotrace > "$TRACING/current_tracer"
cat "$TRACING/trace_pipe" > "$OUT/mmiotrace.txt" &
PIPE=$!

echo "MARK gt218 modprobe nouveau" > "$TRACING/trace_marker"
modprobe nouveau
sleep "$SETTLE_SECONDS"
echo "MARK gt218 settle done" > "$TRACING/trace_marker"

# The reader first: the tracer cannot be changed while trace_pipe is open
# (the first run failed here with EBUSY and left mmiotrace on).
sleep 2
kill "$PIPE" 2>/dev/null || true
wait "$PIPE" 2>/dev/null || true
echo nop > "$TRACING/current_tracer"

dmesg > "$OUT/dmesg-after.txt"
cat /proc/fb > "$OUT/proc-fb-after.txt"
grep -c . "$OUT/mmiotrace.txt" > "$OUT/trace-lines.txt"
if [ -n "${SUDO_USER:-}" ]; then
    chown -R "$SUDO_USER" "$OUT"
fi
echo "Trace: $OUT/mmiotrace.txt ($(cat "$OUT/trace-lines.txt") lines)"
grep -i 'lost\|overrun' "$OUT/dmesg-after.txt" | tail -3 || true
