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
    je      short Hsw_Dioc_Ok
    cmp     ecx, DIOC_CLOSEHANDLE
    je      short Hsw_Dioc_Ok
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
