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
    Description = 'ATI Mach64 VT2, Rage Mobility-M and 3D Rage IIC, dispatched at run time by PCI id, plus bound-but-unvalidated aliases of each: Mach64 VT3/VT4, the Rage II class and the Rage Pro class.'

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
            # and appear in the s3, matrox and vbe images too - claiming
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

            # The VT3 and VT4: the VT2's 2D engine and video, no 3D engine,
            # bound to this chip's tier-0 path (VBE modes, CPU drawing).
            # Ids from ATI's MACXW4 INF list (Michael, 2026-10-03); neither
            # part has run anywhere.
            Aliases = @(
                @{ DeviceId = '5655'
                   Name = 'ATI Mach64 VT3 264VT3'
                   DeviceDesc = 'Velocity9x ATI Mach64 VT3' }
                @{ DeviceId = '5656'
                   Name = 'ATI Mach64 VT4 264VT4'
                   DeviceDesc = 'Velocity9x ATI Mach64 VT4' }
            )
        }
        @{
            Id = 'rage-mobility-m'
            Name = 'ATI Rage Mobility-M AGP'
            VendorId = '1002'
            DeviceId = '4C4D'
            # The Gateway Solo 2150's HardwareID leads with SUBSYS_2150107B;
            # Have Disk matched it with the qualified id first (2026-09-28).
            SubsystemId = '2150107B'
            DeviceDesc = 'Velocity9x ATI Rage Mobility-M'
            Adapter = 'ATI Rage Mobility-M AGP'
            ClockDetector = 'ati-mach64-unavailable-v1'
            ModeSwitching = 'vbe-lfb'
            # The Mach64 engine behind BAR2: Direct3D (d3d_mach64.c), and the
            # Phase 2 fill for DirectDraw colour and depth fills, which the
            # HAL routes by engine type. Screen copy still declines.
            Acceleration = 'directdraw-fill'
            Direct3D = 'hardware-mach64'
            EngineType = 'ATI_MACH64'
            EngineCaps = @('D3D')
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

            # The Rage Pro class - Rage Pro, LT Pro, XL, XC and the other
            # Mobility parts - bound to this chip's Mach64 3D engine path:
            # the Mobility-M is a Rage Pro core, and its setup engine,
            # register window and VTB+ FIFO are what these parts share.
            # Bound at Michael's request (2026-10-03) from ATI's MACXW4 INF
            # list; none has run anywhere, so each is "not validated" in the
            # INF. eng_mach64.c accepts their CONFIG_CHIP_ID as the Rage Pro
            # class (v9x_m64_chip_class) before it drives the engine.
            Aliases = @(
                @{ DeviceId = '4742'
                   Name = 'ATI 3D Rage Pro AGP 2X'
                   DeviceDesc = 'Velocity9x ATI Rage Pro AGP 2X' }
                @{ DeviceId = '4744'
                   Name = 'ATI 3D Rage Pro AGP'
                   DeviceDesc = 'Velocity9x ATI Rage Pro AGP' }
                @{ DeviceId = '4747'
                   Name = 'ATI 3D Rage Pro'
                   DeviceDesc = 'Velocity9x ATI Rage Pro' }
                @{ DeviceId = '4749'
                   Name = 'ATI 3D Rage Pro PCI'
                   DeviceDesc = 'Velocity9x ATI Rage Pro PCI (4749)' }
                @{ DeviceId = '4750'
                   Name = 'ATI 3D Rage Pro PCI'
                   DeviceDesc = 'Velocity9x ATI Rage Pro PCI (4750)' }
                @{ DeviceId = '4751'
                   Name = 'ATI 3D Rage Pro PCI'
                   DeviceDesc = 'Velocity9x ATI Rage Pro PCI (4751)' }
                @{ DeviceId = '474C'
                   Name = 'ATI 3D Rage XC PCI-66'
                   DeviceDesc = 'Velocity9x ATI Rage XC PCI-66' }
                @{ DeviceId = '474D'
                   Name = 'ATI 3D Rage XL AGP'
                   DeviceDesc = 'Velocity9x ATI Rage XL AGP' }
                @{ DeviceId = '474E'
                   Name = 'ATI 3D Rage XC AGP'
                   DeviceDesc = 'Velocity9x ATI Rage XC AGP' }
                @{ DeviceId = '474F'
                   Name = 'ATI 3D Rage XL PCI-66'
                   DeviceDesc = 'Velocity9x ATI Rage XL PCI-66' }
                @{ DeviceId = '4752'
                   Name = 'ATI 3D Rage XL PCI'
                   DeviceDesc = 'Velocity9x ATI Rage XL PCI' }
                @{ DeviceId = '4753'
                   Name = 'ATI 3D Rage XC PCI'
                   DeviceDesc = 'Velocity9x ATI Rage XC PCI' }
                @{ DeviceId = '4C42'
                   Name = 'ATI 3D Rage LT Pro AGP 2X'
                   DeviceDesc = 'Velocity9x ATI Rage LT Pro AGP 2X' }
                @{ DeviceId = '4C44'
                   Name = 'ATI 3D Rage LT Pro AGP'
                   DeviceDesc = 'Velocity9x ATI Rage LT Pro AGP' }
                @{ DeviceId = '4C49'
                   Name = 'ATI 3D Rage LT Pro PCI'
                   DeviceDesc = 'Velocity9x ATI Rage LT Pro PCI (4C49)' }
                @{ DeviceId = '4C50'
                   Name = 'ATI 3D Rage LT Pro PCI'
                   DeviceDesc = 'Velocity9x ATI Rage LT Pro PCI (4C50)' }
                @{ DeviceId = '4C51'
                   Name = 'ATI 3D Rage LT Pro PCI'
                   DeviceDesc = 'Velocity9x ATI Rage LT Pro PCI (4C51)' }
                @{ DeviceId = '4C4E'
                   Name = 'ATI 3D Rage Mobility-L AGP'
                   DeviceDesc = 'Velocity9x ATI Rage Mobility-L AGP' }
                @{ DeviceId = '4C52'
                   Name = 'ATI 3D Rage Mobility P/M PCI'
                   DeviceDesc = 'Velocity9x ATI Rage Mobility P/M PCI' }
                @{ DeviceId = '4C53'
                   Name = 'ATI 3D Rage Mobility-L PCI'
                   DeviceDesc = 'Velocity9x ATI Rage Mobility-L PCI' }
            )
        }
        @{
            Id = 'rage-iic'
            Name = 'ATI 3D Rage IIC AGP'
            VendorId = '1002'
            DeviceId = '4757'
            # A8U4I5's HardwareID leads with SUBSYS_47571002 (inventory,
            # 2026-10-02); qualified first, as for the Gateway.
            SubsystemId = '47571002'
            DeviceDesc = 'Velocity9x ATI 3D Rage IIC'
            Adapter = 'ATI 3D Rage IIC AGP'
            ClockDetector = 'ati-mach64-unavailable-v1'
            ModeSwitching = 'vbe-lfb'
            # A Rage II-class 264GT2C: the Mobility's register window and 2D
            # engine, no triangle setup engine. The 2D engine serves
            # DirectDraw fill and copy as ATI_RAGE2, a type of its own so
            # d3d_select.c never routes it to d3d_mach64.c. Direct3D is
            # d3d_rage2.c, which sets every triangle up on the CPU from the
            # Phase 1-4 measurements (docs\plans\ati-rage-iic-hardware-3d.md).
            Acceleration = 'directdraw-fill-copy'
            Direct3D = 'hardware-rage2'
            EngineType = 'ATI_RAGE2'
            EngineCaps = @('SOLID_FILL', 'SCREEN_COPY', 'FLIP', 'VBLANK', 'D3D')
            # MEM_CNTL measured 4 MiB on A8U4I5; 3DMark reported 4074 KB.
            VideoMemoryBytes = 4194304

            # The VESA-standard rows the other two chips carry. NOT yet
            # checked against this BIOS (3.096): the read-only probe could
            # not issue 4F01h. The first enable's V9XMODES.INI settles it.
            Modes = @(
                @{ BitsPerPixel = 8; Width = 640; Height = 480; RefreshRate = 60; VbeMode = '0101' }
                @{ BitsPerPixel = 8; Width = 800; Height = 600; RefreshRate = 60; VbeMode = '0103' }
                @{ BitsPerPixel = 8; Width = 1024; Height = 768; RefreshRate = 60; VbeMode = '0105' }
                @{ BitsPerPixel = 8; Width = 640; Height = 400; RefreshRate = 60; VbeMode = '0100' }
                @{ BitsPerPixel = 16; Width = 640; Height = 480; RefreshRate = 60; VbeMode = '0111' }
                @{ BitsPerPixel = 16; Width = 800; Height = 600; RefreshRate = 60; VbeMode = '0114' }
                @{ BitsPerPixel = 16; Width = 1024; Height = 768; RefreshRate = 60; VbeMode = '0117' }
            )

            Objects = @('rage_iic_hw16')
            Audit = @{
                Required = @()
                Forbidden = @()
            }
            MapSymbols = @('v9x_rage_iic_device')

            # The rest of the Rage II class - Rage II, II+, the other Rage IIC
            # ids, LT and LT-G - bound to this chip's path: the Rage II 3D
            # engine with no setup engine, set up on the CPU (d3d_rage2.c),
            # and the pre-VTB 16-entry FIFO. Bound at Michael's request
            # (2026-10-03) from ATI's MACXW4 INF list; none has run anywhere.
            # The register window is BAR2 on the 264GT2C; a part without one
            # has its engine refused by the mini-VDD's map and stays tier-0.
            Aliases = @(
                @{ DeviceId = '4754'
                   Name = 'ATI 3D Rage II PCI'
                   DeviceDesc = 'Velocity9x ATI Rage II PCI' }
                @{ DeviceId = '4755'
                   Name = 'ATI 3D Rage II+ DVD PCI'
                   DeviceDesc = 'Velocity9x ATI Rage II+ DVD PCI' }
                @{ DeviceId = '4756'
                   Name = 'ATI 3D Rage IIC PCI'
                   DeviceDesc = 'Velocity9x ATI Rage IIC PCI (4756)' }
                @{ DeviceId = '4759'
                   Name = 'ATI 3D Rage IIC PCI'
                   DeviceDesc = 'Velocity9x ATI Rage IIC PCI (4759)' }
                @{ DeviceId = '475A'
                   Name = 'ATI 3D Rage IIC AGP'
                   DeviceDesc = 'Velocity9x ATI Rage IIC AGP' }
                @{ DeviceId = '4C54'
                   Name = 'ATI 3D Rage LT PCI'
                   DeviceDesc = 'Velocity9x ATI Rage LT PCI' }
                @{ DeviceId = '4C47'
                   Name = 'ATI 3D Rage LT-G PCI'
                   DeviceDesc = 'Velocity9x ATI Rage LT-G PCI' }
            )
        }
    )

    # The host-testable policy backend; see the s3 manifest for the shape.
    Backend = @{
        Getter = 'v9x_ati_mach64_backend'
        Header = 'velocity9x/ati_mach64.h'
        Sources = @(
            'src\chipsets\ati\ati_backend.c'
        )
    }

    Build = @{
        # This family has no read_aperture hook, so the mini-VDD's 4F9Ch cache
        # is the only way it can learn where its framebuffer is. The collection
        # therefore has to stay on, and Test-V9xFamilyManifest now enforces
        # that pairing rather than leaving it to a comment.
        #
        # It was $false between the Stage 1 pipeline work and 2026-08-26, on the
        # reasoning that "Stage 1's many-mode BIOS walk rolls out to QEMU VBE
        # first" - a sequencing preference. The consequence was not sequencing:
        # a freshly built package could not enable at all, refusing at stage 3
        # with fail-hardware-aperture and falling back to 4-bpp vga.drv. It went
        # unnoticed because the only guest that would have shown it was running
        # an installed binary from before the change
        # (docs\issues\2026-08-26-ati-package-cannot-enable.md).
        #
        # Verified on Win98SE-Mach64VT2 after restoring it: enable-ok with 22
        # modes cached off a VBE 2.0 BIOS.
        MiniVddVbeCollect = $true
        # Compile order is link order. Reordering changes the image.
        # One object per chip, then the family table that points at both -
        # the split the per-object audit layer needs to tell them apart.
        Sources = @(
            @{ Name = 'build'; Path = 'src\common\build.c' }
            @{ Name = 'log'; Path = 'src\common\log.c' }
            @{ Name = 'mode'; Path = 'src\common\mode.c' }
            @{ Name = 'resources'; Path = 'src\common\resources.c' }
            @{ Name = 'vbe_parse'; Path = 'src\common\vbe_parse.c' }
            @{ Name = 'vbe_modes'; Path = 'src\common\vbe_modes.c' }
            @{ Name = 'edid'; Path = 'src\common\edid.c' }
            @{ Name = 'mtrr'; Path = 'src\common\mtrr.c' }
            # The Direct3D back-end decision behind [Velocity9x] Direct3D.
            # Pure policy, host-tested; dd16.c resolves it at Enable and every
            # family publishes the result as Direct3DMode= whether or not it
            # has a DirectDraw HAL to apply it to.
            @{ Name = 'd3dmode'; Path = 'src\common\d3dmode.c' }
            @{ Name = 'vsync'; Path = 'src\common\vsync.c' }
            @{ Name = 'modes16'; Path = 'src\display16\modes16.c' }
            @{ Name = 'vt2_hw16'; Path = 'src\chipsets\ati\vt2\vt2_hw16.c' }
            @{ Name = 'mobility_hw16'; Path = 'src\chipsets\ati\mobility\mobility_hw16.c' }
            @{ Name = 'rage_iic_hw16'; Path = 'src\chipsets\ati\rageiic\rage_iic_hw16.c' }
            @{ Name = 'ati_hw16'; Path = 'src\chipsets\ati\ati_hw16.c' }
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
        Defines = @()
        # runtime.asm's BAR2 read and mini-VDD map for the Mobility engine.
        RuntimeDefines = @('V9X_ATI_FAMILY')
        SkeletonOutput = 'build\win16-ddi-ati'
        PackageOutput = 'build\win98se-ati'
        VmStageDirectory = 'build\vm-probe\ATI'
    }

    Audit = @{
        RequiredInstructions = @()
        ForbiddenInstructions = @()
        RequiredMapSymbols = @()
        DispatchSymbol = 'v9x_hw16'
        BackendSymbols = @('v9x_mach64_vt2_device', 'v9x_rage_mobility_device',
                           'v9x_rage_iic_device')
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
        HardwareIdHint = 'PCI 1002:5654, 1002:4C4D, 1002:4757, and the Rage II, Rage Pro and VT3/VT4 aliases'
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
            @{
                ChipId = 'rage-iic'
                Emulator = 'none'
            }
        )
    }
}

