; Matrox MGA-2064W 2D drawing engine write probe VxD.
;
; Loaded dynamically by MGA2D.EXE, which decides every access; this VxD only
; executes an op list against the card and refuses anything outside it. It
; finds PCI 102B:0519 with configuration mechanism 1, maps BAR0 (MGABASE1,
; the 16 KiB control aperture) and BAR1 (MGABASE2, the 8 MiB framebuffer
; aperture) once - the 2064W orders them this way round, unlike the 2164W
; (docs\decisions\2026-09-09-millennium-2064w-bar-ordering.md) - and runs:
;
;   region       - names the one 1 MiB, 64 KiB-aligned window of BAR1 the LFB
;                  ops may touch. Cleared on every open; LFB ops are refused
;                  until it is set.
;   MMIO write32 - BAR0 offsets 1C00h-1DFCh only, dword-aligned: the drawing
;                  registers and their 1D00h-1DFFh go mirror (MGA-1064SG
;                  Developer Specification, Table 3-4).
;   MMIO read32  - FIFOSTATUS (1E10h) and STATUS (1E14h) only.
;   LFB write/read/fill - dword-aligned, wholly inside the region.
;   wait idle    - read STATUS until dwgengsts<16> is clear (p.4-74).
;   wait FIFO n  - read FIFOSTATUS until fifocount<5:0> >= n, 1 <= n <= 32
;                  (p.4-57).
;   cache flush  - read the CRTC index at 3D4h and write the same value back.
;                  The chip keeps a 4-dword CPU read cache in front of the
;                  framebuffer that the drawing engine does not invalidate;
;                  any VGA register write does (MGA-1064SG section 5.1.6).
;                  Writing the index back unchanged is the VGA write that
;                  alters no state.
;
; Every wait is bounded at MGA2D_WAIT_LIMIT reads. A wait that times out
; ends the run: nothing after it executes, so no further register write is
; queued behind an engine that has stopped answering.
;
; Unlike the read-only Matrox tools this writes the card: the drawing
; registers MGA2D.EXE names, and framebuffer dwords inside the region, which
; MGA2D.EXE places beyond the visible desktop. No configuration-space write,
; no sequencer, CRTC or extension register other than the CRTC index
; written back as found.

.386p

.xlist
include VMM.INC
include MINIVDD.INC
include VWIN32.INC
.list

Declare_Virtual_Device MGA2D, 1, 0, Mga2d_Control, \
                       Undefined_Device_ID, Undefined_Init_Order, , ,

MGA2D_MAGIC             equ 44324d47h ; "GM2D"
MGA2D_DIOC_RUN          equ 1
MGA2D_PCI_ID            equ 0519102bh
MGA2D_OP_MAX            equ 512
MGA2D_MMIO_BYTES        equ 00004000h
MGA2D_LFB_BYTES         equ 00800000h
MGA2D_REGION_BYTES      equ 00100000h
MGA2D_REGION_ALIGN      equ 00010000h
MGA2D_WAIT_LIMIT        equ 1000000
MGA2D_TIMEOUT           equ 0ffffffffh

; Register window the MMIO ops may reach (MGA-1064SG Table 3-4).
MGA2D_DRAW_FIRST        equ 1c00h
MGA2D_DRAW_LAST         equ 1dfch
MGA2D_FIFOSTATUS        equ 1e10h
MGA2D_STATUS            equ 1e14h
MGA2D_FIFO_COUNT_MASK   equ 0000003fh
MGA2D_FIFO_DEPTH        equ 32
MGA2D_STATUS_BUSY       equ 00010000h
MGA2D_CRTC_INDEX        equ 03d4h

; Op codes. Must match mga2d_win32.c.
MGA2D_OP_REGION         equ 1
MGA2D_OP_MMIO_WRITE32   equ 2
MGA2D_OP_MMIO_READ32    equ 3
MGA2D_OP_LFB_WRITE32    equ 4
MGA2D_OP_LFB_READ32     equ 5
MGA2D_OP_LFB_FILL32     equ 6
MGA2D_OP_WAIT_IDLE      equ 7
MGA2D_OP_WAIT_FIFO      equ 8
MGA2D_OP_CACHE_FLUSH    equ 9

; Status bits.
MGA2D_PCI_FOUND         equ 00000001h
MGA2D_MAPPED            equ 00000002h
MGA2D_RAN               equ 00000004h
MGA2D_REFUSED           equ 00000008h
MGA2D_TIMED_OUT         equ 00000010h

; One op: code, a, b, c, result - five dwords.
MGA2D_OP_DWORDS         equ 5
; Request: count, then ops. Result: magic, status, bar0, bar1, executed,
; refused index, then the ops with their results.
MGA2D_IN_MIN_BYTES      equ 4
MGA2D_HEADER_DWORDS     equ 6
MGA2D_RESULT_DWORDS     equ (MGA2D_HEADER_DWORDS + MGA2D_OP_MAX * MGA2D_OP_DWORDS)

VxD_LOCKED_DATA_SEG
Mga2dResult label dword
    dd MGA2D_MAGIC              ; 00 magic
    dd 0                        ; 01 status
    dd 0                        ; 02 BAR0 physical
    dd 0                        ; 03 BAR1 physical
    dd 0                        ; 04 ops executed
    dd 0                        ; 05 index of the refused or timed-out op
Mga2dOps   dd (MGA2D_OP_MAX * MGA2D_OP_DWORDS) dup (0)
Mga2dLfbLinear   dd 0
Mga2dMmioLinear  dd 0
Mga2dLfbPhys     dd 0
Mga2dMmioPhys    dd 0
Mga2dCount       dd 0
Mga2dRegionBase  dd 0
Mga2dRegionEnd   dd 0
Mga2dTimedOut    dd 0
VxD_LOCKED_DATA_ENDS

VxD_LOCKED_CODE_SEG

; EAX = PCI configuration address. Returns EAX = configuration dword, with
; 0CF8h restored and interrupts off around the cycle.
BeginProc Mga2d_Pci_Read
    push    ebx
    push    edx
    pushfd
    cli
    mov     ebx, eax
    mov     dx, 0cf8h
    in      eax, dx
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
EndProc Mga2d_Pci_Read

; Find the card and map both BARs once. Sets MGA2D_MAPPED on success. A BAR
; that moved since the first mapping is refused rather than mapped twice.
; Only alignment can be checked here: BAR sizing needs a configuration
; write, so MGA2D.EXE checks the range lengths Configuration Manager
; allocated before it sends any write.
BeginProc Mga2d_Map
    pushad
    mov     ebx, 80000000h
Mga2d_Map_Next:
    mov     eax, ebx
    call    Mga2d_Pci_Read
    cmp     eax, MGA2D_PCI_ID
    je      Mga2d_Map_Found
    add     ebx, 0800h
    cmp     ebx, 81000000h
    jb      Mga2d_Map_Next
    jmp     Mga2d_Map_Done

Mga2d_Map_Found:
    or      Mga2dResult[4], MGA2D_PCI_FOUND
    lea     eax, [ebx+10h]
    call    Mga2d_Pci_Read
    mov     esi, eax
    lea     eax, [ebx+14h]
    call    Mga2d_Pci_Read
    mov     edi, eax
    mov     Mga2dResult[8], esi
    mov     Mga2dResult[12], edi

    ; Both must be memory BARs (bit 0 clear), nonzero, and naturally
    ; aligned to the size this VxD maps.
    test    esi, 1
    jnz     Mga2d_Map_Done
    test    edi, 1
    jnz     Mga2d_Map_Done
    and     esi, 0fffffff0h
    and     edi, 0fffffff0h
    test    esi, esi
    jz      Mga2d_Map_Done
    test    esi, MGA2D_MMIO_BYTES - 1
    jnz     Mga2d_Map_Done
    test    edi, edi
    jz      Mga2d_Map_Done
    test    edi, MGA2D_LFB_BYTES - 1
    jnz     Mga2d_Map_Done

    cmp     Mga2dLfbLinear, 0
    je      Mga2d_Map_Fresh
    cmp     edi, Mga2dLfbPhys
    jne     Mga2d_Map_Done
    cmp     esi, Mga2dMmioPhys
    jne     Mga2d_Map_Done
    jmp     Mga2d_Map_Ok

Mga2d_Map_Fresh:
    VMMcall _MapPhysToLinear,<edi,MGA2D_LFB_BYTES,0>
    cmp     eax, 0ffffffffh
    je      Mga2d_Map_Done
    mov     Mga2dLfbLinear, eax
    mov     Mga2dLfbPhys, edi
    VMMcall _MapPhysToLinear,<esi,MGA2D_MMIO_BYTES,0>
    cmp     eax, 0ffffffffh
    je      Mga2d_Map_Unmapped
    mov     Mga2dMmioLinear, eax
    mov     Mga2dMmioPhys, esi
Mga2d_Map_Ok:
    or      Mga2dResult[4], MGA2D_MAPPED
    jmp     Mga2d_Map_Done
Mga2d_Map_Unmapped:
    mov     Mga2dLfbLinear, 0
Mga2d_Map_Done:
    popad
    ret
EndProc Mga2d_Map

; EBX = BAR1 offset, EAX = byte count (nonzero). Returns CF clear when
; [EBX, EBX + EAX) is dword-aligned and wholly inside the region.
BeginProc Mga2d_Check_Lfb
    push    edx
    mov     edx, Mga2dRegionEnd
    test    edx, edx
    jz      Mga2d_Check_Lfb_Bad
    test    ebx, 3
    jnz     Mga2d_Check_Lfb_Bad
    cmp     ebx, Mga2dRegionBase
    jb      Mga2d_Check_Lfb_Bad
    cmp     ebx, edx
    jae     Mga2d_Check_Lfb_Bad
    sub     edx, ebx
    cmp     eax, edx
    ja      Mga2d_Check_Lfb_Bad
    pop     edx
    clc
    ret
Mga2d_Check_Lfb_Bad:
    pop     edx
    stc
    ret
EndProc Mga2d_Check_Lfb

; ESI = op (code, a, b, c, result). Returns CF set if the op is refused. A
; wait that times out sets Mga2dTimedOut and returns CF clear.
BeginProc Mga2d_Execute_One
    pushad
    mov     eax, [esi]
    mov     ebx, [esi+4]
    mov     ecx, [esi+8]
    mov     edx, [esi+12]

    cmp     eax, MGA2D_OP_REGION
    je      Mga2d_Do_Region
    cmp     eax, MGA2D_OP_MMIO_WRITE32
    je      Mga2d_Do_Mmio_Write32
    cmp     eax, MGA2D_OP_MMIO_READ32
    je      Mga2d_Do_Mmio_Read32
    cmp     eax, MGA2D_OP_LFB_WRITE32
    je      Mga2d_Do_Lfb_Write32
    cmp     eax, MGA2D_OP_LFB_READ32
    je      Mga2d_Do_Lfb_Read32
    cmp     eax, MGA2D_OP_LFB_FILL32
    je      Mga2d_Do_Lfb_Fill32
    cmp     eax, MGA2D_OP_WAIT_IDLE
    je      Mga2d_Do_Wait_Idle
    cmp     eax, MGA2D_OP_WAIT_FIFO
    je      Mga2d_Do_Wait_Fifo
    cmp     eax, MGA2D_OP_CACHE_FLUSH
    je      Mga2d_Do_Cache_Flush
    jmp     Mga2d_Do_Refuse

; a = BAR1 offset, 64 KiB-aligned; b = MGA2D_REGION_BYTES exactly.
Mga2d_Do_Region:
    test    ebx, MGA2D_REGION_ALIGN - 1
    jnz     Mga2d_Do_Refuse
    cmp     ecx, MGA2D_REGION_BYTES
    jne     Mga2d_Do_Refuse
    cmp     ebx, MGA2D_LFB_BYTES - MGA2D_REGION_BYTES
    ja      Mga2d_Do_Refuse
    mov     Mga2dRegionBase, ebx
    add     ebx, ecx
    mov     Mga2dRegionEnd, ebx
    jmp     Mga2d_Do_Done

; a = drawing register offset, b = value.
Mga2d_Do_Mmio_Write32:
    cmp     ebx, MGA2D_DRAW_FIRST
    jb      Mga2d_Do_Refuse
    cmp     ebx, MGA2D_DRAW_LAST
    ja      Mga2d_Do_Refuse
    test    ebx, 3
    jnz     Mga2d_Do_Refuse
    mov     edi, Mga2dMmioLinear
    mov     [edi+ebx], ecx
    jmp     Mga2d_Do_Done

; a = FIFOSTATUS or STATUS.
Mga2d_Do_Mmio_Read32:
    cmp     ebx, MGA2D_FIFOSTATUS
    je      Mga2d_Do_Mmio_Read32_Ok
    cmp     ebx, MGA2D_STATUS
    jne     Mga2d_Do_Refuse
Mga2d_Do_Mmio_Read32_Ok:
    mov     edi, Mga2dMmioLinear
    mov     eax, [edi+ebx]
    mov     [esi+16], eax
    jmp     Mga2d_Do_Done

Mga2d_Do_Lfb_Write32:
    mov     eax, 4
    call    Mga2d_Check_Lfb
    jc      Mga2d_Do_Refuse
    mov     edi, Mga2dLfbLinear
    mov     [edi+ebx], ecx
    jmp     Mga2d_Do_Done

Mga2d_Do_Lfb_Read32:
    mov     eax, 4
    call    Mga2d_Check_Lfb
    jc      Mga2d_Do_Refuse
    mov     edi, Mga2dLfbLinear
    mov     eax, [edi+ebx]
    mov     [esi+16], eax
    jmp     Mga2d_Do_Done

; a = offset, b = value, c = dword count (1 to the region's dwords); the
; whole run inside the region. The count is bounded before it is scaled.
Mga2d_Do_Lfb_Fill32:
    test    edx, edx
    jz      Mga2d_Do_Refuse
    cmp     edx, MGA2D_REGION_BYTES / 4
    ja      Mga2d_Do_Refuse
    mov     eax, edx
    shl     eax, 2
    call    Mga2d_Check_Lfb
    jc      Mga2d_Do_Refuse
    mov     edi, Mga2dLfbLinear
    add     edi, ebx
    mov     eax, ecx
    mov     ecx, edx
    cld
    rep     stosd
    jmp     Mga2d_Do_Done

; Result: reads taken, or MGA2D_TIMEOUT.
Mga2d_Do_Wait_Idle:
    mov     edi, Mga2dMmioLinear
    xor     edx, edx
Mga2d_Do_Wait_Idle_Next:
    inc     edx
    mov     eax, [edi+MGA2D_STATUS]
    test    eax, MGA2D_STATUS_BUSY
    jz      Mga2d_Do_Wait_Store
    cmp     edx, MGA2D_WAIT_LIMIT
    jb      Mga2d_Do_Wait_Idle_Next
    jmp     Mga2d_Do_Wait_Timeout

; a = free slots wanted, 1 to MGA2D_FIFO_DEPTH. Result as for the idle wait.
Mga2d_Do_Wait_Fifo:
    test    ebx, ebx
    jz      Mga2d_Do_Refuse
    cmp     ebx, MGA2D_FIFO_DEPTH
    ja      Mga2d_Do_Refuse
    mov     edi, Mga2dMmioLinear
    xor     edx, edx
Mga2d_Do_Wait_Fifo_Next:
    inc     edx
    mov     eax, [edi+MGA2D_FIFOSTATUS]
    and     eax, MGA2D_FIFO_COUNT_MASK
    cmp     eax, ebx
    jae     Mga2d_Do_Wait_Store
    cmp     edx, MGA2D_WAIT_LIMIT
    jb      Mga2d_Do_Wait_Fifo_Next

Mga2d_Do_Wait_Timeout:
    mov     Mga2dTimedOut, 1
    mov     edx, MGA2D_TIMEOUT
Mga2d_Do_Wait_Store:
    mov     [esi+16], edx
    jmp     Mga2d_Do_Done

; Result: the CRTC index found (and written back).
Mga2d_Do_Cache_Flush:
    pushfd
    cli
    mov     dx, MGA2D_CRTC_INDEX
    in      al, dx
    out     dx, al
    popfd
    movzx   eax, al
    mov     [esi+16], eax
    jmp     Mga2d_Do_Done

Mga2d_Do_Refuse:
    popad
    stc
    ret
Mga2d_Do_Done:
    popad
    clc
    ret
EndProc Mga2d_Execute_One

BeginProc Mga2d_Run
    pushad
    call    Mga2d_Map
    test    Mga2dResult[4], MGA2D_MAPPED
    jz      Mga2d_Run_Done
    mov     Mga2dTimedOut, 0
    mov     ecx, Mga2dCount
    mov     Mga2dResult[20], ecx
    mov     esi, OFFSET32 Mga2dOps
    xor     ebx, ebx
    jecxz   Mga2d_Run_All
Mga2d_Run_Next:
    call    Mga2d_Execute_One
    jc      Mga2d_Run_Refused
    inc     ebx
    cmp     Mga2dTimedOut, 0
    jne     Mga2d_Run_Timed_Out
    add     esi, MGA2D_OP_DWORDS * 4
    cmp     ebx, ecx
    jb      Mga2d_Run_Next
Mga2d_Run_All:
    or      Mga2dResult[4], MGA2D_RAN
    jmp     Mga2d_Run_Count
Mga2d_Run_Timed_Out:
    ; The wait executed (its result is the timeout), nothing after it did.
    or      Mga2dResult[4], MGA2D_TIMED_OUT
    lea     eax, [ebx-1]
    mov     Mga2dResult[20], eax
    jmp     Mga2d_Run_Count
Mga2d_Run_Refused:
    or      Mga2dResult[4], MGA2D_REFUSED
    mov     Mga2dResult[20], ebx
Mga2d_Run_Count:
    mov     Mga2dResult[16], ebx
Mga2d_Run_Done:
    popad
    ret
EndProc Mga2d_Run

BeginProc Mga2d_W32_DeviceIoControl
    cmp     ecx, DIOC_OPEN
    je      Mga2d_Dioc_Open
    cmp     ecx, DIOC_CLOSEHANDLE
    je      Mga2d_Dioc_Ok
    cmp     ecx, MGA2D_DIOC_RUN
    jne     Mga2d_Dioc_Fail

    pushad
    mov     ebx, esi
    mov     ebp, [ebx.lpvInBuffer]
    test    ebp, ebp
    jz      Mga2d_Dioc_Copy_Fail
    mov     ecx, [ebx.cbInBuffer]
    cmp     ecx, MGA2D_IN_MIN_BYTES
    jb      Mga2d_Dioc_Copy_Fail
    mov     edi, [ebx.lpvOutBuffer]
    test    edi, edi
    jz      Mga2d_Dioc_Copy_Fail
    cmp     [ebx.cbOutBuffer], MGA2D_RESULT_DWORDS * 4
    jb      Mga2d_Dioc_Copy_Fail

    ; The count must fit both the op table and the bytes supplied.
    mov     eax, [ebp]
    cmp     eax, MGA2D_OP_MAX
    ja      Mga2d_Dioc_Copy_Fail
    mov     edx, eax
    imul    edx, MGA2D_OP_DWORDS * 4
    add     edx, 4
    cmp     ecx, edx
    jb      Mga2d_Dioc_Copy_Fail
    mov     Mga2dCount, eax

    mov     Mga2dResult[4], 0
    mov     Mga2dResult[16], 0
    mov     Mga2dResult[20], 0
    lea     esi, [ebp+4]
    mov     edi, OFFSET32 Mga2dOps
    mov     ecx, eax
    imul    ecx, MGA2D_OP_DWORDS
    cld
    rep     movsd

    call    Mga2d_Run

    mov     edi, [ebx.lpvOutBuffer]
    mov     esi, OFFSET32 Mga2dResult
    mov     ecx, MGA2D_RESULT_DWORDS
    cld
    rep     movsd
    mov     eax, [ebx.lpcbBytesReturned]
    test    eax, eax
    jz      Mga2d_Dioc_Copy_Done
    mov     dword ptr [eax], MGA2D_RESULT_DWORDS * 4
Mga2d_Dioc_Copy_Done:
    popad
    jmp     Mga2d_Dioc_Ok

; A fresh handle starts with no region: a previous run's window is never
; inherited.
Mga2d_Dioc_Open:
    mov     Mga2dRegionBase, 0
    mov     Mga2dRegionEnd, 0
Mga2d_Dioc_Ok:
    xor     eax, eax
    ret

Mga2d_Dioc_Copy_Fail:
    popad
Mga2d_Dioc_Fail:
    mov     eax, 1
    ret
EndProc Mga2d_W32_DeviceIoControl

BeginProc Mga2d_Dynamic_Init
    clc
    ret
EndProc Mga2d_Dynamic_Init

BeginProc Mga2d_Dynamic_Exit
    clc
    ret
EndProc Mga2d_Dynamic_Exit

Begin_Control_Dispatch Mga2d
    Control_Dispatch Sys_Dynamic_Device_Init, Mga2d_Dynamic_Init
    Control_Dispatch Sys_Dynamic_Device_Exit, Mga2d_Dynamic_Exit
    Control_Dispatch W32_DeviceIoControl, Mga2d_W32_DeviceIoControl
End_Control_Dispatch Mga2d

VxD_LOCKED_CODE_ENDS

end
