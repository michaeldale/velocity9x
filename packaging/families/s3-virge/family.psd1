# Velocity9x family manifest: S3 ViRGE/DX.
#
# Data only. Loaded with Import-PowerShellDataFile, which is built into
# PowerShell 5.1 and evaluates no code, so the regex audit patterns below need
# no JSON escaping. See docs\plans\multi-chip-restructure.md.
#
# This manifest encodes exactly what scripts\build-win16-ddi-skeleton.ps1 and
# scripts\build-active-package.ps1 do today for the default (non-switch)
# target. It has no consumers yet; phase 3 wires the builders to it.
@{
    SchemaVersion = 1
    Id = 's3-virge'
    DisplayName = 'S3 ViRGE/DX'
    Description = 'Single-chip family for the 86C375 ViRGE/DX bring-up target.'

    Chips = @(
        @{
            Id = 'virge-dx'
            Name = 'S3 ViRGE/DX 86C375'
            VendorId = '5333'
            DeviceId = '8A01'
            DeviceDesc = 'Velocity9x S3 ViRGE/DX 86C375 (Phase 3 mode matrix)'
            # Written to C:\V9XHW.INI by ddi.c's diagnostics publisher.
            Adapter = 'S3 ViRGE/DX 86C375'
            ClockDetector = 's3-virge-pll-v1'
            ModeSwitching = 'live-any-depth'
            Acceleration = 'directdraw-fill-blt'
            Direct3D = 'hardware-s3d'
            EngineFlag = 'V9X_DD_ENGINE_S3_VIRGE_DX'
            EngineCaps = @('SOLID_FILL', 'SCREEN_COPY', 'FLIP', 'VBLANK', 'D3D')

            # Per-chip MODES capability. The order is the order ddi.c's mode
            # table uses and the order GDI enumerates the MODES registry key,
            # so 640x400 sits after the other 8-bpp entries (Doom95).
            Modes = @(
                @{ BitsPerPixel = 8; Width = 640; Height = 480; RefreshRate = 60; VbeMode = '0101' }
                @{ BitsPerPixel = 8; Width = 800; Height = 600; RefreshRate = 60; VbeMode = '0103' }
                @{ BitsPerPixel = 8; Width = 1024; Height = 768; RefreshRate = 60; VbeMode = '0105' }
                @{ BitsPerPixel = 8; Width = 640; Height = 400; RefreshRate = 60; VbeMode = '0100' }
                @{ BitsPerPixel = 16; Width = 640; Height = 480; RefreshRate = 60; VbeMode = '0111' }
                @{ BitsPerPixel = 16; Width = 800; Height = 600; RefreshRate = 60; VbeMode = '0114' }
                @{ BitsPerPixel = 16; Width = 1024; Height = 768; RefreshRate = 60; VbeMode = '0117' }
            )

            # wdis signature patterns for the assembled runtime object. Required
            # patterns must appear in this chip's code; every other family's
            # required patterns become forbidden here automatically, so only
            # patterns that no other manifest declares need listing under
            # Forbidden.
            # The PCI identity and the VBE mode-set flag are no longer
            # immediates in the assembled runtime: they are data in this
            # chip's hw16 object, stamped into DGROUP at load. Identity is
            # audited through MapSymbols below and through the INF
            # hardware-ID set equality; what remains here is the chip
            # register sequence, which is still instructions.
            Audit = @{
                Required = @(
                    'or\s+al,13H'
                    'cmp\s+al,13H'
                    'mov\s+ax,53H'
                    'or\s+al,8\b'
                    'test\s+al,8'
                )
                Forbidden = @()
            }
            MapSymbols = @('v9x_virge_devices')
        }
    )

    Build = @{
        # Ordered compile list. The object order is also the link order, so it
        # must not be reshuffled without a golden re-baseline.
        Sources = @(
            @{ Name = 'build'; Path = 'src\common\build.c' }
            @{ Name = 'log'; Path = 'src\common\log.c' }
            @{ Name = 'mode'; Path = 'src\common\mode.c' }
            @{ Name = 'resources'; Path = 'src\common\resources.c' }
            @{ Name = 'virge_backend'; Path = 'src\chipsets\s3\virge\backend.c' }
            @{ Name = 'virge_clocks'; Path = 'src\chipsets\s3\virge\clocks.c' }
            @{ Name = 'virge_memory'; Path = 'src\chipsets\s3\virge\memory.c' }
            @{ Name = 's3_regs16'; Path = 'src\chipsets\s3\common\s3_regs16.c' }
            @{ Name = 'virge_hw16'; Path = 'src\chipsets\s3\virge\virge_hw16.c' }
            @{ Name = 'vbe16'; Path = 'src\display16\hw\vbe16.c' }
            @{ Name = 'enable16'; Path = 'src\display16\enable16.c' }
            @{ Name = 'display_component'; Path = 'src\display16\display_component.c' }
            @{ Name = 'loader'; Path = 'src\display16\loader.c' }
            @{ Name = 'ddi'; Path = 'src\display16\ddi.c' }
            @{ Name = 'dd16'; Path = 'src\display16\dd16.c' }
        )
        # Compiler and assembler defines beyond the shared ones. The ViRGE
        # target is the no-define default.
        Defines = @()
        RuntimeDefines = @()
        SkeletonOutput = 'build\win16-ddi'
        PackageOutput = 'build\win98se-virge'
        # Retired at phase 8. Until then the builders keep writing the historic
        # directory names so the golden compare stays meaningful.
        LegacyOutputName = 'win98se-active'
        LegacySkeletonOutput = 'build\win16-ddi'
        LegacySwitch = ''
        VmStageDirectory = 'build\vm-probe\ACTIVE'
    }

    Audit = @{
        # Family-wide additions to the per-chip patterns above. Chip-agnostic
        # audits (VDD handoff, DIB thunk guards, NE header, exports, segment
        # flags) stay in the build scripts.
        RequiredInstructions = @()
        ForbiddenInstructions = @()
        RequiredMapSymbols = @()
        # The family's hardware table. Every family defines this symbol, so it
        # is required rather than cross-family forbidden.
        DispatchSymbol = 'v9x_hw16'
        # Symbols another family's binary must not contain.
        BackendSymbols = @('v9x_virge_devices')
    }

    Inf = @{
        Provider = 'Velocity9x Project'
        Manufacturer = 'Velocity9x'
        DiskName = 'Velocity9x Windows 98SE driver-stage disk'
        ModelsSection = 'Velocity9x.Models'
        DefaultMode = '8,640,480'
        # ForceModeIndex selects an alternative default from this list.
        ForcedModes = @('8,640,480', '8,800,600', '8,1024,768',
                        '16,640,480', '16,800,600', '16,1024,768')
    }

    # Lines the package MANIFEST.TXT states about this family.
    Package = @{
        ModesSummary = '640x480, 800x600, 1024x768 at 8/16 bpp and 60 Hz'
        HalDescription = 'V9XHAL.DLL (vidmem + flip + bounded solid fill)'
    }

    Floppy = @{
        Include = $true
        Folder = 'VIRGE'
        # Position in the disk's chip table and copy order.
        Order = 1
        HardwareIdHint = 'PCI 5333:8A01'
    }

    Vm = @{
        Emulator = '86box'
        Controller = 'virge_dx_pci'
        Bios = 'virge375_pci'
        Profile = 'Win86SE'
        Port = 9869
        ReferenceProfile = 'Win98SE-Native-S3'
        ReferencePort = 9870
        Modes = @('640x480x8', '800x600x8', '1024x768x8',
                  '640x480x16', '800x600x16', '1024x768x16')
    }
}
