; SiS 6326 2D engine write probe VxD.
;
; Loaded dynamically by SIS2D.EXE, which decides every access; this VxD only
; executes an op list against the card and refuses anything outside it. It
; finds PCI 1039:6326 with configuration mechanism 1, maps BAR0 (the 4 MiB
; framebuffer) and BAR1 (the 64 KiB register window) once, and runs:
;
;   SR read / SR write   - sequencer index and data, atomic with interrupts
;                          off and the index restored; index 00h-3Fh only.
;   MMIO write32/16/read - BAR1, offsets below 64 KiB, naturally aligned.
;   LFB write/read/fill  - BAR0, dword-aligned offsets below 4 MiB.
;   wait clear / set     - read an MMIO dword until (value & mask) is 0, or
;                          equals mask, at most SIS2D_WAIT_LIMIT reads.
;
; SIS3D.EXE, the 3D write probe, drives the 3D registers through this same
; VxD: they are in the same BAR1 window.
;
; Unlike SIS6326.VXD this writes the card: the sequencer registers SIS2D.EXE
; names and the engine registers in BAR1. SIS2D.EXE saves and restores the
; sequencer state it changes and confines engine output to off-screen VRAM.

.386p

.xlist
include VMM.INC
include MINIVDD.INC
include VWIN32.INC
.list

Declare_Virtual_Device SIS2D, 1, 0, Sis2d_Control, \
                       Undefined_Device_ID, Undefined_Init_Order, , ,

SIS2D_MAGIC             equ 44325349h ; "SI2D"
SIS2D_DIOC_RUN          equ 1
SIS2D_PCI_ID            equ 63261039h
SIS2D_OP_MAX            equ 512
SIS2D_LFB_BYTES         equ 00400000h
SIS2D_MMIO_BYTES        equ 00010000h
SIS2D_WAIT_LIMIT        equ 1000000
SIS2D_TIMEOUT           equ 0ffffffffh

; Op codes. Must match sis6326_2d_win32.c.
SIS2D_OP_SR_READ        equ 1
SIS2D_OP_SR_WRITE       equ 2
SIS2D_OP_MMIO_WRITE32   equ 3
SIS2D_OP_MMIO_WRITE16   equ 4
SIS2D_OP_MMIO_READ32    equ 5
SIS2D_OP_LFB_WRITE32    equ 6
SIS2D_OP_LFB_READ32     equ 7
SIS2D_OP_LFB_FILL32     equ 8
SIS2D_OP_WAIT_CLEAR     equ 9
SIS2D_OP_WAIT_SET       equ 10

; Status bits.
SIS2D_PCI_FOUND         equ 00000001h
SIS2D_MAPPED            equ 00000002h
SIS2D_RAN               equ 00000004h
SIS2D_REFUSED           equ 00000008h

; One op: code, a, b, c, result - five dwords.
SIS2D_OP_DWORDS         equ 5
; Request: count, then ops. Result: magic, status, bar0, bar1, executed,
; refused index, then the ops with their results.
SIS2D_IN_MIN_BYTES      equ 4
SIS2D_HEADER_DWORDS     equ 6
SIS2D_RESULT_DWORDS     equ (SIS2D_HEADER_DWORDS + SIS2D_OP_MAX * SIS2D_OP_DWORDS)

VxD_LOCKED_DATA_SEG
Sis2dResult label dword
    dd SIS2D_MAGIC              ; 00 magic
    dd 0                        ; 01 status
    dd 0                        ; 02 BAR0 physical
    dd 0                        ; 03 BAR1 physical
    dd 0                        ; 04 ops executed
    dd 0                        ; 05 index of the refused op, or count
Sis2dOps   dd (SIS2D_OP_MAX * SIS2D_OP_DWORDS) dup (0)
Sis2dLfbLinear  dd 0
Sis2dMmioLinear dd 0
Sis2dLfbPhys    dd 0
Sis2dMmioPhys   dd 0
Sis2dCount      dd 0
VxD_LOCKED_DATA_ENDS

VxD_LOCKED_CODE_SEG

; EAX = PCI configuration address. Returns EAX = configuration dword, with
; 0CF8h restored and interrupts off around the cycle.
BeginProc Sis2d_Pci_Read
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
EndProc Sis2d_Pci_Read

; Find the card and map both BARs once. Sets SIS2D_MAPPED on success. A BAR
; that moved since the first mapping is refused rather than mapped twice.
BeginProc Sis2d_Map
    pushad
    mov     ebx, 80000000h
Sis2d_Map_Next:
    mov     eax, ebx
    call    Sis2d_Pci_Read
    cmp     eax, SIS2D_PCI_ID
    je      short Sis2d_Map_Found
    add     ebx, 0800h
    cmp     ebx, 81000000h
    jb      short Sis2d_Map_Next
    jmp     Sis2d_Map_Done

Sis2d_Map_Found:
    or      Sis2dResult[4], SIS2D_PCI_FOUND
    lea     eax, [ebx+10h]
    call    Sis2d_Pci_Read
    and     eax, 0fffffff0h
    mov     esi, eax
    lea     eax, [ebx+14h]
    call    Sis2d_Pci_Read
    and     eax, 0fffffff0h
    mov     edi, eax
    mov     Sis2dResult[8], esi
    mov     Sis2dResult[12], edi

    ; Both must be naturally aligned to their size and nonzero.
    test    esi, esi
    jz      Sis2d_Map_Done
    test    esi, SIS2D_LFB_BYTES - 1
    jnz     short Sis2d_Map_Done
    test    edi, edi
    jz      short Sis2d_Map_Done
    test    edi, SIS2D_MMIO_BYTES - 1
    jnz     short Sis2d_Map_Done

    cmp     Sis2dLfbLinear, 0
    je      short Sis2d_Map_Fresh
    cmp     esi, Sis2dLfbPhys
    jne     short Sis2d_Map_Done
    cmp     edi, Sis2dMmioPhys
    jne     short Sis2d_Map_Done
    jmp     short Sis2d_Map_Ok

Sis2d_Map_Fresh:
    VMMcall _MapPhysToLinear,<esi,SIS2D_LFB_BYTES,0>
    cmp     eax, 0ffffffffh
    je      short Sis2d_Map_Done
    mov     Sis2dLfbLinear, eax
    mov     Sis2dLfbPhys, esi
    VMMcall _MapPhysToLinear,<edi,SIS2D_MMIO_BYTES,0>
    cmp     eax, 0ffffffffh
    je      short Sis2d_Map_Unmapped
    mov     Sis2dMmioLinear, eax
    mov     Sis2dMmioPhys, edi
Sis2d_Map_Ok:
    or      Sis2dResult[4], SIS2D_MAPPED
    jmp     short Sis2d_Map_Done
Sis2d_Map_Unmapped:
    mov     Sis2dLfbLinear, 0
Sis2d_Map_Done:
    popad
    ret
EndProc Sis2d_Map

; ESI = op (code, a, b, c, result). Returns CF set if the op is refused.
BeginProc Sis2d_Execute_One
    pushad
    mov     eax, [esi]
    mov     ebx, [esi+4]
    mov     ecx, [esi+8]
    mov     edx, [esi+12]

    cmp     eax, SIS2D_OP_SR_READ
    je      Sis2d_Do_Sr_Read
    cmp     eax, SIS2D_OP_SR_WRITE
    je      Sis2d_Do_Sr_Write
    cmp     eax, SIS2D_OP_MMIO_WRITE32
    je      Sis2d_Do_Mmio_Write32
    cmp     eax, SIS2D_OP_MMIO_WRITE16
    je      Sis2d_Do_Mmio_Write16
    cmp     eax, SIS2D_OP_MMIO_READ32
    je      Sis2d_Do_Mmio_Read32
    cmp     eax, SIS2D_OP_LFB_WRITE32
    je      Sis2d_Do_Lfb_Write32
    cmp     eax, SIS2D_OP_LFB_READ32
    je      Sis2d_Do_Lfb_Read32
    cmp     eax, SIS2D_OP_LFB_FILL32
    je      Sis2d_Do_Lfb_Fill32
    cmp     eax, SIS2D_OP_WAIT_CLEAR
    je      Sis2d_Do_Wait_Clear
    cmp     eax, SIS2D_OP_WAIT_SET
    je      Sis2d_Do_Wait_Set
    jmp     Sis2d_Do_Refuse

; a = index 00h-3Fh.
Sis2d_Do_Sr_Read:
    cmp     ebx, 3fh
    ja      Sis2d_Do_Refuse
    pushfd
    cli
    mov     dx, 03c4h
    in      al, dx
    mov     ah, al
    mov     al, bl
    out     dx, al
    inc     dx
    in      al, dx
    dec     dx
    movzx   edi, al
    mov     al, ah
    out     dx, al
    popfd
    mov     [esi+16], edi
    jmp     Sis2d_Do_Done

; a = index 00h-3Fh, b = value.
Sis2d_Do_Sr_Write:
    cmp     ebx, 3fh
    ja      Sis2d_Do_Refuse
    pushfd
    cli
    mov     dx, 03c4h
    in      al, dx
    mov     ah, al
    mov     al, bl
    out     dx, al
    inc     dx
    mov     al, cl
    out     dx, al
    dec     dx
    mov     al, ah
    out     dx, al
    popfd
    jmp     Sis2d_Do_Done

Sis2d_Do_Mmio_Write32:
    cmp     ebx, SIS2D_MMIO_BYTES - 4
    ja      Sis2d_Do_Refuse
    test    ebx, 3
    jnz     Sis2d_Do_Refuse
    mov     edi, Sis2dMmioLinear
    mov     [edi+ebx], ecx
    jmp     Sis2d_Do_Done

Sis2d_Do_Mmio_Write16:
    cmp     ebx, SIS2D_MMIO_BYTES - 2
    ja      Sis2d_Do_Refuse
    test    ebx, 1
    jnz     Sis2d_Do_Refuse
    mov     edi, Sis2dMmioLinear
    mov     word ptr [edi+ebx], cx
    jmp     Sis2d_Do_Done

Sis2d_Do_Mmio_Read32:
    cmp     ebx, SIS2D_MMIO_BYTES - 4
    ja      Sis2d_Do_Refuse
    test    ebx, 3
    jnz     Sis2d_Do_Refuse
    mov     edi, Sis2dMmioLinear
    mov     eax, [edi+ebx]
    mov     [esi+16], eax
    jmp     Sis2d_Do_Done

Sis2d_Do_Lfb_Write32:
    cmp     ebx, SIS2D_LFB_BYTES - 4
    ja      Sis2d_Do_Refuse
    test    ebx, 3
    jnz     Sis2d_Do_Refuse
    mov     edi, Sis2dLfbLinear
    mov     [edi+ebx], ecx
    jmp     Sis2d_Do_Done

Sis2d_Do_Lfb_Read32:
    cmp     ebx, SIS2D_LFB_BYTES - 4
    ja      Sis2d_Do_Refuse
    test    ebx, 3
    jnz     Sis2d_Do_Refuse
    mov     edi, Sis2dLfbLinear
    mov     eax, [edi+ebx]
    mov     [esi+16], eax
    jmp     Sis2d_Do_Done

; a = offset, b = value, c = dword count; the whole run inside the window.
Sis2d_Do_Lfb_Fill32:
    test    ebx, 3
    jnz     Sis2d_Do_Refuse
    cmp     ebx, SIS2D_LFB_BYTES
    jae     Sis2d_Do_Refuse
    cmp     edx, SIS2D_LFB_BYTES / 4
    ja      Sis2d_Do_Refuse
    mov     eax, edx
    shl     eax, 2
    add     eax, ebx
    cmp     eax, SIS2D_LFB_BYTES
    ja      Sis2d_Do_Refuse
    mov     edi, Sis2dLfbLinear
    add     edi, ebx
    mov     eax, ecx
    mov     ecx, edx
    cld
    rep     stosd
    jmp     Sis2d_Do_Done

; a = MMIO offset, b = mask. Result: reads taken, or SIS2D_TIMEOUT.
Sis2d_Do_Wait_Clear:
    cmp     ebx, SIS2D_MMIO_BYTES - 4
    ja      Sis2d_Do_Refuse
    test    ebx, 3
    jnz     Sis2d_Do_Refuse
    mov     edi, Sis2dMmioLinear
    xor     edx, edx
Sis2d_Do_Wait_Next:
    inc     edx
    mov     eax, [edi+ebx]
    test    eax, ecx
    jz      short Sis2d_Do_Wait_Clear_Done
    cmp     edx, SIS2D_WAIT_LIMIT
    jb      short Sis2d_Do_Wait_Next
    mov     edx, SIS2D_TIMEOUT
Sis2d_Do_Wait_Clear_Done:
    mov     [esi+16], edx
    jmp     short Sis2d_Do_Done

; a = MMIO offset, b = mask. Waits for every mask bit set - the 3D status
; bits at 89FCh are active-high idle flags. Result as for the clear wait.
Sis2d_Do_Wait_Set:
    cmp     ebx, SIS2D_MMIO_BYTES - 4
    ja      Sis2d_Do_Refuse
    test    ebx, 3
    jnz     Sis2d_Do_Refuse
    mov     edi, Sis2dMmioLinear
    xor     edx, edx
Sis2d_Do_Wait_Set_Next:
    inc     edx
    mov     eax, [edi+ebx]
    and     eax, ecx
    cmp     eax, ecx
    je      short Sis2d_Do_Wait_Set_Done
    cmp     edx, SIS2D_WAIT_LIMIT
    jb      short Sis2d_Do_Wait_Set_Next
    mov     edx, SIS2D_TIMEOUT
Sis2d_Do_Wait_Set_Done:
    mov     [esi+16], edx
    jmp     short Sis2d_Do_Done

Sis2d_Do_Refuse:
    popad
    stc
    ret
Sis2d_Do_Done:
    popad
    clc
    ret
EndProc Sis2d_Execute_One

BeginProc Sis2d_Run
    pushad
    call    Sis2d_Map
    test    Sis2dResult[4], SIS2D_MAPPED
    jz      short Sis2d_Run_Done
    mov     ecx, Sis2dCount
    mov     Sis2dResult[20], ecx
    mov     esi, OFFSET32 Sis2dOps
    xor     ebx, ebx
    jecxz   short Sis2d_Run_All
Sis2d_Run_Next:
    call    Sis2d_Execute_One
    jc      short Sis2d_Run_Refused
    add     esi, SIS2D_OP_DWORDS * 4
    inc     ebx
    cmp     ebx, ecx
    jb      short Sis2d_Run_Next
Sis2d_Run_All:
    or      Sis2dResult[4], SIS2D_RAN
    jmp     short Sis2d_Run_Count
Sis2d_Run_Refused:
    or      Sis2dResult[4], SIS2D_REFUSED
    mov     Sis2dResult[20], ebx
Sis2d_Run_Count:
    mov     Sis2dResult[16], ebx
Sis2d_Run_Done:
    popad
    ret
EndProc Sis2d_Run

BeginProc Sis2d_W32_DeviceIoControl
    cmp     ecx, DIOC_OPEN
    je      Sis2d_Dioc_Ok
    cmp     ecx, DIOC_CLOSEHANDLE
    je      Sis2d_Dioc_Ok
    cmp     ecx, SIS2D_DIOC_RUN
    jne     Sis2d_Dioc_Fail

    pushad
    mov     ebx, esi
    mov     ebp, [ebx.lpvInBuffer]
    test    ebp, ebp
    jz      Sis2d_Dioc_Copy_Fail
    mov     ecx, [ebx.cbInBuffer]
    cmp     ecx, SIS2D_IN_MIN_BYTES
    jb      Sis2d_Dioc_Copy_Fail
    mov     edi, [ebx.lpvOutBuffer]
    test    edi, edi
    jz      Sis2d_Dioc_Copy_Fail
    cmp     [ebx.cbOutBuffer], SIS2D_RESULT_DWORDS * 4
    jb      Sis2d_Dioc_Copy_Fail

    ; The count must fit both the op table and the bytes supplied.
    mov     eax, [ebp]
    cmp     eax, SIS2D_OP_MAX
    ja      Sis2d_Dioc_Copy_Fail
    mov     edx, eax
    imul    edx, SIS2D_OP_DWORDS * 4
    add     edx, 4
    cmp     ecx, edx
    jb      Sis2d_Dioc_Copy_Fail
    mov     Sis2dCount, eax

    mov     Sis2dResult[4], 0
    mov     Sis2dResult[16], 0
    mov     Sis2dResult[20], 0
    lea     esi, [ebp+4]
    mov     edi, OFFSET32 Sis2dOps
    mov     ecx, eax
    imul    ecx, SIS2D_OP_DWORDS
    cld
    rep     movsd

    call    Sis2d_Run

    mov     edi, [ebx.lpvOutBuffer]
    mov     esi, OFFSET32 Sis2dResult
    mov     ecx, SIS2D_RESULT_DWORDS
    cld
    rep     movsd
    mov     eax, [ebx.lpcbBytesReturned]
    test    eax, eax
    jz      short Sis2d_Dioc_Copy_Done
    mov     dword ptr [eax], SIS2D_RESULT_DWORDS * 4
Sis2d_Dioc_Copy_Done:
    popad
Sis2d_Dioc_Ok:
    xor     eax, eax
    ret

Sis2d_Dioc_Copy_Fail:
    popad
Sis2d_Dioc_Fail:
    mov     eax, 1
    ret
EndProc Sis2d_W32_DeviceIoControl

BeginProc Sis2d_Dynamic_Init
    clc
    ret
EndProc Sis2d_Dynamic_Init

BeginProc Sis2d_Dynamic_Exit
    clc
    ret
EndProc Sis2d_Dynamic_Exit

Begin_Control_Dispatch Sis2d
    Control_Dispatch Sys_Dynamic_Device_Init, Sis2d_Dynamic_Init
    Control_Dispatch Sys_Dynamic_Device_Exit, Sis2d_Dynamic_Exit
    Control_Dispatch W32_DeviceIoControl, Sis2d_W32_DeviceIoControl
End_Control_Dispatch Sis2d

VxD_LOCKED_CODE_ENDS

end
