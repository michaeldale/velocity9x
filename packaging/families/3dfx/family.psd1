# Velocity9x family manifest: 3dfx.
#
# The VSA-100 (Voodoo4, Voodoo5): the VBE 4F02h mode set programs the card
# and 4F01h reports where the framebuffer landed; memory comes from the chip's
# straps through BAR2. DirectDraw fill and copy run on the 2D engine (TDFX);
# everything else is drawn by the CPU (docs\plans\3dfx-vsa100-family.md,
# Phases 2 and 3). The Voodoo3 is
# planned as a second chip of this family (docs\plans\3dfx-voodoo3-family.md).
#
# The package ships no Glide: the card keeps the GLIDE2X.DLL and GLIDE3X.DLL
# installed for it, and check-tree requires that of every 3dfx family.
#
# Physical-only: the local 86Box build models the Banshee and Voodoo3, not
# the VSA-100.
@{
    SchemaVersion = 1
    Id = '3dfx'
    DisplayName = '3dfx VSA-100'
    Description = '3dfx VSA-100 (PCI 121A:0009; Voodoo4, Voodoo5): VBE mode set, CPU drawing.'

    Chips = @(
        @{
            Id = 'vsa100'
            Name = '3dfx VSA-100'
            VendorId = '121A'
            DeviceId = '0009'
            # No SubsystemId, deliberately. Two VSA-100 boards are measured
            # with different subsystems (0004121A, a retail Voodoo4 4500 AGP;
            # 0002121A, the single-chip Mini-Me), and the subsystem does not
            # say how many chips a board carries; the bare id binds either.
            DeviceDesc = 'Velocity9x 3dfx VSA-100'
            Adapter = '3dfx VSA-100'
            ClockDetector = 'tdfx-vsa100-unavailable-v1'
            ModeSwitching = 'vbe-lfb'
            # The 2D engine through BAR0's 2D block, measured pixel-exact at
            # 16 bpp (docs\decisions\2026-10-11-voodoo4-2d-engine-fills-and-copies-pixel-exact.md).
            Acceleration = 'directdraw-fill-copy'
            # Direct3D through the triangle setup unit (d3d_tdfx.c), drawn
            # pixel-exact by TDFX3D
            # (docs\decisions\2026-10-12-voodoo4-3d-engine-draws-pixel-exact.md).
            Direct3D = 'hardware-vsa100'
            EngineType = 'TDFX'
            EngineCaps = @('SOLID_FILL', 'SCREEN_COPY', 'D3D')
            # One chip's memory: the Voodoo4 4500's 32 MiB, decoded from
            # dramInit0 under 3dfx's driver
            # (docs\decisions\2026-10-11-voodoo4-stock-driver-tiles-its-desktop-at-the-top-of-memory.md).
            # The driver uses one page less: the BIOS serves the last 4 KiB
            # through the tiled view.
            VideoMemoryBytes = 33554432

            # Every VbeMode is in the 1.15 BIOS's own mode list; see the
            # comment over v9x_tdfx_modes in tdfx_hw16.c.
            Modes = @(
                @{ BitsPerPixel = 8; Width = 640; Height = 480; RefreshRate = 60; VbeMode = '0101' }
                @{ BitsPerPixel = 8; Width = 800; Height = 600; RefreshRate = 60; VbeMode = '0103' }
                @{ BitsPerPixel = 8; Width = 1024; Height = 768; RefreshRate = 60; VbeMode = '0105' }
                @{ BitsPerPixel = 8; Width = 640; Height = 400; RefreshRate = 60; VbeMode = '0100' }
                @{ BitsPerPixel = 16; Width = 640; Height = 480; RefreshRate = 60; VbeMode = '0111' }
                @{ BitsPerPixel = 16; Width = 800; Height = 600; RefreshRate = 60; VbeMode = '0114' }
                @{ BitsPerPixel = 16; Width = 1024; Height = 768; RefreshRate = 60; VbeMode = '0117' }
            )

            Objects = @('vsa100_hw16')

            # No required instructions, for the reason the ati manifest gives:
            # identity is carried by MapSymbols, Audit.DispatchSymbol and the
            # INF's hardware-id set.
            Audit = @{
                Required = @()
                Forbidden = @()
            }
            MapSymbols = @('v9x_vsa100_device')
        }
    )

    # The host-testable policy backend; see the s3 manifest for the shape.
    Backend = @{
        Getter = 'v9x_tdfx_backend'
        Header = 'velocity9x/tdfx.h'
        Sources = @(
            'src\chipsets\tdfx\tdfx_backend.c'
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
            @{ Name = 'vsa100_hw16'; Path = 'src\chipsets\tdfx\vsa100\vsa100_hw16.c' }
            # The memory and tile-boundary decodes the read_video_memory hook
            # decides with; host-tested in tests\host\test_vsa100_decode.c.
            @{ Name = 'vsa100_decode'; Path = 'src\chipsets\tdfx\vsa100_decode.c' }
            # The 2D builder, for gdi_accel.c's TDFX arm; the HAL links the
            # same file for eng_tdfx.c.
            @{ Name = 'tdfx_2d'; Path = 'src\chipsets\tdfx\tdfx_2d.c' }
            # Glide's EXT_HWC replies, for dd16.c's Control; host-tested in
            # tests\host\test_tdfx_hwc.c.
            @{ Name = 'tdfx_hwc'; Path = 'src\chipsets\tdfx\tdfx_hwc.c' }
            @{ Name = 'tdfx_hw16'; Path = 'src\chipsets\tdfx\tdfx_hw16.c' }
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
        # gdi_accel.c's TDFX arm.
        Defines = @('V9X_TDFX_FAMILY')
        # runtime.asm's BAR2 read of dramInit0 and the tile boundary.
        RuntimeDefines = @('V9X_TDFX_FAMILY')
        SkeletonOutput = 'build\win16-ddi-3dfx'
        PackageOutput = 'build\win98se-3dfx'
        VmStageDirectory = 'build\vm-probe\3DFX'
    }

    Audit = @{
        RequiredInstructions = @()
        ForbiddenInstructions = @()
        RequiredMapSymbols = @()
        DispatchSymbol = 'v9x_hw16'
        BackendSymbols = @('v9x_vsa100_device')
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
        # The card keeps its own Glide (check-tree requires this of 3dfx).
        GlideWrappers = $false
    }

    Floppy = @{
        Include = $true
        Folder = '3DFX'
        Order = 7
        HardwareIdHint = 'PCI 121A:0009'
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
