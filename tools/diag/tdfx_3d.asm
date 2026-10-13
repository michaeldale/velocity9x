; 3dfx VSA-100 3D write probe VxD.
;
; Loaded dynamically by TDFX3D.EXE, which decides every access; this VxD only
; executes an op list against the card and refuses anything outside it. It
; finds PCI 121A:0009 with configuration mechanism 1, maps BAR0 from the init
; registers through the 3D window, and the first 32 MiB of BAR1 (the linear
; framebuffer), once, and runs:
;
;   MMIO write32      - BAR0 3D window only (200000h-200BFCh: FBI and TMU0
;                       through chip fields 0-2), aligned.
;   MMIO read32       - BAR0 init block (0-FCh), aligned.
;   LFB write/read    - BAR1, dword-aligned offsets below 32 MiB less 4 KiB,
;                       the end of the linear view the driver restores.
;   LFB fill          - a dword run of one value, inside the same window.
;   LFB gradient      - a dword run where dword i holds pixels 2i and 2i+1.
;   LFB read block    - up to 1024 dwords into the result's block area.
;   wait idle         - status (BAR0 + 0) bit 9 clear on three consecutive
;                       reads, at most TDFX3D_WAIT_LIMIT reads.
;   wait FIFO         - status bits 4:0 (PCI FIFO free entries) at least a,
;                       at most TDFX3D_WAIT_LIMIT reads.
;
; It writes the 3D registers and the off-screen VRAM TDFX3D.EXE names. The
; init registers are read, never written; the 2D block is not touched.

.386p

.xlist
include VMM.INC
include MINIVDD.INC
include VWIN32.INC
.list

Declare_Virtual_Device TDFX3D, 1, 0, Tdfx3d_Control, \
                       Undefined_Device_ID, Undefined_Init_Order, , ,

TDFX3D_MAGIC            equ 44335854h ; "TX3D"
TDFX3D_DIOC_RUN         equ 1
TDFX3D_PCI_ID           equ 0009121ah
TDFX3D_OP_MAX           equ 512
TDFX3D_BLOCK_DWORDS     equ 1024
TDFX3D_WAIT_LIMIT       equ 1000000
TDFX3D_TIMEOUT          equ 0ffffffffh

; BAR0: the init block at 0 through the 3D window at 200000h, mapped as one
; run that ends past TMU0's registers (chip field 2, +800h).
TDFX3D_MMIO_MAP_BYTES   equ 00201000h
TDFX3D_INIT_LIMIT       equ 00000100h
TDFX3D_3D_BASE          equ 00200000h
TDFX3D_3D_LIMIT         equ 00200c00h
; BAR1: the linear view, 32 MiB less its last page.
TDFX3D_LFB_MAP_BYTES    equ 02000000h
TDFX3D_LFB_LIMIT        equ 01fff000h
TDFX3D_BAR_ALIGN        equ 01ffffffh
TDFX3D_STATUS_BUSY      equ 00000200h
TDFX3D_STATUS_FIFO      equ 0000001fh
TDFX3D_IDLE_READS       equ 3

; Op codes. Must match tdfx_3d_win32.c.
TDFX3D_OP_MMIO_WRITE32  equ 1
TDFX3D_OP_MMIO_READ32   equ 2
TDFX3D_OP_LFB_WRITE32   equ 3
TDFX3D_OP_LFB_READ32    equ 4
TDFX3D_OP_LFB_FILL32    equ 5
TDFX3D_OP_LFB_GRADIENT  equ 6
TDFX3D_OP_LFB_READ_BLOCK equ 7
TDFX3D_OP_WAIT_IDLE     equ 8
TDFX3D_OP_WAIT_FIFO     equ 9

; Status bits.
TDFX3D_PCI_FOUND        equ 00000001h
TDFX3D_MAPPED           equ 00000002h
TDFX3D_RAN              equ 00000004h
TDFX3D_REFUSED          equ 00000008h

; One op: code, a, b, c, result - five dwords.
TDFX3D_OP_DWORDS        equ 5
TDFX3D_IN_MIN_BYTES     equ 4
TDFX3D_HEADER_DWORDS    equ 6
TDFX3D_RESULT_DWORDS    equ (TDFX3D_HEADER_DWORDS + \
                             TDFX3D_OP_MAX * TDFX3D_OP_DWORDS + \
                             TDFX3D_BLOCK_DWORDS)

VxD_LOCKED_DATA_SEG
Tdfx3dResult label dword
    dd TDFX3D_MAGIC             ; 00 magic
    dd 0                        ; 01 status
    dd 0                        ; 02 BAR0 physical
    dd 0                        ; 03 BAR1 physical
    dd 0                        ; 04 ops executed
    dd 0                        ; 05 index of the refused op, or count
Tdfx3dOps   dd (TDFX3D_OP_MAX * TDFX3D_OP_DWORDS) dup (0)
Tdfx3dBlock dd TDFX3D_BLOCK_DWORDS dup (0)
Tdfx3dLfbLinear  dd 0
Tdfx3dMmioLinear dd 0
Tdfx3dLfbPhys    dd 0
Tdfx3dMmioPhys   dd 0
Tdfx3dCount      dd 0
VxD_LOCKED_DATA_ENDS

VxD_LOCKED_CODE_SEG

; EAX = PCI configuration address. Returns EAX = configuration dword, with
; 0CF8h restored and interrupts off around the cycle.
BeginProc Tdfx3d_Pci_Read
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
EndProc Tdfx3d_Pci_Read

; Find the card and map both BARs once. Sets TDFX3D_MAPPED on success. A BAR
; that moved since the first mapping is refused rather than mapped twice.
BeginProc Tdfx3d_Map
    pushad
    mov     ebx, 80000000h
Tdfx3d_Map_Next:
    mov     eax, ebx
    call    Tdfx3d_Pci_Read
    cmp     eax, TDFX3D_PCI_ID
    je      short Tdfx3d_Map_Found
    add     ebx, 0800h
    cmp     ebx, 81000000h
    jb      short Tdfx3d_Map_Next
    jmp     Tdfx3d_Map_Done

Tdfx3d_Map_Found:
    or      Tdfx3dResult[4], TDFX3D_PCI_FOUND
    lea     eax, [ebx+10h]
    call    Tdfx3d_Pci_Read
    and     eax, 0fffffff0h
    mov     esi, eax
    lea     eax, [ebx+14h]
    call    Tdfx3d_Pci_Read
    and     eax, 0fffffff0h
    mov     edi, eax
    mov     Tdfx3dResult[8], esi
    mov     Tdfx3dResult[12], edi

    ; Both BARs are 32 MiB or larger on every VSA-100 board, so both are at
    ; least 32 MiB aligned; zero means decode is off.
    test    esi, esi
    jz      Tdfx3d_Map_Done
    test    esi, TDFX3D_BAR_ALIGN
    jnz     Tdfx3d_Map_Done
    test    edi, edi
    jz      Tdfx3d_Map_Done
    test    edi, TDFX3D_BAR_ALIGN
    jnz     Tdfx3d_Map_Done

    cmp     Tdfx3dLfbLinear, 0
    je      short Tdfx3d_Map_Fresh
    cmp     esi, Tdfx3dMmioPhys
    jne     short Tdfx3d_Map_Done
    cmp     edi, Tdfx3dLfbPhys
    jne     short Tdfx3d_Map_Done
    jmp     short Tdfx3d_Map_Ok

Tdfx3d_Map_Fresh:
    VMMcall _MapPhysToLinear,<esi,TDFX3D_MMIO_MAP_BYTES,0>
    cmp     eax, 0ffffffffh
    je      short Tdfx3d_Map_Done
    mov     Tdfx3dMmioLinear, eax
    mov     Tdfx3dMmioPhys, esi
    VMMcall _MapPhysToLinear,<edi,TDFX3D_LFB_MAP_BYTES,0>
    cmp     eax, 0ffffffffh
    je      short Tdfx3d_Map_Unmapped
    mov     Tdfx3dLfbLinear, eax
    mov     Tdfx3dLfbPhys, edi
Tdfx3d_Map_Ok:
    or      Tdfx3dResult[4], TDFX3D_MAPPED
    jmp     short Tdfx3d_Map_Done
Tdfx3d_Map_Unmapped:
    mov     Tdfx3dMmioLinear, 0
Tdfx3d_Map_Done:
    popad
    ret
EndProc Tdfx3d_Map

; EBX = offset, ECX = dword count. Returns CF set unless the whole run is
; dword aligned and inside the linear window.
BeginProc Tdfx3d_Check_Lfb_Run
    push    eax
    test    ebx, 3
    jnz     short Tdfx3d_Check_Lfb_Bad
    cmp     ebx, TDFX3D_LFB_LIMIT
    jae     short Tdfx3d_Check_Lfb_Bad
    cmp     ecx, TDFX3D_LFB_LIMIT / 4
    ja      short Tdfx3d_Check_Lfb_Bad
    mov     eax, ecx
    shl     eax, 2
    add     eax, ebx
    cmp     eax, TDFX3D_LFB_LIMIT
    ja      short Tdfx3d_Check_Lfb_Bad
    pop     eax
    clc
    ret
Tdfx3d_Check_Lfb_Bad:
    pop     eax
    stc
    ret
EndProc Tdfx3d_Check_Lfb_Run

; ESI = op (code, a, b, c, result). Returns CF set if the op is refused.
BeginProc Tdfx3d_Execute_One
    pushad
    mov     eax, [esi]
    mov     ebx, [esi+4]
    mov     ecx, [esi+8]
    mov     edx, [esi+12]

    cmp     eax, TDFX3D_OP_MMIO_WRITE32
    je      Tdfx3d_Do_Mmio_Write32
    cmp     eax, TDFX3D_OP_MMIO_READ32
    je      Tdfx3d_Do_Mmio_Read32
    cmp     eax, TDFX3D_OP_LFB_WRITE32
    je      Tdfx3d_Do_Lfb_Write32
    cmp     eax, TDFX3D_OP_LFB_READ32
    je      Tdfx3d_Do_Lfb_Read32
    cmp     eax, TDFX3D_OP_LFB_FILL32
    je      Tdfx3d_Do_Lfb_Fill32
    cmp     eax, TDFX3D_OP_LFB_GRADIENT
    je      Tdfx3d_Do_Lfb_Gradient
    cmp     eax, TDFX3D_OP_LFB_READ_BLOCK
    je      Tdfx3d_Do_Lfb_Read_Block
    cmp     eax, TDFX3D_OP_WAIT_IDLE
    je      Tdfx3d_Do_Wait_Idle
    cmp     eax, TDFX3D_OP_WAIT_FIFO
    je      Tdfx3d_Do_Wait_Fifo
    jmp     Tdfx3d_Do_Refuse

; a = BAR0 offset in the 3D window, b = value.
Tdfx3d_Do_Mmio_Write32:
    test    ebx, 3
    jnz     Tdfx3d_Do_Refuse
    cmp     ebx, TDFX3D_3D_BASE
    jb      Tdfx3d_Do_Refuse
    cmp     ebx, TDFX3D_3D_LIMIT
    jae     Tdfx3d_Do_Refuse
    mov     edi, Tdfx3dMmioLinear
    mov     [edi+ebx], ecx
    jmp     Tdfx3d_Do_Done

; a = BAR0 offset in the init block.
Tdfx3d_Do_Mmio_Read32:
    test    ebx, 3
    jnz     Tdfx3d_Do_Refuse
    cmp     ebx, TDFX3D_INIT_LIMIT
    jae     Tdfx3d_Do_Refuse
    mov     edi, Tdfx3dMmioLinear
    mov     eax, [edi+ebx]
    mov     [esi+16], eax
    jmp     Tdfx3d_Do_Done

; a = offset, b = value.
Tdfx3d_Do_Lfb_Write32:
    push    ecx
    mov     ecx, 1
    call    Tdfx3d_Check_Lfb_Run
    pop     ecx
    jc      Tdfx3d_Do_Refuse
    mov     edi, Tdfx3dLfbLinear
    mov     [edi+ebx], ecx
    jmp     Tdfx3d_Do_Done

; a = offset.
Tdfx3d_Do_Lfb_Read32:
    mov     ecx, 1
    call    Tdfx3d_Check_Lfb_Run
    jc      Tdfx3d_Do_Refuse
    mov     edi, Tdfx3dLfbLinear
    mov     eax, [edi+ebx]
    mov     [esi+16], eax
    jmp     Tdfx3d_Do_Done

; a = offset, b = value, c = dword count.
Tdfx3d_Do_Lfb_Fill32:
    push    ecx
    mov     ecx, edx
    call    Tdfx3d_Check_Lfb_Run
    pop     eax
    jc      Tdfx3d_Do_Refuse
    mov     edi, Tdfx3dLfbLinear
    add     edi, ebx
    mov     ecx, edx
    cld
    rep     stosd
    jmp     Tdfx3d_Do_Done

; a = offset, c = dword count. Dword i holds 16-bit pixels 2i (low half)
; and 2i+1 (high half), counted from the start of the run.
Tdfx3d_Do_Lfb_Gradient:
    mov     ecx, edx
    call    Tdfx3d_Check_Lfb_Run
    jc      Tdfx3d_Do_Refuse
    mov     edi, Tdfx3dLfbLinear
    add     edi, ebx
    xor     eax, eax
Tdfx3d_Do_Lfb_Gradient_Next:
    jecxz   short Tdfx3d_Do_Lfb_Gradient_Done
    mov     edx, eax
    shl     edx, 1
    and     edx, 0ffffh
    lea     ebx, [edx+1]
    and     ebx, 0ffffh
    shl     ebx, 16
    or      edx, ebx
    mov     [edi], edx
    add     edi, 4
    inc     eax
    dec     ecx
    jmp     short Tdfx3d_Do_Lfb_Gradient_Next
Tdfx3d_Do_Lfb_Gradient_Done:
    jmp     Tdfx3d_Do_Done

; a = offset, c = dword count (at most the block). Copies into the block.
Tdfx3d_Do_Lfb_Read_Block:
    cmp     edx, TDFX3D_BLOCK_DWORDS
    ja      Tdfx3d_Do_Refuse
    mov     ecx, edx
    call    Tdfx3d_Check_Lfb_Run
    jc      Tdfx3d_Do_Refuse
    push    esi
    mov     esi, Tdfx3dLfbLinear
    add     esi, ebx
    mov     edi, OFFSET32 Tdfx3dBlock
    mov     ecx, edx
    cld
    rep     movsd
    pop     esi
    jmp     Tdfx3d_Do_Done

; Result: reads taken, or TDFX3D_TIMEOUT. Idle is three consecutive reads
; with the busy bit clear (docs\specifications\vsa100-registers.md, status).
Tdfx3d_Do_Wait_Idle:
    mov     edi, Tdfx3dMmioLinear
    xor     edx, edx
    xor     ecx, ecx
Tdfx3d_Do_Wait_Idle_Next:
    inc     edx
    mov     eax, [edi]
    cmp     eax, 0ffffffffh
    je      short Tdfx3d_Do_Wait_Idle_Gone
    test    eax, TDFX3D_STATUS_BUSY
    jz      short Tdfx3d_Do_Wait_Idle_Clear
    xor     ecx, ecx
    jmp     short Tdfx3d_Do_Wait_Idle_Check
Tdfx3d_Do_Wait_Idle_Clear:
    inc     ecx
    cmp     ecx, TDFX3D_IDLE_READS
    jae     short Tdfx3d_Do_Wait_Idle_Done
Tdfx3d_Do_Wait_Idle_Check:
    cmp     edx, TDFX3D_WAIT_LIMIT
    jb      short Tdfx3d_Do_Wait_Idle_Next
Tdfx3d_Do_Wait_Idle_Gone:
    mov     edx, TDFX3D_TIMEOUT
Tdfx3d_Do_Wait_Idle_Done:
    mov     [esi+16], edx
    jmp     short Tdfx3d_Do_Done

; a = free PCI FIFO entries wanted (1-31). Result: reads taken, or
; TDFX3D_TIMEOUT. Status bits 4:0 count the free entries, 1Fh empty
; (vsa100-registers.md section 8.1).
Tdfx3d_Do_Wait_Fifo:
    test    ebx, ebx
    jz      Tdfx3d_Do_Refuse
    cmp     ebx, TDFX3D_STATUS_FIFO
    ja      Tdfx3d_Do_Refuse
    mov     edi, Tdfx3dMmioLinear
    xor     edx, edx
Tdfx3d_Do_Wait_Fifo_Next:
    inc     edx
    mov     eax, [edi]
    cmp     eax, 0ffffffffh
    je      short Tdfx3d_Do_Wait_Fifo_Gone
    and     eax, TDFX3D_STATUS_FIFO
    cmp     eax, ebx
    jae     short Tdfx3d_Do_Wait_Fifo_Done
    cmp     edx, TDFX3D_WAIT_LIMIT
    jb      short Tdfx3d_Do_Wait_Fifo_Next
Tdfx3d_Do_Wait_Fifo_Gone:
    mov     edx, TDFX3D_TIMEOUT
Tdfx3d_Do_Wait_Fifo_Done:
    mov     [esi+16], edx
    jmp     short Tdfx3d_Do_Done

Tdfx3d_Do_Refuse:
    popad
    stc
    ret
Tdfx3d_Do_Done:
    popad
    clc
    ret
EndProc Tdfx3d_Execute_One

BeginProc Tdfx3d_Run
    pushad
    call    Tdfx3d_Map
    test    Tdfx3dResult[4], TDFX3D_MAPPED
    jz      short Tdfx3d_Run_Done
    mov     ecx, Tdfx3dCount
    mov     Tdfx3dResult[20], ecx
    mov     esi, OFFSET32 Tdfx3dOps
    xor     ebx, ebx
    jecxz   short Tdfx3d_Run_All
Tdfx3d_Run_Next:
    call    Tdfx3d_Execute_One
    jc      short Tdfx3d_Run_Refused
    add     esi, TDFX3D_OP_DWORDS * 4
    inc     ebx
    cmp     ebx, ecx
    jb      short Tdfx3d_Run_Next
Tdfx3d_Run_All:
    or      Tdfx3dResult[4], TDFX3D_RAN
    jmp     short Tdfx3d_Run_Count
Tdfx3d_Run_Refused:
    or      Tdfx3dResult[4], TDFX3D_REFUSED
    mov     Tdfx3dResult[20], ebx
Tdfx3d_Run_Count:
    mov     Tdfx3dResult[16], ebx
Tdfx3d_Run_Done:
    popad
    ret
EndProc Tdfx3d_Run

BeginProc Tdfx3d_W32_DeviceIoControl
    cmp     ecx, DIOC_OPEN
    je      Tdfx3d_Dioc_Ok
    cmp     ecx, DIOC_CLOSEHANDLE
    je      Tdfx3d_Dioc_Ok
    cmp     ecx, TDFX3D_DIOC_RUN
    jne     Tdfx3d_Dioc_Fail

    pushad
    mov     ebx, esi
    mov     ebp, [ebx.lpvInBuffer]
    test    ebp, ebp
    jz      Tdfx3d_Dioc_Copy_Fail
    mov     ecx, [ebx.cbInBuffer]
    cmp     ecx, TDFX3D_IN_MIN_BYTES
    jb      Tdfx3d_Dioc_Copy_Fail
    mov     edi, [ebx.lpvOutBuffer]
    test    edi, edi
    jz      Tdfx3d_Dioc_Copy_Fail
    cmp     [ebx.cbOutBuffer], TDFX3D_RESULT_DWORDS * 4
    jb      Tdfx3d_Dioc_Copy_Fail

    ; The count must fit both the op table and the bytes supplied.
    mov     eax, [ebp]
    cmp     eax, TDFX3D_OP_MAX
    ja      Tdfx3d_Dioc_Copy_Fail
    mov     edx, eax
    imul    edx, TDFX3D_OP_DWORDS * 4
    add     edx, 4
    cmp     ecx, edx
    jb      Tdfx3d_Dioc_Copy_Fail
    mov     Tdfx3dCount, eax

    mov     Tdfx3dResult[4], 0
    mov     Tdfx3dResult[16], 0
    mov     Tdfx3dResult[20], 0
    lea     esi, [ebp+4]
    mov     edi, OFFSET32 Tdfx3dOps
    mov     ecx, eax
    imul    ecx, TDFX3D_OP_DWORDS
    cld
    rep     movsd

    call    Tdfx3d_Run

    mov     edi, [ebx.lpvOutBuffer]
    mov     esi, OFFSET32 Tdfx3dResult
    mov     ecx, TDFX3D_RESULT_DWORDS
    cld
    rep     movsd
    mov     eax, [ebx.lpcbBytesReturned]
    test    eax, eax
    jz      short Tdfx3d_Dioc_Copy_Done
    mov     dword ptr [eax], TDFX3D_RESULT_DWORDS * 4
Tdfx3d_Dioc_Copy_Done:
    popad
Tdfx3d_Dioc_Ok:
    xor     eax, eax
    ret

Tdfx3d_Dioc_Copy_Fail:
    popad
Tdfx3d_Dioc_Fail:
    mov     eax, 1
    ret
EndProc Tdfx3d_W32_DeviceIoControl

BeginProc Tdfx3d_Dynamic_Init
    clc
    ret
EndProc Tdfx3d_Dynamic_Init

BeginProc Tdfx3d_Dynamic_Exit
    clc
    ret
EndProc Tdfx3d_Dynamic_Exit

Begin_Control_Dispatch Tdfx3d
    Control_Dispatch Sys_Dynamic_Device_Init, Tdfx3d_Dynamic_Init
    Control_Dispatch Sys_Dynamic_Device_Exit, Tdfx3d_Dynamic_Exit
    Control_Dispatch W32_DeviceIoControl, Tdfx3d_W32_DeviceIoControl
End_Control_Dispatch Tdfx3d

VxD_LOCKED_CODE_ENDS

end
