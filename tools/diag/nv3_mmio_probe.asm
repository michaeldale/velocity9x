; NVIDIA RIVA 128 (NV3) register probe VxD.
;
; Phase 2 of docs\plans\nvidia-riva128.md. Maps BAR0, the 16 MiB register
; window, and two 32 KiB windows of BAR1, the framebuffer, and reads a
; caller's list of offsets, each twice. The Rage 128 probe
; (r128_mmio_probe.asm) is the model.
;
; Writes: no register, ever. The capture writes nothing at all. The alias
; test (N3_DIOC_ALIAS, below) writes 32 dwords of video memory in the
; top-of-VRAM window and puts the saved words back. 0CF8h is saved and
; restored around each configuration read. Mapping a physical range with
; _MapPhysToLinear changes no device state.
;
; The caller names the device by its configuration address; the VxD reads
; the id and both BARs itself and refuses anything that is not PCI
; 12D2:0018 with 16 MiB-aligned memory BARs. Once BAR0 is mapped,
; PMC_BOOT_0 (BAR0 + 0) must name chip 3 in bits 19:16 (envytools
; docs/hw/bus/pmc.rst), or nothing else is read: a window that decodes
; something else is never sampled.
;
; Offsets are accepted only inside N3Windows below. An offset with bit 31
; set names BAR1 instead: N3_TOP_START for the top 32 KiB of an 8 MiB
; card, where 86Box's NV3 model keeps RAMIN, and N3_RAMIN_START, where
; Linux's rivafb maps RAMIN (fbdev.c, rivafb_probe). Left out on purpose:
;   0C0000h-0C0FFFh  PRMVIO, the VGA sequencer and graphics ports, indexed;
;   601000h-601FFFh  PRMCIO, the VGA CRTC ports, indexed, and input status
;                    1, whose read resets the attribute flip-flop;
;   681000h-681FFFh  PRMDIO, the palette ports, whose reads advance;
;   003300h and up   CACHE1's method and data arrays;
;   everything else in the USER area but the channel 0 free count.
; PFIFO and PGRAPH are in the list but V9XN3MM.EXE asks for them only when
; PMC_ENABLE shows them on: an engine whose enable bit is off "disappears
; from MMIO space" (pmc.rst). Offsets are Linux riva_hw.c's and envytools'
; rnndb (nv3_pgraph.xml, nv1_pfifo.xml, nv3_pfb.xml).

.386p

.xlist
include VMM.INC
include VWIN32.INC
.list

Declare_Virtual_Device N3MM, 1, 0, N3_Control, \
                       Undefined_Device_ID, Undefined_Init_Order, , ,

N3_MAGIC                equ 4d4d334eh ; "N3MM"
N3_DIOC_CAPTURE         equ 1
N3_DIOC_ALIAS           equ 2
N3_ALIAS_DWORDS         equ 16
N3_ALIAS_END            equ 7fc0h     ; the window's last 16 dwords
N3_ID                   equ 001812d2h ; device 0018h, vendor 12D2h
N3_CFG_REVISION         equ 08h
N3_CFG_BAR0             equ 10h
N3_CFG_BAR1             equ 14h
N3_MMIO_BYTES           equ 01000000h
N3_BASE_ALIGN           equ 00ffffffh
N3_BAR_IO               equ 00000001h
N3_BOOT_CHIP_MASK       equ 000f0000h
N3_BOOT_CHIP_NV3        equ 00030000h
N3_BAR1_FLAG            equ 80000000h
N3_TOP_START            equ 007f8000h
N3_RAMIN_START          equ 00c00000h
N3_WINDOW_BYTES         equ 00008000h
N3_OFFSET_MAX           equ 256

; Status bits returned to the publisher.
N3_ST_NV3               equ 00000001h
N3_ST_BASE_INVALID      equ 00000002h
N3_ST_MMIO_MAPPED       equ 00000004h
N3_ST_IDENTITY_MATCH    equ 00000008h
N3_ST_OFFSETS_REFUSED   equ 00000010h
N3_ST_SNAPSHOT_TAKEN    equ 00000020h
N3_ST_FB_MAPPED         equ 00000040h
N3_ST_NOT_RIVA128       equ 00000080h
N3_ST_ADDRESS_INVALID   equ 00000100h
N3_ST_ALIAS_DONE        equ 00000200h

; Input from V9XN3MM.EXE.
N3_IN_CONFIG            equ 0
N3_IN_COUNT             equ 4
N3_IN_OFFSETS           equ 8
N3_IN_MIN_BYTES         equ 8

; Output layout, in dwords. Must match struct v9x_n3mm_result in
; nv3_mmio_probe_win32.c.
N3_HEADER_DWORDS        equ 16
N3_RESULT_DWORDS        equ (N3_HEADER_DWORDS + N3_OFFSET_MAX * 3)

VxD_LOCKED_DATA_SEG
; Accepted BAR0 windows: start, limit (exclusive).
N3Windows label dword
    dd 000000h, 000004h ; PMC_BOOT_0
    dd 000100h, 000104h ; PMC_INTR_0
    dd 000140h, 000144h ; PMC_INTR_EN_0
    dd 000200h, 000204h ; PMC_ENABLE
    dd 002080h, 002084h ; PFIFO_DEBUG_0
    dd 002100h, 002104h ; PFIFO_INTR_0
    dd 002140h, 002144h ; PFIFO_INTR_EN_0
    dd 002200h, 00221ch ; PFIFO_CONFIG_0, RAMHT, RAMFC, RAMRO
    dd 002400h, 002424h ; PFIFO runout status, put, get
    dd 002500h, 002504h ; PFIFO_CACHES
    dd 003000h, 003004h ; CACHE0_PUSH0
    dd 003200h, 003214h ; CACHE1_PUSH0, PUSH1, PUT
    dd 003240h, 003254h ; CACHE1_PULL0, PULL1, PULL_STATE
    dd 003270h, 003274h ; CACHE1_GET
    dd 003280h, 003300h ; CACHE1 context, 8 registers 10h apart
    dd 009100h, 009104h ; PTIMER_INTR_0
    dd 009140h, 009144h ; PTIMER_INTR_EN_0
    dd 009200h, 009214h ; PTIMER numerator, denominator
    dd 009400h, 009408h ; PTIMER time
    dd 100000h, 100004h ; PFB_BOOT_0
    dd 100080h, 100084h ; PFB_DEBUG_0
    dd 100200h, 100208h ; PFB_CONFIG_0, PFB_CONFIG_1
    dd 101000h, 101004h ; PSTRAPS
    dd 400080h, 400090h ; PGRAPH_DEBUG_0 .. 3
    dd 400100h, 400108h ; PGRAPH_INTR_0, INTR_1
    dd 400140h, 400148h ; PGRAPH_INTR_EN_0, EN_1
    dd 400180h, 4001a0h ; PGRAPH context switch and control
    dd 400550h, 400560h ; PGRAPH canvas
    dd 400630h, 400640h ; PGRAPH surface offsets 0-3
    dd 400650h, 400660h ; PGRAPH surface pitches 0-3
    dd 4006a0h, 4006b4h ; PGRAPH FIFO enable, SURF_CONFIG, STATUS
    dd 680500h, 680510h ; PRAMDAC NVPLL, MPLL, VPLL, PLL select
    dd 680600h, 680604h ; PRAMDAC general control
    dd 800010h, 800014h ; USER channel 0, subchannel 0 free count
N3_WINDOW_COUNT         equ 34

N3Result label dword
    dd N3_MAGIC                 ; 00 magic
    dd 0                        ; 01 status
    dd 0                        ; 02 id dword at configuration 00h
    dd 0                        ; 03 BAR0 as read
    dd 0                        ; 04 offsets accepted
    dd 0                        ; 05 PMC_BOOT_0 through the BAR
    dd 0                        ; 06 0CF8h found and restored
    dd 0                        ; 07 first offset refused, if any
    dd 0                        ; 08 configuration address used
    dd 0                        ; 09 BAR1 as read
    dd 0                        ; 10 class and revision at 08h
    dd 0                        ; 11 reserved
    dd 0                        ; 12 reserved
    dd 0                        ; 13 reserved
    dd 0                        ; 14 reserved
    dd 0                        ; 15 reserved
N3Offsets    dd N3_OFFSET_MAX dup (0)
N3First      dd N3_OFFSET_MAX dup (0)
N3Second     dd N3_OFFSET_MAX dup (0)
N3Config     dd 0
N3Mmio       dd 0
N3Top        dd 0
N3Ramin      dd 0
N3Request    dd 0
N3Saved      dd N3_ALIAS_DWORDS * 2 dup (0)
VxD_LOCKED_DATA_ENDS

VxD_LOCKED_CODE_SEG

; EAX = PCI configuration address. Returns EAX = configuration dword.
; 0CF8h saved and restored with interrupts off.
BeginProc N3_Pci_Read
    push    ebx
    push    edx
    pushfd
    cli
    mov     ebx, eax
    mov     dx, 0cf8h
    in      eax, dx
    mov     N3Result[24], eax
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
EndProc N3_Pci_Read

; EDX = offset. Returns CF clear if it lies in an accepted window.
BeginProc N3_Offset_Allowed
    push    ecx
    push    esi
    test    edx, 3
    jnz     short N3_Offset_Refused
    test    edx, N3_BAR1_FLAG
    jnz     short N3_Offset_Bar1
    mov     esi, OFFSET32 N3Windows
    mov     ecx, N3_WINDOW_COUNT
N3_Offset_Next:
    cmp     edx, [esi]
    jb      short N3_Offset_Skip
    cmp     edx, [esi+4]
    jb      short N3_Offset_Accepted
N3_Offset_Skip:
    add     esi, 8
    dec     ecx
    jnz     short N3_Offset_Next
N3_Offset_Refused:
    pop     esi
    pop     ecx
    stc
    ret

; A BAR1 offset: inside one of the two 32 KiB windows.
N3_Offset_Bar1:
    mov     ecx, edx
    and     ecx, NOT N3_BAR1_FLAG
    sub     ecx, N3_TOP_START
    cmp     ecx, N3_WINDOW_BYTES
    jb      short N3_Offset_Accepted
    mov     ecx, edx
    and     ecx, NOT N3_BAR1_FLAG
    sub     ecx, N3_RAMIN_START
    cmp     ecx, N3_WINDOW_BYTES
    jb      short N3_Offset_Accepted
    jmp     short N3_Offset_Refused
N3_Offset_Accepted:
    pop     esi
    pop     ecx
    clc
    ret
EndProc N3_Offset_Allowed

; EBP = DIOC input buffer, ECX = its byte count. Copies the offset list,
; refusing the whole list if any entry is unaligned or outside the windows.
BeginProc N3_Accept_Offsets
    pushad
    mov     N3Result[16], 0
    sub     ecx, N3_IN_OFFSETS
    shr     ecx, 2
    mov     eax, [ebp+N3_IN_COUNT]
    cmp     eax, N3_OFFSET_MAX
    ja      short N3_Accept_Refused
    cmp     eax, ecx
    ja      short N3_Accept_Refused
    mov     ecx, eax
    lea     esi, [ebp+N3_IN_OFFSETS]
    mov     edi, OFFSET32 N3Offsets
    jecxz   short N3_Accept_Done
N3_Accept_Next:
    mov     edx, [esi]
    call    N3_Offset_Allowed
    jc      short N3_Accept_Refused_At
    mov     [edi], edx
    add     esi, 4
    add     edi, 4
    dec     ecx
    jnz     short N3_Accept_Next
N3_Accept_Done:
    mov     eax, [ebp+N3_IN_COUNT]
    mov     N3Result[16], eax
    popad
    ret
N3_Accept_Refused_At:
    mov     N3Result[28], edx
N3_Accept_Refused:
    or      N3Result[4], N3_ST_OFFSETS_REFUSED
    popad
    ret
EndProc N3_Accept_Offsets

; EDI = destination. Reads every accepted offset through its mapping. A
; BAR1 offset whose window did not map reads as zero.
BeginProc N3_Read_Snapshot
    pushad
    mov     ecx, N3Result[16]
    mov     ebx, OFFSET32 N3Offsets
    jecxz   short N3_Read_Snapshot_Done
N3_Read_Snapshot_Next:
    mov     eax, [ebx]
    test    eax, N3_BAR1_FLAG
    jnz     short N3_Read_Bar1
    mov     esi, N3Mmio
    jmp     short N3_Read_Word
N3_Read_Bar1:
    and     eax, NOT N3_BAR1_FLAG
    cmp     eax, N3_RAMIN_START
    jae     short N3_Read_Ramin
    sub     eax, N3_TOP_START
    mov     esi, N3Top
    jmp     short N3_Read_Mapped
N3_Read_Ramin:
    sub     eax, N3_RAMIN_START
    mov     esi, N3Ramin
N3_Read_Mapped:
    test    esi, esi
    jnz     short N3_Read_Word
    xor     eax, eax
    jmp     short N3_Read_Store
N3_Read_Word:
    mov     eax, [esi+eax]
N3_Read_Store:
    mov     [edi], eax
    add     ebx, 4
    add     edi, 4
    dec     ecx
    jnz     short N3_Read_Snapshot_Next
N3_Read_Snapshot_Done:
    popad
    ret
EndProc N3_Read_Snapshot

; EAX = BAR as read. Returns CF clear and EAX = base if it is a 16 MiB-
; aligned, non-zero memory BAR.
BeginProc N3_Memory_Base
    test    eax, N3_BAR_IO
    jnz     short N3_Memory_Base_Bad
    and     eax, NOT 0fh
    jz      short N3_Memory_Base_Bad
    test    eax, N3_BASE_ALIGN
    jnz     short N3_Memory_Base_Bad
    clc
    ret
N3_Memory_Base_Bad:
    stc
    ret
EndProc N3_Memory_Base

BeginProc N3_Capture
    pushad

    ; A configuration address: enable bit set, register field zero, and
    ; nothing in the reserved bits 30:24.
    mov     eax, N3Config
    mov     N3Result[32], eax
    test    eax, 80000000h
    jz      N3_Capture_Bad_Address
    test    eax, 7f0000ffh
    jnz     N3_Capture_Bad_Address

    call    N3_Pci_Read
    mov     N3Result[8], eax
    cmp     eax, N3_ID
    je      short N3_Capture_Check_Base
    or      N3Result[4], N3_ST_NOT_RIVA128
    jmp     N3_Capture_Done

N3_Capture_Check_Base:
    or      N3Result[4], N3_ST_NV3
    mov     eax, N3Config
    or      eax, N3_CFG_REVISION
    call    N3_Pci_Read
    mov     N3Result[40], eax
    mov     eax, N3Config
    or      eax, N3_CFG_BAR1
    call    N3_Pci_Read
    mov     N3Result[36], eax
    mov     eax, N3Config
    or      eax, N3_CFG_BAR0
    call    N3_Pci_Read
    mov     N3Result[12], eax
    call    N3_Memory_Base
    jc      N3_Capture_Bad_Base

    VMMcall _MapPhysToLinear,<eax,N3_MMIO_BYTES,0>
    cmp     eax, 0ffffffffh
    je      N3_Capture_Done
    mov     N3Mmio, eax
    or      N3Result[4], N3_ST_MMIO_MAPPED

    ; PMC_BOOT_0: chip 3. Its low byte is recorded beside the
    ; configuration revision but not required to match, because that
    ; layout is the research brief's reading of pmc.rst, unmeasured.
    mov     edx, [eax]
    mov     N3Result[20], edx
    and     edx, N3_BOOT_CHIP_MASK
    cmp     edx, N3_BOOT_CHIP_NV3
    jne     N3_Capture_Done
    or      N3Result[4], N3_ST_IDENTITY_MATCH

    ; BAR1's two windows. A failure leaves them unmapped; their offsets
    ; then read as zero and the status says so.
    mov     eax, N3Result[36]
    call    N3_Memory_Base
    jc      short N3_Capture_Read
    mov     ebx, eax
    add     eax, N3_TOP_START
    VMMcall _MapPhysToLinear,<eax,N3_WINDOW_BYTES,0>
    cmp     eax, 0ffffffffh
    je      short N3_Capture_Read
    mov     N3Top, eax
    mov     eax, ebx
    add     eax, N3_RAMIN_START
    VMMcall _MapPhysToLinear,<eax,N3_WINDOW_BYTES,0>
    cmp     eax, 0ffffffffh
    je      short N3_Capture_Read
    mov     N3Ramin, eax
    or      N3Result[4], N3_ST_FB_MAPPED

N3_Capture_Read:
    mov     edi, OFFSET32 N3First
    call    N3_Read_Snapshot
    mov     edi, OFFSET32 N3Second
    call    N3_Read_Snapshot
    or      N3Result[4], N3_ST_SNAPSHOT_TAKEN
    jmp     short N3_Capture_Done

N3_Capture_Bad_Address:
    or      N3Result[4], N3_ST_ADDRESS_INVALID
    jmp     short N3_Capture_Done
N3_Capture_Bad_Base:
    or      N3Result[4], N3_ST_BASE_INVALID
N3_Capture_Done:
    popad
    ret
EndProc N3_Capture

; The alias test, N3_DIOC_ALIAS. Video memory only, never a register: 16
; dwords at the start and 16 at the end of the top-of-VRAM window get a
; pattern naming their own window offset (A5xxxxxxh at the start,
; 5Axxxxxxh at the end), every dword of BAR1 + C00000h is scanned for
; non-zero words, the 32 saved words are put back, and each hit is read
; again. N3Offsets/N3First hold the hits with the pattern in place,
; N3Second the same words after the restore; N3Result[16] the hit count.
; The first run on M-7V636C (boot 10) read the whole of both windows as
; zero, so a hit is the pattern or something that moved. Run with the
; captures done, N3Top and N3Ramin mapped.
BeginProc N3_Alias_Test
    pushad
    mov     esi, N3Top
    mov     edi, OFFSET32 N3Saved
    xor     ecx, ecx
N3_Alias_Save:
    mov     eax, [esi+ecx*4]
    mov     [edi+ecx*4], eax
    mov     eax, [esi+ecx*4+N3_ALIAS_END]
    mov     [edi+ecx*4+N3_ALIAS_DWORDS*4], eax
    mov     eax, ecx
    shl     eax, 2
    or      eax, 0a5000000h
    mov     [esi+ecx*4], eax
    mov     eax, ecx
    shl     eax, 2
    add     eax, N3_ALIAS_END
    or      eax, 5a000000h
    mov     [esi+ecx*4+N3_ALIAS_END], eax
    inc     ecx
    cmp     ecx, N3_ALIAS_DWORDS
    jb      short N3_Alias_Save

    ; Scan the RAMIN window, keeping the first N3_OFFSET_MAX hits.
    mov     esi, N3Ramin
    xor     ecx, ecx
    xor     edx, edx
N3_Alias_Scan:
    mov     eax, [esi+ecx]
    test    eax, eax
    jz      short N3_Alias_Scan_Next
    cmp     edx, N3_OFFSET_MAX
    jae     short N3_Alias_Scan_Next
    mov     N3Offsets[edx*4], ecx
    mov     N3First[edx*4], eax
    inc     edx
N3_Alias_Scan_Next:
    add     ecx, 4
    cmp     ecx, N3_WINDOW_BYTES
    jb      short N3_Alias_Scan
    mov     N3Result[16], edx

    ; Put the saved words back, then read each hit again.
    mov     esi, N3Top
    mov     edi, OFFSET32 N3Saved
    xor     ecx, ecx
N3_Alias_Restore:
    mov     eax, [edi+ecx*4]
    mov     [esi+ecx*4], eax
    mov     eax, [edi+ecx*4+N3_ALIAS_DWORDS*4]
    mov     [esi+ecx*4+N3_ALIAS_END], eax
    inc     ecx
    cmp     ecx, N3_ALIAS_DWORDS
    jb      short N3_Alias_Restore

    mov     esi, N3Ramin
    xor     ecx, ecx
N3_Alias_Reread:
    cmp     ecx, edx
    jae     short N3_Alias_Done
    mov     eax, N3Offsets[ecx*4]
    mov     eax, [esi+eax]
    mov     N3Second[ecx*4], eax
    inc     ecx
    jmp     short N3_Alias_Reread
N3_Alias_Done:
    or      N3Result[4], N3_ST_ALIAS_DONE
    popad
    ret
EndProc N3_Alias_Test

BeginProc N3_Reset_Result
    pushad
    mov     edi, OFFSET32 N3Result + 4
    mov     ecx, N3_RESULT_DWORDS - 1
    xor     eax, eax
    cld
    rep     stosd
    mov     N3Mmio, eax
    mov     N3Top, eax
    mov     N3Ramin, eax
    popad
    ret
EndProc N3_Reset_Result

BeginProc N3_W32_DeviceIoControl
    cmp     ecx, DIOC_OPEN
    je      N3_Dioc_Ok
    cmp     ecx, DIOC_CLOSEHANDLE
    je      N3_Dioc_Ok
    cmp     ecx, N3_DIOC_CAPTURE
    je      short N3_Dioc_Known
    cmp     ecx, N3_DIOC_ALIAS
    jne     N3_Dioc_Fail
N3_Dioc_Known:
    mov     N3Request, ecx

    pushad
    mov     ebx, esi
    mov     ebp, [ebx.lpvInBuffer]
    test    ebp, ebp
    jz     N3_Dioc_Copy_Fail
    mov     ecx, [ebx.cbInBuffer]
    cmp     ecx, N3_IN_MIN_BYTES
    jb     N3_Dioc_Copy_Fail
    mov     edi, [ebx.lpvOutBuffer]
    test    edi, edi
    jz     N3_Dioc_Copy_Fail
    cmp     [ebx.cbOutBuffer], N3_RESULT_DWORDS * 4
    jb     N3_Dioc_Copy_Fail

    call    N3_Reset_Result
    mov     eax, [ebp+N3_IN_CONFIG]
    mov     N3Config, eax
    call    N3_Accept_Offsets
    test    N3Result[4], N3_ST_OFFSETS_REFUSED
    jnz     short N3_Dioc_Copy_Out
    call    N3_Capture
    cmp     N3Request, N3_DIOC_ALIAS
    jne     short N3_Dioc_Copy_Out
    test    N3Result[4], N3_ST_IDENTITY_MATCH
    jz      short N3_Dioc_Copy_Out
    test    N3Result[4], N3_ST_FB_MAPPED
    jz      short N3_Dioc_Copy_Out
    call    N3_Alias_Test

N3_Dioc_Copy_Out:
    mov     edi, [ebx.lpvOutBuffer]
    mov     esi, OFFSET32 N3Result
    mov     ecx, N3_RESULT_DWORDS
    cld
    rep     movsd
    mov     eax, [ebx.lpcbBytesReturned]
    test    eax, eax
    jz      short N3_Dioc_Copy_Done
    mov     dword ptr [eax], N3_RESULT_DWORDS * 4
N3_Dioc_Copy_Done:
    popad
N3_Dioc_Ok:
    xor     eax, eax
    ret

N3_Dioc_Copy_Fail:
    popad
N3_Dioc_Fail:
    mov     eax, 1
    ret
EndProc N3_W32_DeviceIoControl

BeginProc N3_Dynamic_Init
    clc
    ret
EndProc N3_Dynamic_Init

BeginProc N3_Dynamic_Exit
    clc
    ret
EndProc N3_Dynamic_Exit

Begin_Control_Dispatch N3
    Control_Dispatch Sys_Dynamic_Device_Init, N3_Dynamic_Init
    Control_Dispatch Sys_Dynamic_Device_Exit, N3_Dynamic_Exit
    Control_Dispatch W32_DeviceIoControl, N3_W32_DeviceIoControl
End_Control_Dispatch N3

VxD_LOCKED_CODE_ENDS

end
