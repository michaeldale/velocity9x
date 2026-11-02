# Velocity9x family manifest: NVIDIA.
#
# The RIVA 128 and 128ZX at tier-0: the VBE 4F02h mode set programs the card,
# 4F01h reports where the framebuffer landed, and the CPU draws. No NVIDIA
# register is written (docs\plans\nvidia-riva128.md, Phase 1).
#
# Physical-only: the local 86Box build has no NV3 model.
@{
    SchemaVersion = 1
    Id = 'nvidia'
    DisplayName = 'NVIDIA RIVA 128'
    Description = 'NVIDIA RIVA 128 and 128ZX (PCI 12D2:0018) at tier-0: VBE mode set, CPU drawing, no NVIDIA register writes.'

    Chips = @(
        @{
            Id = 'riva128'
            Name = 'NVIDIA RIVA 128'
            VendorId = '12D2'
            DeviceId = '0018'
            # No SubsystemId, deliberately: the measured card is an STB
            # board (10B4:222A), and the RIVA 128 shipped on many boards
            # with their own subsystems; the bare id binds any of them.
            DeviceDesc = 'Velocity9x NVIDIA RIVA 128'
            Adapter = 'NVIDIA RIVA 128'
            ClockDetector = 'nvidia-riva128-unavailable-v1'
            ModeSwitching = 'vbe-lfb'
            Acceleration = 'none'
            Direct3D = 'not-advertised'
            EngineType = 'NONE'
            EngineCaps = @()
            # The measured 128ZX reports 8 MiB through 4F00h, the chip's
            # maximum. Every advertised mode lays out in 4 MiB, so the
            # original RIVA 128's 4 MiB holds them too.
            VideoMemoryBytes = 8388608

            # Every VbeMode is in the measured BIOS's own mode list; see the
            # comment over v9x_nvidia_modes in nvidia_hw16.c.
            Modes = @(
                @{ BitsPerPixel = 8; Width = 640; Height = 480; RefreshRate = 60; VbeMode = '0101' }
                @{ BitsPerPixel = 8; Width = 800; Height = 600; RefreshRate = 60; VbeMode = '0103' }
                @{ BitsPerPixel = 8; Width = 1024; Height = 768; RefreshRate = 60; VbeMode = '0105' }
                @{ BitsPerPixel = 8; Width = 640; Height = 400; RefreshRate = 60; VbeMode = '0100' }
                @{ BitsPerPixel = 16; Width = 640; Height = 480; RefreshRate = 60; VbeMode = '0111' }
                @{ BitsPerPixel = 16; Width = 800; Height = 600; RefreshRate = 60; VbeMode = '0114' }
                @{ BitsPerPixel = 16; Width = 1024; Height = 768; RefreshRate = 60; VbeMode = '0117' }
            )

            Objects = @('riva128_hw16')

            # No required instructions, for the reason the ati manifest gives:
            # at tier-0 this chip owns no register sequence, and identity is
            # carried by MapSymbols, Audit.DispatchSymbol and the INF's
            # hardware-id set.
            Audit = @{
                Required = @()
                Forbidden = @()
            }
            MapSymbols = @('v9x_riva128_device')
        }
    )

    # The host-testable policy backend; see the s3 manifest for the shape.
    Backend = @{
        Getter = 'v9x_nvidia_backend'
        Header = 'velocity9x/nvidia.h'
        Sources = @(
            'src\chipsets\nvidia\nvidia_backend.c'
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
            @{ Name = 'riva128_hw16'; Path = 'src\chipsets\nvidia\riva128\riva128_hw16.c' }
            @{ Name = 'nvidia_hw16'; Path = 'src\chipsets\nvidia\nvidia_hw16.c' }
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
        SkeletonOutput = 'build\win16-ddi-nvidia'
        PackageOutput = 'build\win98se-nvidia'
        VmStageDirectory = 'build\vm-probe\NVIDIA'
    }

    Audit = @{
        RequiredInstructions = @()
        ForbiddenInstructions = @()
        RequiredMapSymbols = @()
        DispatchSymbol = 'v9x_hw16'
        BackendSymbols = @('v9x_riva128_device')
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
        HalDescription = 'V9XHAL.DLL (vidmem + flip, CPU drawing)'
    }

    Floppy = @{
        Include = $true
        Folder = 'NVIDIA'
        Order = 8
        HardwareIdHint = 'PCI 12D2:0018'
        # Installed files that ride a third disk, as every other family's do.
        ThirdDisk = @('V9XUPD.EXE', 'V9XTRACE.EXE', 'GLIDE3X.DLL',
                      'ATI3DCIF.DLL')
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
