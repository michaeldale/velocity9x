# S3 Trio64 + Voodoo2 Hybrid 3D Architecture

## Goal

Use an **S3 Trio64** as the normal Windows 95/98 display and 2D
accelerator, while using a **3dfx Voodoo2** as a 3D coprocessor.

The design supports two presentation paths:

1.  **Windowed Direct3D:** Voodoo2 renders the 3D scene, then the
    completed image is read back over PCI and copied into the Trio64
    framebuffer.
2.  **Fullscreen Direct3D/Glide:** Voodoo2 takes over the monitor using
    its normal analogue VGA pass-through switching, avoiding framebuffer
    copying and retaining native Voodoo2 fullscreen performance.

The physical VGA pass-through cable is retained.

------------------------------------------------------------------------

## Confirmed Voodoo2 Controls

The Voodoo2 exposes the low-level controls needed for this architecture.

  -----------------------------------------------------------------------
  Function                Voodoo2 control         Purpose
  ----------------------- ----------------------- -----------------------
  VGA pass-through        `FBIINIT0`, bit 0       Controls whether the
                                                  Trio64 signal passes
                                                  through or the Voodoo2
                                                  owns the display output

  Enable framebuffer      `FBIINIT1`, bit 3       `EN_LFB_READ` enables
  reads                                           linear-framebuffer
                                                  reads

  Select framebuffer      `LFBMODE`, bits 7:6     Select front, back, or
                                                  depth buffer for LFB
                                                  access

  Pixel access            Linear framebuffer      Supports 16-bit
                                                  framebuffer pixels over
                                                  PCI

  PCI read optimisation   `FBIINIT4`              Includes LFB read-ahead
                                                  / PCI read optimisation
                                                  controls
  -----------------------------------------------------------------------

The Linux `sstfb` driver implements VGA pass-through switching
approximately as:

``` c
if (passthrough)
    fbiInit0 &= ~DIS_VGA_PASSTHROUGH;
else
    fbiInit0 |= DIS_VGA_PASSTHROUGH;
```

It temporarily enables access to protected Voodoo initialization
registers through PCI configuration register `0x40`.

Useful references:

-   Voodoo2 specification:
    https://people.freedesktop.org/\~anholt/specs/3dfx/voodoo2.pdf
-   Linux `sstfb` implementation:
    https://codebrowser.dev/linux/linux/drivers/video/fbdev/sstfb.c.html
-   Linux `sstfb` register definitions:
    https://codebrowser.dev/linux/linux/include/video/sstfb.h.html

------------------------------------------------------------------------

# Presentation Architecture

``` text
                    Direct3D
                       |
                  Voodoo2 renders
                       |
            +----------+----------+
            |                     |
         WINDOWED              FULLSCREEN
            |                     |
     V2 framebuffer          Voodoo2 DAC
            |                     |
       PCI readback          analogue switch
            |                     |
       Trio64 VRAM           VGA output
            |                     |
       Trio64 DAC                 |
            +----------+----------+
                       |
                    Monitor
```

------------------------------------------------------------------------

# 1. Windowed Direct3D

During ordinary Windows operation, the Voodoo2 remains in VGA
pass-through mode.

Conceptually:

``` text
FBIINIT0.DIS_VGA_PASSTHROUGH = 0
```

Display path:

``` text
Trio64 framebuffer
        |
    Trio64 DAC
        |
Voodoo2 VGA input
        |
Voodoo2 pass-through
        |
      Monitor
```

Windows, GDI, DirectDraw and normal 2D operations remain on the Trio64.

The Voodoo2 can still render internally.

## Voodoo2 Rendering Path

``` text
D3D application
       |
Custom D3D HAL
       |
Voodoo2
       |
Voodoo2 back buffer
```

Enable linear framebuffer reads:

``` text
FBIINIT1.EN_LFB_READ = 1
```

Select the desired framebuffer using `LFBMODE`.

For example:

``` text
LFBMODE[7:6] = 00   front buffer
LFBMODE[7:6] = 01   back buffer
```

The completed image can then be transferred:

``` text
Voodoo2 back buffer
       |
PCI 32-bit reads
       |
      CPU
       |
Trio64 framebuffer
       |
Windows desktop
```

The Voodoo2 specification describes LFB reads as returning two 16-bit
pixels per 32-bit PCI read, making a 16-bit RGB desktop/render target
particularly attractive.

The Voodoo2 does not need to understand Windows clipping or window
positions. It simply renders its framebuffer. The hybrid driver treats
that framebuffer as an off-screen 3D surface and composites the required
rectangle into Trio64 VRAM.

------------------------------------------------------------------------

# 2. Fullscreen Direct3D / Glide

Fullscreen mode should avoid PCI framebuffer readback completely.

When an application enters an appropriate exclusive fullscreen mode:

``` text
Wait for Voodoo2 idle
        |
Configure Voodoo2 video timing
        |
Configure framebuffer
        |
Enable Voodoo2 video output
        |
Disable VGA pass-through
        |
Voodoo2 owns monitor
```

Conceptually:

``` text
FBIINIT0.DIS_VGA_PASSTHROUGH = 1
```

Display path becomes:

``` text
Voodoo2 framebuffer
       |
Voodoo2 RAMDAC
       |
VGA output
       |
Monitor
```

The Trio64 can remain configured in the background with the Windows
desktop still resident in its VRAM.

When the game exits fullscreen or an Alt-Tab transition occurs:

``` text
Wait for Voodoo2 idle
        |
Enable VGA pass-through
        |
Trio64 becomes visible
```

Conceptually:

``` text
FBIINIT0.DIS_VGA_PASSTHROUGH = 0
```

The display returns to:

``` text
Trio64 DAC
    |
Voodoo2 pass-through
    |
Monitor
```

This allows fullscreen rendering to retain essentially the normal
Voodoo2 presentation architecture and avoids the PCI readback
bottleneck.

------------------------------------------------------------------------

# Driver Architecture

A Windows 95/98 hybrid driver could conceptually expose a single
accelerated graphics environment:

``` text
                    Windows 95/98
                         |
                  DirectDraw / D3D
                         |
                    Hybrid HAL
                   /          \
                  /            \
             2D / GDI           3D
                |                |
             Trio64           Voodoo2
                ^                |
                |                |
                +-- Windowed ----+
                |   PCI LFB
                |
                +-- Fullscreen
                    Voodoo2 DAC
```

Responsibilities would be divided approximately as follows.

## S3 Trio64

-   Windows desktop
-   GDI acceleration
-   2D BitBLT
-   screen framebuffer
-   hardware cursor
-   DirectDraw operations where appropriate
-   normal VGA output
-   final presentation of windowed Voodoo2 output

## Voodoo2

-   triangle rasterization
-   texture mapping
-   Z buffering
-   Gouraud shading
-   fog
-   alpha/blending features supported by Voodoo2
-   texture memory
-   fullscreen framebuffer
-   fullscreen video output
-   off-screen 3D rendering for windowed mode

------------------------------------------------------------------------

# Windowed Frame Synchronisation

Voodoo2 LFB reads are not equivalent to modern asynchronous GPU
readback.

Framebuffer reads interact with the graphics pipeline and can force
synchronization. A naive implementation that repeatedly reads the
framebuffer while rendering could therefore perform badly.

The initial implementation should use a simple synchronous sequence:

``` text
Render frame N into Voodoo2 back buffer
               |
          Finish frame
               |
        Wait for V2 idle
               |
     Read required rectangle
          over PCI
               |
      Copy into Trio64 VRAM
               |
        Render frame N+1
```

This is deliberately conservative.

Once working, command scheduling and buffering can be investigated to
reduce synchronization overhead.

------------------------------------------------------------------------

# PCI Bandwidth Requirements

For a 16-bit framebuffer:

  Render size     Bytes/frame      30 FPS      60 FPS
  ------------- ------------- ----------- -----------
  320x240             153,600    4.6 MB/s    9.2 MB/s
  400x300             240,000    7.2 MB/s   14.4 MB/s
  512x384             393,216   11.8 MB/s   23.6 MB/s
  640x480             614,400   18.4 MB/s   36.9 MB/s

These are payload figures only.

Actual PCI traffic and performance will be worse because of transaction
overhead, chipset behaviour, synchronization and the subsequent write
into Trio64 VRAM.

Therefore:

-   **320x240 windowed:** very promising
-   **400x300 windowed:** promising
-   **512x384 windowed:** needs measurement
-   **640x480 windowed:** strongly dependent on PCI readback performance
-   **Fullscreen:** no readback bandwidth problem because the Voodoo2
    directly drives the display

Partial/dirty-rectangle copying could substantially reduce traffic when
the complete Voodoo2 render target does not need to be transferred.

------------------------------------------------------------------------

# PCI Read Optimisation

The Voodoo2 exposes controls in `FBIINIT4` associated with PCI/LFB read
behaviour, including LFB read-ahead.

These should be benchmarked rather than assumed to improve every
motherboard/chipset combination.

Potential tests:

``` text
normal LFB reads
LFB read-ahead enabled
fast PCI read options
different DWORD burst sizes
different source alignment
different destination alignment
different PCI chipsets
```

------------------------------------------------------------------------

# First Proof of Concept

Do **not** start by implementing Direct3D.

The first milestone should be a small Windows 9x VxD plus a user-mode
test application.

## V2HYBRID.VXD

Responsibilities:

``` text
Enumerate PCI devices
        |
Find 3dfx Voodoo2
        |
Map Voodoo2 MMIO/BAR
        |
Enable protected init-register access
        |
Configure LFB reads
        |
Control VGA pass-through
        |
Expose framebuffer read operation
```

Potential interfaces exposed to the test program:

``` c
V2_GetInfo();
V2_SetPassthrough(bool enabled);
V2_EnableLFBRead(bool enabled);
V2_SelectReadBuffer(...);
V2_WaitIdle();
V2_ReadFramebuffer(...);
```

The exact interface should be designed after establishing the Win9x VxD
environment and existing 3dfx register definitions.

------------------------------------------------------------------------

# V2TEST.EXE

The user-mode test program should:

1.  Detect the VxD.
2.  Detect the Voodoo2.
3.  Report PCI BARs and mapped regions.
4.  Test VGA pass-through switching.
5.  Initialize a known Voodoo2 rendering mode.
6.  Render a known pattern or triangle.
7.  Read the resulting framebuffer over PCI.
8.  Validate pixel correctness.
9.  Benchmark sequential framebuffer reads.
10. Display/copy the resulting image through the Trio64.

------------------------------------------------------------------------

# Test A --- VGA Mux

First prove that software can reliably change display ownership.

Example keyboard test:

``` text
Key 1 -> Trio64 visible
Key 2 -> Voodoo2 visible
Key 1 -> Trio64 visible
Key 2 -> Voodoo2 visible
```

Test:

-   repeated switching
-   switching while Voodoo2 idle
-   switching after rendering
-   restoration after application termination
-   restoration after a driver/application crash where practical

A safe recovery mechanism is important because an incorrect mux state
could leave the user with a blank display.

------------------------------------------------------------------------

# Test B --- Framebuffer Readback

Render a deterministic pattern using the Voodoo2.

For example:

``` text
red quadrant
green quadrant
blue quadrant
white quadrant
```

or a simple textured triangle.

Read the Voodoo2 framebuffer through the LFB aperture into system RAM.

Validate:

-   pixel order
-   RGB565/RGB555 interpretation
-   scanline orientation
-   stride
-   front-buffer selection
-   back-buffer selection
-   clipping
-   framebuffer offsets

Then copy the image to the Trio64 framebuffer.

Successful completion proves the fundamental windowed-rendering
architecture.

------------------------------------------------------------------------

# Test C --- PCI Readback Benchmark

Measure sequential Voodoo2 framebuffer read performance.

Benchmark:

``` text
320x240x16
400x300x16
512x384x16
640x480x16
```

Measure at least:

-   MB/s
-   milliseconds per frame
-   theoretical maximum FPS
-   CPU utilization
-   effect of LFB read-ahead
-   effect of PCI read options
-   front versus back buffer if relevant

For example, if actual Voodoo2 readback is 25 MB/s:

``` text
640x480x16 = 614,400 bytes

25,000,000 / 614,400
    ~= 40.7 complete framebuffer reads/sec
```

That does **not** mean 40 FPS in a real application because rendering,
synchronization and Trio64 copying still consume time.

------------------------------------------------------------------------

# Test D --- Windowed Presentation

Once framebuffer readback works:

1.  Create a normal Windows window.
2.  Render continuously on the Voodoo2.
3.  Read the completed render target.
4.  Copy it into the corresponding Trio64 framebuffer region.
5.  Handle window movement.
6.  Add clipping.
7.  Test partially obscured windows.
8.  Test different desktop colour depths.

Initial target:

``` text
Windows desktop: 640x480x16 or 800x600x16
3D window:       320x240x16
```

Keeping both devices at compatible 16-bit formats avoids expensive
colour conversion.

------------------------------------------------------------------------

# Test E --- Hybrid Fullscreen Switching

Once windowed presentation works:

``` text
Windows desktop
     |
  Trio64
     |
Start fullscreen 3D
     |
Configure Voodoo2
     |
Switch VGA mux
     |
Native Voodoo2 fullscreen
     |
Exit / Alt-Tab
     |
Switch VGA mux
     |
Original Trio64 desktop
```

The key goal is to demonstrate that the same driver/runtime can move
cleanly between the two presentation models.

------------------------------------------------------------------------

# Direct3D Phase

Only after the hardware tests work should Direct3D support be
implemented.

A potential flow is:

``` text
Application
    |
Direct3D
    |
Custom HAL
    |
Translate D3D state
    |
Voodoo2 commands
    |
Render
    |
Presentation decision
   / \
  /   \
Window Fullscreen
 |       |
PCI     V2 DAC
 |       |
Trio64  Monitor
```

The HAL must translate relevant Direct3D state into Voodoo2 hardware
state, including:

-   render target
-   texture state
-   texture coordinates
-   Z-buffer state
-   blending
-   fog
-   Gouraud shading
-   clipping
-   viewport
-   triangle setup
-   surface management

Existing 3dfx Glide and open-source driver code should be used as
reference material rather than re-deriving all Voodoo2 hardware
programming from scratch.

------------------------------------------------------------------------

# Presentation Decision

Conceptually:

``` c
if (exclusive_fullscreen && mode_supported_by_voodoo2) {
    wait_for_voodoo2_idle();
    configure_voodoo2_video_mode();
    disable_vga_passthrough();
    present_using_voodoo2_dac();
}
else {
    enable_vga_passthrough();

    render_to_voodoo2_backbuffer();
    wait_for_voodoo2_idle();

    read_voodoo2_render_target();
    copy_to_trio64_window();
}
```

The real implementation will require significantly more state
management, but this captures the central architecture.

------------------------------------------------------------------------

# Why Keep the VGA Pass-Through Cable?

Removing the cable entirely would force fullscreen rendering through PCI
readback as well.

That would turn:

``` text
Voodoo2 -> RAMDAC -> monitor
```

into:

``` text
Voodoo2
   |
PCI read
   |
CPU
   |
PCI/VRAM write
   |
Trio64
   |
RAMDAC
   |
monitor
```

This adds substantial bandwidth consumption and latency for no benefit
in fullscreen operation.

Keeping the cable allows:

### Windowed

``` text
Voodoo2 rendering
       |
PCI framebuffer read
       |
Trio64 framebuffer
       |
Trio64 DAC
       |
Voodoo2 pass-through
       |
Monitor
```

### Fullscreen

``` text
Voodoo2 rendering
       |
Voodoo2 framebuffer
       |
Voodoo2 DAC
       |
Monitor
```

This provides the best characteristics of both architectures.

------------------------------------------------------------------------

# Expected Result

If successful, the machine effectively behaves as though it has a split
2D/3D graphics architecture:

``` text
S3 Trio64
==========
Display controller
Windows desktop
GDI acceleration
2D acceleration
Windowed presentation

3dfx Voodoo2
=============
3D rasterizer
Texture mapping
Z buffering
Blending
Fog
Fullscreen display
Off-screen windowed rendering
```

The PCI bus becomes the interconnect between the 3D accelerator and the
primary 2D/display adapter when operating windowed.

In fullscreen mode, the Voodoo2 bypasses that interconnect for
presentation and directly drives the monitor.

------------------------------------------------------------------------

# Recommended Development Order

1.  **PCI detection**
2.  **Map Voodoo2 registers**
3.  **Read/write safe Voodoo2 registers**
4.  **VGA mux switching**
5.  **Voodoo2 initialization**
6.  **Known test rendering**
7.  **LFB framebuffer readback**
8.  **PCI bandwidth benchmark**
9.  **Copy framebuffer into Trio64**
10. **320x240 windowed continuous rendering**
11. **Window clipping/movement**
12. **Fullscreen Voodoo2 transition**
13. **Windowed/fullscreen switching**
14. **DirectDraw integration**
15. **Minimal Direct3D HAL**
16. **D3D state translation**
17. **Game compatibility testing**
18. **Performance optimization**

The most important early milestone is **Step 8**. Real Voodoo2
framebuffer-read bandwidth on period PCI hardware determines how
practical the windowed presentation path is.

Fullscreen operation does not have this limitation.

------------------------------------------------------------------------

# Key Research References

## 3dfx Voodoo2 Specification

https://people.freedesktop.org/\~anholt/specs/3dfx/voodoo2.pdf

Contains the hardware-level information needed for:

-   FBI registers
-   framebuffer access
-   LFB configuration
-   PCI interface
-   initialization
-   VGA pass-through
-   rendering state

## Linux `sstfb`

https://codebrowser.dev/linux/linux/drivers/video/fbdev/sstfb.c.html

Useful working implementation of low-level Voodoo/Voodoo2 initialization
and framebuffer operation.

## Linux `sstfb` Header

https://codebrowser.dev/linux/linux/include/video/sstfb.h.html

Contains useful register addresses, bit definitions and initialization
constants.

------------------------------------------------------------------------

# Summary

The proposed architecture is:

``` text
WINDOWED D3D

Voodoo2
   |
3D render
   |
LFB read over PCI
   |
Trio64 VRAM
   |
Trio64 DAC
   |
Voodoo2 VGA pass-through
   |
Monitor


FULLSCREEN D3D / GLIDE

Voodoo2
   |
3D render
   |
Voodoo2 framebuffer
   |
Voodoo2 DAC
   |
Monitor
```

The Voodoo2's software-controlled VGA pass-through and readable linear
framebuffer provide the fundamental hardware mechanisms required.

The first implementation should therefore be a **Win9x VxD proof of
concept for PCI discovery, Voodoo2 MMIO access, VGA mux control, LFB
framebuffer reads and throughput benchmarking** before attempting a
Direct3D HAL.
