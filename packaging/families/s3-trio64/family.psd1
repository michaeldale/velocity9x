# Velocity9x family manifest: S3 Trio32/64.
#
# Encodes exactly what the -S3Trio64 switch does today. Phase 8 merges this
# manifest and s3-virge into a single two-chip `s3` family; until then the two
# stay separate so the per-package golden compare keeps working.
@{
    SchemaVersion = 1
    Id = 's3-trio64'
    DisplayName = 'S3 Trio32/64'
    Description = 'Single-chip family for the 86C764 Trio32/64 software-GDI target.'

    Chips = @(
        @{
            Id = 'trio64'
            Name = 'S3 Trio32/64 86C764'
            VendorId = '5333'
            DeviceId = '8811'
            DeviceDesc = 'Velocity9x S3 Trio32/64 86C764 (software GDI)'
            Adapter = 'S3 Trio32/64 86C764'
            ClockDetector = 's3-virge-pll-v1'
            ModeSwitching = 'live-any-depth'
            Acceleration = 'directdraw-fill-blt'
            # The Trio has no S3d core. dd16.c nulls lpD3D*/GetDriverInfo for
            # it, and the 32-bit caps builder never runs.
            Direct3D = 'not-advertised'
            EngineFlag = 'V9X_DD_ENGINE_S3_TRIO64'
            EngineCaps = @('SOLID_FILL', 'SCREEN_COPY', 'FLIP', 'VBLANK')

            Modes = @(
                @{ BitsPerPixel = 8; Width = 640; Height = 480; RefreshRate = 60; VbeMode = '0101' }
                @{ BitsPerPixel = 8; Width = 800; Height = 600; RefreshRate = 60; VbeMode = '0103' }
                @{ BitsPerPixel = 8; Width = 1024; Height = 768; RefreshRate = 60; VbeMode = '0105' }
                @{ BitsPerPixel = 8; Width = 640; Height = 400; RefreshRate = 60; VbeMode = '0100' }
                @{ BitsPerPixel = 16; Width = 640; Height = 480; RefreshRate = 60; VbeMode = '0111' }
                @{ BitsPerPixel = 16; Width = 800; Height = 600; RefreshRate = 60; VbeMode = '0114' }
                @{ BitsPerPixel = 16; Width = 1024; Height = 768; RefreshRate = 60; VbeMode = '0117' }
            )

            # The Trio shares the S3 unlock, CR58 aperture and CR40 engine
            # enable with the ViRGE. It must NOT carry the CR53[3] new-MMIO
            # poke: that register selects the ViRGE engine window, which the
            # Trio does not have. Those two patterns are declared required by
            # the s3-virge manifest, so cross-family derivation forbids them
            # here automatically.
            # PCI identity and the VBE flag are data in this chip's hw16
            # object now; see the note in the s3-virge manifest.
            Audit = @{
                Required = @(
                    'or\s+al,13H'
                    'cmp\s+al,13H'
                )
                Forbidden = @()
            }
            MapSymbols = @('v9x_trio_devices')
        }
    )

    Build = @{
        Sources = @(
            @{ Name = 'build'; Path = 'src\common\build.c' }
            @{ Name = 'log'; Path = 'src\common\log.c' }
            @{ Name = 'mode'; Path = 'src\common\mode.c' }
            @{ Name = 'resources'; Path = 'src\common\resources.c' }
            @{ Name = 'virge_backend'; Path = 'src\chipsets\s3\virge\backend.c' }
            @{ Name = 'virge_clocks'; Path = 'src\chipsets\s3\virge\clocks.c' }
            @{ Name = 'virge_memory'; Path = 'src\chipsets\s3\virge\memory.c' }
            @{ Name = 's3_regs16'; Path = 'src\chipsets\s3\common\s3_regs16.c' }
            @{ Name = 'trio_hw16'; Path = 'src\chipsets\s3\trio64\trio_hw16.c' }
            @{ Name = 'vbe16'; Path = 'src\display16\hw\vbe16.c' }
            @{ Name = 'enable16'; Path = 'src\display16\enable16.c' }
            @{ Name = 'display_component'; Path = 'src\display16\display_component.c' }
            @{ Name = 'loader'; Path = 'src\display16\loader.c' }
            @{ Name = 'ddi'; Path = 'src\display16\ddi.c' }
            @{ Name = 'dd16'; Path = 'src\display16\dd16.c' }
        )
        Defines = @('V9X_TARGET_S3_TRIO64=1')
        RuntimeDefines = @('V9X_TARGET_S3_TRIO64=1')
        SkeletonOutput = 'build\win16-ddi-trio64'
        PackageOutput = 'build\win98se-trio64'
        LegacyOutputName = 'win98se-trio64'
        LegacySkeletonOutput = 'build\win16-ddi-trio64'
        LegacySwitch = 'S3Trio64'
        VmStageDirectory = 'build\vm-probe\ACTIVE'
    }

    Audit = @{
        RequiredInstructions = @()
        ForbiddenInstructions = @()
        RequiredMapSymbols = @()
        DispatchSymbol = 'v9x_hw16'
        BackendSymbols = @('v9x_trio_devices')
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
        HalDescription = 'V9XHAL.DLL (vidmem + CRTC flip + bounded Trio solid fill)'
    }

    Floppy = @{
        Include = $true
        Folder = 'TRIO64'
        Order = 2
        HardwareIdHint = 'PCI 5333:8811'
    }

    Vm = @{
        Emulator = '86box'
        Controller = 's3_trio64_pci'
        Bios = ''
        # A clone of the native-S3 VM, so its guest agent still reports
        # ComputerName WIN98-S3NATIVE. Identify this VM by port, not by name.
        Profile = 'Win98SE-Trio64'
        Port = 9871
        ReferenceProfile = 'Win98SE-Native-S3'
        ReferencePort = 9870
        Modes = @('640x480x8', '800x600x8', '1024x768x8',
                  '640x480x16', '800x600x16', '1024x768x16')
    }
}
