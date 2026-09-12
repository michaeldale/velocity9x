@{
    SchemaVersion = 1
    Id = 'intel-gma'
    DisplayName = 'Intel GMA (Gen3)'
    Description = 'Intel GMA 950 on 945GSE: VBE display with read-only Gen3 engine fingerprinting; no acceleration advertised.'

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
            Direct3D = 'not-advertised'
            EngineType = 'NONE'
            EngineCaps = @()
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
            @{ Name = 'i9xx_mmio'; Path = 'src\chipsets\intel\i9xx_mmio.c' }
            @{ Name = 'modes16'; Path = 'src\display16\modes16.c' }
            @{ Name = 'intel_diag16'; Path = 'src\display16\intel_diag16.c' }
            @{ Name = 'gma950_hw16'; Path = 'src\chipsets\intel\gma950\gma950_hw16.c' }
            @{ Name = 'intel_hw16'; Path = 'src\chipsets\intel\intel_hw16.c' }
            @{ Name = 'vbe16'; Path = 'src\display16\hw\vbe16.c' }
            @{ Name = 'enable16'; Path = 'src\display16\enable16.c' }
            @{ Name = 'display_component'; Path = 'src\display16\display_component.c' }
            @{ Name = 'loader'; Path = 'src\display16\loader.c' }
            @{ Name = 'ddi'; Path = 'src\display16\ddi.c' }
            @{ Name = 'dd16'; Path = 'src\display16\dd16.c' }
            @{ Name = 'gdi_accel'; Path = 'src\display16\gdi_accel.c' }
        )
        Defines = @()
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
