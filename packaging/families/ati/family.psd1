# Velocity9x family manifest: ATI Mach64 / Rage.
#
# One binary, two chips, dispatched at run time by PCI id - the shape the s3
# family proved at phase 8. The chips are three years apart deliberately: the
# Mach64 VT2 (1996) is what 86Box can emulate, and the Rage Mobility-M (1999)
# is the physical target. One binary serves both because the Mach64 GUI
# register set is common across GX, CT, VT and Rage, so what the emulated part
# proves about the command stream is true of the real one.
#
# What the emulated part proves NOTHING about is timing, memory sizing or the
# LCD panel. 86Box's engine can never be observed busy, its MEM_CNTL is a
# scratch register unconnected to the configured VRAM, and it has no panel
# registers at all. See docs\decisions\2026-08-16-ati-mach64-hardware-audit.md.
#
# This is tier-0: every hw16 hook is NULL, so the VBE 4F02h mode set programs
# the card, 4F01h reports where the framebuffer landed, and the CPU draws.
# EngineType and EngineCaps below say exactly that, and change only when
# src\display32\engines\eng_mach64.c exists and has been measured.
@{
    SchemaVersion = 1
    Id = 'ati'
    DisplayName = 'ATI Mach64 / Rage'
    Description = 'ATI Mach64 VT2 and Rage Mobility-M, dispatched at run time by PCI id. Tier-0 VBE bring-up, no acceleration.'

    Chips = @(
        @{
            Id = 'mach64-vt2'
            Name = 'ATI Mach64 VT2 264VT2'
            VendorId = '1002'
            DeviceId = '5654'
            DeviceDesc = 'Velocity9x ATI Mach64 VT2'
            Adapter = 'ATI Mach64 VT2 264VT2'
            ClockDetector = 'ati-mach64-unavailable-v1'
            ModeSwitching = 'vbe-lfb'
            Acceleration = 'none'
            Direct3D = 'not-advertised'
            EngineType = 'NONE'
            EngineCaps = @()
            # A declared floor for the mode-layout check, not a measurement.
            # Deliberately NOT derived from VBE 4F00h: the Rage Mobility's BIOS
            # reports 512 KiB on a panel running 1024x768x16, which is 1.5 MiB
            # of visible pixels on its own. The driver learns the real size at
            # run time; this number is only what every advertised mode has to
            # lay out against.
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

            Objects = @('vt2_hw16')

            # No required instructions, and this is a decision rather than an
            # omission.
            #
            # A family's Required patterns become every OTHER family's
            # Forbidden patterns, and the audit scans the whole image. At
            # tier-0 this chip owns no register sequence at all: its PCI
            # identity, its VBE mode numbers and the linear-framebuffer flag
            # are data in this object, stamped into DGROUP by ddi.c, so no
            # instruction signature could find them. The 4F00h/4F01h calls that
            # do produce instructions live in shared src\display16\hw\vbe16.c
            # and appear in the s3, matrox-m2 and vbe images too - claiming
            # them here would fail those three builds while proving nothing
            # about this one. Same reasoning as the vbe family.
            #
            # Identity is carried by MapSymbols below, Audit.DispatchSymbol,
            # and the generated INF's hardware-id set equality.
            #
            # When eng_mach64.c lands: move the code producing a signature into
            # this family's own object FIRST, then anchor the immediate
            # (or\s+al,8\b, never or\s+al,8) - an unanchored pattern also
            # matches longer immediates and convicts a different family.
            Audit = @{
                Required = @()
                Forbidden = @()
            }
            MapSymbols = @('v9x_mach64_vt2_device')
        }
        @{
            Id = 'rage-mobility-m'
            Name = 'ATI Rage Mobility-M AGP'
            VendorId = '1002'
            DeviceId = '4C4D'
            DeviceDesc = 'Velocity9x ATI Rage Mobility-M'
            Adapter = 'ATI Rage Mobility-M AGP'
            ClockDetector = 'ati-mach64-unavailable-v1'
            ModeSwitching = 'vbe-lfb'
            Acceleration = 'none'
            Direct3D = 'not-advertised'
            EngineType = 'NONE'
            EngineCaps = @()
            VideoMemoryBytes = 4194304

            # Every row confirmed present with a linear framebuffer in this
            # card's own BIOS mode list, and every row present in the panel's
            # per-mode timing table - including 640x400, which the plan had
            # flagged as doubtful.
            Modes = @(
                @{ BitsPerPixel = 8; Width = 640; Height = 480; RefreshRate = 60; VbeMode = '0101' }
                @{ BitsPerPixel = 8; Width = 800; Height = 600; RefreshRate = 60; VbeMode = '0103' }
                @{ BitsPerPixel = 8; Width = 1024; Height = 768; RefreshRate = 60; VbeMode = '0105' }
                @{ BitsPerPixel = 8; Width = 640; Height = 400; RefreshRate = 60; VbeMode = '0100' }
                @{ BitsPerPixel = 16; Width = 640; Height = 480; RefreshRate = 60; VbeMode = '0111' }
                @{ BitsPerPixel = 16; Width = 800; Height = 600; RefreshRate = 60; VbeMode = '0114' }
                @{ BitsPerPixel = 16; Width = 1024; Height = 768; RefreshRate = 60; VbeMode = '0117' }
            )

            Objects = @('mobility_hw16')
            Audit = @{
                Required = @()
                Forbidden = @()
            }
            MapSymbols = @('v9x_rage_mobility_device')
        }
    )

    Build = @{
        # Compile order is link order. Reordering changes the image.
        # One object per chip, then the family table that points at both -
        # the split the per-object audit layer needs to tell them apart.
        Sources = @(
            @{ Name = 'build'; Path = 'src\common\build.c' }
            @{ Name = 'log'; Path = 'src\common\log.c' }
            @{ Name = 'mode'; Path = 'src\common\mode.c' }
            @{ Name = 'resources'; Path = 'src\common\resources.c' }
            @{ Name = 'vbe_parse'; Path = 'src\common\vbe_parse.c' }
            @{ Name = 'vt2_hw16'; Path = 'src\chipsets\ati\vt2\vt2_hw16.c' }
            @{ Name = 'mobility_hw16'; Path = 'src\chipsets\ati\mobility\mobility_hw16.c' }
            @{ Name = 'ati_hw16'; Path = 'src\chipsets\ati\ati_hw16.c' }
            @{ Name = 'vbe16'; Path = 'src\display16\hw\vbe16.c' }
            @{ Name = 'enable16'; Path = 'src\display16\enable16.c' }
            @{ Name = 'display_component'; Path = 'src\display16\display_component.c' }
            @{ Name = 'loader'; Path = 'src\display16\loader.c' }
            @{ Name = 'ddi'; Path = 'src\display16\ddi.c' }
            @{ Name = 'dd16'; Path = 'src\display16\dd16.c' }
        )
        Defines = @()
        RuntimeDefines = @()
        SkeletonOutput = 'build\win16-ddi-ati'
        PackageOutput = 'build\win98se-ati'
        VmStageDirectory = 'build\vm-probe\ATI'
    }

    Audit = @{
        RequiredInstructions = @()
        ForbiddenInstructions = @()
        RequiredMapSymbols = @()
        DispatchSymbol = 'v9x_hw16'
        BackendSymbols = @('v9x_mach64_vt2_device', 'v9x_rage_mobility_device')
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
        Folder = 'ATI'
        Order = 3
        HardwareIdHint = 'PCI 1002:5654, 1002:4C4D'
    }

    Vm = @{
        Emulator = '86box'
        Controller = 'mach64vt2'
        Bios = ''
        Profile = 'Win98SE-Mach64VT2'
        Port = 9873
        ReferenceProfile = ''
        ReferencePort = 0
        Modes = @('640x480x8', '800x600x8', '1024x768x8',
                  '640x480x16', '800x600x16', '1024x768x16')
        # One entry per chip. 86Box emulates no Rage of any kind - its mach64
        # ROMs stop at the VT2 - so that chip is real hardware only and carries
        # a per-target Emulator of 'none'. The mode matrix will refuse it with
        # an explicit real-hardware error rather than testing the wrong guest.
        #
        # Note this means a green run-checks is NOT a green ati family: half of
        # it has no automated coverage at all.
        Targets = @(
            @{
                ChipId = 'mach64-vt2'
                Profile = 'Win98SE-Mach64VT2'
                Port = 9873
                Controller = 'mach64vt2'
            }
            @{
                ChipId = 'rage-mobility-m'
                Emulator = 'none'
            }
        )
    }
}
