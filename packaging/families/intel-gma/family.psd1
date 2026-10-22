@{
    SchemaVersion = 1
    Id = 'intel-gma'
    DisplayName = 'Intel GMA and HD Graphics'
    Description = 'Intel GMA 950 on 945GSE, GMA 900 on 915GM and GMA 3150 on Pineview: VBE display; hardware Direct3D, ring page flips and DirectDraw blits on the Gen3 engine. GMA X3100 on GM965: VBE display; DirectDraw blits on the ring. HD Graphics on Ironlake: VBE display; DirectDraw blits on the ring and page flips on plane A. HD Graphics 4600 (Haswell): VBE display; hardware Direct3D and OpenGL on the render engine, page flips on plane A, DirectDraw blits on the BCS ring. HD Graphics 4000 (Ivy Bridge): VBE display; hardware Direct3D on the render engine, page flips on plane A, DirectDraw blits on the BCS ring.'

    Chips = @(
        @{
            Id = 'gma950-945gse'
            Name = 'Intel GMA 950 (945GSE)'
            VendorId = '8086'
            DeviceId = '27AE'
            DeviceDesc = 'Velocity9x Intel GMA 950 (945GSE)'
            Adapter = 'Intel GMA 950 (945GSE)'
            ClockDetector = 'intel-gen3-mmio-fingerprint-v1'
            ModeSwitching = 'vbe-lfb'
            # DirectDraw fill and copy on the Gen3 blitter, flips through the
            # ring (eng_i9xx.c, 2026-09-25). Was 'none' until 2026-10-04.
            Acceleration = 'directdraw-fill-blt'
            # The settings page's notion of "this card has an engine", which
            # is a different question from whether this boot may run it. It
            # said 'not-advertised' for the first hours of the capability
            # existing, four lines above EngineCaps claiming D3D, and the
            # page believed it: no Hardware entry on the one card the engine
            # was written for. check-tree now refuses the disagreement.
            Direct3D = 'hardware-gen3'
            # Claimed from 2026-09-16, under the sustained-3D amendment to
            # the errata gate. The descriptor the driver actually publishes
            # comes from the chip's fill_engine_descriptor, which claims it
            # only when both apertures AND the ring came up; this is the
            # manifest agreeing with that rather than a second authority.
            EngineType = 'INTEL_GEN3'
            # D3D alone. Engine-only ownership leaves the display to the
            # VBIOS, so no 2D capability is claimed - nothing here has
            # driven a blit outside the armed diagnostic.
            EngineCaps = @('D3D')
            VideoMemoryBytes = 4194304
            Modes = @(
                @{ BitsPerPixel = 8; Width = 640; Height = 480; RefreshRate = 60; VbeMode = '0101' }
                @{ BitsPerPixel = 8; Width = 1024; Height = 576; RefreshRate = 60; VbeMode = '0160' }
                @{ BitsPerPixel = 16; Width = 640; Height = 480; RefreshRate = 60; VbeMode = '0111' }
                @{ BitsPerPixel = 16; Width = 1024; Height = 576; RefreshRate = 60; VbeMode = '0161' }
            )
            Audit = @{ Required = @(); Forbidden = @() }
            Objects = @('gma950_hw16')
            MapSymbols = @('v9x_gma950_device')
        }
        @{
            # The 915GM's GMA 900, from 0.13.0. Claimed on a DOS survey of a
            # Lenovo 3000 C100 sent with GitHub issue 3; no 915 has run this
            # driver. The descriptor is the 945's plus the stacked mip layout
            # bit: docs\decisions\2026-10-08-intel-915gm-gma900-support.md.
            Id = 'gma900-915gm'
            Name = 'Intel GMA 900 (915GM)'
            VendorId = '8086'
            DeviceId = '2592'
            DeviceDesc = 'Velocity9x Intel GMA 900 (915GM)'
            Adapter = 'Intel GMA 900 (915GM)'
            ClockDetector = 'intel-gen3-mmio-fingerprint-v1'
            ModeSwitching = 'vbe-lfb'
            Acceleration = 'directdraw-fill-blt'
            Direct3D = 'hardware-gen3'
            EngineType = 'INTEL_GEN3'
            EngineCaps = @('D3D')
            VideoMemoryBytes = 4194304
            # The family's table, which the driver's own must equal. The
            # 1024x576 rows are the netbook's OEM modes and this BIOS does not
            # describe them; the runtime VBE scan hides them and offers the
            # BIOS's 1024x768 instead.
            Modes = @(
                @{ BitsPerPixel = 8; Width = 640; Height = 480; RefreshRate = 60; VbeMode = '0101' }
                @{ BitsPerPixel = 8; Width = 1024; Height = 576; RefreshRate = 60; VbeMode = '0160' }
                @{ BitsPerPixel = 16; Width = 640; Height = 480; RefreshRate = 60; VbeMode = '0111' }
                @{ BitsPerPixel = 16; Width = 1024; Height = 576; RefreshRate = 60; VbeMode = '0161' }
            )
            Audit = @{ Required = @(); Forbidden = @() }
            Objects = @('gma950_hw16')
            MapSymbols = @('v9x_gma900_device')
        }
        @{
            # Pineview's GMA 3150 (Atom N4xx/N5xx). The 945's descriptor and
            # mip layout, plus I9XX_NO_FW_BLC: its watermarks are DSPFW1/3,
            # never FW_BLC. Tier-0 measured on TOMMO33, the engine not run:
            # docs\decisions\2026-10-20-pineview-tommo33-tier0.md.
            Id = 'gma3150-pineview'
            Name = 'Intel GMA 3150 (Pineview)'
            VendorId = '8086'
            DeviceId = 'A011'
            DeviceDesc = 'Velocity9x Intel GMA 3150 (Pineview)'
            Adapter = 'Intel GMA 3150 (Pineview)'
            ClockDetector = 'intel-gen3-mmio-fingerprint-v1'
            ModeSwitching = 'vbe-lfb'
            Acceleration = 'directdraw-fill-blt'
            Direct3D = 'hardware-gen3'
            EngineType = 'INTEL_GEN3'
            EngineCaps = @('D3D')
            VideoMemoryBytes = 4194304
            # The family's table, which the driver's own must equal. TOMMO33's
            # BIOS describes 0160-0162 as 1024x600, not the netbook's 1024x576.
            Modes = @(
                @{ BitsPerPixel = 8; Width = 640; Height = 480; RefreshRate = 60; VbeMode = '0101' }
                @{ BitsPerPixel = 8; Width = 1024; Height = 576; RefreshRate = 60; VbeMode = '0160' }
                @{ BitsPerPixel = 16; Width = 640; Height = 480; RefreshRate = 60; VbeMode = '0111' }
                @{ BitsPerPixel = 16; Width = 1024; Height = 576; RefreshRate = 60; VbeMode = '0161' }
            )
            Audit = @{ Required = @(); Forbidden = @() }
            Objects = @('gma950_hw16')
            MapSymbols = @('v9x_gma3150_device')
        }
        @{
            # The GM965's GMA X3100 (Gen4), from 0.23.0: the 945's ring and
            # blits with the GTT found in BAR0, Direct3D on the Gen4 engine
            # (d3d_g4.c; Gen4 has no Gen3 3D command set), and register flips
            # through DSPSURF and DSPLINOFF. TOMMOHP:
            # docs\decisions\2026-10-21-gm965-direct3d.md.
            Id = 'gma-x3100-gm965'
            Name = 'Intel GMA X3100 (GM965)'
            VendorId = '8086'
            DeviceId = '2A02'
            DeviceDesc = 'Velocity9x Intel GMA X3100 (GM965)'
            Adapter = 'Intel GMA X3100 (GM965)'
            ClockDetector = 'intel-gen3-mmio-fingerprint-v1'
            ModeSwitching = 'vbe-lfb'
            Acceleration = 'directdraw-fill-blt'
            Direct3D = 'hardware-gen4'
            EngineType = 'INTEL_GEN3'
            EngineCaps = @('SOLID_FILL', 'SCREEN_COPY', 'FLIP', 'D3D')
            VideoMemoryBytes = 4194304
            # The family's table, which the driver's own must equal. TOMMOHP's
            # BIOS describes 0160-0165 as 1024x768; the runtime scan publishes
            # the modes it finds.
            Modes = @(
                @{ BitsPerPixel = 8; Width = 640; Height = 480; RefreshRate = 60; VbeMode = '0101' }
                @{ BitsPerPixel = 8; Width = 1024; Height = 576; RefreshRate = 60; VbeMode = '0160' }
                @{ BitsPerPixel = 16; Width = 640; Height = 480; RefreshRate = 60; VbeMode = '0111' }
                @{ BitsPerPixel = 16; Width = 1024; Height = 576; RefreshRate = 60; VbeMode = '0161' }
            )
            Audit = @{ Required = @(); Forbidden = @() }
            Objects = @('gma950_hw16')
            MapSymbols = @('v9x_gm965_device')
        }
        @{
            # Ironlake desktop HD Graphics (Gen5): the 945's ring and blits,
            # with the GTT at BAR0 + 2 MiB, and register flips through DSPSURF
            # and DSPLINOFF with the Gen3 display registers it lacks skipped
            # (I9XX_GEN5). No Direct3D (no Gen5 kernels exist), no ring flips.
            # Measured read-only on W9QI2F4F:
            # docs\decisions\2026-10-22-ironlake-gen5-read-only-mmio.md.
            Id = 'hd-ironlake'
            Name = 'Intel HD Graphics (Ironlake)'
            VendorId = '8086'
            DeviceId = '0042'
            DeviceDesc = 'Velocity9x Intel HD Graphics (Ironlake)'
            Adapter = 'Intel HD Graphics (Ironlake)'
            ClockDetector = 'intel-gen3-mmio-fingerprint-v1'
            ModeSwitching = 'vbe-lfb'
            Acceleration = 'directdraw-fill-blt'
            Direct3D = 'not-advertised'
            EngineType = 'INTEL_GEN3'
            EngineCaps = @('SOLID_FILL', 'SCREEN_COPY', 'FLIP')
            VideoMemoryBytes = 4194304
            # The family's table, which the driver's own must equal. This
            # BIOS describes neither 1024x576 row; the runtime scan publishes
            # the modes it finds.
            Modes = @(
                @{ BitsPerPixel = 8; Width = 640; Height = 480; RefreshRate = 60; VbeMode = '0101' }
                @{ BitsPerPixel = 8; Width = 1024; Height = 576; RefreshRate = 60; VbeMode = '0160' }
                @{ BitsPerPixel = 16; Width = 640; Height = 480; RefreshRate = 60; VbeMode = '0111' }
                @{ BitsPerPixel = 16; Width = 1024; Height = 576; RefreshRate = 60; VbeMode = '0161' }
            )
            Audit = @{ Required = @(); Forbidden = @() }
            Objects = @('gma950_hw16')
            MapSymbols = @('v9x_ilk_hd_device')
        }
        @{
            # Haswell GT2 desktop, from 0.19.0. Gen7.5 shares no Gen3 path:
            # its own hw16 device (hsw_hw16.c), its own mini-VDD verbs
            # (HSW_ENGINE_OPEN/CLOSE) and engine type, and every family-wide
            # Gen3 hook asks v9x_intel_is_gen7 first. DirectDraw fills and
            # copies on the BCS ring, measured as B1 on the i5-4570:
            # docs\decisions\2026-10-17-haswell-phase-b1-bcs-ring.md.
            Id = 'hd4600-hsw'
            Name = 'Intel HD Graphics 4600 (Haswell)'
            VendorId = '8086'
            DeviceId = '0412'
            DeviceDesc = 'Velocity9x Intel HD Graphics 4600 (Haswell)'
            Adapter = 'Intel HD Graphics 4600 (Haswell)'
            ClockDetector = 'none'
            ModeSwitching = 'vbe-lfb'
            Acceleration = 'directdraw-fill-blt'
            Direct3D = 'hardware-gen75'
            EngineType = 'INTEL_GEN75'
            # FLIP: plane A's base (Phase F3, measured by V9XHSWMM /flip).
            EngineCaps = @('SOLID_FILL', 'SCREEN_COPY', 'FLIP', 'D3D')
            VideoMemoryBytes = 4194304
            # The family's table; this VBIOS describes neither 1024x576 row
            # and the runtime scan publishes its own modes instead.
            Modes = @(
                @{ BitsPerPixel = 8; Width = 640; Height = 480; RefreshRate = 60; VbeMode = '0101' }
                @{ BitsPerPixel = 8; Width = 1024; Height = 576; RefreshRate = 60; VbeMode = '0160' }
                @{ BitsPerPixel = 16; Width = 640; Height = 480; RefreshRate = 60; VbeMode = '0111' }
                @{ BitsPerPixel = 16; Width = 1024; Height = 576; RefreshRate = 60; VbeMode = '0161' }
            )
            Audit = @{ Required = @(); Forbidden = @() }
            Objects = @('hsw_hw16')
            MapSymbols = @('v9x_hsw_hd4600_device')
        }
        @{
            # Ivy Bridge GT2 desktop on the Haswell path: its BCS ring and
            # blits (the same BAR layout, GTT and ring registers), with Ivy
            # Bridge's MT forcewake (acknowledge 130040h, i915's
            # reset-then-wait); flips through plane A with the rest of the
            # offset in PRI_LINOFF; Direct3D on the same builders with
            # Gen7's SURFACE_STATE DW7, 3DSTATE_PS DW4 and IVB-only
            # PIPE_CONTROLs. CAP_GEN7_IVB tells the HAL. Measured on
            # W9QI4E14: docs\plans\intel-ivybridge-gen7.md.
            Id = 'hd4000-ivb'
            Name = 'Intel HD Graphics 4000 (Ivy Bridge)'
            VendorId = '8086'
            DeviceId = '0162'
            DeviceDesc = 'Velocity9x Intel HD Graphics 4000 (Ivy Bridge)'
            Adapter = 'Intel HD Graphics 4000 (Ivy Bridge)'
            ClockDetector = 'none'
            ModeSwitching = 'vbe-lfb'
            Acceleration = 'directdraw-fill-blt'
            Direct3D = 'hardware-gen7'
            EngineType = 'INTEL_GEN75'
            EngineCaps = @('SOLID_FILL', 'SCREEN_COPY', 'FLIP', 'D3D')
            VideoMemoryBytes = 4194304
            # The family's table; this VBIOS describes neither 1024x576 row
            # and the runtime scan publishes its own modes instead.
            Modes = @(
                @{ BitsPerPixel = 8; Width = 640; Height = 480; RefreshRate = 60; VbeMode = '0101' }
                @{ BitsPerPixel = 8; Width = 1024; Height = 576; RefreshRate = 60; VbeMode = '0160' }
                @{ BitsPerPixel = 16; Width = 640; Height = 480; RefreshRate = 60; VbeMode = '0111' }
                @{ BitsPerPixel = 16; Width = 1024; Height = 576; RefreshRate = 60; VbeMode = '0161' }
            )
            Audit = @{ Required = @(); Forbidden = @() }
            Objects = @('hsw_hw16')
            MapSymbols = @('v9x_ivb_hd4000_device')
        }
    )

    Backend = @{
        Getter = 'v9x_intel_gma_backend'
        Header = 'velocity9x/intel_gma.h'
        Sources = @('src\chipsets\intel\intel_backend.c')
    }

    Build = @{
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
            @{ Name = 'i9xx_mmio'; Path = 'src\chipsets\intel\i9xx_mmio.c'; CodeSegment = 'I9XXCODE' }
            @{ Name = 'i9xx_gtt'; Path = 'src\chipsets\intel\i9xx_gtt.c'; CodeSegment = 'I9XXCODE' }
            @{ Name = 'i9xx_ring'; Path = 'src\chipsets\intel\i9xx_ring.c'; CodeSegment = 'I9XXCODE' }
            @{ Name = 'i9xx_arm'; Path = 'src\chipsets\intel\i9xx_arm.c'; CodeSegment = 'I9XXCODE' }
            @{ Name = 'i9xx_float'; Path = 'src\chipsets\intel\i9xx_float.c'; CodeSegment = 'I9XXCODE' }
            @{ Name = 'i9xx_3d'; Path = 'src\chipsets\intel\i9xx_3d.c'; CodeSegment = 'I9XXCODE' }
            @{ Name = 'i9xx_fragprog'; Path = 'src\chipsets\intel\i9xx_fragprog.c'; CodeSegment = 'I9XXCODE' }
            @{ Name = 'i9xx_vertex'; Path = 'src\chipsets\intel\i9xx_vertex.c'; CodeSegment = 'I9XXCODE' }
            @{ Name = 'i9xx_3d_stream'; Path = 'src\chipsets\intel\i9xx_3d_stream.c'; CodeSegment = 'I9XXCODE' }
            @{ Name = 'i9xx_scene'; Path = 'src\chipsets\intel\i9xx_scene.c'; CodeSegment = 'I9XXCODE' }
            @{ Name = 'i9xx_texture'; Path = 'src\chipsets\intel\i9xx_texture.c'; CodeSegment = 'I9XXCODE' }
            @{ Name = 'i9xx_3d_decode'; Path = 'src\chipsets\intel\i9xx_3d_decode.c'; CodeSegment = 'I9XXCODE' }
            @{ Name = 'i9xx_chain'; Path = 'src\chipsets\intel\i9xx_chain.c'; CodeSegment = 'I9XXCODE' }
            @{ Name = 'modes16'; Path = 'src\display16\modes16.c' }
            @{ Name = 'intel_diag16'; Path = 'src\display16\intel_diag16.c'; CodeSegment = 'I9XXCODE' }
            @{ Name = 'intel_gtt16'; Path = 'src\display16\intel_gtt16.c'; CodeSegment = 'I9XXCODE' }
            @{ Name = 'intel_event16'; Path = 'src\display16\intel_event16.c'; CodeSegment = 'I9XXCODE' }
            @{ Name = 'intel_ring16'; Path = 'src\display16\intel_ring16.c'; CodeSegment = 'I9XXCODE' }
            @{ Name = 'intel_exec16'; Path = 'src\display16\intel_exec16.c'; CodeSegment = 'I9XXCODE' }
            @{ Name = 'intel_boot16'; Path = 'src\display16\intel_boot16.c'; CodeSegment = 'I9XXCODE' }
            @{ Name = 'intel_3d16'; Path = 'src\display16\intel_3d16.c'; CodeSegment = 'I9XXCODE' }
            @{ Name = 'intel_str16'; Path = 'src\display16\intel_str16.c'; CodeSegment = 'I9XXCODE' }
            @{ Name = 'gma950_hw16'; Path = 'src\chipsets\intel\gma950\gma950_hw16.c' }
            @{ Name = 'hsw_hw16'; Path = 'src\chipsets\intel\hsw\hsw_hw16.c' }
            @{ Name = 'hsw_config'; Path = 'src\chipsets\intel\hsw_config.c' }
            @{ Name = 'intel_hw16'; Path = 'src\chipsets\intel\intel_hw16.c' }
            @{ Name = 'intel_bridge16'; Path = 'src\display16\intel_bridge16.c' }
            @{ Name = 'vbe16'; Path = 'src\display16\hw\vbe16.c' }
            @{ Name = 'enable16'; Path = 'src\display16\enable16.c' }
            @{ Name = 'display_component'; Path = 'src\display16\display_component.c' }
            @{ Name = 'loader'; Path = 'src\display16\loader.c' }
            @{ Name = 'ddi'; Path = 'src\display16\ddi.c' }
            @{ Name = 'dd16'; Path = 'src\display16\dd16.c' }
            @{ Name = 'gdi_accel'; Path = 'src\display16\gdi_accel.c' }
        )
        Defines = @('V9X_INTEL_GMA_FAMILY', 'V9X_I9XX_FIRST_WRITE_EXECUTOR',
                    # Phase 5's sequencer and its submit path. Both are now
                    # family-derived, exactly as Phase 4's executor is,
                    # because the 2026-09-15 errata-gate decision covers a
                    # Phase 5 draw. Nothing here arms anything: a run still
                    # needs IntelArmPhase=5, the combined CRC and the
                    # one-shot token transfer.
                    'V9X_I9XX_PHASE5_EXECUTOR',
                    'V9X_I9XX_PHASE5_SUBMIT')
        RuntimeDefines = @('V9X_INTEL_GMA_FAMILY')
        SkeletonOutput = 'build\win16-ddi-intel-gma'
        PackageOutput = 'build\win98se-intel-gma'
        VmStageDirectory = 'build\vm-probe\INTELGMA'
    }

    Audit = @{
        RequiredInstructions = @()
        ForbiddenInstructions = @()
        RequiredMapSymbols = @()
        DispatchSymbol = 'v9x_hw16'
        BackendSymbols = @('v9x_gma950_device')
    }

    Inf = @{
        Provider = 'Velocity9x Project'
        Manufacturer = 'Velocity9x'
        DiskName = 'Velocity9x Intel GMA driver-stage disk'
        ModelsSection = 'Velocity9x.Models'
        DefaultMode = '8,640,480'
        ForcedModes = @('8,640,480', '8,1024,576',
                        '16,640,480', '16,1024,576')
    }

    Package = @{
        ModesSummary = '640x480 and native 1024x576 at 8/16 bpp and 60 Hz'
        HalDescription = 'V9XHAL.DLL (vidmem + flips, engine blits, Gen3 and Haswell Direct3D)'
    }

    Floppy = @{
        # The four established packages already fill the 1.44 MB aggregate
        # transfer image. Intel is built and staged as its own physical-only
        # package; adding it here would make every run-checks fail on capacity.
        Include = $false
        Folder = 'INTELGMA'
        Order = 4
        HardwareIdHint = 'PCI 8086:27AE, 8086:2592, 8086:A011, 8086:2A02, 8086:0042, 8086:0412 or 8086:0162 exactly'
    }

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
