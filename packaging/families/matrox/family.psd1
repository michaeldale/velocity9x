# Velocity9x family manifest: Matrox MGA (the original Millennium).
#
# Tier-0: every hw16 hook is NULL, so the VBE 4F02h mode set programs the
# card, 4F01h reports where the framebuffer landed, and the CPU draws.
# EngineType and EngineCaps below say exactly that, and change only when the
# MGA drawing engine exists and has been measured
# (docs\plans\matrox-millennium-family.md, Phase 2).
#
# This is the INF-installed family. The Millennium II stays in matrox-m2,
# the guarded drop-in candidate, until it has run on this path.
#
# Physical-only: the local 86Box build's Millennium model is not a
# Velocity9x target yet, and A8U4I5 carries the real card.
@{
    SchemaVersion = 1
    Id = 'matrox'
    DisplayName = 'Matrox Millennium'
    Description = 'Matrox Millennium MGA-2064W (PCI 102B:0519) at tier-0: VBE mode set, CPU drawing, no MGA register writes.'

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
            Acceleration = 'none'
            Direct3D = 'not-advertised'
            EngineType = 'NONE'
            EngineCaps = @()
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

            Objects = @('mga2064w_hw16')

            # No required instructions, for the reason the ati manifest gives:
            # identity is carried by MapSymbols, Audit.DispatchSymbol and the
            # INF's hardware-id set.
            Audit = @{
                Required = @()
                Forbidden = @()
            }
            MapSymbols = @('v9x_mga2064w_device')
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
            @{ Name = 'mga2064w_hw16'; Path = 'src\chipsets\matrox\mga2064w\mga2064w_hw16.c' }
            @{ Name = 'mga_hw16'; Path = 'src\chipsets\matrox\mga_hw16.c' }
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
        RuntimeDefines = @()
        SkeletonOutput = 'build\win16-ddi-matrox'
        PackageOutput = 'build\win98se-matrox'
        VmStageDirectory = 'build\vm-probe\MATROX'
    }

    Audit = @{
        RequiredInstructions = @()
        ForbiddenInstructions = @()
        RequiredMapSymbols = @()
        DispatchSymbol = 'v9x_hw16'
        BackendSymbols = @('v9x_mga2064w_device')
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
        HalDescription = 'V9XHAL.DLL (vidmem, CPU blits only)'
    }

    Floppy = @{
        Include = $true
        Folder = 'MATROX'
        Order = 4
        HardwareIdHint = 'PCI 102B:0519'
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
