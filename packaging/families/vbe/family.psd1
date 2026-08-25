# Velocity9x family manifest: VBE tier-0.
#
# The chip-agnostic package. Its 16-bit table supplies no hardware hooks at
# all, so every one of them takes the VBE default: 4F02h sets the mode, 4F01h
# reports where the linear framebuffer landed, 4F00h reports how much memory
# the card has, and the CPU does the drawing. Nothing in it names a chip.
#
# That is also what it costs. There is no acceleration, and there is no card
# this package is *tuned* for - a card that deserves better gets its own family
# later and keeps this one as the fallback it started on.
#
# Known limit: the S3 ViRGE/DX BIOS ignores the generic linear-framebuffer bit,
# so this package cannot drive one. It refuses at stage 3 rather than rendering
# incorrectly, which is the documented behaviour, not a bug to fix here.
@{
    SchemaVersion = 1
    Id = 'vbe'
    DisplayName = 'VBE tier-0 (generic VESA)'
    Description = 'Chip-agnostic VBE 2.0+ backend: BIOS mode set, linear framebuffer from 4F01h, VRAM from 4F00h, no acceleration.'

    Chips = @(
        @{
            Id = 'std-vga'
            Name = 'QEMU/Bochs VBE std-vga'
            VendorId = '1234'
            DeviceId = '1111'
            # QEMU stamps its std-vga with the Red Hat subsystem 1af4:1100 on
            # every observed host (QEMU 4.2 Windows, UTM/QEMU on macOS). Win98
            # Have Disk matches HardwareIDs, which carry this SUBSYS.
            SubsystemId = '11001AF4'
            DeviceDesc = 'Velocity9x VBE-generic display (QEMU std-vga)'
            Adapter = 'QEMU/Bochs VBE (generic VESA linear framebuffer)'
            ClockDetector = 'vbe-generic-unavailable-v1'
            ModeSwitching = 'vbe-lfb'
            Acceleration = 'none'
            Direct3D = 'not-advertised'
            # No engine, by definition of the tier. The 32-bit HAL resolves no
            # ops table for this type and every blit falls to blt_cpu.c.
            EngineType = 'NONE'
            EngineCaps = @()
            # A declared floor, not a measurement: the driver learns the real
            # size from 4F00h at run time and clamps it to what it maps. This
            # value is what the host mode-agreement check lays the modes out
            # against, so it must be enough for the largest of them (1024x768
            # at 16 bpp is 1.5 MiB) and honest about the smallest card claimed.
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

            # No required instructions, and this is a decision rather than an
            # omission.
            #
            # A family's Required patterns become every other family's
            # Forbidden patterns, and the audit scans the whole image. The only
            # instructions that would identify tier-0 are the 4F00h and 4F01h
            # calls - and those live in src\display16\hw\vbe16.c, which every
            # family links, so they appear in the S3 and Matrox images too.
            # Claiming them here would fail those two families' audits while
            # proving nothing about this one.
            #
            # Identity is carried instead by the map symbol below, the family
            # dispatch symbol, and the INF hardware-id set equality. Same
            # reasoning as the Trio64 chip in the s3 manifest.
            #
            # Do not add patterns here without first moving the code they
            # match out of shared objects.
            Audit = @{
                Required = @()
                Forbidden = @()
            }
            Objects = @('vbe_hw16')
            MapSymbols = @('v9x_vbe_device')
        }
    )

    Build = @{
        # No chipset modules at all beyond the generic pair - that absence is
        # the family.
        Sources = @(
            @{ Name = 'build'; Path = 'src\common\build.c' }
            @{ Name = 'log'; Path = 'src\common\log.c' }
            @{ Name = 'mode'; Path = 'src\common\mode.c' }
            @{ Name = 'resources'; Path = 'src\common\resources.c' }
            @{ Name = 'vbe_parse'; Path = 'src\common\vbe_parse.c' }
            @{ Name = 'vbe_modes'; Path = 'src\common\vbe_modes.c' }
            @{ Name = 'modes16'; Path = 'src\display16\modes16.c' }
            @{ Name = 'vbe_hw16'; Path = 'src\chipsets\generic\vbe\vbe_hw16.c' }
            @{ Name = 'vbe16'; Path = 'src\display16\hw\vbe16.c' }
            @{ Name = 'enable16'; Path = 'src\display16\enable16.c' }
            @{ Name = 'display_component'; Path = 'src\display16\display_component.c' }
            @{ Name = 'loader'; Path = 'src\display16\loader.c' }
            @{ Name = 'ddi'; Path = 'src\display16\ddi.c' }
            @{ Name = 'dd16'; Path = 'src\display16\dd16.c' }
        )
        Defines = @()
        RuntimeDefines = @()
        SkeletonOutput = 'build\win16-ddi-vbe'
        PackageOutput = 'build\win98se-vbe'
        VmStageDirectory = 'build\vm-probe\VBE'
    }

    Audit = @{
        # Nothing family-wide either, for the reason given on the chip above.
        RequiredInstructions = @()
        ForbiddenInstructions = @()
        RequiredMapSymbols = @()
        DispatchSymbol = 'v9x_hw16'
        BackendSymbols = @('v9x_vbe_device')
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
        HalDescription = 'V9XHAL.DLL (vidmem + flip, CPU blits only)'
    }

    Floppy = @{
        Include = $true
        Folder = 'VBE'
        Order = 2
        # The INF claims one id because one is what has been tested - not a
        # wildcard that makes Windows bind this driver to every display adapter
        # it finds. Other VBE 2.0 cards are reached by choosing this package
        # through Have-Disk, which is a decision a person makes rather than one
        # Windows makes for them.
        #
        # That route only works because this family sets pci_match_optional:
        # until 2026-08-16 the driver carried a second allowlist of its own and
        # refused at stage 1 on any card the family did not name, whatever the
        # INF said. See docs\issues\2026-08-16-tier0-defects-deferred.md D3.
        HardwareIdHint = 'PCI 1234:1111 automatically; any VBE 2.0 card via Have-Disk'
    }

    Vm = @{
        Emulator = 'qemu'
        Controller = 'std-vga'
        Bios = ''
        Profile = 'Win98SE-QEMU-StdVGA'
        Port = 9872
        ReferenceProfile = ''
        ReferencePort = 0
        Modes = @('640x480x8', '800x600x8', '1024x768x8',
                  '640x480x16', '800x600x16', '1024x768x16')
    }
}
