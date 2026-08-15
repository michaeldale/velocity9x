# Velocity9x family manifest: Matrox Millennium II.
#
# Encodes what the -MatroxMillennium2 switch does today. This family ships as
# a guarded drop-in candidate rather than an INF package: the physical machine
# it targets has no recoverable install path, so the files replace the stock
# MGAPDX64 driver under the recovery guard. It therefore declares no INF and
# is excluded from the floppy.
@{
    SchemaVersion = 1
    Id = 'matrox-m2'
    DisplayName = 'Matrox Millennium II'
    Description = 'Guarded drop-in candidate for the MGA-2164W. VBE mode-set plus a scan-line pitch hook; no chip register writes.'

    Chips = @(
        @{
            Id = 'mga2164w'
            Name = 'Matrox Millennium II MGA-2164W'
            VendorId = '102B'
            DeviceId = '051B'
            DeviceDesc = 'Velocity9x Matrox Millennium II MGA-2164W (guarded candidate)'
            Adapter = 'Matrox Millennium II MGA-2164W'
            ClockDetector = 'matrox-mga2164w-unavailable-v1'
            ModeSwitching = 'single-mode'
            Acceleration = 'none'
            Direct3D = 'not-advertised'
            EngineFlag = ''
            EngineCaps = @()

            # The 8-bpp build is single-mode by construction; the 16-bpp build
            # carries the three-mode table. Both are forced-mode builds, so the
            # capability list here is the 16-bpp superset and the 8-bpp variant
            # narrows it through Build.Variants below.
            Modes = @(
                @{ BitsPerPixel = 8; Width = 640; Height = 480; RefreshRate = 60; VbeMode = '0101' }
                @{ BitsPerPixel = 16; Width = 640; Height = 480; RefreshRate = 60; VbeMode = '0111' }
                @{ BitsPerPixel = 16; Width = 800; Height = 600; RefreshRate = 60; VbeMode = '0114' }
                @{ BitsPerPixel = 16; Width = 1024; Height = 768; RefreshRate = 60; VbeMode = '0117' }
            )

            Audit = @{
                Required = @(
                    'or\s+bx,4000H'
                    'mov\s+cx,51BH'
                    'mov\s+dx,102BH'
                    'mov\s+ax,0B10AH'
                    'mov\s+di,10H'
                    'and\s+eax,0FFFFFFF0H'
                    'test\s+eax,0FFFFFFH'
                    'mov\s+ax,4F06H'
                    'mov\s+cx,word ptr DGROUP:_v9x_active_width'
                    'cmp\s+bx,word ptr DGROUP:_v9x_active_pitch'
                )
                # Carried verbatim from the pre-manifest audit list. These two
                # patterns are no longer produced by any family, so cross-family
                # derivation cannot supply them; they stay as an explicit guard
                # against an S3 aperture path reappearing here.
                Forbidden = @(
                    'mov\s+di,14H'
                    'dword ptr es:\[1E54H\]'
                )
            }
        }
    )

    Build = @{
        # No S3 chipset modules: the Matrox path does not compile the ViRGE
        # backend, clocks or memory probes.
        Sources = @(
            @{ Name = 'build'; Path = 'src\common\build.c' }
            @{ Name = 'log'; Path = 'src\common\log.c' }
            @{ Name = 'mode'; Path = 'src\common\mode.c' }
            @{ Name = 'resources'; Path = 'src\common\resources.c' }
            @{ Name = 'display_component'; Path = 'src\display16\display_component.c' }
            @{ Name = 'loader'; Path = 'src\display16\loader.c' }
            @{ Name = 'ddi'; Path = 'src\display16\ddi.c' }
            @{ Name = 'dd16'; Path = 'src\display16\dd16.c' }
        )
        Defines = @('V9X_TARGET_MATROX_MILLENNIUM2=1')
        RuntimeDefines = @('V9X_TARGET_MATROX_MILLENNIUM2=1')
        SkeletonOutput = 'build\win16-ddi-mga2'
        PackageOutput = 'build\matrox-candidate'
        LegacyOutputName = 'matrox-candidate'
        LegacySkeletonOutput = 'build\win16-ddi-mga2'
        LegacySwitch = 'MatroxMillennium2'
        VmStageDirectory = ''
        # Build-time variants beyond the plain family build. The 16-bpp variant
        # adds a define and unlocks mode indices 1 and 2.
        Variants = @(
            @{
                Id = '8bpp'
                Defines = @()
                RuntimeDefines = @()
                AllowedModeIndexes = @(0)
                Default = $true
            }
            @{
                Id = '16bpp'
                Defines = @('V9X_MATROX_16BPP=1')
                RuntimeDefines = @('V9X_MATROX_16BPP=1')
                AllowedModeIndexes = @(0, 1, 2)
                Default = $false
            }
        )
    }

    Audit = @{
        RequiredInstructions = @()
        ForbiddenInstructions = @()
        RequiredMapSymbols = @()
    }

    # No INF: this family installs by guarded file replacement, and
    # build-matrox-candidate.ps1 fails the build if an INF appears in the drop.
    Inf = @{
        Generate = $false
    }

    Floppy = @{
        Include = $false
        Folder = ''
        Order = 0
        HardwareIdHint = 'PCI 102B:051B'
    }

    # No emulator covers this card, so the VM runner must refuse rather than
    # silently test something else.
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
