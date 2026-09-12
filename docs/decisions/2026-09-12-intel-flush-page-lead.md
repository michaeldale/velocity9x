# The 945 "Intel Flush Page": what it is, and what it does not explain

Status: research note, 2026-09-12. The Phase 4 errata gate
(`docs/decisions/2026-09-12-intel-phase4-errata-gate.md`) stays closed. This
record exists so the lead is not re-chased from memory.

## The lead

Linux drains a chipset write buffer before the GPU reads CPU-written memory by
writing to a page whose physical address the driver programs into an
undocumented host-bridge config register. The resource is literally named
"Intel Flush Page". The claim that this is present in 2.6.32 is correct; it
first appears in 2.6.25 and is still there, moved to `intel-gtt.c`.

Checked against `drivers/char/agp/intel-agp.c` at tag `v2.6.32`:

```c
#define I915_IFPADDR    0x60        /* line 135 */
#define I965_IFPADDR    0x70        /* line 139 */

static void intel_i9xx_setup_flush(void)
{
	if (intel_private.ifp_resource.start) { return; }
	intel_private.ifp_resource.name = "Intel Flush Page";
	intel_private.ifp_resource.flags = IORESOURCE_MEM;
	if (IS_I965 || IS_G33 || IS_G4X) { intel_i965_g33_setup_chipset_flush(); }
	else                             { intel_i915_setup_chipset_flush(); }
	...ioremap_nocache(intel_private.ifp_resource.start, PAGE_SIZE);
}

static void intel_i915_chipset_flush(struct agp_bridge_data *bridge)
{
	if (intel_private.i9xx_flush_page)
		writel(1, intel_private.i9xx_flush_page);
}
```

`intel_i915_setup_chipset_flush()` reads D0:F0 `0x60`. If bit 0 is clear the
driver allocates a page of MMIO space itself and writes `addr | 1` back; if bit
0 is set the BIOS already placed the page and the driver only claims the
resource. The flush itself is a single dword store of `1` to that page.

The host bridge measured on MICHAEL-NETBOOK, `8086:27AC`, is
`PCI_DEVICE_ID_INTEL_82945GME_HB` in that same file (line 37) and is bound to
`intel_915_driver`, whose `.chipset_flush` is `intel_i915_chipset_flush`. So
the mechanism does cover 945GSE, by the 0x60 path, not the 965 one.

The ordering the DRM uses is visible in `i915_gem.c` at the same tag: CPU cache
lines out with `clflush`, then `drm_agp_chipset_flush()`, then `MI_FLUSH` in
the ring. Three distinct flushes of three distinct things. The chipset flush
sits exactly on the processor/integrated-graphics boundary the erratum names.

## Origin, and what the author said it was for

Dave Airlie, 29 October 2007, posted as "AGP initial support for chipset
flushing" (LWN 256335): "Certain Intel chipsets contains a global write buffer,
and this can require flushing from the drm or X.org to make sure all data has
hit RAM before initiating a GPU transfer, due to a lack of coherency with the
integrated graphics device and this buffer." He also stated the 915 case
involved "some guesswork ... from comments in the documentation".

The 8xx comment in the same file describes the older, cruder equivalent and
gives the buffer's size from the 865 specs — 64 octwords, 1 KiB — which the
driver overwrites and clflushes to push the previous contents out.

## What the public documentation says

- Mobile Intel 945 Express Chipset Family Datasheet, 309219-006, Table 3,
  Device 0 configuration registers: offsets `60-63h` are listed as
  **Reserved**. There is no section 5.1.x describing them. The register Linux
  programs is undocumented on this part.
- Intel 3 Series Datasheet, 316966, Table 5-1: the D0:F0 map runs `68-6Fh`
  DMIBAR then jumps to `90h` PAM0. `0x70` is not listed either.
- The string "write buffer" does not occur anywhere in 309219-006.

So the mechanism is real and shipped, but it is not licensed by any Intel
document we can cite, on 945 or on its successors.

## Assessment against erratum 12

Erratum 12, verbatim from 309220-0132: "A logic issue may cause an incorrect
internal buffer flush to occur. Specific sequence of processor and internal
graphics memory access must occur in a certain sequence for issue to occur."
Implication: "System may hang." Workaround: "available in Intel Graphics driver
for Windows XP PV14.31.1.4864 or later and for Windows Vista PV15.6.0.1322 or
later."

What matches:

- both concern an internal buffer between the processor and the integrated
  graphics, not the render cache, so `MI_FLUSH` remains the wrong candidate;
- both are triggered by a sequence of CPU and IGD memory accesses;
- the erratum's workaround lives in a *display driver*, and the flush page is
  the only known display-driver-reachable control over that buffer.

What does not match, and this is the part that stops the lead short:

- the flush page addresses **stale data** — the GPU reading memory the CPU
  wrote but the buffer has not yet posted. The erratum's implication is a
  **hang**. Nothing in the Linux history connects the flush page to a hang;
- the timeline runs the wrong way. The flush page is in Linux from January
  2008 and was written for 915/945/965/G33 generally. Intel's erratum-fixed XP
  driver is a later, 945-family-specific change. A workaround that was already
  two years old cannot be the thing Intel added;
- no public source — kernel, mesa, xf86-video-intel, IEGD community threads —
  ties either mechanism to erratum 12. Searched; nothing.

Conclusion: the flush page is a better-shaped candidate than `MI_FLUSH`, and it
is a mechanism we will eventually need for CPU-written command and vertex data
regardless of the erratum. It is not evidence of the erratum's workaround, and
it does not open the gate.

## Consequence for the arm contract

`v9x_i9xx_arm_evaluate()` gates ring submission. A flush-page write is neither
a graphics MMIO write nor a ring command: it is a config write to D0:F0 `0x60`
and a dword store to an arbitrary physical page. As the contract stands it
would not be gated at all. If flush-page support is ever implemented, both the
config write and the page store must be brought under the same gate, or the
gate's guarantee — "the package is incapable of touching the part" — becomes
false.

## Next probes, all read-only

1. Read D0:F0 `0x60` on MICHAEL-NETBOOK and record the dword. Bit 0 tells us
   whether this BIOS already placed a flush page, and the address tells us
   whether a Win9x implementation would have to allocate MMIO space itself —
   the harder and riskier half of the Linux code. This is a config-space read;
   it is inside what Phases 1-3 already do.
2. Binary-diff the Windows XP miniport across the erratum boundary: a build
   before 14.31.1.4864 against 14.31.1.4864 or later, looking for new config
   accesses to D0:F0 `0x60` or new MMIO writes. This is the only route to the
   actual workaround that does not depend on Intel publishing it, and it costs
   no hardware risk. 14.31.1 for 2000/XP is listed on driver archives; the
   immediately preceding build still has to be located.
3. Read `GFX_FLSH_CNTL` (`0x2170`, "915+ only" in `i915_reg.h`) into the Phase
   3 event matrix as a second candidate. It is the GPU-side counterpart —
   draining writes that went through the GTT aperture — and is untested here.

## Sources

- <https://lwn.net/Articles/256335/>
- <https://raw.githubusercontent.com/torvalds/linux/v2.6.32/drivers/char/agp/intel-agp.c>
- <https://raw.githubusercontent.com/torvalds/linux/v2.6.32/drivers/gpu/drm/i915/i915_gem.c>
- <https://www.intel.com/content/dam/www/public/us/en/documents/datasheets/mobile-945-express-chipset-datasheet.pdf> (309219-006)
- <https://www.intel.com/Assets/PDF/datasheet/316966.pdf> (3 Series, 316966)
- <https://www.intel.com/Assets/PDF/specupdate/309220.pdf> (309220-0132)
