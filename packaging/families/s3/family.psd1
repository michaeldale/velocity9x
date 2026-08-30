# Velocity9x family manifest: S3.
#
# Data only. Loaded with Import-PowerShellDataFile, which is built into
# PowerShell 5.1 and evaluates no code, so the regex audit patterns below need
# no JSON escaping. See docs\plans\multi-chip-restructure.md and
# docs\specifications\family-manifest.md.
#
# This is the first family with more than one chip, and it is what the whole
# restructure was for: one binary, one INF with two models, runtime PCI
# dispatch between them. It replaces the single-chip s3-virge and s3-trio64
# manifests, and with them the byte-for-byte golden compare - two chips in one
# image cannot reproduce either one-chip image.
@{
    SchemaVersion = 1
    Id = 's3'
    DisplayName = 'S3'
    Description = 'S3 ViRGE/DX and Trio32/64, dispatched at runtime by PCI id, plus five bound-but-unvalidated Trio64 aliases.'

    Chips = @(
        @{
            Id = 'virge-dx'
            Name = 'S3 ViRGE/DX 86C375'
            VendorId = '5333'
            DeviceId = '8A01'
            DeviceDesc = 'Velocity9x S3 ViRGE/DX 86C375'
            # Written to C:\V9XDIAG\V9XHW.INI by ddi.c's diagnostics publisher, for
            # whichever chip the PCI scan matched.
            Adapter = 'S3 ViRGE/DX 86C375'
            ClockDetector = 's3-virge-pll-v1'
            ModeSwitching = 'live-any-depth'
            Acceleration = 'directdraw-fill-blt'
            Direct3D = 'hardware-s3d'
            EngineType = 'S3_VIRGE_DX'
            EngineCaps = @('SOLID_FILL', 'SCREEN_COPY', 'FLIP', 'VBLANK', 'D3D')
            # The VRAM the mode list below is declared against. The host
            # family-matrix test binds this and asserts every declared mode
            # validates, which is what stops the INF advertising a mode the
            # card cannot hold.
            VideoMemoryBytes = 4194304

            # Per-chip MODES capability. Both chips take the same list; the
            # order is the order the shared mode table uses and the order GDI
            # enumerates the MODES registry key, so 640x400 sits after the
            # other 8-bpp entries (Doom95).
            Modes = @(
                @{ BitsPerPixel = 8; Width = 640; Height = 480; RefreshRate = 60; VbeMode = '0101' }
                @{ BitsPerPixel = 8; Width = 800; Height = 600; RefreshRate = 60; VbeMode = '0103' }
                @{ BitsPerPixel = 8; Width = 1024; Height = 768; RefreshRate = 60; VbeMode = '0105' }
                @{ BitsPerPixel = 8; Width = 1280; Height = 1024; RefreshRate = 60; VbeMode = '0107' }
                @{ BitsPerPixel = 8; Width = 640; Height = 400; RefreshRate = 60; VbeMode = '0100' }
                @{ BitsPerPixel = 16; Width = 640; Height = 480; RefreshRate = 60; VbeMode = '0111' }
                @{ BitsPerPixel = 16; Width = 800; Height = 600; RefreshRate = 60; VbeMode = '0114' }
                @{ BitsPerPixel = 16; Width = 1024; Height = 768; RefreshRate = 60; VbeMode = '0117' }
                @{ BitsPerPixel = 16; Width = 1280; Height = 1024; RefreshRate = 60; VbeMode = '011A' }
                @{ BitsPerPixel = 32; Width = 640; Height = 480; RefreshRate = 60; VbeMode = '0112' }
                @{ BitsPerPixel = 32; Width = 800; Height = 600; RefreshRate = 60; VbeMode = '0115' }
                @{ BitsPerPixel = 32; Width = 1024; Height = 768; RefreshRate = 60; VbeMode = '0118' }
            )

            # The object this chip's code compiles into. Both chips are now in
            # one image, so the image-wide signature audit can no longer tell
            # them apart - this is what does.
            Objects = @('virge_hw16')

            # CR53[3] opens the ViRGE's new-MMIO window. It is the one register
            # sequence the Trio64 must never execute, so it is required in this
            # chip's object and, by sibling derivation, forbidden in the Trio's.
            #
            # The shared S3 unlock (or/cmp al,13H) is NOT listed here: it lives
            # in s3_regs16.obj, which both chips call, so it is a family-wide
            # required instruction below rather than a per-chip one.
            # The \b anchors matter. These patterns become forbidden patterns in
            # every other family's image, so an unanchored 'test\s+al,8' also
            # matches an unrelated 'test al,80H' and convicts a foreign image of
            # running the ViRGE's MMIO sequence. That is not hypothetical: the
            # VBE linear-framebuffer attribute test in src\common\vbe_parse.c
            # compiles to exactly that, in every family.
            Audit = @{
                Required = @(
                    'mov\s+ax,53H'
                    'or\s+al,8\b'
                    'test\s+al,8\b'
                )
                Forbidden = @()
            }
            MapSymbols = @('v9x_virge_device')
        }
        @{
            Id = 'trio64'
            Name = 'S3 Trio32/64 86C764'
            VendorId = '5333'
            DeviceId = '8811'
            DeviceDesc = 'Velocity9x S3 Trio32/64 86C764'
            Adapter = 'S3 Trio32/64 86C764'
            ClockDetector = 's3-virge-pll-v1'
            ModeSwitching = 'live-any-depth'
            Acceleration = 'directdraw-fill-blt'
            # No S3d core. dd16.c nulls lpD3D*/GetDriverInfo from this chip's
            # engine_caps, and the 32-bit D3D module is never reached.
            Direct3D = 'not-advertised'
            EngineType = 'S3_TRIO64'
            EngineCaps = @('SOLID_FILL', 'SCREEN_COPY', 'FLIP', 'VBLANK')
            VideoMemoryBytes = 4194304

            Modes = @(
                @{ BitsPerPixel = 8; Width = 640; Height = 480; RefreshRate = 60; VbeMode = '0101' }
                @{ BitsPerPixel = 8; Width = 800; Height = 600; RefreshRate = 60; VbeMode = '0103' }
                @{ BitsPerPixel = 8; Width = 1024; Height = 768; RefreshRate = 60; VbeMode = '0105' }
                @{ BitsPerPixel = 8; Width = 1280; Height = 1024; RefreshRate = 60; VbeMode = '0107' }
                @{ BitsPerPixel = 8; Width = 640; Height = 400; RefreshRate = 60; VbeMode = '0100' }
                @{ BitsPerPixel = 16; Width = 640; Height = 480; RefreshRate = 60; VbeMode = '0111' }
                @{ BitsPerPixel = 16; Width = 800; Height = 600; RefreshRate = 60; VbeMode = '0114' }
                @{ BitsPerPixel = 16; Width = 1024; Height = 768; RefreshRate = 60; VbeMode = '0117' }
                @{ BitsPerPixel = 16; Width = 1280; Height = 1024; RefreshRate = 60; VbeMode = '011A' }
                @{ BitsPerPixel = 32; Width = 640; Height = 480; RefreshRate = 60; VbeMode = '0112' }
                @{ BitsPerPixel = 32; Width = 800; Height = 600; RefreshRate = 60; VbeMode = '0115' }
                @{ BitsPerPixel = 32; Width = 1024; Height = 768; RefreshRate = 60; VbeMode = '0118' }
            )

            # Further PCI ids this chip's code drives unchanged: same S3
            # unlock, same CR58/CR40 aperture enable, same CR59/CR5A base,
            # same CR36 memory decode, same 8514/A engine driven by port I/O.
            # They share this chip's install section, registry section and
            # mode list, and they are aliases rather than chips because a chip
            # must carry a VM target and be covered by the mode matrix, and
            # none of these has run anywhere.
            #
            # Evidence, from docs\decisions\2026-08-29-s3-device-id-survey.md:
            # 8901 was read out of the PCIR structure of 86Box's 86c775_2.bin
            # Trio64V2/DX ROM. The other four appear in no dump in this tree -
            # their names come from the public PCI id list. Nothing here has
            # seen any of the five silicon.
            #
            # Deliberately absent: the Vision864/868/964/968 (88C0, 88C1,
            # 8880, 88D0, 88F0) and the 86C928 (88B0), all measured and all
            # unclaimed. Those are external-RAMDAC boards whose CR36 encoding
            # differs from the Trio line - src\chipsets\s3\virge\memory.c
            # refuses the codes they use rather than guessing - so binding
            # them would be a claim rather than an alias.
            #
            # 8901 is the one worth promoting to a chip first: 86Box emulates
            # it as trio64v2dx_pci, so it needs a guest and a VBE inventory
            # and nothing else.
            Aliases = @(
                @{ DeviceId = '8810'
                   Name = 'S3 Trio32 86C732'
                   DeviceDesc = 'Velocity9x S3 Trio32 86C732' }
                @{ DeviceId = '8812'
                   Name = 'S3 Aurora64V+ 86C862'
                   DeviceDesc = 'Velocity9x S3 Aurora64V+ 86C862' }
                @{ DeviceId = '8813'
                   Name = 'S3 Trio32/64 86C732/86C764'
                   DeviceDesc = 'Velocity9x S3 Trio32/64 86C732/86C764' }
                @{ DeviceId = '8814'
                   Name = 'S3 Trio64UV+ 86C767'
                   DeviceDesc = 'Velocity9x S3 Trio64UV+ 86C767' }
                @{ DeviceId = '8901'
                   Name = 'S3 Trio64V2/DX or /GX 86C775/86C785'
                   DeviceDesc = 'Velocity9x S3 Trio64V2/DX or /GX 86C775/86C785' }
            )

            Objects = @('trio_hw16')

            # This chip has no register sequence of its own: what makes it the
            # Trio64 is its PCI id and its engine descriptor, both data. The
            # audit that matters here is therefore the derived forbidden set -
            # the ViRGE's CR53 patterns must not appear in this object - plus
            # the map symbol and the INF hardware-ID set equality. Declaring a
            # required instruction it does not actually own would be a check
            # that proves nothing.
            Audit = @{
                Required = @()
                Forbidden = @()
            }
            MapSymbols = @('v9x_trio_device')
        }
    )

    # The host-testable policy backend: the I/O-free half under src\chipsets
    # that the backend registry dispatches to by PCI id. Getter and Header are
    # what scripts\lib\backend-registry.ps1 emits into the generated
    # src\common\backend_registry_table.inc - one row per chip above, all
    # pointing at this family's getter. Sources is what the host test builds
    # compile; the driver build takes its own list from Build.Sources below.
    Backend = @{
        Getter = 'v9x_s3_virge_backend'
        Header = 'velocity9x/s3_virge.h'
        Sources = @(
            'src\chipsets\s3\virge\backend.c',
            'src\chipsets\s3\virge\clocks.c',
            'src\chipsets\s3\virge\memory.c'
        )
    }

    Build = @{
        # Ordered compile list. The object order is also the link order.
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
            @{ Name = 'virge_backend'; Path = 'src\chipsets\s3\virge\backend.c' }
            @{ Name = 'virge_clocks'; Path = 'src\chipsets\s3\virge\clocks.c' }
            @{ Name = 'virge_memory'; Path = 'src\chipsets\s3\virge\memory.c' }
            @{ Name = 's3_regs16'; Path = 'src\chipsets\s3\common\s3_regs16.c' }
            # One object per chip, then the family table that points at both.
            @{ Name = 'virge_hw16'; Path = 'src\chipsets\s3\virge\virge_hw16.c' }
            @{ Name = 'trio_hw16'; Path = 'src\chipsets\s3\trio64\trio_hw16.c' }
            @{ Name = 's3_hw16'; Path = 'src\chipsets\s3\s3_hw16.c' }
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
        RuntimeDefines = @()
        SkeletonOutput = 'build\win16-ddi-s3'
        PackageOutput = 'build\win98se-s3'
        VmStageDirectory = 'build\vm-probe\S3'
        # Both S3 chips read the aperture from hardware, so the driver never
        # consults the mini-VDD's 4F9Ch VBE cache. Boot-time BIOS collection is
        # all risk and no benefit here, and it hung a physical Trio64 (see
        # docs\issues\2026-08-18-trio64-minivdd-boot-hang.md), so this family
        # ships the mini-VDD with the collection assembled out.
        MiniVddVbeCollect = $false
    }

    Audit = @{
        # The shared S3 unlock, in s3_regs16.obj. It belongs to the family
        # rather than to either chip: both call it, and neither chip's object
        # contains it.
        RequiredInstructions = @(
            'or\s+al,13H'
            'cmp\s+al,13H'
        )
        ForbiddenInstructions = @()
        RequiredMapSymbols = @()
        DispatchSymbol = 'v9x_hw16'
        # Symbols another family's binary must not contain.
        BackendSymbols = @('v9x_virge_device', 'v9x_trio_device')
    }

    Inf = @{
        Provider = 'Velocity9x Project'
        Manufacturer = 'Velocity9x'
        DiskName = 'Velocity9x Windows 98SE driver-stage disk'
        ModelsSection = 'Velocity9x.Models'
        DefaultMode = '8,640,480'
        # Indexed by build-active-package.ps1 -ForceModeIndex, which checks the
        # index against this array's own length rather than a literal range.
        ForcedModes = @('8,640,480', '8,800,600', '8,1024,768', '8,1280,1024',
                        '16,640,480', '16,800,600', '16,1024,768',
                        '16,1280,1024',
                        '32,640,480', '32,800,600', '32,1024,768')
        # A second models line with no hardware ID at all, pickable only by hand
        # from Have Disk. It exists for the 486: a Trio64 on VESA Local Bus,
        # root-enumerated by Win95 as *PNP0913, on a machine with no PCI bus for
        # SetupX to match a PCI\VEN_ model against. Deliberately not bound to
        # *PNP0913 either - that ID covers every S3 801/805/928 card DETECTS3801
        # finds, and this driver has code for none of them.
        #
        # identify_without_pci picks the chip at Enable, so the model needs no
        # PCI id; VideoMemoryBytes is what the physical card has, and it is why
        # the derived mode list is a subset. The two rows the chips declare
        # against 4 MiB that a 2 MiB card cannot hold - 16bpp 1280x1024 at
        # 2.5 MiB and 32bpp 1024x768 at 3 MiB, see the Vm.Modes comment below -
        # are pruned here rather than refused at the next boot with only a stage
        # code to show for it.
        ManualSelect = @{
            Description = 'Velocity9x S3 (VLB manual select)'
            VideoMemoryBytes = 2097152
            # No CompatibleId. The emitter supports one - see the manifest spec -
            # and this family briefly declared `*PNP0913` as a diagnostic, on the
            # theory that a model claiming no id could not be re-bound when the
            # Configuration Manager re-enumerates a DetFunc-detected device. It
            # was wrong. Binding it did make Windows filter Have Disk's
            # compatible list down to this model alone, so the id matched, but
            # Problem 24 survived it unchanged. The stock-driver control then
            # showed the two devnodes structurally identical, and the cause was
            # DEFAULT,minivdd below.
            #
            # So the plan's original refusal stands on its own terms: *PNP0913 is
            # what DETECTS3801 hangs on every S3 801/805/928 board, this driver
            # has code for none of them, and nothing here needs the claim.
            # No mini-VDD for this model. Measured on the 486 on 2026-08-22:
            # with DEFAULT,minivdd naming v9xmini.vxd the devnode never reached
            # DN_STARTED on Win95 4.00.950, Device Manager reported Code 24,
            # Display Properties then offered no modes at all, and the desktop
            # stayed on the 4-bpp vga.drv fallback row. Clearing that one value
            # took the devnode to Problem 0 and the driver to enable-ok, with
            # V9XHW.INI naming the Trio64 and reading its 2 MiB correctly.
            #
            # Scoped to this model rather than the family: the PCI models are
            # validated on Win98SE *with* the mini-VDD, so they keep it via the
            # per-chip registry sections. Both S3 chips read the aperture
            # directly and this family already builds the mini-VDD with
            # MiniVddVbeCollect = $false, so what is lost here is its DPMS and
            # mode-save callbacks, not the framebuffer.
            #
            # Why it fails to load on Win95 is still unknown - a logged
            # BOOTLOG.TXT would say - so this is the measured configuration
            # rather than a diagnosis.
            MiniVdd = $false
        }
    }

    Package = @{
        ModesSummary = '640x480, 800x600 and 1024x768 at 8/16/32 bpp, 1280x1024 at 8/16 bpp, 640x400 at 8 bpp, 60 Hz'
        HalDescription = 'V9XHAL.DLL (vidmem + flip + per-chip engine blit)'
    }

    Floppy = @{
        Include = $true
        Folder = 'S3'
        Order = 1
        # Hand-written display string, printed into the floppy README's chip
        # table at column 35 - keep it inside 38 characters (see the 73-column
        # wrap at build-floppy-package.ps1:75).
        HardwareIdHint = 'PCI 5333 ViRGE/DX or Trio, VLB by hand'
    }

    Vm = @{
        Emulator = '86box'
        Controller = 'virge_dx_pci'
        Bios = 'virge375_pci'
        # The primary target, used when no -ChipId is given.
        Profile = 'Win86SE'
        Port = 9869
        ReferenceProfile = 'Win98SE-Native-S3'
        ReferencePort = 9870
        # The mode matrix runs these on both 86Box targets, which are 4 MiB and
        # can hold every one. The 2 MiB physical Trio64 is not driven by this
        # list.
        #
        # The two oversized rows ARE refused by ValidateMode on the 2 MiB card.
        # Measured on BARRY 2026-08-26; see
        # docs\decisions\2026-08-26-s3-physical-pipeline-inert.md section 7.
        #
        # This comment used to claim the opposite - that ValidateMode's memory
        # test is inert here because enable16.c assigns v9x_vbe_vram_reported
        # only on the tier-0 VBE path. It does not: enable16.c:719-731 assigns
        # it from the family's read_video_memory hook (the S3 CR36 decode)
        # whenever that hook exists, which is exactly this family. On BARRY the
        # figure is the CR36-decoded 2 MiB, and a live ChangeDisplaySettings to
        # 1024x768x32 or 1280x1024x16 returns DISP_CHANGE_BADMODE while
        # 1024x768x16 succeeds.
        #
        # Two traps that made the wrong claim look right, both worth keeping:
        #
        # - The inventory's "Vram=reported=" line is NOT this variable. That is
        #   v9x_runtime_vram_reported, the mini-VDD 4F00h figure, and it is
        #   correctly zero on a scan-disabled family. The ValidateMode variable
        #   is v9x_vbe_vram_reported, which s3's publish_diagnostics does not
        #   report; the inventory's "usable=" figure is what evidences it,
        #   since both are assigned in the same branch.
        # - Rebooting into an oversized mode does not test ValidateMode at all.
        #   GDI calls ValidateMode on a mode CHANGE; at boot it calls Enable
        #   directly, so an oversized row fails at 4F02h with
        #   Stage=fail-hardware-vbe-mode instead. Use a live switch.
        #
        # Inf.ManualSelect's pruned list is also not what keeps these rows off
        # a 2 MiB card: it applies only to the VLB manual-select install, and
        # BARRY's PCI\VEN_5333&DEV_8811 takes Velocity9x.Registry.trio64, which
        # publishes all twelve rows. On the PnP path the runtime refusal is the
        # only guard.
        Modes = @('640x480x8', '800x600x8', '1024x768x8', '1280x1024x8',
                  '640x480x16', '800x600x16', '1024x768x16', '1280x1024x16',
                  '640x480x32', '800x600x32', '1024x768x32')
        # One entry per chip. The phase 8 gate is the mode matrix passing on
        # both of these from the one binary, which is the whole claim of the
        # merge; a single-profile pass would prove only that one of the two
        # chips still works.
        Targets = @(
            @{
                ChipId = 'virge-dx'
                Profile = 'Win86SE'
                Port = 9869
                Controller = 'virge_dx_pci'
            }
            @{
                ChipId = 'trio64'
                Profile = 'Win98SE-Trio64'
                Port = 9871
                Controller = 's3_trio64_pci'
            }
        )
    }
}

