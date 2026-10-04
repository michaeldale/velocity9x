# Velocity9x family manifest: SiS 6326.
#
# The VBE 4F02h mode set programs the card and 4F01h reports where the
# framebuffer landed. After every mode set the chip's enable hook turns the
# 2D engine's register window on, and DirectDraw fill and copy run on the
# engine (SIS_6326); everything else is drawn by the CPU
# (docs\plans\sis-6326-family.md, Phase 2).
#
# Physical-only: the local 86Box build has no 6326 device model.
@{
    SchemaVersion = 1
    Id = 'sis'
    DisplayName = 'SiS 6326'
    Description = 'SiS 6326 (PCI 1039:6326): VBE mode set, the 2D engine for DirectDraw fill and copy, CPU drawing otherwise.'

    Chips = @(
        @{
            Id = 'sis6326'
            Name = 'SiS 6326'
            VendorId = '1039'
            DeviceId = '6326'
            # No SubsystemId, deliberately. Two boards were measured with
            # different subsystems (63261039 and 63261569) and both are this
            # chip; the bare id binds either.
            DeviceDesc = 'Velocity9x SiS 6326'
            Adapter = 'SiS 6326'
            ClockDetector = 'sis-6326-unavailable-v1'
            ModeSwitching = 'vbe-lfb'
            # The 2D engine through BAR1, measured byte-exact at 8 and 16 bpp
            # (docs\decisions\2026-10-05-sis6326-2d-engine-writes.md). The
            # Direct3D engine, d3d_sis6326.c, from the three SIS3D probe
            # phases (docs\decisions\2026-10-05-sis6326-3d-*.md); 16 bpp.
            Acceleration = 'directdraw-fill-copy'
            Direct3D = 'hardware-sis6326'
            EngineType = 'SIS_6326'
            EngineCaps = @('SOLID_FILL', 'SCREEN_COPY', 'D3D')
            # Both measured boards carry 4 MiB (SRC D[2:1] = 10 under SiS's
            # driver) behind a 4 MiB BAR0, the datasheet's maximum. Every
            # advertised mode lays out in it.
            VideoMemoryBytes = 4194304

            # Every VbeMode is in both measured BIOSes' own mode lists; see
            # the comment over v9x_sis_modes in sis_hw16.c.
            Modes = @(
                @{ BitsPerPixel = 8; Width = 640; Height = 480; RefreshRate = 60; VbeMode = '0101' }
                @{ BitsPerPixel = 8; Width = 800; Height = 600; RefreshRate = 60; VbeMode = '0103' }
                @{ BitsPerPixel = 8; Width = 1024; Height = 768; RefreshRate = 60; VbeMode = '0105' }
                @{ BitsPerPixel = 8; Width = 640; Height = 400; RefreshRate = 60; VbeMode = '0100' }
                @{ BitsPerPixel = 16; Width = 640; Height = 480; RefreshRate = 60; VbeMode = '0111' }
                @{ BitsPerPixel = 16; Width = 800; Height = 600; RefreshRate = 60; VbeMode = '0114' }
                @{ BitsPerPixel = 16; Width = 1024; Height = 768; RefreshRate = 60; VbeMode = '0117' }
            )

            Objects = @('sis6326_hw16')

            # No required instructions, for the reason the ati manifest gives:
            # identity is carried by MapSymbols, Audit.DispatchSymbol and the
            # INF's hardware-id set.
            Audit = @{
                Required = @()
                Forbidden = @()
            }
            MapSymbols = @('v9x_sis6326_device')
        }
    )

    # The host-testable policy backend; see the s3 manifest for the shape.
    Backend = @{
        Getter = 'v9x_sis_6326_backend'
        Header = 'velocity9x/sis_6326.h'
        Sources = @(
            'src\chipsets\sis\sis_backend.c'
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
            @{ Name = 'sis6326_hw16'; Path = 'src\chipsets\sis\sis6326\sis6326_hw16.c' }
            @{ Name = 'sis_hw16'; Path = 'src\chipsets\sis\sis_hw16.c' }
            @{ Name = 'vbe16'; Path = 'src\display16\hw\vbe16.c' }
            @{ Name = 'enable16'; Path = 'src\display16\enable16.c' }
            @{ Name = 'display_component'; Path = 'src\display16\display_component.c' }
            @{ Name = 'loader'; Path = 'src\display16\loader.c' }
            @{ Name = 'ddi'; Path = 'src\display16\ddi.c' }
            @{ Name = 'dd16'; Path = 'src\display16\dd16.c' }
            # Ordinal 1, last so it links after the runtime symbols it calls;
            # this family takes its decline branch on every blit.
            @{ Name = 'gdi_accel'; Path = 'src\display16\gdi_accel.c' }
        )
        Defines = @()
        # runtime.asm's BAR1 read and mini-VDD map for the 2D engine.
        RuntimeDefines = @('V9X_SIS_FAMILY')
        SkeletonOutput = 'build\win16-ddi-sis'
        PackageOutput = 'build\win98se-sis'
        VmStageDirectory = 'build\vm-probe\SIS'
    }

    Audit = @{
        RequiredInstructions = @()
        ForbiddenInstructions = @()
        RequiredMapSymbols = @()
        DispatchSymbol = 'v9x_hw16'
        BackendSymbols = @('v9x_sis6326_device')
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
        Folder = 'SIS'
        Order = 5
        HardwareIdHint = 'PCI 1039:6326'
    }

    # No emulator models this chip, so the VM runner must refuse rather than
    # test something else.
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
