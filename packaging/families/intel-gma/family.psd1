@{
    SchemaVersion = 1
    Id = 'intel-gma'
    DisplayName = 'Intel GMA (Gen3)'
    Description = 'Intel GMA 950 on 945GSE: VBE display with guarded Phase 4 ring experiment; no acceleration advertised.'

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
            Acceleration = 'none'
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
        HalDescription = 'V9XHAL.DLL (vidmem + flip, CPU blits only)'
    }

    Floppy = @{
        # The four established packages already fill the 1.44 MB aggregate
        # transfer image. Intel is built and staged as its own physical-only
        # package; adding it here would make every run-checks fail on capacity.
        Include = $false
        Folder = 'INTELGMA'
        Order = 4
        HardwareIdHint = 'PCI 8086:27AE exactly'
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
