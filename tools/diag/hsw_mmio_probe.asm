; Haswell (Gen7.5) read-only MMIO and GTT probe VxD.
;
; Phase 2 of docs\plans\intel-haswell-bringup.md. Loaded dynamically by
; V9XHSWMM.EXE, which passes the GTTMMADR base it read from config space and
; a list of register offsets. This maps the 2 MB MMIO half of GTTMMADR and
; the 2 MB GTT half above it (datasheet 328898 3.5.10: "GTTADR will begin at
; (GTTMMADR + 2 MB)"), reads every listed offset twice, copies the first
; HSW_GTT_COPY GTT entries, and counts the valid entries in the whole table.
;
; Writes: none to the device unless V9XHSWMM.EXE passes HSW_FLAG_FORCEWAKE.
; 0CF8h is saved and restored around the one configuration read (the IGD's
; ID, to confirm what the base belongs to). Mapping a physical range with
; _MapPhysToLinear changes no device state.
;
; HSW_FLAG_FORCEWAKE is Phase A of docs\plans\intel-haswell-direct3d.md,
; the first write to this GPU, approved 2026-10-17. With interrupts off:
; read the forcewake acknowledge (130044h); if bit 0 is already set someone
; else holds forcewake and nothing is written; otherwise write 00010001h to
; MULTIFORCEWAKE (A188h, a masked write setting bit 0), poll the acknowledge
; for bit 0 a bounded number of times, read the offset list (the second
; snapshot is taken while awake), write 00010000h to release - always, acked
; or not - and poll for bit 0 clear. Two writes, both to A188h, the second
; undoing the first. The register and the masked handshake are i915's
; (FORCEWAKE_MT, FORCEWAKE_ACK_HSW); the public PRM names A188h and
; documents 130044h 15:0 as the multiple-forcewake status.
;
; Offsets are accepted only inside HSW_WINDOWS below. Each is a register
; block this probe has a reason to read and that sits outside the display
; power well the PRM says may be off (Vol 11a): the render and blitter
; command streamers, GT power management, the always-on display blocks
; (VGA control, DE interrupts, power well control, pipe A, plane A), the
; PCH transcoder A and DAC, and the forcewake acknowledge at 130044h.
; Pipes B and C, DDI and panel fitter registers are refused. The Gen3 lesson
; is the reason for the allowlist: reading a disabled pipe's registers
; hard-locked the 945GSE (docs\decisions\2026-10-15-a-disabled-pipes-
; counters-hang-the-945.md), and nothing here is allowed to find that out
; again by accident.

.386p

.xlist
include VMM.INC
include VWIN32.INC
.list

Declare_Virtual_Device HSWMM, 1, 0, Hsw_Control, \
                       Undefined_Device_ID, Undefined_Init_Order, , ,

HSW_MAGIC               equ 4d4d5348h ; "HSMM"
HSW_DIOC_CAPTURE        equ 1
HSW_IGD_ADDRESS         equ 80001000h ; bus 0, device 2, function 0
HSW_VENDOR_INTEL        equ 8086h
HSW_MMIO_BYTES          equ 00200000h
HSW_GTT_OFFSET          equ 00200000h
HSW_GTT_BYTES           equ 00200000h
HSW_GTT_ENTRIES         equ (HSW_GTT_BYTES / 4)
HSW_GTT_COPY            equ 4096
HSW_OFFSET_MAX          equ 256
HSW_BASE_ALIGN          equ 003fffffh  ; 4 MB, the BAR's own alignment

; Status bits returned to the publisher.
HSW_ST_IGD_INTEL        equ 00000001h
HSW_ST_BASE_INVALID     equ 00000002h
HSW_ST_MMIO_MAPPED      equ 00000004h
HSW_ST_GTT_MAPPED       equ 00000008h
HSW_ST_OFFSETS_REFUSED  equ 00000010h
HSW_ST_SNAPSHOT_TAKEN   equ 00000020h
HSW_ST_GTT_READ         equ 00000040h
HSW_ST_FW_ALREADY_HELD  equ 00000100h
HSW_ST_FW_REQUESTED     equ 00000200h
HSW_ST_FW_ACKED         equ 00000400h
HSW_ST_FW_ACK_TIMEOUT   equ 00000800h
HSW_ST_FW_RELEASED      equ 00001000h
HSW_ST_FW_RELEASE_TIMEOUT equ 00002000h

; Forcewake, Phase A (see the header). Polls are MMIO reads of the
; acknowledge, so the bound is in reads, not time.
HSW_FW_REQUEST          equ 0a188h
HSW_FW_ACK              equ 130044h
HSW_FW_SET              equ 00010001h ; mask bit 16, value bit 0
HSW_FW_CLEAR            equ 00010000h ; mask bit 16, value 0
HSW_FW_ACK_BIT          equ 00000001h
HSW_FW_POLLS            equ 100000

; Input from V9XHSWMM.EXE.
HSW_IN_BASE             equ 0
HSW_IN_FLAGS            equ 4
HSW_IN_COUNT            equ 8
HSW_IN_OFFSETS          equ 12
HSW_IN_MIN_BYTES        equ 12
HSW_FLAG_FORCEWAKE      equ 00000001h

; Output layout, in dwords. Must match struct hsw_result in
; hsw_mmio_probe_win32.c.
HSW_HEADER_DWORDS       equ 16
HSW_RESULT_DWORDS       equ (HSW_HEADER_DWORDS + HSW_OFFSET_MAX * 3 + \
                             HSW_GTT_COPY)

VxD_LOCKED_DATA_SEG
; Accepted windows: start, limit (exclusive). Offsets within 2 MB of MMIO.
HswWindows label dword
    dd 002000h, 003000h   ; render command streamer
    dd 004000h, 005000h   ; GT fault and arbiter registers
    dd 007000h, 008000h   ; GT_MODE, cache modes
    dd 00a000h, 00b000h   ; GT power management, forcewake request
    dd 022000h, 023000h   ; blitter command streamer
    dd 041000h, 041004h   ; VGA_CONTROL
    dd 044000h, 046000h   ; DE interrupts, DE_RRMR, PWR_WELL_CTL1-4
    dd 060000h, 061000h   ; pipe A timings
    ; From 70008h, not 70000h: PIPE_SCANLINE_A and its compare register are
    ; the live counters, the class of register the 945 hung on when its
    ; pipe was off. Nothing here needs them.
    dd 070008h, 071000h   ; pipe A configuration, plane A
    dd 0e0000h, 0e2000h   ; PCH transcoder A, DAC_CTL
    dd 130000h, 131000h   ; LCPLL_CTL, forcewake acknowledge
HSW_WINDOW_COUNT        equ 11

HswResult label dword
    dd HSW_MAGIC                ; 00 magic
    dd 0                        ; 01 status
    dd 0                        ; 02 IGD vendor/device as read
    dd 0                        ; 03 GTTMMADR base used
    dd 0                        ; 04 offsets accepted
    dd 0                        ; 05 valid GTT entries, whole table
    dd 0                        ; 06 0CF8h found and restored
    dd 0                        ; 07 first offset refused, if any
    dd 0                        ; 08 flags received
    dd 0                        ; 09 forcewake acknowledge before request
    dd 0                        ; 10 polls until acknowledged
    dd 0                        ; 11 acknowledge when acknowledged
    dd 0                        ; 12 MULTIFORCEWAKE read while awake
    dd 0                        ; 13 polls until released
    dd 0                        ; 14 acknowledge after release
    dd 0                        ; 15 reserved
HswOffsets   dd HSW_OFFSET_MAX dup (0)
HswFirst     dd HSW_OFFSET_MAX dup (0)
HswSecond    dd HSW_OFFSET_MAX dup (0)
HswGtt       dd HSW_GTT_COPY dup (0)
HswBase      dd 0
HswFlags     dd 0
HswMmio      dd 0
HswGttLinear dd 0

; Phase B1: the BCS ring (docs\plans\intel-haswell-direct3d.md, approved
; 2026-10-17). DIOC code 2, separate from the snapshot so the read-only
; and forcewake paths are unchanged.
HSW_DIOC_BCS            equ 2
HSW_BCS_MAGIC           equ 43425348h ; "HSBC"
HSW_BCS_IN_BASE         equ 0         ; GTTMMADR
HSW_BCS_IN_APERTURE     equ 4         ; GMADR
HSW_BCS_IN_BDSM         equ 8
HSW_BCS_IN_OFFSET       equ 12        ; aperture/graphics offset of the ring
HSW_BCS_IN_N1           equ 16
HSW_BCS_IN_N2           equ 20
HSW_BCS_IN_CMD1         equ 24
HSW_BCS_IN_CMD2         equ 56
HSW_BCS_IN_BYTES        equ 120
HSW_BCS_CMD1_MAX        equ 8
HSW_BCS_CMD2_MAX        equ 16
HSW_BCS_PAGES           equ 18        ; ring 1 + status 1 + scratch 16
HSW_BCS_MAP_BYTES       equ 12000h
HSW_BCS_STATUS_OFF      equ 1000h
HSW_BCS_SCRATCH_OFF     equ 2000h
HSW_BCS_SCRATCH_DWORDS  equ 4000h
HSW_BCS_GUARD           equ 0a5a5a5a5h
HSW_BCS_CPU_PATTERN     equ 5a5aa5a5h
HSW_BCS_STORE1          equ 0c0de0001h
HSW_BCS_STORE2          equ 0c0de0002h
HSW_BCS_POLLS           equ 1000000
HSW_BCS_STOLEN_MASK     equ 0fff00000h
; PTE bits compared: address 31:12, address 38:32 (10:4) and valid (0);
; the cacheability bits 11 and 3:1 may be anything.
HSW_BCS_PTE_MASK        equ 0fffff7f1h

; BCS ring registers, HSW Vol 2c; ACTHD and EIR offsets are i915's.
HSW_BCS_TAIL            equ 22030h
HSW_BCS_HEAD            equ 22034h
HSW_BCS_START           equ 22038h
HSW_BCS_CTL             equ 2203ch
HSW_BCS_ACTHD           equ 22074h
HSW_BCS_EIR             equ 220b0h
HSW_RING_HEAD_MASK      equ 001ffffch ; Head Offset 20:2
HSW_RING_CTL_ONE_PAGE   equ 00000001h ; Buffer Length 0 (4 KB), enable
HSW_RING_CTL_CHECK      equ 001ff001h ; length and enable

; B1 status bits.
HSW_BCS_ST_PTES_OK      equ 00000001h
HSW_BCS_ST_CPU_OK       equ 00000002h
HSW_BCS_ST_FW_ACKED     equ 00000004h
HSW_BCS_ST_RING_PROGRAMMED equ 00000008h
HSW_BCS_ST_STORE1       equ 00000010h
HSW_BCS_ST_STORE2       equ 00000020h
HSW_BCS_ST_TORN_DOWN    equ 00000040h
HSW_BCS_ST_FW_RELEASED  equ 00000080h
HSW_BCS_ST_MAP_FAILED   equ 00000100h
HSW_BCS_ST_FW_HELD      equ 00000200h
HSW_BCS_ST_RING_WAS_ON  equ 00000400h
HSW_BCS_ST_POLL1_TIMEOUT equ 00000800h
HSW_BCS_ST_POLL2_TIMEOUT equ 00001000h
HSW_BCS_ST_BAD_INPUT    equ 00002000h
HSW_BCS_ST_FW_TIMEOUT   equ 00004000h
HSW_BCS_ST_FW_REQUESTED equ 00008000h

; Output: a 64-dword header (offsets in bytes, matching struct
; hsw_bcs_result in hsw_mmio_probe_win32.c), then the scratch copy.
HSW_BCS_HEADER_DWORDS   equ 64
HSW_BCS_OUT_DWORDS      equ (HSW_BCS_HEADER_DWORDS + HSW_BCS_SCRATCH_DWORDS)
HSW_BO_STATUS           equ 4
HSW_BO_STAGE            equ 8
HSW_BO_ACK_BEFORE       equ 12
HSW_BO_POLLS_ACK        equ 16
HSW_BO_ACK_AFTER        equ 20
HSW_BO_POLLS_RELEASE    equ 24
HSW_BO_CTL_BEFORE       equ 28
HSW_BO_START_RB         equ 32
HSW_BO_CTL_RB           equ 36
HSW_BO_HEAD_RB          equ 40
HSW_BO_TAIL_RB          equ 44
HSW_BO_POLLS1           equ 48
HSW_BO_HEAD1            equ 52
HSW_BO_STATUS0          equ 56
HSW_BO_POLLS2           equ 60
HSW_BO_HEAD2            equ 64
HSW_BO_STATUS1          equ 68
HSW_BO_ACTHD            equ 72
HSW_BO_EIR              equ 76
HSW_BO_CTL_AFTER        equ 80
HSW_BO_START_AFTER      equ 84
HSW_BO_HEAD_AFTER       equ 88
HSW_BO_TAIL_AFTER       equ 92
HSW_BO_CPU_READBACK     equ 96
HSW_BO_BAD_PTE          equ 100
HSW_BO_PTES             equ 104

; Phases C and D: the render ring (docs\plans\intel-haswell-direct3d.md).
; DIOC code 3. The ring takes up to four segments, each ending in a store
; the probe waits for; the image (batches, then the indirect state) is
; copied to region + 2000h, and the two render targets after it are filled
; with the guard and copied back. B1's BCS procedure above is left as it
; was measured.
HSW_DIOC_RCS            equ 3
HSW_RCS_MAGIC           equ 43525348h ; "HSRC"
HSW_RCS_IN_BASE         equ 0
HSW_RCS_IN_APERTURE     equ 4
HSW_RCS_IN_BDSM         equ 8
HSW_RCS_IN_OFFSET       equ 12
HSW_RCS_IN_SEGMENTS     equ 16
HSW_RCS_IN_SEG_DWORDS   equ 20        ; 4 dwords
HSW_RCS_IN_SEG_WAIT     equ 36        ; 4 status-page byte offsets
HSW_RCS_IN_SEG_VALUE    equ 52        ; 4 values
HSW_RCS_IN_IMAGE_DWORDS equ 68
HSW_RCS_IN_RING         equ 72
HSW_RCS_IN_IMAGE        equ 328
HSW_RCS_RING_MAX        equ 64
HSW_RCS_IMAGE_MAX       equ 4096
HSW_RCS_SEG_MAX         equ 4
HSW_RCS_IN_BYTES        equ (HSW_RCS_IN_IMAGE + HSW_RCS_IMAGE_MAX * 4)
HSW_RCS_PAGES           equ 15        ; ring, status, image 4, RTs 8, over-fetch
HSW_RCS_MAP_BYTES       equ 0f000h
HSW_RCS_STATUS_OFF      equ 1000h
HSW_RCS_IMAGE_OFF       equ 2000h
HSW_RCS_RT_OFF          equ 6000h     ; two 64x64x4 targets
HSW_RCS_RT_DWORDS       equ 2000h
HSW_RCS_POLLS           equ 1000000

; RCS ring registers (Vol 2c), ACTHD, IPEIR, IPEHR, INSTDONE, BBADDR and
; EIR at i915's offsets; GFX_MODE, MI_MODE and INSTPM are read, not written.
HSW_RCS_TAIL            equ 02030h
HSW_RCS_HEAD            equ 02034h
HSW_RCS_START           equ 02038h
HSW_RCS_CTL             equ 0203ch
HSW_RCS_IPEIR           equ 02064h
HSW_RCS_IPEHR           equ 02068h
HSW_RCS_INSTDONE        equ 0206ch
HSW_RCS_ACTHD           equ 02074h
HSW_RCS_MI_MODE         equ 0209ch
HSW_RCS_EIR             equ 020b0h
HSW_RCS_INSTPM          equ 020c0h
HSW_RCS_BBADDR          equ 02140h
HSW_RCS_GFX_MODE        equ 0229ch

HSW_RCS_ST_PTES_OK      equ 00000001h
HSW_RCS_ST_CPU_OK       equ 00000002h
HSW_RCS_ST_FW_ACKED     equ 00000004h
HSW_RCS_ST_RING_PROGRAMMED equ 00000008h
HSW_RCS_ST_SEG1         equ 00000010h ; SEG2 20h, SEG3 40h, SEG4 80h
HSW_RCS_ST_TORN_DOWN    equ 00010000h
HSW_RCS_ST_FW_RELEASED  equ 00000100h
HSW_RCS_ST_MAP_FAILED   equ 00000200h
HSW_RCS_ST_FW_BORROWED  equ 00000400h
HSW_RCS_ST_RING_WAS_ON  equ 00000800h
HSW_RCS_ST_TIMEOUT      equ 00001000h
HSW_RCS_ST_BAD_INPUT    equ 00002000h
HSW_RCS_ST_FW_TIMEOUT   equ 00004000h
HSW_RCS_ST_FW_REQUESTED equ 00008000h
HSW_RCS_ST_RING_BORROWED equ 00020000h
; Phase F2b: the driver's render ring (velocity9x\intel_hsw.h), borrowed
; idle and given back empty and enabled.
HSW_RCS_DRIVER_RING     equ 01802000h

; Output: a 96-dword header, offsets in bytes, matching struct
; v9x_rcs_result in hsw_mmio_probe_win32.c, then both targets.
HSW_RCS_HEADER_DWORDS   equ 96
HSW_RCS_OUT_DWORDS      equ (HSW_RCS_HEADER_DWORDS + HSW_RCS_RT_DWORDS)
HSW_RO_STATUS           equ 4
HSW_RO_STAGE            equ 8
HSW_RO_ACK_BEFORE       equ 12
HSW_RO_POLLS_ACK        equ 16
HSW_RO_ACK_AFTER        equ 20
HSW_RO_POLLS_RELEASE    equ 24
HSW_RO_CTL_BEFORE       equ 28
HSW_RO_GFX_MODE         equ 32
HSW_RO_MI_MODE          equ 36
HSW_RO_INSTPM           equ 40
HSW_RO_START_RB         equ 44
HSW_RO_CTL_RB           equ 48
HSW_RO_HEAD_RB          equ 52
HSW_RO_TAIL_RB          equ 56
HSW_RO_POLLS            equ 60        ; 4 dwords
HSW_RO_HEADS            equ 76        ; 4 dwords
HSW_RO_ACTHD            equ 92
HSW_RO_IPEIR            equ 96
HSW_RO_IPEHR            equ 100
HSW_RO_INSTDONE         equ 104
HSW_RO_BBADDR           equ 108
HSW_RO_EIR              equ 112
HSW_RO_CTL_AFTER        equ 116
HSW_RO_START_AFTER      equ 120
HSW_RO_HEAD_AFTER       equ 124
HSW_RO_TAIL_AFTER       equ 128
HSW_RO_CPU_READBACK     equ 132
HSW_RO_BAD_PTE          equ 136
HSW_RO_PTES             equ 140       ; 15 dwords
HSW_RO_STATUS_PAGE      equ 200       ; 16 dwords
HSW_RO_SEGS_DONE        equ 264
HSW_RO_RT               equ (HSW_RCS_HEADER_DWORDS * 4)

HswRcsOut     dd HSW_RCS_MAGIC
              dd (HSW_RCS_OUT_DWORDS - 1) dup (0)
HswRcsIn      dd (HSW_RCS_IN_BYTES / 4) dup (0)
HswRcsRegion  dd 0
HswRcsSeg     dd 0
HswRcsTailBytes dd 0

HswBcsOut     dd HSW_BCS_MAGIC
              dd (HSW_BCS_HEADER_DWORDS - 1) dup (0)
HswBcsScratch dd HSW_BCS_SCRATCH_DWORDS dup (0)
HswBcsIn      dd (HSW_BCS_IN_BYTES / 4) dup (0)
HswBcsRegion  dd 0
VxD_LOCKED_DATA_ENDS

VxD_LOCKED_CODE_SEG

; EAX = PCI configuration address. Returns EAX = configuration dword.
; 0CF8h saved and restored with interrupts off.
BeginProc Hsw_Pci_Read
    push    ebx
    push    edx
    pushfd
    cli
    mov     ebx, eax
    mov     dx, 0cf8h
    in      eax, dx
    mov     HswResult[24], eax
    xchg    eax, ebx
    out     dx, eax
    mov     dx, 0cfch
    in      eax, dx
    xchg    eax, ebx
    mov     dx, 0cf8h
    out     dx, eax
    mov     eax, ebx
    popfd
    pop     edx
    pop     ebx
    ret
EndProc Hsw_Pci_Read

; EDX = offset. Returns CF clear if it lies in an accepted window.
BeginProc Hsw_Offset_Allowed
    push    ecx
    push    esi
    test    edx, 3
    jnz     short Hsw_Offset_Refused
    mov     esi, OFFSET32 HswWindows
    mov     ecx, HSW_WINDOW_COUNT
Hsw_Offset_Next:
    cmp     edx, [esi]
    jb      short Hsw_Offset_Skip
    cmp     edx, [esi+4]
    jb      short Hsw_Offset_Accepted
Hsw_Offset_Skip:
    add     esi, 8
    dec     ecx
    jnz     short Hsw_Offset_Next
Hsw_Offset_Refused:
    pop     esi
    pop     ecx
    stc
    ret
Hsw_Offset_Accepted:
    pop     esi
    pop     ecx
    clc
    ret
EndProc Hsw_Offset_Allowed

; EBP = DIOC input buffer, ECX = its byte count. Copies the offset list,
; refusing the whole list if any entry is unaligned or outside the windows.
BeginProc Hsw_Accept_Offsets
    pushad
    mov     HswResult[16], 0
    sub     ecx, HSW_IN_OFFSETS
    shr     ecx, 2
    mov     eax, [ebp+HSW_IN_COUNT]
    cmp     eax, HSW_OFFSET_MAX
    ja      short Hsw_Accept_Refused
    cmp     eax, ecx
    ja      short Hsw_Accept_Refused
    mov     ecx, eax
    lea     esi, [ebp+HSW_IN_OFFSETS]
    mov     edi, OFFSET32 HswOffsets
    jecxz   short Hsw_Accept_Done
Hsw_Accept_Next:
    mov     edx, [esi]
    call    Hsw_Offset_Allowed
    jc      short Hsw_Accept_Refused_At
    mov     [edi], edx
    add     esi, 4
    add     edi, 4
    dec     ecx
    jnz     short Hsw_Accept_Next
Hsw_Accept_Done:
    mov     eax, [ebp+HSW_IN_COUNT]
    mov     HswResult[16], eax
    popad
    ret
Hsw_Accept_Refused_At:
    mov     HswResult[28], edx
Hsw_Accept_Refused:
    or      HswResult[4], HSW_ST_OFFSETS_REFUSED
    popad
    ret
EndProc Hsw_Accept_Offsets

; EDI = destination. Reads every accepted offset through the MMIO mapping.
BeginProc Hsw_Read_Snapshot
    pushad
    mov     ecx, HswResult[16]
    mov     ebx, OFFSET32 HswOffsets
    mov     esi, HswMmio
    jecxz   short Hsw_Read_Snapshot_Done
Hsw_Read_Snapshot_Next:
    mov     eax, [ebx]
    mov     eax, [esi+eax]
    mov     [edi], eax
    add     ebx, 4
    add     edi, 4
    dec     ecx
    jnz     short Hsw_Read_Snapshot_Next
Hsw_Read_Snapshot_Done:
    popad
    ret
EndProc Hsw_Read_Snapshot

; Copy the first HSW_GTT_COPY entries and count valid ones (bit 0) in all
; HSW_GTT_ENTRIES. Vol 5: "Software may read PTE values from the global
; Graphics Translation Table (GTT)."
BeginProc Hsw_Read_Gtt
    pushad
    mov     esi, HswGttLinear
    mov     edi, OFFSET32 HswGtt
    mov     ecx, HSW_GTT_COPY
    cld
    rep     movsd

    mov     esi, HswGttLinear
    mov     ecx, HSW_GTT_ENTRIES
    xor     edx, edx
Hsw_Read_Gtt_Next:
    test    dword ptr [esi], 1
    jz      short Hsw_Read_Gtt_Skip
    inc     edx
Hsw_Read_Gtt_Skip:
    add     esi, 4
    dec     ecx
    jnz     short Hsw_Read_Gtt_Next
    mov     HswResult[20], edx
    or      HswResult[4], HSW_ST_GTT_READ
    popad
    ret
EndProc Hsw_Read_Gtt

; Phase A: request forcewake, take the second snapshot while awake, release.
; See the header for the sequence and its source. Interrupts are off from
; the first acknowledge read to the last, so nothing else touches A188h
; between our two writes.
BeginProc Hsw_Forcewake
    pushad
    pushfd
    cli
    mov     esi, HswMmio

    mov     eax, [esi+HSW_FW_ACK]
    mov     HswResult[36], eax
    test    eax, HSW_FW_ACK_BIT
    jz      short Hsw_Fw_Write_Request
    ; Someone else holds forcewake: write nothing, read as we are.
    or      HswResult[4], HSW_ST_FW_ALREADY_HELD
    mov     edi, OFFSET32 HswSecond
    call    Hsw_Read_Snapshot
    jmp     Hsw_Fw_Done

Hsw_Fw_Write_Request:
    mov     dword ptr [esi+HSW_FW_REQUEST], HSW_FW_SET
    or      HswResult[4], HSW_ST_FW_REQUESTED
    xor     ecx, ecx
Hsw_Fw_Poll:
    mov     eax, [esi+HSW_FW_ACK]
    test    eax, HSW_FW_ACK_BIT
    jnz     short Hsw_Fw_Acked
    inc     ecx
    cmp     ecx, HSW_FW_POLLS
    jb      short Hsw_Fw_Poll
    or      HswResult[4], HSW_ST_FW_ACK_TIMEOUT
    jmp     short Hsw_Fw_Awake
Hsw_Fw_Acked:
    or      HswResult[4], HSW_ST_FW_ACKED
Hsw_Fw_Awake:
    mov     HswResult[40], ecx
    mov     HswResult[44], eax
    mov     eax, [esi+HSW_FW_REQUEST]
    mov     HswResult[48], eax

    mov     edi, OFFSET32 HswSecond
    call    Hsw_Read_Snapshot

    ; Release, whether or not the request was acknowledged.
    mov     dword ptr [esi+HSW_FW_REQUEST], HSW_FW_CLEAR
    xor     ecx, ecx
Hsw_Fw_Release_Poll:
    mov     eax, [esi+HSW_FW_ACK]
    test    eax, HSW_FW_ACK_BIT
    jz      short Hsw_Fw_Released
    inc     ecx
    cmp     ecx, HSW_FW_POLLS
    jb      short Hsw_Fw_Release_Poll
    or      HswResult[4], HSW_ST_FW_RELEASE_TIMEOUT
    jmp     short Hsw_Fw_Release_Done
Hsw_Fw_Released:
    or      HswResult[4], HSW_ST_FW_RELEASED
Hsw_Fw_Release_Done:
    mov     HswResult[52], ecx
    mov     HswResult[56], eax

Hsw_Fw_Done:
    popfd
    popad
    ret
EndProc Hsw_Forcewake

BeginProc Hsw_Capture
    pushad

    ; The base must belong to an Intel IGD at 00:02.0 and carry the BAR's
    ; own 4 MB alignment; anything else is refused before it is mapped.
    mov     eax, HSW_IGD_ADDRESS
    call    Hsw_Pci_Read
    mov     HswResult[8], eax
    cmp     ax, HSW_VENDOR_INTEL
    jne     Hsw_Capture_Bad_Base
    or      HswResult[4], HSW_ST_IGD_INTEL

    mov     eax, HswBase
    test    eax, eax
    jz      Hsw_Capture_Bad_Base
    test    eax, HSW_BASE_ALIGN
    jnz     Hsw_Capture_Bad_Base
    mov     HswResult[12], eax

    VMMcall _MapPhysToLinear,<eax,HSW_MMIO_BYTES,0>
    cmp     eax, 0ffffffffh
    je      Hsw_Capture_Done
    mov     HswMmio, eax
    or      HswResult[4], HSW_ST_MMIO_MAPPED

    mov     eax, HswBase
    add     eax, HSW_GTT_OFFSET
    VMMcall _MapPhysToLinear,<eax,HSW_GTT_BYTES,0>
    cmp     eax, 0ffffffffh
    je      short Hsw_Capture_Snapshot
    mov     HswGttLinear, eax
    or      HswResult[4], HSW_ST_GTT_MAPPED

Hsw_Capture_Snapshot:
    mov     edi, OFFSET32 HswFirst
    call    Hsw_Read_Snapshot
    test    HswFlags, HSW_FLAG_FORCEWAKE
    jz      short Hsw_Capture_Second
    ; The second snapshot is taken inside the forcewake window.
    call    Hsw_Forcewake
    jmp     short Hsw_Capture_Snapped
Hsw_Capture_Second:
    mov     edi, OFFSET32 HswSecond
    call    Hsw_Read_Snapshot
Hsw_Capture_Snapped:
    or      HswResult[4], HSW_ST_SNAPSHOT_TAKEN

    cmp     HswGttLinear, 0
    je      short Hsw_Capture_Done
    call    Hsw_Read_Gtt
    jmp     short Hsw_Capture_Done

Hsw_Capture_Bad_Base:
    or      HswResult[4], HSW_ST_BASE_INVALID
Hsw_Capture_Done:
    popad
    ret
EndProc Hsw_Capture

; EBP = MMIO, EBX = target tail, EDX = status page offset of the expected
; store, EAX in = expected value. Polls until HEAD reaches the tail and the
; store has landed. Returns ECX = polls, CF set on timeout.
BeginProc Hsw_Bcs_Wait
    push    esi
    push    edi
    mov     edi, eax
    mov     esi, HswBcsRegion
    add     esi, edx
    xor     ecx, ecx
Hsw_Bcs_Wait_Next:
    mov     eax, [ebp+HSW_BCS_HEAD]
    and     eax, HSW_RING_HEAD_MASK
    cmp     eax, ebx
    jne     short Hsw_Bcs_Wait_Again
    cmp     [esi], edi
    je      short Hsw_Bcs_Wait_Done
Hsw_Bcs_Wait_Again:
    inc     ecx
    cmp     ecx, HSW_BCS_POLLS
    jb      short Hsw_Bcs_Wait_Next
    pop     edi
    pop     esi
    stc
    ret
Hsw_Bcs_Wait_Done:
    pop     edi
    pop     esi
    clc
    ret
EndProc Hsw_Bcs_Wait

; Phase B1, stages 1-8 as the plan lists them. Each stage records how far
; it got in HSW_BO_STAGE; stage 8 (tear down, release) runs whenever the
; ring was programmed, and forcewake is released whenever it was requested.
BeginProc Hsw_Bcs
    pushad
    mov     HswBcsRegion, 0

    ; Input bounds: command counts, qword-aligned tails, a 4 KB-aligned
    ; ring offset, a 4 MB-aligned GTTMMADR.
    mov     eax, HswBcsIn[HSW_BCS_IN_N1]
    cmp     eax, HSW_BCS_CMD1_MAX
    ja      Hsw_Bcs_Bad_Input
    test    eax, 1
    jnz     Hsw_Bcs_Bad_Input
    mov     ebx, HswBcsIn[HSW_BCS_IN_N2]
    cmp     ebx, HSW_BCS_CMD2_MAX
    ja      Hsw_Bcs_Bad_Input
    test    ebx, 1
    jnz     Hsw_Bcs_Bad_Input
    test    HswBcsIn[HSW_BCS_IN_OFFSET], 0fffh
    jnz     Hsw_Bcs_Bad_Input
    mov     eax, HswBcsIn[HSW_BCS_IN_BASE]
    test    eax, eax
    jz      Hsw_Bcs_Bad_Input
    test    eax, HSW_BASE_ALIGN
    jnz     Hsw_Bcs_Bad_Input

    VMMcall _MapPhysToLinear,<eax,HSW_MMIO_BYTES,0>
    cmp     eax, 0ffffffffh
    je      Hsw_Bcs_Map_Failed
    mov     HswMmio, eax
    mov     ebp, eax
    mov     eax, HswBcsIn[HSW_BCS_IN_BASE]
    add     eax, HSW_GTT_OFFSET
    VMMcall _MapPhysToLinear,<eax,HSW_GTT_BYTES,0>
    cmp     eax, 0ffffffffh
    je      Hsw_Bcs_Map_Failed
    mov     HswGttLinear, eax

    ; Stage 1: every page's PTE is valid and maps BDSM + offset.
    mov     HswBcsOut[HSW_BO_STAGE], 1
    mov     HswBcsOut[HSW_BO_BAD_PTE], 0ffffffffh
    mov     esi, HswGttLinear
    mov     eax, HswBcsIn[HSW_BCS_IN_OFFSET]
    shr     eax, 12
    lea     esi, [esi+eax*4]
    mov     ebx, HswBcsIn[HSW_BCS_IN_BDSM]
    and     ebx, HSW_BCS_STOLEN_MASK
    add     ebx, HswBcsIn[HSW_BCS_IN_OFFSET]
    or      ebx, 1
    xor     ecx, ecx
    xor     edi, edi
Hsw_Bcs_Pte_Next:
    mov     eax, [esi+ecx*4]
    mov     dword ptr HswBcsOut[HSW_BO_PTES+ecx*4], eax
    and     eax, HSW_BCS_PTE_MASK
    cmp     eax, ebx
    je      short Hsw_Bcs_Pte_Ok
    test    edi, edi
    jnz     short Hsw_Bcs_Pte_Ok
    mov     HswBcsOut[HSW_BO_BAD_PTE], ecx
    inc     edi
Hsw_Bcs_Pte_Ok:
    add     ebx, 1000h
    inc     ecx
    cmp     ecx, HSW_BCS_PAGES
    jb      short Hsw_Bcs_Pte_Next
    test    edi, edi
    jnz     Hsw_Bcs_Exit
    or      HswBcsOut[HSW_BO_STATUS], HSW_BCS_ST_PTES_OK

    ; Stage 2: the CPU reaches the pages through the aperture; then the
    ; ring is filled with MI_NOOP (0), the status page with 0 and the
    ; scratch with the guard.
    mov     HswBcsOut[HSW_BO_STAGE], 2
    mov     eax, HswBcsIn[HSW_BCS_IN_APERTURE]
    add     eax, HswBcsIn[HSW_BCS_IN_OFFSET]
    VMMcall _MapPhysToLinear,<eax,HSW_BCS_MAP_BYTES,0>
    cmp     eax, 0ffffffffh
    je      Hsw_Bcs_Map_Failed
    mov     HswBcsRegion, eax
    mov     edi, eax
    mov     dword ptr [edi], HSW_BCS_CPU_PATTERN
    mov     dword ptr [edi+HSW_BCS_SCRATCH_OFF], HSW_BCS_CPU_PATTERN
    mov     eax, [edi]
    mov     HswBcsOut[HSW_BO_CPU_READBACK], eax
    cmp     eax, HSW_BCS_CPU_PATTERN
    jne     Hsw_Bcs_Exit
    cmp     dword ptr [edi+HSW_BCS_SCRATCH_OFF], HSW_BCS_CPU_PATTERN
    jne     Hsw_Bcs_Exit
    cld
    xor     eax, eax
    mov     ecx, (HSW_BCS_SCRATCH_OFF / 4)
    rep     stosd
    mov     eax, HSW_BCS_GUARD
    mov     ecx, HSW_BCS_SCRATCH_DWORDS
    rep     stosd
    mov     edi, HswBcsRegion
    cmp     dword ptr [edi], 0
    jne     Hsw_Bcs_Exit
    cmp     dword ptr [edi+HSW_BCS_SCRATCH_OFF], HSW_BCS_GUARD
    jne     Hsw_Bcs_Exit
    or      HswBcsOut[HSW_BO_STATUS], HSW_BCS_ST_CPU_OK

    ; Stage 3: forcewake, as Phase A, interrupts off only for the
    ; handshake itself.
    mov     HswBcsOut[HSW_BO_STAGE], 3
    pushfd
    cli
    mov     eax, [ebp+HSW_FW_ACK]
    mov     HswBcsOut[HSW_BO_ACK_BEFORE], eax
    test    eax, HSW_FW_ACK_BIT
    jz      short Hsw_Bcs_Fw_Write
    popfd
    or      HswBcsOut[HSW_BO_STATUS], HSW_BCS_ST_FW_HELD
    jmp     Hsw_Bcs_Exit
Hsw_Bcs_Fw_Write:
    mov     dword ptr [ebp+HSW_FW_REQUEST], HSW_FW_SET
    or      HswBcsOut[HSW_BO_STATUS], HSW_BCS_ST_FW_REQUESTED
    xor     ecx, ecx
Hsw_Bcs_Fw_Poll:
    mov     eax, [ebp+HSW_FW_ACK]
    test    eax, HSW_FW_ACK_BIT
    jnz     short Hsw_Bcs_Fw_Acked
    inc     ecx
    cmp     ecx, HSW_FW_POLLS
    jb      short Hsw_Bcs_Fw_Poll
    popfd
    mov     HswBcsOut[HSW_BO_POLLS_ACK], ecx
    or      HswBcsOut[HSW_BO_STATUS], HSW_BCS_ST_FW_TIMEOUT
    jmp     Hsw_Bcs_Release
Hsw_Bcs_Fw_Acked:
    popfd
    mov     HswBcsOut[HSW_BO_POLLS_ACK], ecx
    or      HswBcsOut[HSW_BO_STATUS], HSW_BCS_ST_FW_ACKED

    ; Stage 4: the ring must be off; then HEAD, TAIL, START, CTL, read
    ; back. An enabled ring belongs to someone else and is left alone.
    mov     HswBcsOut[HSW_BO_STAGE], 4
    mov     eax, [ebp+HSW_BCS_CTL]
    mov     HswBcsOut[HSW_BO_CTL_BEFORE], eax
    test    eax, 1
    jz      short Hsw_Bcs_Program
    or      HswBcsOut[HSW_BO_STATUS], HSW_BCS_ST_RING_WAS_ON
    jmp     Hsw_Bcs_Release
Hsw_Bcs_Program:
    mov     dword ptr [ebp+HSW_BCS_HEAD], 0
    mov     dword ptr [ebp+HSW_BCS_TAIL], 0
    mov     eax, HswBcsIn[HSW_BCS_IN_OFFSET]
    mov     [ebp+HSW_BCS_START], eax
    mov     dword ptr [ebp+HSW_BCS_CTL], HSW_RING_CTL_ONE_PAGE
    mov     eax, [ebp+HSW_BCS_START]
    mov     HswBcsOut[HSW_BO_START_RB], eax
    mov     eax, [ebp+HSW_BCS_CTL]
    mov     HswBcsOut[HSW_BO_CTL_RB], eax
    mov     eax, [ebp+HSW_BCS_HEAD]
    mov     HswBcsOut[HSW_BO_HEAD_RB], eax
    mov     eax, [ebp+HSW_BCS_TAIL]
    mov     HswBcsOut[HSW_BO_TAIL_RB], eax
    mov     eax, HswBcsOut[HSW_BO_START_RB]
    cmp     eax, HswBcsIn[HSW_BCS_IN_OFFSET]
    jne     Hsw_Bcs_Teardown
    mov     eax, HswBcsOut[HSW_BO_CTL_RB]
    and     eax, HSW_RING_CTL_CHECK
    cmp     eax, HSW_RING_CTL_ONE_PAGE
    jne     Hsw_Bcs_Teardown
    or      HswBcsOut[HSW_BO_STATUS], HSW_BCS_ST_RING_PROGRAMMED

    ; Stage 5: the first store.
    mov     HswBcsOut[HSW_BO_STAGE], 5
    cld
    mov     esi, OFFSET32 HswBcsIn + HSW_BCS_IN_CMD1
    mov     edi, HswBcsRegion
    mov     ecx, HswBcsIn[HSW_BCS_IN_N1]
    rep     movsd
    mov     ebx, HswBcsIn[HSW_BCS_IN_N1]
    shl     ebx, 2
    mov     [ebp+HSW_BCS_TAIL], ebx
    mov     edx, HSW_BCS_STATUS_OFF
    mov     eax, HSW_BCS_STORE1
    call    Hsw_Bcs_Wait
    mov     HswBcsOut[HSW_BO_POLLS1], ecx
    mov     eax, [ebp+HSW_BCS_HEAD]
    mov     HswBcsOut[HSW_BO_HEAD1], eax
    mov     edi, HswBcsRegion
    mov     eax, [edi+HSW_BCS_STATUS_OFF]
    mov     HswBcsOut[HSW_BO_STATUS0], eax
    jnc     short Hsw_Bcs_Store1_Ok
    or      HswBcsOut[HSW_BO_STATUS], HSW_BCS_ST_POLL1_TIMEOUT
    jmp     Hsw_Bcs_Record
Hsw_Bcs_Store1_Ok:
    or      HswBcsOut[HSW_BO_STATUS], HSW_BCS_ST_STORE1

    ; Stage 6: the fill and the second store, after the first commands.
    mov     HswBcsOut[HSW_BO_STAGE], 6
    cld
    mov     esi, OFFSET32 HswBcsIn + HSW_BCS_IN_CMD2
    mov     edi, HswBcsRegion
    mov     eax, HswBcsIn[HSW_BCS_IN_N1]
    lea     edi, [edi+eax*4]
    mov     ecx, HswBcsIn[HSW_BCS_IN_N2]
    rep     movsd
    mov     ebx, HswBcsIn[HSW_BCS_IN_N1]
    add     ebx, HswBcsIn[HSW_BCS_IN_N2]
    shl     ebx, 2
    mov     [ebp+HSW_BCS_TAIL], ebx
    mov     edx, HSW_BCS_STATUS_OFF + 4
    mov     eax, HSW_BCS_STORE2
    call    Hsw_Bcs_Wait
    mov     HswBcsOut[HSW_BO_POLLS2], ecx
    mov     eax, [ebp+HSW_BCS_HEAD]
    mov     HswBcsOut[HSW_BO_HEAD2], eax
    mov     edi, HswBcsRegion
    mov     eax, [edi+HSW_BCS_STATUS_OFF+4]
    mov     HswBcsOut[HSW_BO_STATUS1], eax
    jnc     short Hsw_Bcs_Store2_Ok
    or      HswBcsOut[HSW_BO_STATUS], HSW_BCS_ST_POLL2_TIMEOUT
    jmp     short Hsw_Bcs_Record
Hsw_Bcs_Store2_Ok:
    or      HswBcsOut[HSW_BO_STATUS], HSW_BCS_ST_STORE2

    ; Stage 7: record the engine and copy the scratch, also after a
    ; timeout, since that is when it matters most.
Hsw_Bcs_Record:
    mov     HswBcsOut[HSW_BO_STAGE], 7
    mov     eax, [ebp+HSW_BCS_ACTHD]
    mov     HswBcsOut[HSW_BO_ACTHD], eax
    mov     eax, [ebp+HSW_BCS_EIR]
    mov     HswBcsOut[HSW_BO_EIR], eax
    cld
    mov     esi, HswBcsRegion
    add     esi, HSW_BCS_SCRATCH_OFF
    mov     edi, OFFSET32 HswBcsScratch
    mov     ecx, HSW_BCS_SCRATCH_DWORDS
    rep     movsd

    ; Stage 8: ring off and cleared, read back; then forcewake released.
Hsw_Bcs_Teardown:
    mov     dword ptr [ebp+HSW_BCS_CTL], 0
    mov     dword ptr [ebp+HSW_BCS_HEAD], 0
    mov     dword ptr [ebp+HSW_BCS_TAIL], 0
    mov     dword ptr [ebp+HSW_BCS_START], 0
    mov     eax, [ebp+HSW_BCS_CTL]
    mov     HswBcsOut[HSW_BO_CTL_AFTER], eax
    mov     eax, [ebp+HSW_BCS_START]
    mov     HswBcsOut[HSW_BO_START_AFTER], eax
    mov     eax, [ebp+HSW_BCS_HEAD]
    mov     HswBcsOut[HSW_BO_HEAD_AFTER], eax
    mov     eax, [ebp+HSW_BCS_TAIL]
    mov     HswBcsOut[HSW_BO_TAIL_AFTER], eax
    or      HswBcsOut[HSW_BO_STATUS], HSW_BCS_ST_TORN_DOWN

Hsw_Bcs_Release:
    pushfd
    cli
    mov     dword ptr [ebp+HSW_FW_REQUEST], HSW_FW_CLEAR
    xor     ecx, ecx
Hsw_Bcs_Release_Poll:
    mov     eax, [ebp+HSW_FW_ACK]
    test    eax, HSW_FW_ACK_BIT
    jz      short Hsw_Bcs_Released
    inc     ecx
    cmp     ecx, HSW_FW_POLLS
    jb      short Hsw_Bcs_Release_Poll
    popfd
    jmp     short Hsw_Bcs_Release_Done
Hsw_Bcs_Released:
    popfd
    or      HswBcsOut[HSW_BO_STATUS], HSW_BCS_ST_FW_RELEASED
Hsw_Bcs_Release_Done:
    mov     HswBcsOut[HSW_BO_POLLS_RELEASE], ecx
    mov     HswBcsOut[HSW_BO_ACK_AFTER], eax
    jmp     short Hsw_Bcs_Exit

Hsw_Bcs_Bad_Input:
    or      HswBcsOut[HSW_BO_STATUS], HSW_BCS_ST_BAD_INPUT
    jmp     short Hsw_Bcs_Exit
Hsw_Bcs_Map_Failed:
    or      HswBcsOut[HSW_BO_STATUS], HSW_BCS_ST_MAP_FAILED
Hsw_Bcs_Exit:
    popad
    ret
EndProc Hsw_Bcs

; ESI = DIOCParams. Copies the input, runs Hsw_Bcs, copies the result.
BeginProc Hsw_Bcs_Dioc
    pushad
    mov     ebx, esi
    mov     esi, [ebx.lpvInBuffer]
    test    esi, esi
    jz      short Hsw_Bcs_Dioc_Fail
    cmp     [ebx.cbInBuffer], HSW_BCS_IN_BYTES
    jb      short Hsw_Bcs_Dioc_Fail
    mov     edi, [ebx.lpvOutBuffer]
    test    edi, edi
    jz      short Hsw_Bcs_Dioc_Fail
    cmp     [ebx.cbOutBuffer], HSW_BCS_OUT_DWORDS * 4
    jb      short Hsw_Bcs_Dioc_Fail

    cld
    mov     edi, OFFSET32 HswBcsIn
    mov     ecx, HSW_BCS_IN_BYTES / 4
    rep     movsd
    mov     edi, OFFSET32 HswBcsOut + 4
    mov     ecx, HSW_BCS_OUT_DWORDS - 1
    xor     eax, eax
    rep     stosd

    call    Hsw_Bcs

    mov     edi, [ebx.lpvOutBuffer]
    mov     esi, OFFSET32 HswBcsOut
    mov     ecx, HSW_BCS_OUT_DWORDS
    rep     movsd
    mov     eax, [ebx.lpcbBytesReturned]
    test    eax, eax
    jz      short Hsw_Bcs_Dioc_Done
    mov     dword ptr [eax], HSW_BCS_OUT_DWORDS * 4
Hsw_Bcs_Dioc_Done:
    popad
    xor     eax, eax
    ret
Hsw_Bcs_Dioc_Fail:
    popad
    mov     eax, 1
    ret
EndProc Hsw_Bcs_Dioc

; EBP = MMIO, EBX = target tail, EDX = region offset of the expected
; store, EAX in = expected value. Polls until RCS HEAD reaches the tail and
; the store has landed. Returns ECX = polls, CF set on timeout.
BeginProc Hsw_Rcs_Wait
    push    esi
    push    edi
    mov     edi, eax
    mov     esi, HswRcsRegion
    add     esi, edx
    xor     ecx, ecx
Hsw_Rcs_Wait_Next:
    mov     eax, [ebp+HSW_RCS_HEAD]
    and     eax, HSW_RING_HEAD_MASK
    cmp     eax, ebx
    jne     short Hsw_Rcs_Wait_Again
    cmp     [esi], edi
    je      short Hsw_Rcs_Wait_Done
Hsw_Rcs_Wait_Again:
    inc     ecx
    cmp     ecx, HSW_RCS_POLLS
    jb      short Hsw_Rcs_Wait_Next
    pop     edi
    pop     esi
    stc
    ret
Hsw_Rcs_Wait_Done:
    pop     edi
    pop     esi
    clc
    ret
EndProc Hsw_Rcs_Wait

; Phase C, stages 1-9 as the plan lists them. Stage 9 (tear down) runs
; whenever the ring was programmed; forcewake is released only if this
; procedure requested it. A hold found already in place (the B2 driver's)
; is used and left alone.
BeginProc Hsw_Rcs
    pushad
    mov     HswRcsRegion, 0
    mov     HswRcsSeg, 0
    mov     HswRcsTailBytes, 0

    ; Input bounds: 1-3 segments of an even, non-zero dword count within
    ; the ring copy, status offsets dword-aligned inside the status page,
    ; an image that fits, a 4 KB-aligned region, a 4 MB-aligned GTTMMADR.
    mov     ecx, HswRcsIn[HSW_RCS_IN_SEGMENTS]
    test    ecx, ecx
    jz      Hsw_Rcs_Bad_Input
    cmp     ecx, HSW_RCS_SEG_MAX
    ja      Hsw_Rcs_Bad_Input
    xor     ebx, ebx
    xor     edx, edx
Hsw_Rcs_Check_Seg:
    mov     eax, HswRcsIn[HSW_RCS_IN_SEG_DWORDS+edx*4]
    test    eax, eax
    jz      Hsw_Rcs_Bad_Input
    test    eax, 1
    jnz     Hsw_Rcs_Bad_Input
    add     ebx, eax
    mov     eax, HswRcsIn[HSW_RCS_IN_SEG_WAIT+edx*4]
    test    eax, 3
    jnz     Hsw_Rcs_Bad_Input
    cmp     eax, HSW_RCS_STATUS_OFF
    jae     Hsw_Rcs_Bad_Input
    inc     edx
    cmp     edx, ecx
    jb      short Hsw_Rcs_Check_Seg
    cmp     ebx, HSW_RCS_RING_MAX
    ja      Hsw_Rcs_Bad_Input
    cmp     HswRcsIn[HSW_RCS_IN_IMAGE_DWORDS], HSW_RCS_IMAGE_MAX
    ja      Hsw_Rcs_Bad_Input
    test    HswRcsIn[HSW_RCS_IN_OFFSET], 0fffh
    jnz     Hsw_Rcs_Bad_Input
    mov     eax, HswRcsIn[HSW_RCS_IN_BASE]
    test    eax, eax
    jz      Hsw_Rcs_Bad_Input
    test    eax, HSW_BASE_ALIGN
    jnz     Hsw_Rcs_Bad_Input

    VMMcall _MapPhysToLinear,<eax,HSW_MMIO_BYTES,0>
    cmp     eax, 0ffffffffh
    je      Hsw_Rcs_Map_Failed
    mov     HswMmio, eax
    mov     ebp, eax
    mov     eax, HswRcsIn[HSW_RCS_IN_BASE]
    add     eax, HSW_GTT_OFFSET
    VMMcall _MapPhysToLinear,<eax,HSW_GTT_BYTES,0>
    cmp     eax, 0ffffffffh
    je      Hsw_Rcs_Map_Failed
    mov     HswGttLinear, eax

    ; Stage 1: every page's PTE is valid and maps BDSM + offset.
    mov     HswRcsOut[HSW_RO_STAGE], 1
    mov     HswRcsOut[HSW_RO_BAD_PTE], 0ffffffffh
    mov     esi, HswGttLinear
    mov     eax, HswRcsIn[HSW_RCS_IN_OFFSET]
    shr     eax, 12
    lea     esi, [esi+eax*4]
    mov     ebx, HswRcsIn[HSW_RCS_IN_BDSM]
    and     ebx, HSW_BCS_STOLEN_MASK
    add     ebx, HswRcsIn[HSW_RCS_IN_OFFSET]
    or      ebx, 1
    xor     ecx, ecx
    xor     edi, edi
Hsw_Rcs_Pte_Next:
    mov     eax, [esi+ecx*4]
    mov     dword ptr HswRcsOut[HSW_RO_PTES+ecx*4], eax
    and     eax, HSW_BCS_PTE_MASK
    cmp     eax, ebx
    je      short Hsw_Rcs_Pte_Ok
    test    edi, edi
    jnz     short Hsw_Rcs_Pte_Ok
    mov     HswRcsOut[HSW_RO_BAD_PTE], ecx
    inc     edi
Hsw_Rcs_Pte_Ok:
    add     ebx, 1000h
    inc     ecx
    cmp     ecx, HSW_RCS_PAGES
    jb      short Hsw_Rcs_Pte_Next
    test    edi, edi
    jnz     Hsw_Rcs_Exit
    or      HswRcsOut[HSW_RO_STATUS], HSW_RCS_ST_PTES_OK

    ; Stage 2: the CPU reaches the region through the aperture; then all
    ; of it is zeroed (MI_NOOP in the ring, 0 in the status page and the
    ; over-fetch page) and the image copied in.
    mov     HswRcsOut[HSW_RO_STAGE], 2
    mov     eax, HswRcsIn[HSW_RCS_IN_APERTURE]
    add     eax, HswRcsIn[HSW_RCS_IN_OFFSET]
    VMMcall _MapPhysToLinear,<eax,HSW_RCS_MAP_BYTES,0>
    cmp     eax, 0ffffffffh
    je      Hsw_Rcs_Map_Failed
    mov     HswRcsRegion, eax
    mov     edi, eax
    mov     dword ptr [edi], HSW_BCS_CPU_PATTERN
    mov     dword ptr [edi+HSW_RCS_IMAGE_OFF], HSW_BCS_CPU_PATTERN
    mov     eax, [edi]
    mov     HswRcsOut[HSW_RO_CPU_READBACK], eax
    cmp     eax, HSW_BCS_CPU_PATTERN
    jne     Hsw_Rcs_Exit
    cmp     dword ptr [edi+HSW_RCS_IMAGE_OFF], HSW_BCS_CPU_PATTERN
    jne     Hsw_Rcs_Exit
    cld
    xor     eax, eax
    mov     ecx, (HSW_RCS_MAP_BYTES / 4)
    rep     stosd
    mov     esi, OFFSET32 HswRcsIn + HSW_RCS_IN_IMAGE
    mov     edi, HswRcsRegion
    add     edi, HSW_RCS_IMAGE_OFF
    mov     ecx, HswRcsIn[HSW_RCS_IN_IMAGE_DWORDS]
    rep     movsd
    mov     edi, HswRcsRegion
    add     edi, HSW_RCS_RT_OFF
    mov     eax, HSW_BCS_GUARD
    mov     ecx, HSW_RCS_RT_DWORDS
    rep     stosd
    mov     edi, HswRcsRegion
    cmp     dword ptr [edi], 0
    jne     Hsw_Rcs_Exit
    or      HswRcsOut[HSW_RO_STATUS], HSW_RCS_ST_CPU_OK

    ; Stage 3: forcewake. Held already: use it, release nothing.
    mov     HswRcsOut[HSW_RO_STAGE], 3
    pushfd
    cli
    mov     eax, [ebp+HSW_FW_ACK]
    mov     HswRcsOut[HSW_RO_ACK_BEFORE], eax
    test    eax, HSW_FW_ACK_BIT
    jz      short Hsw_Rcs_Fw_Write
    popfd
    or      HswRcsOut[HSW_RO_STATUS], HSW_RCS_ST_FW_BORROWED
    jmp     short Hsw_Rcs_Ring
Hsw_Rcs_Fw_Write:
    mov     dword ptr [ebp+HSW_FW_REQUEST], HSW_FW_SET
    or      HswRcsOut[HSW_RO_STATUS], HSW_RCS_ST_FW_REQUESTED
    xor     ecx, ecx
Hsw_Rcs_Fw_Poll:
    mov     eax, [ebp+HSW_FW_ACK]
    test    eax, HSW_FW_ACK_BIT
    jnz     short Hsw_Rcs_Fw_Acked
    inc     ecx
    cmp     ecx, HSW_FW_POLLS
    jb      short Hsw_Rcs_Fw_Poll
    popfd
    mov     HswRcsOut[HSW_RO_POLLS_ACK], ecx
    or      HswRcsOut[HSW_RO_STATUS], HSW_RCS_ST_FW_TIMEOUT
    jmp     Hsw_Rcs_Release
Hsw_Rcs_Fw_Acked:
    popfd
    mov     HswRcsOut[HSW_RO_POLLS_ACK], ecx
    or      HswRcsOut[HSW_RO_STATUS], HSW_RCS_ST_FW_ACKED

    ; Stage 4: the render ring must be off; the mode registers are read
    ; for the record; then HEAD, TAIL, START, CTL, read back.
Hsw_Rcs_Ring:
    mov     HswRcsOut[HSW_RO_STAGE], 4
    mov     eax, [ebp+HSW_RCS_GFX_MODE]
    mov     HswRcsOut[HSW_RO_GFX_MODE], eax
    mov     eax, [ebp+HSW_RCS_MI_MODE]
    mov     HswRcsOut[HSW_RO_MI_MODE], eax
    mov     eax, [ebp+HSW_RCS_INSTPM]
    mov     HswRcsOut[HSW_RO_INSTPM], eax
    mov     eax, [ebp+HSW_RCS_CTL]
    mov     HswRcsOut[HSW_RO_CTL_BEFORE], eax
    test    eax, 1
    jz      short Hsw_Rcs_Program
    ; On: only the driver's ring, and only idle, is borrowed. The driver
    ; submits synchronously and reads TAIL from the hardware, so an empty
    ; ring handed back at 0 is one it carries on from.
    cmp     dword ptr [ebp+HSW_RCS_START], HSW_RCS_DRIVER_RING
    jne     short Hsw_Rcs_Not_Ours
    mov     eax, [ebp+HSW_RCS_HEAD]
    and     eax, HSW_RING_HEAD_MASK
    mov     ecx, [ebp+HSW_RCS_TAIL]
    and     ecx, HSW_RING_HEAD_MASK
    cmp     eax, ecx
    jne     short Hsw_Rcs_Not_Ours
    or      HswRcsOut[HSW_RO_STATUS], HSW_RCS_ST_RING_BORROWED
    mov     dword ptr [ebp+HSW_RCS_CTL], 0
    jmp     short Hsw_Rcs_Program
Hsw_Rcs_Not_Ours:
    or      HswRcsOut[HSW_RO_STATUS], HSW_RCS_ST_RING_WAS_ON
    jmp     Hsw_Rcs_Release
Hsw_Rcs_Program:
    mov     dword ptr [ebp+HSW_RCS_HEAD], 0
    mov     dword ptr [ebp+HSW_RCS_TAIL], 0
    mov     eax, HswRcsIn[HSW_RCS_IN_OFFSET]
    mov     [ebp+HSW_RCS_START], eax
    mov     dword ptr [ebp+HSW_RCS_CTL], HSW_RING_CTL_ONE_PAGE
    mov     eax, [ebp+HSW_RCS_START]
    mov     HswRcsOut[HSW_RO_START_RB], eax
    mov     eax, [ebp+HSW_RCS_CTL]
    mov     HswRcsOut[HSW_RO_CTL_RB], eax
    mov     eax, [ebp+HSW_RCS_HEAD]
    mov     HswRcsOut[HSW_RO_HEAD_RB], eax
    mov     eax, [ebp+HSW_RCS_TAIL]
    mov     HswRcsOut[HSW_RO_TAIL_RB], eax
    mov     eax, HswRcsOut[HSW_RO_START_RB]
    cmp     eax, HswRcsIn[HSW_RCS_IN_OFFSET]
    jne     Hsw_Rcs_Teardown
    mov     eax, HswRcsOut[HSW_RO_CTL_RB]
    and     eax, HSW_RING_CTL_CHECK
    cmp     eax, HSW_RING_CTL_ONE_PAGE
    jne     Hsw_Rcs_Teardown
    or      HswRcsOut[HSW_RO_STATUS], HSW_RCS_ST_RING_PROGRAMMED

    ; Stages 5-7: each segment is copied after the last, the tail moved
    ; past it, and its store waited for. A timeout stops the rest.
Hsw_Rcs_Seg_Next:
    mov     ecx, HswRcsSeg
    cmp     ecx, HswRcsIn[HSW_RCS_IN_SEGMENTS]
    jae     Hsw_Rcs_Record
    lea     eax, [ecx+5]
    mov     HswRcsOut[HSW_RO_STAGE], eax
    mov     edx, HswRcsIn[HSW_RCS_IN_SEG_DWORDS+ecx*4]
    mov     esi, OFFSET32 HswRcsIn + HSW_RCS_IN_RING
    add     esi, HswRcsTailBytes
    mov     edi, HswRcsRegion
    add     edi, HswRcsTailBytes
    push    ecx
    mov     ecx, edx
    cld
    rep     movsd
    pop     ecx
    shl     edx, 2
    add     edx, HswRcsTailBytes
    mov     HswRcsTailBytes, edx
    mov     [ebp+HSW_RCS_TAIL], edx
    mov     ebx, edx
    mov     edx, HswRcsIn[HSW_RCS_IN_SEG_WAIT+ecx*4]
    add     edx, HSW_RCS_STATUS_OFF
    mov     eax, HswRcsIn[HSW_RCS_IN_SEG_VALUE+ecx*4]
    push    ecx
    call    Hsw_Rcs_Wait
    mov     eax, ecx
    pop     ecx
    mov     HswRcsOut[HSW_RO_POLLS+ecx*4], eax
    mov     eax, [ebp+HSW_RCS_HEAD]
    mov     HswRcsOut[HSW_RO_HEADS+ecx*4], eax
    jc      short Hsw_Rcs_Seg_Timeout
    mov     eax, HSW_RCS_ST_SEG1
    shl     eax, cl
    or      HswRcsOut[HSW_RO_STATUS], eax
    inc     HswRcsSeg
    jmp     Hsw_Rcs_Seg_Next
Hsw_Rcs_Seg_Timeout:
    or      HswRcsOut[HSW_RO_STATUS], HSW_RCS_ST_TIMEOUT

    ; Stage 8: record the engine and the status page, also after a timeout.
Hsw_Rcs_Record:
    mov     eax, HswRcsSeg
    mov     HswRcsOut[HSW_RO_SEGS_DONE], eax
    mov     HswRcsOut[HSW_RO_STAGE], 8
    mov     eax, [ebp+HSW_RCS_ACTHD]
    mov     HswRcsOut[HSW_RO_ACTHD], eax
    mov     eax, [ebp+HSW_RCS_IPEIR]
    mov     HswRcsOut[HSW_RO_IPEIR], eax
    mov     eax, [ebp+HSW_RCS_IPEHR]
    mov     HswRcsOut[HSW_RO_IPEHR], eax
    mov     eax, [ebp+HSW_RCS_INSTDONE]
    mov     HswRcsOut[HSW_RO_INSTDONE], eax
    mov     eax, [ebp+HSW_RCS_BBADDR]
    mov     HswRcsOut[HSW_RO_BBADDR], eax
    mov     eax, [ebp+HSW_RCS_EIR]
    mov     HswRcsOut[HSW_RO_EIR], eax
    cld
    mov     esi, HswRcsRegion
    add     esi, HSW_RCS_STATUS_OFF
    mov     edi, OFFSET32 HswRcsOut + HSW_RO_STATUS_PAGE
    mov     ecx, 16
    rep     movsd
    mov     esi, HswRcsRegion
    add     esi, HSW_RCS_RT_OFF
    mov     edi, OFFSET32 HswRcsOut + HSW_RO_RT
    mov     ecx, HSW_RCS_RT_DWORDS
    rep     movsd

    ; Stage 9: ring off and cleared, read back.
Hsw_Rcs_Teardown:
    mov     HswRcsOut[HSW_RO_STAGE], 9
    mov     dword ptr [ebp+HSW_RCS_CTL], 0
    mov     dword ptr [ebp+HSW_RCS_HEAD], 0
    mov     dword ptr [ebp+HSW_RCS_TAIL], 0
    mov     dword ptr [ebp+HSW_RCS_START], 0
    test    HswRcsOut[HSW_RO_STATUS], HSW_RCS_ST_RING_BORROWED
    jz      short Hsw_Rcs_Torn
    mov     dword ptr [ebp+HSW_RCS_START], HSW_RCS_DRIVER_RING
    mov     dword ptr [ebp+HSW_RCS_CTL], HSW_RING_CTL_ONE_PAGE
Hsw_Rcs_Torn:
    mov     eax, [ebp+HSW_RCS_CTL]
    mov     HswRcsOut[HSW_RO_CTL_AFTER], eax
    mov     eax, [ebp+HSW_RCS_START]
    mov     HswRcsOut[HSW_RO_START_AFTER], eax
    mov     eax, [ebp+HSW_RCS_HEAD]
    mov     HswRcsOut[HSW_RO_HEAD_AFTER], eax
    mov     eax, [ebp+HSW_RCS_TAIL]
    mov     HswRcsOut[HSW_RO_TAIL_AFTER], eax
    or      HswRcsOut[HSW_RO_STATUS], HSW_RCS_ST_TORN_DOWN

    ; Forcewake back only if stage 3 asked for it.
Hsw_Rcs_Release:
    test    HswRcsOut[HSW_RO_STATUS], HSW_RCS_ST_FW_REQUESTED
    jz      short Hsw_Rcs_Exit
    pushfd
    cli
    mov     dword ptr [ebp+HSW_FW_REQUEST], HSW_FW_CLEAR
    xor     ecx, ecx
Hsw_Rcs_Release_Poll:
    mov     eax, [ebp+HSW_FW_ACK]
    test    eax, HSW_FW_ACK_BIT
    jz      short Hsw_Rcs_Released
    inc     ecx
    cmp     ecx, HSW_FW_POLLS
    jb      short Hsw_Rcs_Release_Poll
    popfd
    jmp     short Hsw_Rcs_Release_Done
Hsw_Rcs_Released:
    popfd
    or      HswRcsOut[HSW_RO_STATUS], HSW_RCS_ST_FW_RELEASED
Hsw_Rcs_Release_Done:
    mov     HswRcsOut[HSW_RO_POLLS_RELEASE], ecx
    mov     HswRcsOut[HSW_RO_ACK_AFTER], eax
    jmp     short Hsw_Rcs_Exit

Hsw_Rcs_Bad_Input:
    or      HswRcsOut[HSW_RO_STATUS], HSW_RCS_ST_BAD_INPUT
    jmp     short Hsw_Rcs_Exit
Hsw_Rcs_Map_Failed:
    or      HswRcsOut[HSW_RO_STATUS], HSW_RCS_ST_MAP_FAILED
Hsw_Rcs_Exit:
    popad
    ret
EndProc Hsw_Rcs

; ESI = DIOCParams. Copies the input, runs Hsw_Rcs, copies the result.
BeginProc Hsw_Rcs_Dioc
    pushad
    mov     ebx, esi
    mov     esi, [ebx.lpvInBuffer]
    test    esi, esi
    jz      short Hsw_Rcs_Dioc_Fail
    cmp     [ebx.cbInBuffer], HSW_RCS_IN_BYTES
    jb      short Hsw_Rcs_Dioc_Fail
    mov     edi, [ebx.lpvOutBuffer]
    test    edi, edi
    jz      short Hsw_Rcs_Dioc_Fail
    cmp     [ebx.cbOutBuffer], HSW_RCS_OUT_DWORDS * 4
    jb      short Hsw_Rcs_Dioc_Fail

    cld
    mov     edi, OFFSET32 HswRcsIn
    mov     ecx, HSW_RCS_IN_BYTES / 4
    rep     movsd
    mov     edi, OFFSET32 HswRcsOut + 4
    mov     ecx, HSW_RCS_OUT_DWORDS - 1
    xor     eax, eax
    rep     stosd

    call    Hsw_Rcs

    mov     edi, [ebx.lpvOutBuffer]
    mov     esi, OFFSET32 HswRcsOut
    mov     ecx, HSW_RCS_OUT_DWORDS
    rep     movsd
    mov     eax, [ebx.lpcbBytesReturned]
    test    eax, eax
    jz      short Hsw_Rcs_Dioc_Done
    mov     dword ptr [eax], HSW_RCS_OUT_DWORDS * 4
Hsw_Rcs_Dioc_Done:
    popad
    xor     eax, eax
    ret
Hsw_Rcs_Dioc_Fail:
    popad
    mov     eax, 1
    ret
EndProc Hsw_Rcs_Dioc

BeginProc Hsw_Reset_Result
    pushad
    mov     edi, OFFSET32 HswResult + 4
    mov     ecx, HSW_RESULT_DWORDS - 1
    xor     eax, eax
    cld
    rep     stosd
    mov     HswMmio, eax
    mov     HswGttLinear, eax
    popad
    ret
EndProc Hsw_Reset_Result

BeginProc Hsw_W32_DeviceIoControl
    cmp     ecx, DIOC_OPEN
    je      Hsw_Dioc_Ok
    cmp     ecx, DIOC_CLOSEHANDLE
    je      Hsw_Dioc_Ok
    cmp     ecx, HSW_DIOC_BCS
    jne     short Hsw_Dioc_Not_Bcs
    call    Hsw_Bcs_Dioc
    ret
Hsw_Dioc_Not_Bcs:
    cmp     ecx, HSW_DIOC_RCS
    jne     short Hsw_Dioc_Not_Rcs
    call    Hsw_Rcs_Dioc
    ret
Hsw_Dioc_Not_Rcs:
    cmp     ecx, HSW_DIOC_CAPTURE
    jne     Hsw_Dioc_Fail

    pushad
    mov     ebx, esi
    mov     ebp, [ebx.lpvInBuffer]
    test    ebp, ebp
    jz      short Hsw_Dioc_Copy_Fail
    mov     ecx, [ebx.cbInBuffer]
    cmp     ecx, HSW_IN_MIN_BYTES
    jb      short Hsw_Dioc_Copy_Fail
    mov     edi, [ebx.lpvOutBuffer]
    test    edi, edi
    jz      short Hsw_Dioc_Copy_Fail
    cmp     [ebx.cbOutBuffer], HSW_RESULT_DWORDS * 4
    jb      short Hsw_Dioc_Copy_Fail

    call    Hsw_Reset_Result
    mov     eax, [ebp+HSW_IN_BASE]
    mov     HswBase, eax
    mov     eax, [ebp+HSW_IN_FLAGS]
    mov     HswFlags, eax
    mov     HswResult[32], eax
    call    Hsw_Accept_Offsets
    test    HswResult[4], HSW_ST_OFFSETS_REFUSED
    jnz     short Hsw_Dioc_Copy_Out
    call    Hsw_Capture

Hsw_Dioc_Copy_Out:
    mov     edi, [ebx.lpvOutBuffer]
    mov     esi, OFFSET32 HswResult
    mov     ecx, HSW_RESULT_DWORDS
    cld
    rep     movsd
    mov     eax, [ebx.lpcbBytesReturned]
    test    eax, eax
    jz      short Hsw_Dioc_Copy_Done
    mov     dword ptr [eax], HSW_RESULT_DWORDS * 4
Hsw_Dioc_Copy_Done:
    popad
Hsw_Dioc_Ok:
    xor     eax, eax
    ret

Hsw_Dioc_Copy_Fail:
    popad
Hsw_Dioc_Fail:
    mov     eax, 1
    ret
EndProc Hsw_W32_DeviceIoControl

BeginProc Hsw_Dynamic_Init
    clc
    ret
EndProc Hsw_Dynamic_Init

BeginProc Hsw_Dynamic_Exit
    clc
    ret
EndProc Hsw_Dynamic_Exit

Begin_Control_Dispatch Hsw
    Control_Dispatch Sys_Dynamic_Device_Init, Hsw_Dynamic_Init
    Control_Dispatch Sys_Dynamic_Device_Exit, Hsw_Dynamic_Exit
    Control_Dispatch W32_DeviceIoControl, Hsw_W32_DeviceIoControl
End_Control_Dispatch Hsw

VxD_LOCKED_CODE_ENDS

end
