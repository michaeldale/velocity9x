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
            # No engine: the 32-bit HAL resolves no ops table for this type and
            # every blit falls to the CPU path.
            EngineType = 'NONE'
            EngineCaps = @()
            VideoMemoryBytes = 4194304

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

            # PCI identity and the VBE flag are data in this chip's hw16
            # object now; see the note in the s3 manifest.
            Audit = @{
                # Only signatures unique to this family's own chip module.
                # The PCI BAR0 read is a shared chip-agnostic primitive in
                # runtime.asm now, so its instructions no longer discriminate
                # between families and are not listed.
                # 'mov\s+ax,4F06H' was here and had to go. Tier-0 now forces
                # the scan line length too, from shared vbe16.c, so the
                # instruction appears in every family image and discriminates
                # nothing - and because one family's Required is every other
                # family's Forbidden, claiming it here failed the ati build.
                # The two patterns left are this chip module's own: the width
                # and pitch comparison that no other object performs.
                Required = @(
                    'mov\s+cx,word ptr _v9x_active_width'
                    'cmp\s+cx,word ptr _v9x_active_pitch'
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
            MapSymbols = @('v9x_mga2_device')
        }
        @{
            Id = 'mga2064w'
            Name = 'Matrox Millennium MGA-2064W'
            VendorId = '102B'
            DeviceId = '0519'
            DeviceDesc = 'Velocity9x Matrox Millennium MGA-2064W (guarded candidate)'
            Adapter = 'Matrox Millennium MGA-2064W'
            ClockDetector = 'matrox-mga2064w-unavailable-v1'
            ModeSwitching = 'single-mode'
            Acceleration = 'none'
            Direct3D = 'not-advertised'
            EngineType = 'NONE'
            EngineCaps = @()
            # A floor, and deliberately still 2 MiB after the card was
            # measured at 8. The 2026-09-11 aperture probe wrote a distinct
            # marker at every power of two to 4 MiB with none folding back to
            # offset 0, which is a real measurement and not the BAR window -
            # but it is a measurement of *one* card, and the MGA-2064W
            # shipped in several memory configurations. 2 MiB is the base
            # Millennium and covers every mode claimed below, so
            # under-reporting cannot hand DirectDraw memory a smaller card
            # does not have. The runtime heap does not read this in any case:
            # enable16.c sizes from the 4F00h total on the VBE path.
            # docs\decisions\2026-09-11-the-2064w-aperture-opens-with-mgamode.md
            VideoMemoryBytes = 2097152

            # Nine modes. Every one is advertised by this card's BIOS with a
            # linear framebuffer at its BAR1 base and a scan line length
            # equal to width x bytes per pixel, measured by emulated int10 on
            # 2026-09-11; and every one fits the 2 MiB floor above, which is
            # what keeps 1280x1024x16 and 1024x768x32 out despite the BIOS
            # offering both.
            #
            # The bar for claiming a mode here is that the BIOS advertises it
            # and its stride matches the driver's packed table - not that
            # someone has set it. Only 0117h has been set on this card, by
            # the kit. The two 800-wide entries the BIOS pads are excluded on
            # measurement: it reports 1920 bytes per scan line for 0114h and
            # 960 for 0103h where the table asks for 1600 and 800, so the
            # post-mode-set check would have to force the pitch and would
            # refuse the mode if the BIOS declined. 800x600x32 is *not*
            # padded - 3200 is exactly 800 x 4 - so it is claimed.
            #
            # This list is latent in the shipped artifact. The packaged
            # candidate is built with a forced single mode - its MANIFEST.TXT
            # says "forced 640x480x8, VBE 0101h" - so widening the table here
            # changes what the driver image carries and not what anyone
            # installing that zip is offered. It matters when the guard comes
            # off.
            Modes = @(
                @{ BitsPerPixel = 8; Width = 640; Height = 400; RefreshRate = 60; VbeMode = '0100' }
                @{ BitsPerPixel = 8; Width = 640; Height = 480; RefreshRate = 60; VbeMode = '0101' }
                @{ BitsPerPixel = 8; Width = 1024; Height = 768; RefreshRate = 60; VbeMode = '0105' }
                @{ BitsPerPixel = 8; Width = 1280; Height = 1024; RefreshRate = 60; VbeMode = '0107' }
                @{ BitsPerPixel = 8; Width = 1600; Height = 1200; RefreshRate = 60; VbeMode = '011C' }
                @{ BitsPerPixel = 16; Width = 640; Height = 480; RefreshRate = 60; VbeMode = '0111' }
                @{ BitsPerPixel = 16; Width = 1024; Height = 768; RefreshRate = 60; VbeMode = '0117' }
                @{ BitsPerPixel = 32; Width = 640; Height = 480; RefreshRate = 60; VbeMode = '0112' }
                @{ BitsPerPixel = 32; Width = 800; Height = 600; RefreshRate = 60; VbeMode = '0115' }
            )

            Audit = @{
                # The same two patterns as the sibling: this chip module's own
                # width and pitch comparison, which no other object performs.
                # Both chips share the module, so both share its signatures.
                Required = @(
                    'mov\s+cx,word ptr _v9x_active_width'
                    'cmp\s+cx,word ptr _v9x_active_pitch'
                )
                Forbidden = @(
                    'mov\s+di,14H'
                    'dword ptr es:\[1E54H\]'
                )
            }
            MapSymbols = @('v9x_mga2064w_device')
        }
    )

    # The host-testable policy backend; see the s3 manifest for the shape.
    Backend = @{
        Getter = 'v9x_matrox_millennium2_backend'
        Header = 'velocity9x/matrox_millennium2.h'
        Sources = @(
            'src\chipsets\matrox\millennium2\mga2_backend.c'
        )
    }

    Build = @{
        # No S3 chipset modules: the Matrox path does not compile the ViRGE
        # backend, clocks or memory probes.
        Sources = @(
            @{ Name = 'build'; Path = 'src\common\build.c' }
            @{ Name = 'log'; Path = 'src\common\log.c' }
            @{ Name = 'mode'; Path = 'src\common\mode.c' }
            @{ Name = 'resources'; Path = 'src\common\resources.c' }
            # vbe16 parses 4F00h/4F01h answers through this, so every family
            # links it even where no hook asks the BIOS anything.
            @{ Name = 'vbe_parse'; Path = 'src\common\vbe_parse.c' }
            @{ Name = 'vbe_modes'; Path = 'src\common\vbe_modes.c' }
            @{ Name = 'edid'; Path = 'src\common\edid.c' }
            @{ Name = 'mtrr'; Path = 'src\common\mtrr.c' }
            # The Direct3D back-end decision behind [Velocity9x] Direct3D.
            # Pure policy, host-tested; dd16.c resolves it at Enable and every
            # family publishes the result as Direct3DMode= whether or not it
            # has a DirectDraw HAL to apply it to.
            @{ Name = 'd3dmode'; Path = 'src\common\d3dmode.c' }
            @{ Name = 'modes16'; Path = 'src\display16\modes16.c' }
            @{ Name = 'mga2_hw16'; Path = 'src\chipsets\matrox\millennium2\mga2_hw16.c' }
            @{ Name = 'vbe16'; Path = 'src\display16\hw\vbe16.c' }
            @{ Name = 'enable16'; Path = 'src\display16\enable16.c' }
            @{ Name = 'display_component'; Path = 'src\display16\display_component.c' }
            @{ Name = 'loader'; Path = 'src\display16\loader.c' }
            @{ Name = 'ddi'; Path = 'src\display16\ddi.c' }
            @{ Name = 'dd16'; Path = 'src\display16\dd16.c' }
            # Ordinal 1. Every family links it, and three of the four take its
            # decline branch on every blit for ever - see the header comment in
            # src\display16\gdi_accel.c. Last in the list so it links after the
            # runtime symbols it calls are declared.
            @{ Name = 'gdi_accel'; Path = 'src\display16\gdi_accel.c' }
        )
        Defines = @('V9X_TARGET_MATROX_MILLENNIUM2=1')
        RuntimeDefines = @('V9X_TARGET_MATROX_MILLENNIUM2=1')
        SkeletonOutput = 'build\win16-ddi-mga2'
        PackageOutput = 'build\matrox-candidate'
        VmStageDirectory = ''
        # The Millennium II reads the aperture from hardware and never consults
        # the mini-VDD's 4F9Ch VBE cache, so this family ships the mini-VDD with
        # the boot-time BIOS collection assembled out (see
        # docs\decisions\2026-08-18-minivdd-vbe-collect-gating.md).
        MiniVddVbeCollect = $false
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
        DispatchSymbol = 'v9x_hw16'
        BackendSymbols = @('v9x_mga2_device')
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

