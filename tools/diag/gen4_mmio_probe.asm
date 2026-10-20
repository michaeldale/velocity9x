; Gen4 (965 / GM965) read-only MMIO and GTT probe VxD.
;
; Phase 1 of docs\plans\intel-gen4-gen5-bringup.md, step 3 of its Order.
; Loaded dynamically by V9XG4MM.EXE, which passes the GTTMMADR base it read
; from config space and a list of register offsets. This maps the 512 KB
; MMIO half of GTTMMADR and the GTT above it - datasheet 316273 21.1.10:
; "The base address of Global GTT is located in the Memory Base Address +
; 512 KB", 1 MB in all - reads every listed offset twice, copies the first
; G4_GTT_COPY GTT entries, and counts the valid entries in the whole table.
;
; Writes: none, to the device or to memory. 0CF8h is saved and restored
; around the one configuration read (the IGD's ID, to confirm what the base
; belongs to). Mapping a physical range with _MapPhysToLinear changes no
; device state.
;
; The GTT's size is PGTBL_CTL (2020h) bits 3:1, 965 PRM Vol 1 8.2.1.1:
; 000 512 KB, 001 256 KB, 010 128 KB; the 1, 1.5 and 2 MB encodings are
; "Reserved" on the 965 (G4x uses them, with its GTT at +2 MB instead), so
; they are refused rather than mapped past the BAR. The device list is the
; 965 family only - Broadwater and Crestline - for the same reason: a G45
; at this layout would be read at the wrong offset.
;
; Offsets are accepted only inside G4_WINDOWS below. The 945's lesson is
; why there is an allowlist and why it has holes: a CPU read of a disabled
; pipe's line counter (PIPExDSL, 70000h/71000h) or frame counter
; (PIPExFRAMEHIGH/PIXEL, 70040h/71040h) hard-locked the 945GSE
; (docs\decisions\2026-10-15-a-disabled-pipes-counters-hang-the-945.md).
; None of those can be named here, live pipe or not. Which pipe's timing,
; clock and plane registers to read is the publisher's decision, made from
; a first capture of PIPECONF.

.386p

.xlist
include VMM.INC
include VWIN32.INC
.list

Declare_Virtual_Device G4MM, 1, 0, G4_Control, \
                       Undefined_Device_ID, Undefined_Init_Order, , ,

G4_MAGIC                equ 4d4d3447h ; "G4MM"
G4_DIOC_CAPTURE         equ 1
G4_IGD_ADDRESS          equ 80001000h ; bus 0, device 2, function 0
G4_VENDOR_INTEL         equ 8086h
G4_MMIO_BYTES           equ 00080000h
G4_GTT_OFFSET           equ 00080000h
G4_GTT_MAX_BYTES        equ 00080000h
G4_GTT_COPY             equ 4096
G4_OFFSET_MAX           equ 256
G4_BASE_ALIGN           equ 000fffffh  ; 1 MB, the BAR's own alignment
G4_PGTBL_CTL            equ 2020h
G4_PGTBL_SIZE_MASK      equ 0000000eh

; Status bits returned to the publisher.
G4_ST_IGD_INTEL         equ 00000001h
G4_ST_BASE_INVALID      equ 00000002h
G4_ST_MMIO_MAPPED       equ 00000004h
G4_ST_GTT_MAPPED        equ 00000008h
G4_ST_OFFSETS_REFUSED   equ 00000010h
G4_ST_SNAPSHOT_TAKEN    equ 00000020h
G4_ST_GTT_READ          equ 00000040h
G4_ST_NOT_GEN4          equ 00000080h
G4_ST_GTT_SIZE_RESERVED equ 00000100h

; Input from V9XG4MM.EXE.
G4_IN_BASE              equ 0
G4_IN_COUNT             equ 4
G4_IN_OFFSETS           equ 8
G4_IN_MIN_BYTES         equ 8

; Output layout, in dwords. Must match struct v9x_g4mm_result in
; gen4_mmio_probe_win32.c.
G4_HEADER_DWORDS        equ 16
G4_RESULT_DWORDS        equ (G4_HEADER_DWORDS + G4_OFFSET_MAX * 3 + \
                             G4_GTT_COPY)

VxD_LOCKED_DATA_SEG
; Gen4 IGD device ids, function 0 (Linux i915_pciids.h INTEL_I965G_IDS and
; INTEL_I965GM_IDS): 946GZ 2972, G35 2982, Q965/Q963 2992, G965 29A2,
; GM965/GL960 2A02, GME965/GLE960 2A12.
G4Devices label word
    dw 2972h, 2982h, 2992h, 29a2h, 2a02h, 2a12h
G4_DEVICE_COUNT         equ 6

; Accepted windows: start, limit (exclusive). Offsets within 512 KB.
G4Windows label dword
    dd 002000h, 002200h   ; render command streamer, PGTBL, errors, FW_BLC
    dd 006000h, 006024h   ; DPLL_A/B and their MD registers
    dd 006040h, 006050h   ; FPA0/1, FPB0/1
    dd 006200h, 006218h   ; clock gating: DSPCLK, RENCLK D1/D2, RAMCLK, DEUC
    dd 060000h, 060020h   ; pipe A timings, PIPEASRC
    dd 061000h, 061020h   ; pipe B timings, PIPEBSRC
    dd 061100h, 061300h   ; ADPA, hotplug, SDVO, LVDS, panel power, pfit, BLC
    dd 070008h, 07000ch   ; PIPEACONF
    dd 070024h, 070028h   ; PIPEASTAT
    dd 070030h, 070040h   ; DSPARB, DSPFW1-3; stops short of 70040h
    dd 070180h, 0701b0h   ; plane A
    dd 071008h, 07100ch   ; PIPEBCONF
    dd 071024h, 071028h   ; PIPEBSTAT
    dd 071180h, 0711b0h   ; plane B
    dd 071400h, 071404h   ; VGACNTRL
G4_WINDOW_COUNT         equ 15

G4Result label dword
    dd G4_MAGIC                 ; 00 magic
    dd 0                        ; 01 status
    dd 0                        ; 02 IGD vendor/device as read
    dd 0                        ; 03 GTTMMADR base used
    dd 0                        ; 04 offsets accepted
    dd 0                        ; 05 valid GTT entries, whole table
    dd 0                        ; 06 0CF8h found and restored
    dd 0                        ; 07 first offset refused, if any
    dd 0                        ; 08 PGTBL_CTL as read
    dd 0                        ; 09 GTT bytes mapped
    dd 0                        ; 10 GTT entries in the table
    dd 0                        ; 11 reserved
    dd 0                        ; 12 reserved
    dd 0                        ; 13 reserved
    dd 0                        ; 14 reserved
    dd 0                        ; 15 reserved
G4Offsets    dd G4_OFFSET_MAX dup (0)
G4First      dd G4_OFFSET_MAX dup (0)
G4Second     dd G4_OFFSET_MAX dup (0)
G4Gtt        dd G4_GTT_COPY dup (0)
G4Base       dd 0
G4Mmio       dd 0
G4GttLinear  dd 0
VxD_LOCKED_DATA_ENDS

VxD_LOCKED_CODE_SEG

; EAX = PCI configuration address. Returns EAX = configuration dword.
; 0CF8h saved and restored with interrupts off.
BeginProc G4_Pci_Read
    push    ebx
    push    edx
    pushfd
    cli
    mov     ebx, eax
    mov     dx, 0cf8h
    in      eax, dx
    mov     G4Result[24], eax
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
EndProc G4_Pci_Read

; EDX = offset. Returns CF clear if it lies in an accepted window.
BeginProc G4_Offset_Allowed
    push    ecx
    push    esi
    test    edx, 3
    jnz     short G4_Offset_Refused
    mov     esi, OFFSET32 G4Windows
    mov     ecx, G4_WINDOW_COUNT
G4_Offset_Next:
    cmp     edx, [esi]
    jb      short G4_Offset_Skip
    cmp     edx, [esi+4]
    jb      short G4_Offset_Accepted
G4_Offset_Skip:
    add     esi, 8
    dec     ecx
    jnz     short G4_Offset_Next
G4_Offset_Refused:
    pop     esi
    pop     ecx
    stc
    ret
G4_Offset_Accepted:
    pop     esi
    pop     ecx
    clc
    ret
EndProc G4_Offset_Allowed

; AX = device id. Returns CF clear if it is in G4Devices.
BeginProc G4_Device_Known
    push    ecx
    push    esi
    mov     esi, OFFSET32 G4Devices
    mov     ecx, G4_DEVICE_COUNT
G4_Device_Next:
    cmp     ax, [esi]
    je      short G4_Device_Found
    add     esi, 2
    dec     ecx
    jnz     short G4_Device_Next
    pop     esi
    pop     ecx
    stc
    ret
G4_Device_Found:
    pop     esi
    pop     ecx
    clc
    ret
EndProc G4_Device_Known

; EBP = DIOC input buffer, ECX = its byte count. Copies the offset list,
; refusing the whole list if any entry is unaligned or outside the windows.
BeginProc G4_Accept_Offsets
    pushad
    mov     G4Result[16], 0
    sub     ecx, G4_IN_OFFSETS
    shr     ecx, 2
    mov     eax, [ebp+G4_IN_COUNT]
    cmp     eax, G4_OFFSET_MAX
    ja      short G4_Accept_Refused
    cmp     eax, ecx
    ja      short G4_Accept_Refused
    mov     ecx, eax
    lea     esi, [ebp+G4_IN_OFFSETS]
    mov     edi, OFFSET32 G4Offsets
    jecxz   short G4_Accept_Done
G4_Accept_Next:
    mov     edx, [esi]
    call    G4_Offset_Allowed
    jc      short G4_Accept_Refused_At
    mov     [edi], edx
    add     esi, 4
    add     edi, 4
    dec     ecx
    jnz     short G4_Accept_Next
G4_Accept_Done:
    mov     eax, [ebp+G4_IN_COUNT]
    mov     G4Result[16], eax
    popad
    ret
G4_Accept_Refused_At:
    mov     G4Result[28], edx
G4_Accept_Refused:
    or      G4Result[4], G4_ST_OFFSETS_REFUSED
    popad
    ret
EndProc G4_Accept_Offsets

; EDI = destination. Reads every accepted offset through the MMIO mapping.
BeginProc G4_Read_Snapshot
    pushad
    mov     ecx, G4Result[16]
    mov     ebx, OFFSET32 G4Offsets
    mov     esi, G4Mmio
    jecxz   short G4_Read_Snapshot_Done
G4_Read_Snapshot_Next:
    mov     eax, [ebx]
    mov     eax, [esi+eax]
    mov     [edi], eax
    add     ebx, 4
    add     edi, 4
    dec     ecx
    jnz     short G4_Read_Snapshot_Next
G4_Read_Snapshot_Done:
    popad
    ret
EndProc G4_Read_Snapshot

; Copy the first G4_GTT_COPY entries (fewer if the table is smaller) and
; count valid ones (bit 0) in the whole table, G4Result[40] entries.
BeginProc G4_Read_Gtt
    pushad
    mov     esi, G4GttLinear
    mov     edi, OFFSET32 G4Gtt
    mov     ecx, G4Result[40]
    cmp     ecx, G4_GTT_COPY
    jbe     short G4_Read_Gtt_Copy
    mov     ecx, G4_GTT_COPY
G4_Read_Gtt_Copy:
    cld
    rep     movsd

    mov     esi, G4GttLinear
    mov     ecx, G4Result[40]
    xor     edx, edx
G4_Read_Gtt_Next:
    test    dword ptr [esi], 1
    jz      short G4_Read_Gtt_Skip
    inc     edx
G4_Read_Gtt_Skip:
    add     esi, 4
    dec     ecx
    jnz     short G4_Read_Gtt_Next
    mov     G4Result[20], edx
    or      G4Result[4], G4_ST_GTT_READ
    popad
    ret
EndProc G4_Read_Gtt

BeginProc G4_Capture
    pushad

    ; The base must belong to a Gen4 Intel IGD at 00:02.0 and carry the
    ; BAR's own 1 MB alignment; anything else is refused before it is
    ; mapped.
    mov     eax, G4_IGD_ADDRESS
    call    G4_Pci_Read
    mov     G4Result[8], eax
    cmp     ax, G4_VENDOR_INTEL
    jne     G4_Capture_Bad_Base
    or      G4Result[4], G4_ST_IGD_INTEL
    shr     eax, 16
    call    G4_Device_Known
    jnc     short G4_Capture_Check_Base
    or      G4Result[4], G4_ST_NOT_GEN4
    jmp     G4_Capture_Done

G4_Capture_Check_Base:
    mov     eax, G4Base
    test    eax, eax
    jz      G4_Capture_Bad_Base
    test    eax, G4_BASE_ALIGN
    jnz     G4_Capture_Bad_Base
    mov     G4Result[12], eax

    VMMcall _MapPhysToLinear,<eax,G4_MMIO_BYTES,0>
    cmp     eax, 0ffffffffh
    je      G4_Capture_Done
    mov     G4Mmio, eax
    or      G4Result[4], G4_ST_MMIO_MAPPED

    mov     edi, OFFSET32 G4First
    call    G4_Read_Snapshot
    mov     edi, OFFSET32 G4Second
    call    G4_Read_Snapshot
    or      G4Result[4], G4_ST_SNAPSHOT_TAKEN

    ; Size the GTT from PGTBL_CTL. Only 512, 256 and 128 KB are defined.
    mov     esi, G4Mmio
    mov     eax, [esi+G4_PGTBL_CTL]
    mov     G4Result[32], eax
    and     eax, G4_PGTBL_SIZE_MASK
    mov     ecx, G4_GTT_MAX_BYTES
    cmp     eax, 0
    je      short G4_Capture_Gtt_Size
    shr     ecx, 1
    cmp     eax, 2
    je      short G4_Capture_Gtt_Size
    shr     ecx, 1
    cmp     eax, 4
    je      short G4_Capture_Gtt_Size
    or      G4Result[4], G4_ST_GTT_SIZE_RESERVED
    jmp     short G4_Capture_Done

G4_Capture_Gtt_Size:
    mov     G4Result[36], ecx
    mov     eax, ecx
    shr     eax, 2
    mov     G4Result[40], eax
    mov     eax, G4Base
    add     eax, G4_GTT_OFFSET
    VMMcall _MapPhysToLinear,<eax,ecx,0>
    cmp     eax, 0ffffffffh
    je      short G4_Capture_Done
    mov     G4GttLinear, eax
    or      G4Result[4], G4_ST_GTT_MAPPED
    call    G4_Read_Gtt
    jmp     short G4_Capture_Done

G4_Capture_Bad_Base:
    or      G4Result[4], G4_ST_BASE_INVALID
G4_Capture_Done:
    popad
    ret
EndProc G4_Capture

BeginProc G4_Reset_Result
    pushad
    mov     edi, OFFSET32 G4Result + 4
    mov     ecx, G4_RESULT_DWORDS - 1
    xor     eax, eax
    cld
    rep     stosd
    mov     G4Mmio, eax
    mov     G4GttLinear, eax
    popad
    ret
EndProc G4_Reset_Result

BeginProc G4_W32_DeviceIoControl
    cmp     ecx, DIOC_OPEN
    je      G4_Dioc_Ok
    cmp     ecx, DIOC_CLOSEHANDLE
    je      G4_Dioc_Ok
    cmp     ecx, G4_DIOC_CAPTURE
    jne     G4_Dioc_Fail

    pushad
    mov     ebx, esi
    mov     ebp, [ebx.lpvInBuffer]
    test    ebp, ebp
    jz      short G4_Dioc_Copy_Fail
    mov     ecx, [ebx.cbInBuffer]
    cmp     ecx, G4_IN_MIN_BYTES
    jb      short G4_Dioc_Copy_Fail
    mov     edi, [ebx.lpvOutBuffer]
    test    edi, edi
    jz      short G4_Dioc_Copy_Fail
    cmp     [ebx.cbOutBuffer], G4_RESULT_DWORDS * 4
    jb      short G4_Dioc_Copy_Fail

    call    G4_Reset_Result
    mov     eax, [ebp+G4_IN_BASE]
    mov     G4Base, eax
    call    G4_Accept_Offsets
    test    G4Result[4], G4_ST_OFFSETS_REFUSED
    jnz     short G4_Dioc_Copy_Out
    call    G4_Capture

G4_Dioc_Copy_Out:
    mov     edi, [ebx.lpvOutBuffer]
    mov     esi, OFFSET32 G4Result
    mov     ecx, G4_RESULT_DWORDS
    cld
    rep     movsd
    mov     eax, [ebx.lpcbBytesReturned]
    test    eax, eax
    jz      short G4_Dioc_Copy_Done
    mov     dword ptr [eax], G4_RESULT_DWORDS * 4
G4_Dioc_Copy_Done:
    popad
G4_Dioc_Ok:
    xor     eax, eax
    ret

G4_Dioc_Copy_Fail:
    popad
G4_Dioc_Fail:
    mov     eax, 1
    ret
EndProc G4_W32_DeviceIoControl

BeginProc G4_Dynamic_Init
    clc
    ret
EndProc G4_Dynamic_Init

BeginProc G4_Dynamic_Exit
    clc
    ret
EndProc G4_Dynamic_Exit

Begin_Control_Dispatch G4
    Control_Dispatch Sys_Dynamic_Device_Init, G4_Dynamic_Init
    Control_Dispatch Sys_Dynamic_Device_Exit, G4_Dynamic_Exit
    Control_Dispatch W32_DeviceIoControl, G4_W32_DeviceIoControl
End_Control_Dispatch G4

VxD_LOCKED_CODE_ENDS

end
