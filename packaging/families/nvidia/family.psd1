# Velocity9x family manifest: NVIDIA.
#
# The RIVA 128 and 128ZX: the VBE 4F02h mode set programs the card and 4F01h
# reports where the framebuffer landed. DirectDraw fill and copy run on the
# 2D engine (NV3) at 8 and 32 bpp, after Linux rivafb's bring-up, which the
# HAL repeats after every mode set; GDI fill and copy run on it too, with
# the same bring-up on the 16-bit side; everything else is drawn by the CPU
# (docs\plans\nvidia-riva128.md, Phases 4 and 6). Direct3D draws on the D3D
# object at 16 bpp, its only depth (docs\plans\nvidia-riva128-d3d.md).
#
# Physical-only: the local 86Box build has no NV3 model.
@{
    SchemaVersion = 1
    Id = 'nvidia'
    DisplayName = 'NVIDIA RIVA 128, GeForce 210 and GeForce 9500 GT'
    Description = 'NVIDIA RIVA 128 and 128ZX (PCI 12D2:0018): VBE mode set, the 2D engine for DirectDraw and GDI fill and copy at 8 and 32 bpp, Direct3D on the 3D engine at 16 bpp, CPU drawing otherwise. GeForce 210 (PCI 10DE:0A65): VBE mode set, the 2D engine for DirectDraw and GDI fill and copy at 8, 16 and 32 bpp, Direct3D on the 3D engine at 16 and 32 bpp with Z16, CPU drawing otherwise. GeForce 9500 GT (PCI 10DE:0640): VBE mode set, the 2D engine for DirectDraw and GDI fill and copy, page flips, Direct3D on the 3D engine at 16 and 32 bpp with the Z kept at 24 bits, CPU drawing otherwise.'

    Chips = @(
        @{
            Id = 'riva128'
            Name = 'NVIDIA RIVA 128'
            VendorId = '12D2'
            DeviceId = '0018'
            # No SubsystemId, deliberately: the measured card is an STB
            # board (10B4:222A), and the RIVA 128 shipped on many boards
            # with their own subsystems; the bare id binds any of them.
            DeviceDesc = 'Velocity9x NVIDIA RIVA 128'
            Adapter = 'NVIDIA RIVA 128'
            ClockDetector = 'nvidia-riva128-unavailable-v1'
            ModeSwitching = 'vbe-lfb'
            # The 2D engine through BAR0's USER area, measured right on the
            # card at 8, 16 and 32 bpp (docs\decisions\2026-11-02-riva128-2d-engine-writes.md);
            # the HAL uses it at 8 and 32 only, because at 16 bpp it sets
            # bit 15 and this BIOS's 16 bpp modes are 5:6:5.
            Acceleration = 'directdraw-fill-copy'
            # The D3D object (class 17h) at 16 bpp, its only depth
            # (docs\plans\nvidia-riva128-d3d.md, step 5).
            Direct3D = 'hardware-nv3'
            EngineType = 'NV3'
            EngineCaps = @('SOLID_FILL', 'SCREEN_COPY', 'D3D')
            # The measured 128ZX reports 8 MiB through 4F00h, the chip's
            # maximum. Every advertised mode lays out in 4 MiB, so the
            # original RIVA 128's 4 MiB holds them too.
            VideoMemoryBytes = 8388608

            # Every VbeMode is in the measured BIOS's own mode list; see the
            # comment over v9x_nvidia_modes in nvidia_hw16.c.
            Modes = @(
                @{ BitsPerPixel = 8; Width = 640; Height = 480; RefreshRate = 60; VbeMode = '0101' }
                @{ BitsPerPixel = 8; Width = 800; Height = 600; RefreshRate = 60; VbeMode = '0103' }
                @{ BitsPerPixel = 8; Width = 1024; Height = 768; RefreshRate = 60; VbeMode = '0105' }
                @{ BitsPerPixel = 8; Width = 640; Height = 400; RefreshRate = 60; VbeMode = '0100' }
                @{ BitsPerPixel = 16; Width = 640; Height = 480; RefreshRate = 60; VbeMode = '0111' }
                @{ BitsPerPixel = 16; Width = 800; Height = 600; RefreshRate = 60; VbeMode = '0114' }
                @{ BitsPerPixel = 16; Width = 1024; Height = 768; RefreshRate = 60; VbeMode = '0117' }
            )

            Objects = @('riva128_hw16')

            # No required instructions, for the reason the ati manifest gives:
            # at tier-0 this chip owns no register sequence, and identity is
            # carried by MapSymbols, Audit.DispatchSymbol and the INF's
            # hardware-id set.
            Audit = @{
                Required = @()
                Forbidden = @()
            }
            MapSymbols = @('v9x_riva128_device')
        }
        @{
            Id = 'gt218'
            Name = 'NVIDIA GeForce 210'
            VendorId = '10DE'
            DeviceId = '0A65'
            # No SubsystemId: the seen card is a Gainward board (10B0:1401)
            # and the GeForce 210 shipped on many boards.
            DeviceDesc = 'Velocity9x NVIDIA GeForce 210'
            Adapter = 'NVIDIA GeForce 210'
            ClockDetector = 'nvidia-gt218-unavailable-v1'
            ModeSwitching = 'vbe-lfb'
            # The 2D engine, class 502D on one PFIFO channel, measured exact
            # on the card at 8, 16 and 32 bpp by the probe
            # (docs\decisions\2026-11-14-gt218-502d-engine-measurements.md).
            # The HAL engine ran on the card in step 4.4
            # (docs\decisions\2026-11-14-gt218-hal-engine-first-run.md).
            # GDI fill and copy share the HAL's channel
            # (docs\decisions\2026-11-14-gt218-gdi-engine-first-run.md).
            # Direct3D: the Tesla 3D engine on that channel, through tiled
            # shadow targets with Z16
            # (docs\decisions\2026-11-14-gt218-3d-d3d-builders.md).
            Acceleration = 'directdraw-fill-copy'
            Direct3D = 'hardware-nv50'
            EngineType = 'NV50'
            EngineCaps = @('SOLID_FILL', 'SCREEN_COPY', 'D3D')
            # What the seen card's VBE reports through 4F00h (224 x 64 KiB),
            # not the card's memory
            # (docs\decisions\2026-11-11-m-bxmfa8-gt218-first-look.md).
            VideoMemoryBytes = 14680064

            # The family's rows; every mode number is in this BIOS's list
            # too, linear, 16 bpp 5:6:5 (boot 81 V9XBOOT.INI).
            Modes = @(
                @{ BitsPerPixel = 8; Width = 640; Height = 480; RefreshRate = 60; VbeMode = '0101' }
                @{ BitsPerPixel = 8; Width = 800; Height = 600; RefreshRate = 60; VbeMode = '0103' }
                @{ BitsPerPixel = 8; Width = 1024; Height = 768; RefreshRate = 60; VbeMode = '0105' }
                @{ BitsPerPixel = 8; Width = 640; Height = 400; RefreshRate = 60; VbeMode = '0100' }
                @{ BitsPerPixel = 16; Width = 640; Height = 480; RefreshRate = 60; VbeMode = '0111' }
                @{ BitsPerPixel = 16; Width = 800; Height = 600; RefreshRate = 60; VbeMode = '0114' }
                @{ BitsPerPixel = 16; Width = 1024; Height = 768; RefreshRate = 60; VbeMode = '0117' }
            )

            Objects = @('gt218_hw16', 'gt218_reserve')

            # No register sequence at tier-0, as for the RIVA 128 row.
            Audit = @{
                Required = @()
                Forbidden = @()
            }
            MapSymbols = @('v9x_gt218_device')
        }
        @{
            Id = 'g96'
            Name = 'NVIDIA GeForce 9500 GT'
            VendorId = '10DE'
            DeviceId = '0640'
            # No SubsystemId: the seen card reports 0000:0000.
            DeviceDesc = 'Velocity9x NVIDIA GeForce 9500 GT'
            Adapter = 'NVIDIA GeForce 9500 GT'
            ClockDetector = 'nvidia-g96-unavailable-v1'
            ModeSwitching = 'vbe-lfb'
            # The GT218's channel and 2D engine, on the G96's chipset
            # (docs\plans\nvidia-g96-geforce9500gt.md, Phase 3; probe in
            # docs\decisions\2026-11-28-g96-register-probe.md). Direct3D
            # on NV84_3D (8297h) with the Z as Z24S8, since that class has
            # no Z16 (docs\decisions\2026-11-28-g96-direct3d.md).
            Acceleration = 'directdraw-fill-copy'
            Direct3D = 'hardware-nv50'
            EngineType = 'NV50'
            EngineCaps = @('SOLID_FILL', 'SCREEN_COPY', 'FLIP', 'D3D')
            # What the seen card's VBE reports through 4F00h, as on the
            # GT218 (issue 30's vbe V9XBOOT.TXT).
            VideoMemoryBytes = 14680064

            # The family's rows; every mode number is in this BIOS's list
            # (issue 30's vbe V9XBOOT.TXT), 16 bpp 5:6:5.
            Modes = @(
                @{ BitsPerPixel = 8; Width = 640; Height = 480; RefreshRate = 60; VbeMode = '0101' }
                @{ BitsPerPixel = 8; Width = 800; Height = 600; RefreshRate = 60; VbeMode = '0103' }
                @{ BitsPerPixel = 8; Width = 1024; Height = 768; RefreshRate = 60; VbeMode = '0105' }
                @{ BitsPerPixel = 8; Width = 640; Height = 400; RefreshRate = 60; VbeMode = '0100' }
                @{ BitsPerPixel = 16; Width = 640; Height = 480; RefreshRate = 60; VbeMode = '0111' }
                @{ BitsPerPixel = 16; Width = 800; Height = 600; RefreshRate = 60; VbeMode = '0114' }
                @{ BitsPerPixel = 16; Width = 1024; Height = 768; RefreshRate = 60; VbeMode = '0117' }
            )

            Objects = @('gt218_hw16', 'gt218_reserve')

            Audit = @{
                Required = @()
                Forbidden = @()
            }
            MapSymbols = @('v9x_g96_device')
        }
    )

    # The host-testable policy backend; see the s3 manifest for the shape.
    Backend = @{
        Getter = 'v9x_nvidia_backend'
        Header = 'velocity9x/nvidia.h'
        Sources = @(
            'src\chipsets\nvidia\nvidia_backend.c'
        )
    }

    Build = @{
        # No read_aperture hook, so the mini-VDD's 4F9Ch cache is the only way
        # this family learns where its framebuffer is; the collection stays on
        # (see the ati manifest for what happened when it was off).
        MiniVddVbeCollect = $true
        # Compile order is link order. Reordering changes the image.
        Sources = @(
            @{ Name = 'build'; Path = 'src\common\build.c' }
            @{ Name = 'log'; Path = 'src\common\log.c' }
            @{ Name = 'mode'; Path = 'src\common\mode.c' }
            @{ Name = 'resources'; Path = 'src\common\resources.c' }
            # In LEAFCODE since the one-display guard in ddi.c took _TEXT
            # 203 bytes past its budget (leaf16.h).
            @{ Name = 'vbe_parse'; Path = 'src\common\vbe_parse.c'; CodeSegment = 'LEAFCODE' }
            @{ Name = 'vbe_modes'; Path = 'src\common\vbe_modes.c' }
            @{ Name = 'edid'; Path = 'src\common\edid.c' }
            @{ Name = 'mtrr'; Path = 'src\common\mtrr.c' }
            @{ Name = 'd3dmode'; Path = 'src\common\d3dmode.c' }
            @{ Name = 'vsync'; Path = 'src\common\vsync.c' }
            @{ Name = 'modes16'; Path = 'src\display16\modes16.c' }
            @{ Name = 'riva128_hw16'; Path = 'src\chipsets\nvidia\riva128\riva128_hw16.c' }
            @{ Name = 'gt218_hw16'; Path = 'src\chipsets\nvidia\gt218\gt218_hw16.c' }
            @{ Name = 'gt218_reserve'; Path = 'src\chipsets\nvidia\gt218\gt218_reserve.c' }
            @{ Name = 'nvidia_hw16'; Path = 'src\chipsets\nvidia\nvidia_hw16.c' }
            @{ Name = 'vbe16'; Path = 'src\display16\hw\vbe16.c' }
            @{ Name = 'enable16'; Path = 'src\display16\enable16.c' }
            @{ Name = 'display_component'; Path = 'src\display16\display_component.c' }
            @{ Name = 'loader'; Path = 'src\display16\loader.c' }
            @{ Name = 'palette16'; Path = 'src\display16\palette16.c' }
            @{ Name = 'ddi'; Path = 'src\display16\ddi.c' }
            @{ Name = 'sswitch16'; Path = 'src\display16\sswitch16.c'; CodeSegment = 'SSWCODE' }
            @{ Name = 'dd16'; Path = 'src\display16\dd16.c' }
            # The HAL's 2D builder, for gdi_accel.c's NV3 arm.
            @{ Name = 'nv3_2d'; Path = 'src\chipsets\nvidia\nv3_2d.c' }
            # The HAL's NV50 push builders, for gdi_accel.c's GT218 arm, in a
            # segment of their own to keep _TEXT under its budget; every
            # call in is far (velocity9x\nv50_chan.h, V9X_NV50_FAR).
            @{ Name = 'nv50_2d'; Path = 'src\chipsets\nvidia\gt218\nv50_2d.c'; CodeSegment = 'NV50CODE' }
            # Ordinal 1, last so it links after the runtime symbols it calls.
            @{ Name = 'gdi_accel'; Path = 'src\display16\gdi_accel.c' }
        )
        # gdi_accel.c's NV3 fill and copy, compiled into this family only;
        # enable16.c's BAR aperture, which the GT218 row asks for
        # (hw16.h aperture_from_bar).
        Defines = @('V9X_NVIDIA_FAMILY', 'V9X_HW16_APERTURE_FROM_BAR')
        # runtime.asm's BAR0 read and mini-VDD map for the 2D engine.
        RuntimeDefines = @('V9X_NVIDIA_FAMILY')
        SkeletonOutput = 'build\win16-ddi-nvidia'
        PackageOutput = 'build\win98se-nvidia'
        VmStageDirectory = 'build\vm-probe\NVIDIA'
    }

    Audit = @{
        RequiredInstructions = @()
        ForbiddenInstructions = @()
        RequiredMapSymbols = @()
        DispatchSymbol = 'v9x_hw16'
        BackendSymbols = @('v9x_riva128_device', 'v9x_gt218_device',
                           'v9x_g96_device')
    }

    Inf = @{
        Provider = 'Velocity9x Project'
        Manufacturer = 'Velocity9x'
        DiskName = 'Velocity9x Windows 98SE driver-stage disk'
        ModelsSection = 'Velocity9x.Models'
        DefaultMode = '8,640,480'
        ForcedModes = @('8,640,480', '8,800,600', '8,1024,768',
                        '16,640,480', '16,800,600', '16,1024,768')
        # The GT218's GDI copies wait for the HAL's NV50 channel; a Run entry
        # brings it up at logon (scripts\lib\inf.ps1, V9xEngineStart). The
        # RIVA 128 row is unaffected: the entry point skips any other engine.
        StartEngineAtLogon = $true
    }

    Package = @{
        ModesSummary = '640x480, 800x600, 1024x768 at 8/16 bpp and 60 Hz'
        HalDescription = 'V9XHAL.DLL (vidmem + flip, engine fill and copy at 8 and 32 bpp, Direct3D at 16 bpp)'
        # The package's Status line (MANIFEST.TXT, the release README's
        # Tested column): what the latest release check measured, and its
        # record. Changed only together with a new record.
        TestedStatus = 'Supported: GeForce 210 (GT218), release probes passing (2026-11-25, 0.61.0 tree). Experimental: RIVA 128ZX, with its known V9XD32 and Glide BlendHalf failures (2026-11-21); GeForce 9500 GT (G96), DirectDraw, GDI, flips, Direct3D and OpenGL (V9XGLP, Quake 3 demo), V9XD32 failing only its 16-bit depth-precision check (the Z is 24-bit) (2026-11-28)'
        CheckRecord = @(
            'docs/decisions/2026-11-21-1.0-baseline-checks.md'
            'docs/decisions/2026-11-25-gt218-cube-levels-dxt-pool-counters.md'
            'docs/decisions/2026-11-28-g96-2d-engine-first-run.md'
            'docs/decisions/2026-11-28-g96-flips-from-the-vbios.md'
            'docs/decisions/2026-11-28-g96-direct3d.md'
            'docs/decisions/2026-11-28-g96-dxt-decode.md'
        )
    }

    Floppy = @{
        Include = $true
        Folder = 'NVIDIA'
        Order = 8
        HardwareIdHint = 'PCI 12D2:0018, 10DE:0A65, 10DE:0640'
        # Installed files that ride a third disk, as every other family's do.
        ThirdDisk = @('V9XUPD.EXE', 'V9XTRACE.EXE', 'GLIDE3X.DLL',
                      'ATI3DCIF.DLL')
    }

    # No emulator models this chip, so the VM runner must refuse rather than
    # test something else.
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
