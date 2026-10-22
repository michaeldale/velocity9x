; Gen5 (Ironlake) read-only MMIO and GTT probe VxD.
;
; Phase 1 of docs\plans\intel-gen4-gen5-bringup.md, step 3 of its Order,
; for Gen5. The Gen4 probe (gen4_mmio_probe.asm) is the model; Ironlake
; differs from it in three ways that each make the Gen4 VxD wrong here:
;
; - The GTT is at GTTMMADR + 2 MB, not + 512 KB, and its size comes from
;   GGC's GGMS field on the host bridge (52h bits 11:8), not from
;   PGTBL_CTL. Linux 6.6 intel-gtt.c, i9xx_setup() ("case 5:
;   gtt_phys_addr = reg_addr + MB(2)") and i965_gtt_total_entries(). The
;   Ironlake PRM says PGTBL_CTL no longer holds the GTT's base (Vol 1
;   Part 2 3.6.1) and does not give the BAR layout.
; - The display is split. The CPU keeps pipes, planes and the panel
;   fitter (Vol 3 Part 2); DPLLs, transcoders, ports and panel power moved
;   to the PCH at C0000h-FFFFFh (Vol 3 Part 3). So the MMIO mapping is
;   1 MB, and the Gen4 windows at 6000h and 61100h (DPLLs, ADPA, LVDS,
;   panel) name different registers or none on this chip.
; - The device list is Ironlake's: 0042h desktop, 0046h mobile (Linux
;   i915_pciids.h INTEL_IRONLAKE_D_IDS / INTEL_IRONLAKE_M_IDS).
;
; Writes: none, to the device or to memory. 0CF8h is saved and restored
; around each configuration read (the IGD's ID, and GGC on the host
; bridge). Mapping a physical range with _MapPhysToLinear changes no
; device state.
;
; Offsets are accepted only inside G5_WINDOWS below. As on Gen4, no
; window can name a pipe's scan line (PIPExDSL 70000h/71000h, PIPEx_SLC
; 70004h/71004h) or frame and flip counters and time stamps
; (70040h-7004Ch, 71040h-7104Ch, TIMESTAMP_HI 70070h): reading a disabled
; pipe's counter hard-locked the 945GSE
; (docs\decisions\2026-10-15-a-disabled-pipes-counters-hang-the-945.md).
; The PRM says DSL "can be safely read any time" (Vol 3 Part 2 p144); the
; 945 measurement outranks a datasheet sentence. The live-surface
; readbacks (*SURFLIVE) are left out for the same reason. Which pipe,
; transcoder and plane registers to read is the publisher's decision,
; made from a first capture of their CONF and CNTR words.

.386p

.xlist
include VMM.INC
include VWIN32.INC
.list

Declare_Virtual_Device G5MM, 1, 0, G5_Control, \
                       Undefined_Device_ID, Undefined_Init_Order, , ,

G5_MAGIC                equ 4d4d3547h ; "G5MM"
G5_DIOC_CAPTURE         equ 1
G5_HOST_GGC_ADDRESS     equ 80000050h ; bus 0, device 0, dword holding 52h
G5_IGD_ADDRESS          equ 80001000h ; bus 0, device 2, function 0
G5_VENDOR_INTEL         equ 8086h
G5_MMIO_BYTES           equ 00100000h
G5_GTT_OFFSET           equ 00200000h
G5_GTT_COPY             equ 4096
G5_OFFSET_MAX           equ 256
G5_BASE_ALIGN           equ 003fffffh  ; 4 MB: 2 MB of MMIO, 2 MB of GTT

; GGC (host 52h) bits 11:8, GGMS, as Linux intel-agp.h G4x_GMCH_SIZE_*:
; 1 = 1 MB, 3 = 2 MB; with bit 11 (VT) set, 9 = 1 MB, 0Ah = 1.5 MB,
; 0Bh = 2 MB. Anything else is refused rather than guessed.
G5_GGMS_SHIFT           equ 24         ; within the dword read at 50h
G5_GGMS_MASK            equ 0fh

; Status bits returned to the publisher.
G5_ST_IGD_INTEL         equ 00000001h
G5_ST_BASE_INVALID      equ 00000002h
G5_ST_MMIO_MAPPED       equ 00000004h
G5_ST_GTT_MAPPED        equ 00000008h
G5_ST_OFFSETS_REFUSED   equ 00000010h
G5_ST_SNAPSHOT_TAKEN    equ 00000020h
G5_ST_GTT_READ          equ 00000040h
G5_ST_NOT_GEN5          equ 00000080h
G5_ST_GTT_SIZE_UNKNOWN  equ 00000100h

; Input from V9XG5MM.EXE.
G5_IN_BASE              equ 0
G5_IN_COUNT             equ 4
G5_IN_OFFSETS           equ 8
G5_IN_MIN_BYTES         equ 8

; Output layout, in dwords. Must match struct v9x_g5mm_result in
; gen5_mmio_probe_win32.c.
G5_HEADER_DWORDS        equ 16
G5_RESULT_DWORDS        equ (G5_HEADER_DWORDS + G5_OFFSET_MAX * 3 + \
                             G5_GTT_COPY)

VxD_LOCKED_DATA_SEG
G5Devices label word
    dw 0042h, 0046h
G5_DEVICE_COUNT         equ 2

; GTT bytes by GGMS value 0-15; 0 means refused.
G5GttBytes label dword
    dd 0, 00100000h, 0, 00200000h, 0, 0, 0, 0
    dd 0, 00100000h, 00180000h, 00200000h, 0, 0, 0, 0

; Accepted windows: start, limit (exclusive). Offsets within 1 MB. Pages
; are the Ironlake PRM's; "i915" marks offsets it does not give.
G5Windows label dword
    dd 002000h, 002200h   ; render command streamer, errors (Vol 1 Part 3)
    dd 004000h, 0040c0h   ; BSD command streamer (Vol 1 Part 1 p70)
    dd 041000h, 041004h   ; VGACNTRL (Vol 3 Part 2 p12)
    dd 042004h, 042008h   ; ILK_DISPLAY_CHICKEN2 (i915)
    dd 042020h, 042024h   ; ILK_DSPCLK_GATE_D (i915)
    dd 044000h, 044044h   ; DE/GT/PM interrupts, hotplug, GTT fault (p30-45)
    dd 045000h, 045008h   ; DISP_ARB_CTL, CTL2 (p49-51)
    dd 045100h, 045124h   ; WM0 A/B, WM1-3, WM1S (p53-57)
    dd 046000h, 046034h   ; FDI/DP PLL BIOS, 3D clock gating (p58-71)
    dd 048250h, 048258h   ; BLC_PWM_CTL2, BLC_PWM_CTL (p76-77)
    dd 060000h, 060020h   ; pipe A timings, PIPEASRC (p109-115)
    dd 060028h, 060050h   ; VSYNCSHIFT_A, pipe A M/N (p116-121)
    dd 061000h, 061020h   ; pipe B timings, PIPEBSRC (i915)
    dd 061028h, 061050h   ; VSYNCSHIFT_B (i915), pipe B M/N (p122-126)
    dd 068060h, 068064h   ; PF_PWR_GATE_CTRL (p126)
    dd 068070h, 068078h   ; PF A window position and size (p131-133)
    dd 068080h, 068084h   ; PF_CTRL_1 A (p134)
    dd 068870h, 068878h   ; PF B window position and size (p132-136)
    dd 068880h, 068884h   ; PF_CTRL_1 B
    dd 070008h, 07000ch   ; PIPEACONF (p146)
    dd 070180h, 0701ach   ; plane A, short of DSPASURFLIVE (p176-183)
    dd 071008h, 07100ch   ; PIPEBCONF (p156)
    dd 071180h, 0711ach   ; plane B, short of DSPBSURFLIVE (p184-190)
    dd 0c2020h, 0c2024h   ; SOUTH_DSPCLK_GATE_D (i915)
    dd 0c4000h, 0c4010h   ; SDE interrupts (Vol 3 Part 3 p13-16)
    dd 0c4030h, 0c4034h   ; PCH hotplug (p17)
    dd 0c6014h, 0c601ch   ; DPLLA_CTRL, DPLLB_CTRL (p39-42)
    dd 0c6040h, 0c6050h   ; FPA0/1, FPB0/1 (p45-49)
    dd 0c6200h, 0c6208h   ; DREF_CONTROL, RAWCLK_FREQ (p50-53)
    dd 0c7200h, 0c7214h   ; panel power sequencing (p54-59)
    dd 0c8250h, 0c8258h   ; PCH backlight (p60-62)
    dd 0e0000h, 0e0018h   ; transcoder A timings (p63-68)
    dd 0e0020h, 0e0024h   ; TRANS_BCLRPAT_A (p69)
    dd 0e0028h, 0e0050h   ; TRANS_VSYNCSHIFT_A, transcoder A M/N (p70-74)
    dd 0e1000h, 0e1018h   ; transcoder B timings (p82-85)
    dd 0e1020h, 0e1024h   ; TRANS_BCLRPAT_B (p85)
    dd 0e1028h, 0e1050h   ; TRANS_VSYNCSHIFT_B, transcoder B M/N (p86-90)
    dd 0e1100h, 0e1104h   ; ADPA, the CRT DAC (p93)
    dd 0e1140h, 0e1144h   ; HDMIB (i915 PCH_SDVOB)
    dd 0e1150h, 0e1154h   ; HDMIC (p97)
    dd 0e1160h, 0e1164h   ; HDMID (p101)
    dd 0e1180h, 0e1184h   ; LVDS (p105)
    dd 0e4100h, 0e4104h   ; DPB (p163)
    dd 0e4200h, 0e4204h   ; DPC (p172)
    dd 0e4300h, 0e4304h   ; DPD (p179)
    dd 0f0008h, 0f001ch   ; TRANSACONF, FDI_RXA CTL/MISC/ISR/IMR (p111-120)
    dd 0f1008h, 0f101ch   ; TRANSBCONF, FDI_RXB (p113-126)
G5_WINDOW_COUNT         equ 47

G5Result label dword
    dd G5_MAGIC                 ; 00 magic
    dd 0                        ; 01 status
    dd 0                        ; 02 IGD vendor/device as read
    dd 0                        ; 03 GTTMMADR base used
    dd 0                        ; 04 offsets accepted
    dd 0                        ; 05 valid GTT entries, whole table
    dd 0                        ; 06 0CF8h found and restored
    dd 0                        ; 07 first offset refused, if any
    dd 0                        ; 08 host dword 50h as read (GGC in 31:16)
    dd 0                        ; 09 GTT bytes mapped
    dd 0                        ; 10 GTT entries in the table
    dd 0                        ; 11 reserved
    dd 0                        ; 12 reserved
    dd 0                        ; 13 reserved
    dd 0                        ; 14 reserved
    dd 0                        ; 15 reserved
G5Offsets    dd G5_OFFSET_MAX dup (0)
G5First      dd G5_OFFSET_MAX dup (0)
G5Second     dd G5_OFFSET_MAX dup (0)
G5Gtt        dd G5_GTT_COPY dup (0)
G5Base       dd 0
G5Mmio       dd 0
G5GttLinear  dd 0
VxD_LOCKED_DATA_ENDS

VxD_LOCKED_CODE_SEG

; EAX = PCI configuration address. Returns EAX = configuration dword.
; 0CF8h saved and restored with interrupts off.
BeginProc G5_Pci_Read
    push    ebx
    push    edx
    pushfd
    cli
    mov     ebx, eax
    mov     dx, 0cf8h
    in      eax, dx
    mov     G5Result[24], eax
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
EndProc G5_Pci_Read

; EDX = offset. Returns CF clear if it lies in an accepted window.
BeginProc G5_Offset_Allowed
    push    ecx
    push    esi
    test    edx, 3
    jnz     short G5_Offset_Refused
    mov     esi, OFFSET32 G5Windows
    mov     ecx, G5_WINDOW_COUNT
G5_Offset_Next:
    cmp     edx, [esi]
    jb      short G5_Offset_Skip
    cmp     edx, [esi+4]
    jb      short G5_Offset_Accepted
G5_Offset_Skip:
    add     esi, 8
    dec     ecx
    jnz     short G5_Offset_Next
G5_Offset_Refused:
    pop     esi
    pop     ecx
    stc
    ret
G5_Offset_Accepted:
    pop     esi
    pop     ecx
    clc
    ret
EndProc G5_Offset_Allowed

; AX = device id. Returns CF clear if it is in G5Devices.
BeginProc G5_Device_Known
    push    ecx
    push    esi
    mov     esi, OFFSET32 G5Devices
    mov     ecx, G5_DEVICE_COUNT
G5_Device_Next:
    cmp     ax, [esi]
    je      short G5_Device_Found
    add     esi, 2
    dec     ecx
    jnz     short G5_Device_Next
    pop     esi
    pop     ecx
    stc
    ret
G5_Device_Found:
    pop     esi
    pop     ecx
    clc
    ret
EndProc G5_Device_Known

; EBP = DIOC input buffer, ECX = its byte count. Copies the offset list,
; refusing the whole list if any entry is unaligned or outside the windows.
BeginProc G5_Accept_Offsets
    pushad
    mov     G5Result[16], 0
    sub     ecx, G5_IN_OFFSETS
    shr     ecx, 2
    mov     eax, [ebp+G5_IN_COUNT]
    cmp     eax, G5_OFFSET_MAX
    ja      short G5_Accept_Refused
    cmp     eax, ecx
    ja      short G5_Accept_Refused
    mov     ecx, eax
    lea     esi, [ebp+G5_IN_OFFSETS]
    mov     edi, OFFSET32 G5Offsets
    jecxz   short G5_Accept_Done
G5_Accept_Next:
    mov     edx, [esi]
    call    G5_Offset_Allowed
    jc      short G5_Accept_Refused_At
    mov     [edi], edx
    add     esi, 4
    add     edi, 4
    dec     ecx
    jnz     short G5_Accept_Next
G5_Accept_Done:
    mov     eax, [ebp+G5_IN_COUNT]
    mov     G5Result[16], eax
    popad
    ret
G5_Accept_Refused_At:
    mov     G5Result[28], edx
G5_Accept_Refused:
    or      G5Result[4], G5_ST_OFFSETS_REFUSED
    popad
    ret
EndProc G5_Accept_Offsets

; EDI = destination. Reads every accepted offset through the MMIO mapping.
BeginProc G5_Read_Snapshot
    pushad
    mov     ecx, G5Result[16]
    mov     ebx, OFFSET32 G5Offsets
    mov     esi, G5Mmio
    jecxz   short G5_Read_Snapshot_Done
G5_Read_Snapshot_Next:
    mov     eax, [ebx]
    mov     eax, [esi+eax]
    mov     [edi], eax
    add     ebx, 4
    add     edi, 4
    dec     ecx
    jnz     short G5_Read_Snapshot_Next
G5_Read_Snapshot_Done:
    popad
    ret
EndProc G5_Read_Snapshot

; Copy the first G5_GTT_COPY entries and count valid ones (bit 0) in the
; whole table, G5Result[40] entries.
BeginProc G5_Read_Gtt
    pushad
    mov     esi, G5GttLinear
    mov     edi, OFFSET32 G5Gtt
    mov     ecx, G5Result[40]
    cmp     ecx, G5_GTT_COPY
    jbe     short G5_Read_Gtt_Copy
    mov     ecx, G5_GTT_COPY
G5_Read_Gtt_Copy:
    cld
    rep     movsd

    mov     esi, G5GttLinear
    mov     ecx, G5Result[40]
    xor     edx, edx
G5_Read_Gtt_Next:
    test    dword ptr [esi], 1
    jz      short G5_Read_Gtt_Skip
    inc     edx
G5_Read_Gtt_Skip:
    add     esi, 4
    dec     ecx
    jnz     short G5_Read_Gtt_Next
    mov     G5Result[20], edx
    or      G5Result[4], G5_ST_GTT_READ
    popad
    ret
EndProc G5_Read_Gtt

BeginProc G5_Capture
    pushad

    ; The base must belong to an Ironlake IGD at 00:02.0 and carry the
    ; BAR's own 4 MB alignment; anything else is refused before it is
    ; mapped.
    mov     eax, G5_IGD_ADDRESS
    call    G5_Pci_Read
    mov     G5Result[8], eax
    cmp     ax, G5_VENDOR_INTEL
    jne     G5_Capture_Bad_Base
    or      G5Result[4], G5_ST_IGD_INTEL
    shr     eax, 16
    call    G5_Device_Known
    jnc     short G5_Capture_Check_Base
    or      G5Result[4], G5_ST_NOT_GEN5
    jmp     G5_Capture_Done

G5_Capture_Check_Base:
    mov     eax, G5Base
    test    eax, eax
    jz      G5_Capture_Bad_Base
    test    eax, G5_BASE_ALIGN
    jnz     G5_Capture_Bad_Base
    mov     G5Result[12], eax

    VMMcall _MapPhysToLinear,<eax,G5_MMIO_BYTES,0>
    cmp     eax, 0ffffffffh
    je      G5_Capture_Done
    mov     G5Mmio, eax
    or      G5Result[4], G5_ST_MMIO_MAPPED

    mov     edi, OFFSET32 G5First
    call    G5_Read_Snapshot
    mov     edi, OFFSET32 G5Second
    call    G5_Read_Snapshot
    or      G5Result[4], G5_ST_SNAPSHOT_TAKEN

    ; Size the GTT from GGMS. A value the table does not define is
    ; reported, not mapped.
    mov     eax, G5_HOST_GGC_ADDRESS
    call    G5_Pci_Read
    mov     G5Result[32], eax
    shr     eax, G5_GGMS_SHIFT
    and     eax, G5_GGMS_MASK
    mov     ecx, G5GttBytes[eax*4]
    test    ecx, ecx
    jnz     short G5_Capture_Gtt_Size
    or      G5Result[4], G5_ST_GTT_SIZE_UNKNOWN
    jmp     short G5_Capture_Done

G5_Capture_Gtt_Size:
    mov     G5Result[36], ecx
    mov     eax, ecx
    shr     eax, 2
    mov     G5Result[40], eax
    mov     eax, G5Base
    add     eax, G5_GTT_OFFSET
    VMMcall _MapPhysToLinear,<eax,ecx,0>
    cmp     eax, 0ffffffffh
    je      short G5_Capture_Done
    mov     G5GttLinear, eax
    or      G5Result[4], G5_ST_GTT_MAPPED
    call    G5_Read_Gtt
    jmp     short G5_Capture_Done

G5_Capture_Bad_Base:
    or      G5Result[4], G5_ST_BASE_INVALID
G5_Capture_Done:
    popad
    ret
EndProc G5_Capture

BeginProc G5_Reset_Result
    pushad
    mov     edi, OFFSET32 G5Result + 4
    mov     ecx, G5_RESULT_DWORDS - 1
    xor     eax, eax
    cld
    rep     stosd
    mov     G5Mmio, eax
    mov     G5GttLinear, eax
    popad
    ret
EndProc G5_Reset_Result

BeginProc G5_W32_DeviceIoControl
    cmp     ecx, DIOC_OPEN
    je      G5_Dioc_Ok
    cmp     ecx, DIOC_CLOSEHANDLE
    je      G5_Dioc_Ok
    cmp     ecx, G5_DIOC_CAPTURE
    jne     G5_Dioc_Fail

    pushad
    mov     ebx, esi
    mov     ebp, [ebx.lpvInBuffer]
    test    ebp, ebp
    jz      short G5_Dioc_Copy_Fail
    mov     ecx, [ebx.cbInBuffer]
    cmp     ecx, G5_IN_MIN_BYTES
    jb      short G5_Dioc_Copy_Fail
    mov     edi, [ebx.lpvOutBuffer]
    test    edi, edi
    jz      short G5_Dioc_Copy_Fail
    cmp     [ebx.cbOutBuffer], G5_RESULT_DWORDS * 4
    jb      short G5_Dioc_Copy_Fail

    call    G5_Reset_Result
    mov     eax, [ebp+G5_IN_BASE]
    mov     G5Base, eax
    call    G5_Accept_Offsets
    test    G5Result[4], G5_ST_OFFSETS_REFUSED
    jnz     short G5_Dioc_Copy_Out
    call    G5_Capture

G5_Dioc_Copy_Out:
    mov     edi, [ebx.lpvOutBuffer]
    mov     esi, OFFSET32 G5Result
    mov     ecx, G5_RESULT_DWORDS
    cld
    rep     movsd
    mov     eax, [ebx.lpcbBytesReturned]
    test    eax, eax
    jz      short G5_Dioc_Copy_Done
    mov     dword ptr [eax], G5_RESULT_DWORDS * 4
G5_Dioc_Copy_Done:
    popad
G5_Dioc_Ok:
    xor     eax, eax
    ret

G5_Dioc_Copy_Fail:
    popad
G5_Dioc_Fail:
    mov     eax, 1
    ret
EndProc G5_W32_DeviceIoControl

BeginProc G5_Dynamic_Init
    clc
    ret
EndProc G5_Dynamic_Init

BeginProc G5_Dynamic_Exit
    clc
    ret
EndProc G5_Dynamic_Exit

Begin_Control_Dispatch G5
    Control_Dispatch Sys_Dynamic_Device_Init, G5_Dynamic_Init
    Control_Dispatch Sys_Dynamic_Device_Exit, G5_Dynamic_Exit
    Control_Dispatch W32_DeviceIoControl, G5_W32_DeviceIoControl
End_Control_Dispatch G5

VxD_LOCKED_CODE_ENDS

end
