# Velocity9x family manifest: Matrox Millennium and Millennium II.
#
# The VBE 4F02h mode set programs the card and 4F01h reports where the
# framebuffer landed. The chips' engine hook has the mini-VDD map the
# control aperture, and DirectDraw and GDI fill and copy run on the drawing
# engine (MGA); everything else is drawn by the CPU
# (docs\plans\matrox-millennium-family.md).
#
# Both chips are one design and one family; the BAR order is per-chip hw16
# data. Until 2026-10-09 the Millennium II was a separate guarded drop-in
# family, matrox-m2, retired into this one.
#
# Vm.Emulator is none: the 86Box Win98SE-Millennium guest (port 9877) models
# the 2064W and is driven by hand, not by the VM runner; A8U4I5 carries the
# real 2064W.
@{
    SchemaVersion = 1
    Id = 'matrox'
    DisplayName = 'Matrox Millennium'
    Description = 'Matrox Millennium MGA-2064W, Millennium II MGA-2164W and Mystique (PCI 102B:0519, 102B:051B, 102B:051A): VBE mode set, the drawing engine for DirectDraw and GDI fill and copy, CPU drawing otherwise.'

    Chips = @(
        @{
            Id = 'mga2064w'
            Name = 'Matrox Millennium MGA-2064W'
            VendorId = '102B'
            DeviceId = '0519'
            # No SubsystemId: the measured part reads 0000, so the bare id is
            # the only one that binds it.
            DeviceDesc = 'Velocity9x Matrox Millennium MGA-2064W'
            Adapter = 'Matrox Millennium MGA-2064W'
            ClockDetector = 'matrox-mga2064w-unavailable-v1'
            ModeSwitching = 'vbe-lfb'
            # The drawing engine through BAR0's control aperture, for
            # DirectDraw fill and copy (eng_mga.c). No 3D engine on this
            # chip; Direct3D is the software rasterizer when selected.
            Acceleration = 'directdraw-fill-copy'
            Direct3D = 'not-advertised'
            EngineType = 'MGA'
            EngineCaps = @('SOLID_FILL', 'SCREEN_COPY', 'FLIP')
            # A floor. The part shipped with 2, 4 and 8 MiB; 2 MiB is the base
            # Millennium and covers every static mode below. The runtime heap
            # sizes from 4F00h, not from this
            # (docs\decisions\2026-09-11-the-2064w-aperture-opens-with-mgamode.md).
            VideoMemoryBytes = 2097152

            # Every VbeMode is in this BIOS's own list with a linear
            # framebuffer; see the comment over v9x_mga_modes in mga_hw16.c.
            Modes = @(
                @{ BitsPerPixel = 8; Width = 640; Height = 480; RefreshRate = 60; VbeMode = '0101' }
                @{ BitsPerPixel = 8; Width = 800; Height = 600; RefreshRate = 60; VbeMode = '0103' }
                @{ BitsPerPixel = 8; Width = 1024; Height = 768; RefreshRate = 60; VbeMode = '0105' }
                @{ BitsPerPixel = 8; Width = 640; Height = 400; RefreshRate = 60; VbeMode = '0100' }
                @{ BitsPerPixel = 16; Width = 640; Height = 480; RefreshRate = 60; VbeMode = '0111' }
                @{ BitsPerPixel = 16; Width = 800; Height = 600; RefreshRate = 60; VbeMode = '0114' }
                @{ BitsPerPixel = 16; Width = 1024; Height = 768; RefreshRate = 60; VbeMode = '0117' }
            )

            # The family object, not the chip module: the one instruction
            # signature this family owns is mga_hw16.c's CRTCEXT read through
            # port 3DEh, which no other family touches. Identity is carried by
            # MapSymbols and the INF's hardware-id set, as in the ati manifest.
            Objects = @('mga_hw16')
            Audit = @{
                Required = @(
                    'mov\s+dx,3deH\b'
                )
                Forbidden = @()
            }
            MapSymbols = @('v9x_mga2064w_device')
        }
        @{
            Id = 'mga2164w'
            Name = 'Matrox Millennium II MGA-2164W'
            VendorId = '102B'
            DeviceId = '051B'
            # No SubsystemId: the physical sample was 1200102B, but the bare
            # id binds every board of the chip.
            DeviceDesc = 'Velocity9x Matrox Millennium II MGA-2164W'
            Adapter = 'Matrox Millennium II MGA-2164W'
            ClockDetector = 'matrox-mga2164w-unavailable-v1'
            ModeSwitching = 'vbe-lfb'
            # The same drawing core as the 2064W, its control aperture in
            # BAR1, plus the texture engine the 2064W lacks: Direct3D through
            # d3d_mga.c (docs\plans\matrox-mga2164w-hardware-3d.md).
            Acceleration = 'directdraw-fill-copy'
            Direct3D = 'hardware-mga2164w'
            EngineType = 'MGA'
            EngineCaps = @('SOLID_FILL', 'SCREEN_COPY', 'FLIP', 'D3D')
            # The smallest Millennium II; the physical sample had 8 MiB. The
            # heap sizes from 4F00h.
            VideoMemoryBytes = 4194304

            Modes = @(
                @{ BitsPerPixel = 8; Width = 640; Height = 480; RefreshRate = 60; VbeMode = '0101' }
                @{ BitsPerPixel = 8; Width = 800; Height = 600; RefreshRate = 60; VbeMode = '0103' }
                @{ BitsPerPixel = 8; Width = 1024; Height = 768; RefreshRate = 60; VbeMode = '0105' }
                @{ BitsPerPixel = 8; Width = 640; Height = 400; RefreshRate = 60; VbeMode = '0100' }
                @{ BitsPerPixel = 16; Width = 640; Height = 480; RefreshRate = 60; VbeMode = '0111' }
                @{ BitsPerPixel = 16; Width = 800; Height = 600; RefreshRate = 60; VbeMode = '0114' }
                @{ BitsPerPixel = 16; Width = 1024; Height = 768; RefreshRate = 60; VbeMode = '0117' }
            )

            # The chip module, which carries the Direct3D word check-tree
            # matches. It has no instruction of its own to audit (the hook
            # differs from the 2064W's in data, not code), so no Required
            # pattern: the family's CRTCEXT signature is audited on mga_hw16
            # through the 2064W entry.
            Objects = @('millennium_hw16')
            Audit = @{
                Required = @()
                Forbidden = @()
            }
            MapSymbols = @('v9x_mga2164w_device')
        }
        @{
            Id = 'mystique'
            Name = 'Matrox Mystique'
            VendorId = '102B'
            DeviceId = '051A'
            # No SubsystemId: Michael's card reads 051A102B, and the bare id
            # also binds the Mystique 220 (MGA-1164SG), which shares it.
            DeviceDesc = 'Velocity9x Matrox Mystique'
            Adapter = 'Matrox Mystique'
            ClockDetector = 'matrox-mystique-unavailable-v1'
            ModeSwitching = 'vbe-lfb'
            # The 2064W's drawing core (MGA-1064SG spec 1-2), measured with
            # MGA2D at 8, 16 and 32 bpp on 2026-10-19
            # (docs\decisions\2026-10-19-mystique-drawing-engine.md). Its
            # texture engine is the 2164W's, with a 16-bit Z buffer only
            # (docs\decisions\2026-10-19-mystique-texture-interface.md):
            # Direct3D through d3d_mga.c's Z16 table.
            Acceleration = 'directdraw-fill-copy'
            Direct3D = 'hardware-mga1064sg'
            EngineType = 'MGA'
            EngineCaps = @('SOLID_FILL', 'SCREEN_COPY', 'FLIP', 'D3D')
            # The smallest Mystique, and the card in A8U4I5 (Matrox's driver
            # reports 4 MiB). The heap sizes from 4F00h and the memory walk.
            VideoMemoryBytes = 2097152

            Modes = @(
                @{ BitsPerPixel = 8; Width = 640; Height = 480; RefreshRate = 60; VbeMode = '0101' }
                @{ BitsPerPixel = 8; Width = 800; Height = 600; RefreshRate = 60; VbeMode = '0103' }
                @{ BitsPerPixel = 8; Width = 1024; Height = 768; RefreshRate = 60; VbeMode = '0105' }
                @{ BitsPerPixel = 8; Width = 640; Height = 400; RefreshRate = 60; VbeMode = '0100' }
                @{ BitsPerPixel = 16; Width = 640; Height = 480; RefreshRate = 60; VbeMode = '0111' }
                @{ BitsPerPixel = 16; Width = 800; Height = 600; RefreshRate = 60; VbeMode = '0114' }
                @{ BitsPerPixel = 16; Width = 1024; Height = 768; RefreshRate = 60; VbeMode = '0117' }
            )

            # The chip module, as for the 2164W: no instruction of its own.
            Objects = @('millennium_hw16')
            Audit = @{
                Required = @()
                Forbidden = @()
            }
            MapSymbols = @('v9x_mystique_device')
        }
    )

    # The host-testable policy backend; see the s3 manifest for the shape.
    Backend = @{
        Getter = 'v9x_matrox_mga_backend'
        Header = 'velocity9x/matrox_mga.h'
        Sources = @(
            'src\chipsets\matrox\mga_backend.c'
        )
    }

    Build = @{
        # No read_aperture hook, so the mini-VDD's 4F9Ch cache is the only way
        # this family learns where its framebuffer is; the collection stays on
        # (see the ati manifest for what happened when it was off).
        MiniVddVbeCollect = $true
        # Compile order is link order. Reordering changes the image.
        Sources = @(
            @{ Name = 'build'; Path = 'src\common\build.c' }
            @{ Name = 'log'; Path = 'src\common\log.c' }
            @{ Name = 'mode'; Path = 'src\common\mode.c' }
            @{ Name = 'resources'; Path = 'src\common\resources.c' }
            @{ Name = 'vbe_parse'; Path = 'src\common\vbe_parse.c' }
            @{ Name = 'vbe_modes'; Path = 'src\common\vbe_modes.c' }
            @{ Name = 'edid'; Path = 'src\common\edid.c' }
            @{ Name = 'mtrr'; Path = 'src\common\mtrr.c' }
            @{ Name = 'd3dmode'; Path = 'src\common\d3dmode.c' }
            @{ Name = 'vsync'; Path = 'src\common\vsync.c' }
            @{ Name = 'modes16'; Path = 'src\display16\modes16.c' }
            # The memory walk's decision half, for mga_hw16.c's hook.
            @{ Name = 'vram_probe'; Path = 'src\common\vram_probe.c' }
            # Which BAR is the control aperture, for millennium_hw16.c's hook.
            @{ Name = 'mga_bars'; Path = 'src\chipsets\matrox\mga_bars.c' }
            @{ Name = 'millennium_hw16'; Path = 'src\chipsets\matrox\millennium\millennium_hw16.c' }
            @{ Name = 'mga_hw16'; Path = 'src\chipsets\matrox\mga_hw16.c' }
            @{ Name = 'vbe16'; Path = 'src\display16\hw\vbe16.c' }
            @{ Name = 'enable16'; Path = 'src\display16\enable16.c' }
            @{ Name = 'display_component'; Path = 'src\display16\display_component.c' }
            @{ Name = 'loader'; Path = 'src\display16\loader.c' }
            @{ Name = 'ddi'; Path = 'src\display16\ddi.c' }
            @{ Name = 'dd16'; Path = 'src\display16\dd16.c' }
            # The HAL's register builder, for gdi_accel.c's MGA arm.
            @{ Name = 'mga_engine'; Path = 'src\chipsets\matrox\mga_engine.c' }
            # Ordinal 1, last so it links after the runtime symbols it calls.
            @{ Name = 'gdi_accel'; Path = 'src\display16\gdi_accel.c' }
        )
        # gdi_accel.c's MGA fill and copy, compiled into this family only.
        Defines = @('V9X_MGA_FAMILY')
        # runtime.asm's BAR0 read and mini-VDD map for the drawing engine.
        RuntimeDefines = @('V9X_MGA_FAMILY')
        SkeletonOutput = 'build\win16-ddi-matrox'
        PackageOutput = 'build\win98se-matrox'
        VmStageDirectory = 'build\vm-probe\MATROX'
    }

    Audit = @{
        RequiredInstructions = @()
        ForbiddenInstructions = @()
        RequiredMapSymbols = @()
        DispatchSymbol = 'v9x_hw16'
        BackendSymbols = @('v9x_mga2064w_device', 'v9x_mga2164w_device', 'v9x_mystique_device')
    }

    Inf = @{
        Provider = 'Velocity9x Project'
        Manufacturer = 'Velocity9x'
        DiskName = 'Velocity9x Windows 98SE driver-stage disk'
        ModelsSection = 'Velocity9x.Models'
        DefaultMode = '8,640,480'
        ForcedModes = @('8,640,480', '8,800,600', '8,1024,768',
                        '16,640,480', '16,800,600', '16,1024,768')
    }

    Package = @{
        ModesSummary = '640x480, 800x600, 1024x768 at 8/16 bpp and 60 Hz'
        HalDescription = 'V9XHAL.DLL (vidmem + flip, engine fill and copy)'
    }

    Floppy = @{
        Include = $true
        Folder = 'MATROX'
        Order = 4
        HardwareIdHint = 'PCI 102B:0519, 102B:051B, 102B:051A'
        # Installed files that ride a third disk, as ATI's do: disk 1 went
        # 553 bytes over when the shared Direct3D core was optimised
        # (Michael, 2026-10-24: "Matrox extra floppy ok"). The update
        # checker, which nothing needs to boot or draw. The trace tool
        # joined it on 2026-10-27, when the Evergreen engine's specular
        # and fog put disk 1 775 bytes over.
        ThirdDisk = @('V9XUPD.EXE', 'V9XTRACE.EXE')
    }

    # Physical-only for now, so the VM runner must refuse rather than test
    # something else.
    Vm = @{
        Emulator = 'none'
        Controller = ''
        Bios = ''
        Profile = ''
        Port = 0
        ReferenceProfile = ''
        ReferencePort = 0
        Modes = @()
    }
}
