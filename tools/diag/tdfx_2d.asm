; 3dfx VSA-100 2D engine write probe VxD.
;
; Loaded dynamically by TDFX2D.EXE, which decides every access; this VxD only
; executes an op list against the card and refuses anything outside it. It
; finds PCI 121A:0009 with configuration mechanism 1, maps BAR0's init and 2D
; register blocks and the first 32 MiB of BAR1 (the linear framebuffer) once,
; and runs:
;
;   MMIO write32      - BAR0 2D block only (100000h-1001FCh), aligned.
;   MMIO read32       - BAR0 init block (0-FCh) or 2D block, aligned.
;   LFB write/read    - BAR1, dword-aligned offsets below 32 MiB less 4 KiB,
;                       the end of the BIOS's linear view (the last page is
;                       served tiled; docs\decisions\2026-10-11-voodoo4-runs-
;                       velocity9x-at-tier-0-and-its-vbe-reports-half-its-
;                       memory.md).
;   LFB fill          - a dword run of one value, inside the same window.
;   LFB gradient      - a dword run where dword i holds pixels 2i and 2i+1.
;   LFB read block    - up to 1024 dwords into the result's block area.
;   wait idle         - status (BAR0 + 0) bit 9 clear on three consecutive
;                       reads, at most TDFX2D_WAIT_LIMIT reads.
;
; It writes the 2D engine's registers and the off-screen VRAM TDFX2D.EXE
; names. The init registers are read, never written.

.386p

.xlist
include VMM.INC
include MINIVDD.INC
include VWIN32.INC
.list

Declare_Virtual_Device TDFX2D, 1, 0, Tdfx2d_Control, \
                       Undefined_Device_ID, Undefined_Init_Order, , ,

TDFX2D_MAGIC            equ 44325854h ; "TX2D"
TDFX2D_DIOC_RUN         equ 1
TDFX2D_PCI_ID           equ 0009121ah
TDFX2D_OP_MAX           equ 512
TDFX2D_BLOCK_DWORDS     equ 1024
TDFX2D_WAIT_LIMIT       equ 1000000
TDFX2D_TIMEOUT          equ 0ffffffffh

; BAR0: the init block at 0 and the 2D block at 100000h, mapped as one run.
TDFX2D_MMIO_MAP_BYTES   equ 00101000h
TDFX2D_INIT_LIMIT       equ 00000100h
TDFX2D_2D_BASE          equ 00100000h
TDFX2D_2D_LIMIT         equ 00100200h
; BAR1: the linear view the BIOS leaves, 32 MiB less its last page.
TDFX2D_LFB_MAP_BYTES    equ 02000000h
TDFX2D_LFB_LIMIT        equ 01fff000h
TDFX2D_BAR_ALIGN        equ 01ffffffh
TDFX2D_STATUS_BUSY      equ 00000200h
TDFX2D_IDLE_READS       equ 3

; Op codes. Must match tdfx_2d_win32.c.
TDFX2D_OP_MMIO_WRITE32  equ 1
TDFX2D_OP_MMIO_READ32   equ 2
TDFX2D_OP_LFB_WRITE32   equ 3
TDFX2D_OP_LFB_READ32    equ 4
TDFX2D_OP_LFB_FILL32    equ 5
TDFX2D_OP_LFB_GRADIENT  equ 6
TDFX2D_OP_LFB_READ_BLOCK equ 7
TDFX2D_OP_WAIT_IDLE     equ 8

; Status bits.
TDFX2D_PCI_FOUND        equ 00000001h
TDFX2D_MAPPED           equ 00000002h
TDFX2D_RAN              equ 00000004h
TDFX2D_REFUSED          equ 00000008h

; One op: code, a, b, c, result - five dwords.
TDFX2D_OP_DWORDS        equ 5
; Request: count, then ops. Result: magic, status, bar0, bar1, executed,
; refused index, then the ops with their results, then the read block.
TDFX2D_IN_MIN_BYTES     equ 4
TDFX2D_HEADER_DWORDS    equ 6
TDFX2D_RESULT_DWORDS    equ (TDFX2D_HEADER_DWORDS + \
                             TDFX2D_OP_MAX * TDFX2D_OP_DWORDS + \
                             TDFX2D_BLOCK_DWORDS)

VxD_LOCKED_DATA_SEG
Tdfx2dResult label dword
    dd TDFX2D_MAGIC             ; 00 magic
    dd 0                        ; 01 status
    dd 0                        ; 02 BAR0 physical
    dd 0                        ; 03 BAR1 physical
    dd 0                        ; 04 ops executed
    dd 0                        ; 05 index of the refused op, or count
Tdfx2dOps   dd (TDFX2D_OP_MAX * TDFX2D_OP_DWORDS) dup (0)
Tdfx2dBlock dd TDFX2D_BLOCK_DWORDS dup (0)
Tdfx2dLfbLinear  dd 0
Tdfx2dMmioLinear dd 0
Tdfx2dLfbPhys    dd 0
Tdfx2dMmioPhys   dd 0
Tdfx2dCount      dd 0
VxD_LOCKED_DATA_ENDS

VxD_LOCKED_CODE_SEG

; EAX = PCI configuration address. Returns EAX = configuration dword, with
; 0CF8h restored and interrupts off around the cycle.
BeginProc Tdfx2d_Pci_Read
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
EndProc Tdfx2d_Pci_Read

; Find the card and map both BARs once. Sets TDFX2D_MAPPED on success. A BAR
; that moved since the first mapping is refused rather than mapped twice.
BeginProc Tdfx2d_Map
    pushad
    mov     ebx, 80000000h
Tdfx2d_Map_Next:
    mov     eax, ebx
    call    Tdfx2d_Pci_Read
    cmp     eax, TDFX2D_PCI_ID
    je      short Tdfx2d_Map_Found
    add     ebx, 0800h
    cmp     ebx, 81000000h
    jb      short Tdfx2d_Map_Next
    jmp     Tdfx2d_Map_Done

Tdfx2d_Map_Found:
    or      Tdfx2dResult[4], TDFX2D_PCI_FOUND
    lea     eax, [ebx+10h]
    call    Tdfx2d_Pci_Read
    and     eax, 0fffffff0h
    mov     esi, eax
    lea     eax, [ebx+14h]
    call    Tdfx2d_Pci_Read
    and     eax, 0fffffff0h
    mov     edi, eax
    mov     Tdfx2dResult[8], esi
    mov     Tdfx2dResult[12], edi

    ; Both BARs are 32 MiB or larger on every VSA-100 board, so both are at
    ; least 32 MiB aligned; zero means decode is off.
    test    esi, esi
    jz      Tdfx2d_Map_Done
    test    esi, TDFX2D_BAR_ALIGN
    jnz     Tdfx2d_Map_Done
    test    edi, edi
    jz      Tdfx2d_Map_Done
    test    edi, TDFX2D_BAR_ALIGN
    jnz     Tdfx2d_Map_Done

    cmp     Tdfx2dLfbLinear, 0
    je      short Tdfx2d_Map_Fresh
    cmp     esi, Tdfx2dMmioPhys
    jne     short Tdfx2d_Map_Done
    cmp     edi, Tdfx2dLfbPhys
    jne     short Tdfx2d_Map_Done
    jmp     short Tdfx2d_Map_Ok

Tdfx2d_Map_Fresh:
    VMMcall _MapPhysToLinear,<esi,TDFX2D_MMIO_MAP_BYTES,0>
    cmp     eax, 0ffffffffh
    je      short Tdfx2d_Map_Done
    mov     Tdfx2dMmioLinear, eax
    mov     Tdfx2dMmioPhys, esi
    VMMcall _MapPhysToLinear,<edi,TDFX2D_LFB_MAP_BYTES,0>
    cmp     eax, 0ffffffffh
    je      short Tdfx2d_Map_Unmapped
    mov     Tdfx2dLfbLinear, eax
    mov     Tdfx2dLfbPhys, edi
Tdfx2d_Map_Ok:
    or      Tdfx2dResult[4], TDFX2D_MAPPED
    jmp     short Tdfx2d_Map_Done
Tdfx2d_Map_Unmapped:
    mov     Tdfx2dMmioLinear, 0
Tdfx2d_Map_Done:
    popad
    ret
EndProc Tdfx2d_Map

; EBX = offset, ECX = dword count. Returns CF set unless the whole run is
; dword aligned and inside the linear window.
BeginProc Tdfx2d_Check_Lfb_Run
    push    eax
    test    ebx, 3
    jnz     short Tdfx2d_Check_Lfb_Bad
    cmp     ebx, TDFX2D_LFB_LIMIT
    jae     short Tdfx2d_Check_Lfb_Bad
    cmp     ecx, TDFX2D_LFB_LIMIT / 4
    ja      short Tdfx2d_Check_Lfb_Bad
    mov     eax, ecx
    shl     eax, 2
    add     eax, ebx
    cmp     eax, TDFX2D_LFB_LIMIT
    ja      short Tdfx2d_Check_Lfb_Bad
    pop     eax
    clc
    ret
Tdfx2d_Check_Lfb_Bad:
    pop     eax
    stc
    ret
EndProc Tdfx2d_Check_Lfb_Run

; ESI = op (code, a, b, c, result). Returns CF set if the op is refused.
BeginProc Tdfx2d_Execute_One
    pushad
    mov     eax, [esi]
    mov     ebx, [esi+4]
    mov     ecx, [esi+8]
    mov     edx, [esi+12]

    cmp     eax, TDFX2D_OP_MMIO_WRITE32
    je      Tdfx2d_Do_Mmio_Write32
    cmp     eax, TDFX2D_OP_MMIO_READ32
    je      Tdfx2d_Do_Mmio_Read32
    cmp     eax, TDFX2D_OP_LFB_WRITE32
    je      Tdfx2d_Do_Lfb_Write32
    cmp     eax, TDFX2D_OP_LFB_READ32
    je      Tdfx2d_Do_Lfb_Read32
    cmp     eax, TDFX2D_OP_LFB_FILL32
    je      Tdfx2d_Do_Lfb_Fill32
    cmp     eax, TDFX2D_OP_LFB_GRADIENT
    je      Tdfx2d_Do_Lfb_Gradient
    cmp     eax, TDFX2D_OP_LFB_READ_BLOCK
    je      Tdfx2d_Do_Lfb_Read_Block
    cmp     eax, TDFX2D_OP_WAIT_IDLE
    je      Tdfx2d_Do_Wait_Idle
    jmp     Tdfx2d_Do_Refuse

; a = BAR0 offset in the 2D block, b = value.
Tdfx2d_Do_Mmio_Write32:
    test    ebx, 3
    jnz     Tdfx2d_Do_Refuse
    cmp     ebx, TDFX2D_2D_BASE
    jb      Tdfx2d_Do_Refuse
    cmp     ebx, TDFX2D_2D_LIMIT
    jae     Tdfx2d_Do_Refuse
    mov     edi, Tdfx2dMmioLinear
    mov     [edi+ebx], ecx
    jmp     Tdfx2d_Do_Done

; a = BAR0 offset in the init or 2D block.
Tdfx2d_Do_Mmio_Read32:
    test    ebx, 3
    jnz     Tdfx2d_Do_Refuse
    cmp     ebx, TDFX2D_INIT_LIMIT
    jb      short Tdfx2d_Do_Mmio_Read32_Ok
    cmp     ebx, TDFX2D_2D_BASE
    jb      Tdfx2d_Do_Refuse
    cmp     ebx, TDFX2D_2D_LIMIT
    jae     Tdfx2d_Do_Refuse
Tdfx2d_Do_Mmio_Read32_Ok:
    mov     edi, Tdfx2dMmioLinear
    mov     eax, [edi+ebx]
    mov     [esi+16], eax
    jmp     Tdfx2d_Do_Done

; a = offset, b = value.
Tdfx2d_Do_Lfb_Write32:
    push    ecx
    mov     ecx, 1
    call    Tdfx2d_Check_Lfb_Run
    pop     ecx
    jc      Tdfx2d_Do_Refuse
    mov     edi, Tdfx2dLfbLinear
    mov     [edi+ebx], ecx
    jmp     Tdfx2d_Do_Done

; a = offset.
Tdfx2d_Do_Lfb_Read32:
    mov     ecx, 1
    call    Tdfx2d_Check_Lfb_Run
    jc      Tdfx2d_Do_Refuse
    mov     edi, Tdfx2dLfbLinear
    mov     eax, [edi+ebx]
    mov     [esi+16], eax
    jmp     Tdfx2d_Do_Done

; a = offset, b = value, c = dword count.
Tdfx2d_Do_Lfb_Fill32:
    push    ecx
    mov     ecx, edx
    call    Tdfx2d_Check_Lfb_Run
    pop     eax
    jc      Tdfx2d_Do_Refuse
    mov     edi, Tdfx2dLfbLinear
    add     edi, ebx
    mov     ecx, edx
    cld
    rep     stosd
    jmp     Tdfx2d_Do_Done

; a = offset, c = dword count. Dword i holds 16-bit pixels 2i (low half)
; and 2i+1 (high half), counted from the start of the run, so every pixel
; differs from its neighbours and its copy can be told from its source.
Tdfx2d_Do_Lfb_Gradient:
    mov     ecx, edx
    call    Tdfx2d_Check_Lfb_Run
    jc      Tdfx2d_Do_Refuse
    mov     edi, Tdfx2dLfbLinear
    add     edi, ebx
    xor     eax, eax
Tdfx2d_Do_Lfb_Gradient_Next:
    jecxz   short Tdfx2d_Do_Lfb_Gradient_Done
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
    jmp     short Tdfx2d_Do_Lfb_Gradient_Next
Tdfx2d_Do_Lfb_Gradient_Done:
    jmp     Tdfx2d_Do_Done

; a = offset, c = dword count (at most the block). Copies into the block.
Tdfx2d_Do_Lfb_Read_Block:
    cmp     edx, TDFX2D_BLOCK_DWORDS
    ja      Tdfx2d_Do_Refuse
    mov     ecx, edx
    call    Tdfx2d_Check_Lfb_Run
    jc      Tdfx2d_Do_Refuse
    push    esi
    mov     esi, Tdfx2dLfbLinear
    add     esi, ebx
    mov     edi, OFFSET32 Tdfx2dBlock
    mov     ecx, edx
    cld
    rep     movsd
    pop     esi
    jmp     Tdfx2d_Do_Done

; Result: reads taken, or TDFX2D_TIMEOUT. Idle is three consecutive reads
; with the busy bit clear (docs\specifications\vsa100-registers.md, status).
Tdfx2d_Do_Wait_Idle:
    mov     edi, Tdfx2dMmioLinear
    xor     edx, edx
    xor     ecx, ecx
Tdfx2d_Do_Wait_Idle_Next:
    inc     edx
    mov     eax, [edi]
    cmp     eax, 0ffffffffh
    je      short Tdfx2d_Do_Wait_Idle_Gone
    test    eax, TDFX2D_STATUS_BUSY
    jz      short Tdfx2d_Do_Wait_Idle_Clear
    xor     ecx, ecx
    jmp     short Tdfx2d_Do_Wait_Idle_Check
Tdfx2d_Do_Wait_Idle_Clear:
    inc     ecx
    cmp     ecx, TDFX2D_IDLE_READS
    jae     short Tdfx2d_Do_Wait_Idle_Done
Tdfx2d_Do_Wait_Idle_Check:
    cmp     edx, TDFX2D_WAIT_LIMIT
    jb      short Tdfx2d_Do_Wait_Idle_Next
Tdfx2d_Do_Wait_Idle_Gone:
    mov     edx, TDFX2D_TIMEOUT
Tdfx2d_Do_Wait_Idle_Done:
    mov     [esi+16], edx
    jmp     short Tdfx2d_Do_Done

Tdfx2d_Do_Refuse:
    popad
    stc
    ret
Tdfx2d_Do_Done:
    popad
    clc
    ret
EndProc Tdfx2d_Execute_One

BeginProc Tdfx2d_Run
    pushad
    call    Tdfx2d_Map
    test    Tdfx2dResult[4], TDFX2D_MAPPED
    jz      short Tdfx2d_Run_Done
    mov     ecx, Tdfx2dCount
    mov     Tdfx2dResult[20], ecx
    mov     esi, OFFSET32 Tdfx2dOps
    xor     ebx, ebx
    jecxz   short Tdfx2d_Run_All
Tdfx2d_Run_Next:
    call    Tdfx2d_Execute_One
    jc      short Tdfx2d_Run_Refused
    add     esi, TDFX2D_OP_DWORDS * 4
    inc     ebx
    cmp     ebx, ecx
    jb      short Tdfx2d_Run_Next
Tdfx2d_Run_All:
    or      Tdfx2dResult[4], TDFX2D_RAN
    jmp     short Tdfx2d_Run_Count
Tdfx2d_Run_Refused:
    or      Tdfx2dResult[4], TDFX2D_REFUSED
    mov     Tdfx2dResult[20], ebx
Tdfx2d_Run_Count:
    mov     Tdfx2dResult[16], ebx
Tdfx2d_Run_Done:
    popad
    ret
EndProc Tdfx2d_Run

BeginProc Tdfx2d_W32_DeviceIoControl
    cmp     ecx, DIOC_OPEN
    je      Tdfx2d_Dioc_Ok
    cmp     ecx, DIOC_CLOSEHANDLE
    je      Tdfx2d_Dioc_Ok
    cmp     ecx, TDFX2D_DIOC_RUN
    jne     Tdfx2d_Dioc_Fail

    pushad
    mov     ebx, esi
    mov     ebp, [ebx.lpvInBuffer]
    test    ebp, ebp
    jz      Tdfx2d_Dioc_Copy_Fail
    mov     ecx, [ebx.cbInBuffer]
    cmp     ecx, TDFX2D_IN_MIN_BYTES
    jb      Tdfx2d_Dioc_Copy_Fail
    mov     edi, [ebx.lpvOutBuffer]
    test    edi, edi
    jz      Tdfx2d_Dioc_Copy_Fail
    cmp     [ebx.cbOutBuffer], TDFX2D_RESULT_DWORDS * 4
    jb      Tdfx2d_Dioc_Copy_Fail

    ; The count must fit both the op table and the bytes supplied.
    mov     eax, [ebp]
    cmp     eax, TDFX2D_OP_MAX
    ja      Tdfx2d_Dioc_Copy_Fail
    mov     edx, eax
    imul    edx, TDFX2D_OP_DWORDS * 4
    add     edx, 4
    cmp     ecx, edx
    jb      Tdfx2d_Dioc_Copy_Fail
    mov     Tdfx2dCount, eax

    mov     Tdfx2dResult[4], 0
    mov     Tdfx2dResult[16], 0
    mov     Tdfx2dResult[20], 0
    lea     esi, [ebp+4]
    mov     edi, OFFSET32 Tdfx2dOps
    mov     ecx, eax
    imul    ecx, TDFX2D_OP_DWORDS
    cld
    rep     movsd

    call    Tdfx2d_Run

    mov     edi, [ebx.lpvOutBuffer]
    mov     esi, OFFSET32 Tdfx2dResult
    mov     ecx, TDFX2D_RESULT_DWORDS
    cld
    rep     movsd
    mov     eax, [ebx.lpcbBytesReturned]
    test    eax, eax
    jz      short Tdfx2d_Dioc_Copy_Done
    mov     dword ptr [eax], TDFX2D_RESULT_DWORDS * 4
Tdfx2d_Dioc_Copy_Done:
    popad
Tdfx2d_Dioc_Ok:
    xor     eax, eax
    ret

Tdfx2d_Dioc_Copy_Fail:
    popad
Tdfx2d_Dioc_Fail:
    mov     eax, 1
    ret
EndProc Tdfx2d_W32_DeviceIoControl

BeginProc Tdfx2d_Dynamic_Init
    clc
    ret
EndProc Tdfx2d_Dynamic_Init

BeginProc Tdfx2d_Dynamic_Exit
    clc
    ret
EndProc Tdfx2d_Dynamic_Exit

Begin_Control_Dispatch Tdfx2d
    Control_Dispatch Sys_Dynamic_Device_Init, Tdfx2d_Dynamic_Init
    Control_Dispatch Sys_Dynamic_Device_Exit, Tdfx2d_Dynamic_Exit
    Control_Dispatch W32_DeviceIoControl, Tdfx2d_W32_DeviceIoControl
End_Control_Dispatch Tdfx2d

VxD_LOCKED_CODE_ENDS

end
