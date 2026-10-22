; ATI Rage 128 2D engine write probe VxD.
;
; Phase 3 of docs\plans\ati-rage128.md, first step. Loaded dynamically by
; R1282D.EXE, which decides every access; this VxD only executes an op list
; against the card and refuses anything outside it. It finds the Rage 128
; with configuration mechanism 1 (an id in R2Devices, the list R1MM.VXD
; checks), maps BAR2 (16 KiB of registers) and the first 32 MiB of BAR0
; (the framebuffer) once, checks that the BAR's copy of configuration space
; at 0F00h returns the same id, and runs:
;
;   MMIO write32      - PC_NGUI_CTLSTAT (0184h), the 2D engine registers
;                       1400h-16FCh, SCALE_3D_CNTL (1A00h). Aligned.
;   MMIO read32       - 0008h-0F5Fh less CLOCK_CNTL_DATA (000Ch) and
;                       PALETTE_DATA (00B4h); 1400h-1D47h less HOST_DATA
;                       (17C0h-17E3h). Aligned.
;   LFB write/read    - BAR0, dword-aligned offsets below 32 MiB.
;   LFB fill          - a dword run of one value.
;   LFB gradient      - a dword run where dword i holds 2i and 2i+1 in its
;                       two halves.
;   LFB read block    - up to 1024 dwords into the result's block area.
;   wait idle         - GUI_STAT: 64 free FIFO slots and GUI_ACTIVE clear,
;                       then the pixel cache flushed (PC_NGUI_CTLSTAT 7:0
;                       set, bit 31 polled clear), as r128_accel.c's
;                       R128WaitForIdle. At most R2_WAIT_LIMIT reads each.
;
; It never writes the CCE's registers, the FIFO ports, HOST_DATA, the PLL
; index or anything outside the engine. Offsets: xf86-video-r128 r128_reg.h.

.386p

.xlist
include VMM.INC
include VWIN32.INC
.list

Declare_Virtual_Device R1282D, 1, 0, R2_Control, \
                       Undefined_Device_ID, Undefined_Init_Order, , ,

R2_MAGIC                equ 44323252h ; "R22D"
R2_DIOC_RUN             equ 1
R2_VENDOR_ATI           equ 1002h
R2_OP_MAX               equ 512
R2_BLOCK_DWORDS         equ 1024
R2_WAIT_LIMIT           equ 1000000
R2_TIMEOUT              equ 0ffffffffh

R2_MMIO_BYTES           equ 00004000h
R2_MMIO_ALIGN           equ 00003fffh
R2_LFB_BYTES            equ 02000000h
R2_LFB_ALIGN            equ 03ffffffh ; BAR0 is a 64 MiB window
R2_PCI_COPY             equ 0f00h

R2_GUI_STAT             equ 1740h
R2_GUI_FIFO_MASK        equ 00000fffh
R2_GUI_FIFO_SLOTS       equ 64
R2_GUI_ACTIVE           equ 80000000h
R2_PC_NGUI_CTLSTAT      equ 0184h
R2_PC_FLUSH_ALL         equ 000000ffh
R2_PC_BUSY              equ 80000000h

; Op codes. Must match r128_2d_win32.c.
R2_OP_MMIO_WRITE32      equ 1
R2_OP_MMIO_READ32       equ 2
R2_OP_LFB_WRITE32       equ 3
R2_OP_LFB_READ32        equ 4
R2_OP_LFB_FILL32        equ 5
R2_OP_LFB_GRADIENT      equ 6
R2_OP_LFB_READ_BLOCK    equ 7
R2_OP_WAIT_IDLE         equ 8

; Status bits.
R2_PCI_FOUND            equ 00000001h
R2_MAPPED               equ 00000002h
R2_RAN                  equ 00000004h
R2_REFUSED              equ 00000008h
R2_IDENTITY_MATCH       equ 00000010h

; One op: code, a, b, c, result - five dwords.
R2_OP_DWORDS            equ 5
R2_IN_MIN_BYTES         equ 4
R2_HEADER_DWORDS        equ 8
R2_RESULT_DWORDS        equ (R2_HEADER_DWORDS + R2_OP_MAX * R2_OP_DWORDS + \
                             R2_BLOCK_DWORDS)

VxD_LOCKED_DATA_SEG
R2Devices label word
    dw 5245h, 5246h, 5247h, 524bh, 524ch
    dw 5345h, 5346h, 5347h, 5348h, 534bh, 534ch, 534dh, 534eh
    dw 5041h, 5042h, 5043h, 5044h, 5045h, 5046h, 5047h, 5048h
    dw 5049h, 504ah, 504bh, 504ch, 504dh, 504eh, 504fh, 5050h
    dw 5051h, 5052h, 5053h, 5054h, 5055h, 5056h, 5057h, 5058h
    dw 5446h, 544ch, 5452h, 5453h, 5454h, 5455h
    dw 4c45h, 4c46h, 4d46h, 4d4ch
R2_DEVICE_COUNT         equ 47

R2Result label dword
    dd R2_MAGIC                 ; 00 magic
    dd 0                        ; 01 status
    dd 0                        ; 02 BAR0 physical (framebuffer)
    dd 0                        ; 03 BAR2 physical (registers)
    dd 0                        ; 04 ops executed
    dd 0                        ; 05 index of the refused op, or count
    dd 0                        ; 06 id dword from configuration space
    dd 0                        ; 07 id dword at 0F00h through BAR2
R2Ops   dd (R2_OP_MAX * R2_OP_DWORDS) dup (0)
R2Block dd R2_BLOCK_DWORDS dup (0)
R2LfbLinear  dd 0
R2MmioLinear dd 0
R2LfbPhys    dd 0
R2MmioPhys   dd 0
R2Count      dd 0
VxD_LOCKED_DATA_ENDS

VxD_LOCKED_CODE_SEG

; EAX = PCI configuration address. Returns EAX = configuration dword, with
; 0CF8h restored and interrupts off around the cycle.
BeginProc R2_Pci_Read
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
EndProc R2_Pci_Read

; EAX = configuration id dword. Returns CF clear for a Rage 128.
BeginProc R2_Device_Known
    push    ecx
    push    esi
    cmp     ax, R2_VENDOR_ATI
    jne     short R2_Device_Unknown
    shr     eax, 16
    mov     esi, OFFSET32 R2Devices
    mov     ecx, R2_DEVICE_COUNT
R2_Device_Next:
    cmp     ax, [esi]
    je      short R2_Device_Found
    add     esi, 2
    dec     ecx
    jnz     short R2_Device_Next
R2_Device_Unknown:
    pop     esi
    pop     ecx
    stc
    ret
R2_Device_Found:
    pop     esi
    pop     ecx
    clc
    ret
EndProc R2_Device_Known

; Find the card on buses 0-7 and map both BARs once. Sets R2_MAPPED on
; success. A BAR that moved since the first mapping is refused rather than
; mapped twice.
BeginProc R2_Map
    pushad
    mov     ebx, 80000000h
R2_Map_Next:
    mov     eax, ebx
    call    R2_Pci_Read
    mov     R2Result[24], eax
    call    R2_Device_Known
    jnc     short R2_Map_Found
    add     ebx, 0800h
    cmp     ebx, 80080000h
    jb      short R2_Map_Next
    jmp     R2_Map_Done

R2_Map_Found:
    or      R2Result[4], R2_PCI_FOUND
    lea     eax, [ebx+10h]
    call    R2_Pci_Read
    test    eax, 1
    jnz     R2_Map_Done
    and     eax, 0fffffff0h
    mov     esi, eax
    lea     eax, [ebx+18h]
    call    R2_Pci_Read
    test    eax, 1
    jnz     R2_Map_Done
    and     eax, 0fffffff0h
    mov     edi, eax
    mov     R2Result[8], esi
    mov     R2Result[12], edi

    test    esi, esi
    jz      R2_Map_Done
    test    esi, R2_LFB_ALIGN
    jnz     R2_Map_Done
    test    edi, edi
    jz      R2_Map_Done
    test    edi, R2_MMIO_ALIGN
    jnz     R2_Map_Done

    cmp     R2LfbLinear, 0
    je      short R2_Map_Fresh
    cmp     esi, R2LfbPhys
    jne     short R2_Map_Done
    cmp     edi, R2MmioPhys
    jne     short R2_Map_Done
    jmp     short R2_Map_Identity

R2_Map_Fresh:
    VMMcall _MapPhysToLinear,<edi,R2_MMIO_BYTES,0>
    cmp     eax, 0ffffffffh
    je      short R2_Map_Done
    mov     R2MmioLinear, eax
    mov     R2MmioPhys, edi
    VMMcall _MapPhysToLinear,<esi,R2_LFB_BYTES,0>
    cmp     eax, 0ffffffffh
    je      short R2_Map_Unmapped
    mov     R2LfbLinear, eax
    mov     R2LfbPhys, esi

    ; The register window must answer with the configuration id, or it is
    ; not this card's and nothing runs against it.
R2_Map_Identity:
    mov     eax, R2MmioLinear
    mov     eax, [eax+R2_PCI_COPY]
    mov     R2Result[28], eax
    cmp     eax, R2Result[24]
    jne     short R2_Map_Done
    or      R2Result[4], R2_IDENTITY_MATCH or R2_MAPPED
    jmp     short R2_Map_Done
R2_Map_Unmapped:
    mov     R2MmioLinear, 0
R2_Map_Done:
    popad
    ret
EndProc R2_Map

; EBX = offset, ECX = dword count. Returns CF set unless the whole run is
; dword aligned and inside the framebuffer window.
BeginProc R2_Check_Lfb_Run
    push    eax
    test    ebx, 3
    jnz     short R2_Check_Lfb_Bad
    cmp     ebx, R2_LFB_BYTES
    jae     short R2_Check_Lfb_Bad
    cmp     ecx, R2_LFB_BYTES / 4
    ja      short R2_Check_Lfb_Bad
    mov     eax, ecx
    shl     eax, 2
    add     eax, ebx
    cmp     eax, R2_LFB_BYTES
    ja      short R2_Check_Lfb_Bad
    pop     eax
    clc
    ret
R2_Check_Lfb_Bad:
    pop     eax
    stc
    ret
EndProc R2_Check_Lfb_Run

; EBX = register offset. CF clear if a write is allowed.
BeginProc R2_Write_Allowed
    test    ebx, 3
    jnz     short R2_Write_Refused
    cmp     ebx, R2_PC_NGUI_CTLSTAT
    je      short R2_Write_Ok
    cmp     ebx, 1a00h
    je      short R2_Write_Ok
    cmp     ebx, 1400h
    jb      short R2_Write_Refused
    cmp     ebx, 1700h
    jae     short R2_Write_Refused
R2_Write_Ok:
    clc
    ret
R2_Write_Refused:
    stc
    ret
EndProc R2_Write_Allowed

; EBX = register offset. CF clear if a read is allowed.
BeginProc R2_Read_Allowed
    test    ebx, 3
    jnz     short R2_Read_Refused
    cmp     ebx, 000ch
    je      short R2_Read_Refused
    cmp     ebx, 00b4h
    je      short R2_Read_Refused
    cmp     ebx, 0008h
    jb      short R2_Read_Refused
    cmp     ebx, 0f60h
    jb      short R2_Read_Ok
    cmp     ebx, 1400h
    jb      short R2_Read_Refused
    cmp     ebx, 1d48h
    jae     short R2_Read_Refused
    cmp     ebx, 17c0h
    jb      short R2_Read_Ok
    cmp     ebx, 17e4h
    jb      short R2_Read_Refused
R2_Read_Ok:
    clc
    ret
R2_Read_Refused:
    stc
    ret
EndProc R2_Read_Allowed

; ESI = op (code, a, b, c, result). Returns CF set if the op is refused.
BeginProc R2_Execute_One
    pushad
    mov     eax, [esi]
    mov     ebx, [esi+4]
    mov     ecx, [esi+8]
    mov     edx, [esi+12]

    cmp     eax, R2_OP_MMIO_WRITE32
    je      R2_Do_Mmio_Write32
    cmp     eax, R2_OP_MMIO_READ32
    je      R2_Do_Mmio_Read32
    cmp     eax, R2_OP_LFB_WRITE32
    je      R2_Do_Lfb_Write32
    cmp     eax, R2_OP_LFB_READ32
    je      R2_Do_Lfb_Read32
    cmp     eax, R2_OP_LFB_FILL32
    je      R2_Do_Lfb_Fill32
    cmp     eax, R2_OP_LFB_GRADIENT
    je      R2_Do_Lfb_Gradient
    cmp     eax, R2_OP_LFB_READ_BLOCK
    je      R2_Do_Lfb_Read_Block
    cmp     eax, R2_OP_WAIT_IDLE
    je      R2_Do_Wait_Idle
    jmp     R2_Do_Refuse

; a = register offset, b = value.
R2_Do_Mmio_Write32:
    call    R2_Write_Allowed
    jc      R2_Do_Refuse
    mov     edi, R2MmioLinear
    mov     [edi+ebx], ecx
    jmp     R2_Do_Done

; a = register offset.
R2_Do_Mmio_Read32:
    call    R2_Read_Allowed
    jc      R2_Do_Refuse
    mov     edi, R2MmioLinear
    mov     eax, [edi+ebx]
    mov     [esi+16], eax
    jmp     R2_Do_Done

; a = offset, b = value.
R2_Do_Lfb_Write32:
    push    ecx
    mov     ecx, 1
    call    R2_Check_Lfb_Run
    pop     ecx
    jc      R2_Do_Refuse
    mov     edi, R2LfbLinear
    mov     [edi+ebx], ecx
    jmp     R2_Do_Done

; a = offset.
R2_Do_Lfb_Read32:
    mov     ecx, 1
    call    R2_Check_Lfb_Run
    jc      R2_Do_Refuse
    mov     edi, R2LfbLinear
    mov     eax, [edi+ebx]
    mov     [esi+16], eax
    jmp     R2_Do_Done

; a = offset, b = value, c = dword count.
R2_Do_Lfb_Fill32:
    push    ecx
    mov     ecx, edx
    call    R2_Check_Lfb_Run
    pop     eax
    jc      R2_Do_Refuse
    mov     edi, R2LfbLinear
    add     edi, ebx
    mov     ecx, edx
    cld
    rep     stosd
    jmp     R2_Do_Done

; a = offset, c = dword count. Dword i holds 2i (low half) and 2i+1 (high
; half), counted from the start of the run.
R2_Do_Lfb_Gradient:
    mov     ecx, edx
    call    R2_Check_Lfb_Run
    jc      R2_Do_Refuse
    mov     edi, R2LfbLinear
    add     edi, ebx
    xor     eax, eax
R2_Do_Lfb_Gradient_Next:
    jecxz   short R2_Do_Lfb_Gradient_Done
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
    jmp     short R2_Do_Lfb_Gradient_Next
R2_Do_Lfb_Gradient_Done:
    jmp     R2_Do_Done

; a = offset, c = dword count (at most the block). Copies into the block.
R2_Do_Lfb_Read_Block:
    cmp     edx, R2_BLOCK_DWORDS
    ja      R2_Do_Refuse
    mov     ecx, edx
    call    R2_Check_Lfb_Run
    jc      R2_Do_Refuse
    push    esi
    mov     esi, R2LfbLinear
    add     esi, ebx
    mov     edi, OFFSET32 R2Block
    mov     ecx, edx
    cld
    rep     movsd
    pop     esi
    jmp     R2_Do_Done

; Result: reads taken in all, or R2_TIMEOUT.
R2_Do_Wait_Idle:
    mov     edi, R2MmioLinear
    xor     edx, edx
R2_Do_Wait_Idle_Gui:
    inc     edx
    mov     eax, [edi+R2_GUI_STAT]
    cmp     eax, 0ffffffffh
    je      short R2_Do_Wait_Idle_Timeout
    test    eax, R2_GUI_ACTIVE
    jnz     short R2_Do_Wait_Idle_Gui_Again
    and     eax, R2_GUI_FIFO_MASK
    cmp     eax, R2_GUI_FIFO_SLOTS
    jae     short R2_Do_Wait_Idle_Flush
R2_Do_Wait_Idle_Gui_Again:
    cmp     edx, R2_WAIT_LIMIT
    jb      short R2_Do_Wait_Idle_Gui
    jmp     short R2_Do_Wait_Idle_Timeout
R2_Do_Wait_Idle_Flush:
    mov     eax, [edi+R2_PC_NGUI_CTLSTAT]
    or      eax, R2_PC_FLUSH_ALL
    mov     [edi+R2_PC_NGUI_CTLSTAT], eax
    xor     ecx, ecx
R2_Do_Wait_Idle_Cache:
    inc     ecx
    mov     eax, [edi+R2_PC_NGUI_CTLSTAT]
    test    eax, R2_PC_BUSY
    jz      short R2_Do_Wait_Idle_Done
    cmp     ecx, R2_WAIT_LIMIT
    jb      short R2_Do_Wait_Idle_Cache
R2_Do_Wait_Idle_Timeout:
    mov     edx, R2_TIMEOUT
    jmp     short R2_Do_Wait_Idle_Store
R2_Do_Wait_Idle_Done:
    add     edx, ecx
R2_Do_Wait_Idle_Store:
    mov     [esi+16], edx
    jmp     short R2_Do_Done

R2_Do_Refuse:
    popad
    stc
    ret
R2_Do_Done:
    popad
    clc
    ret
EndProc R2_Execute_One

BeginProc R2_Run
    pushad
    call    R2_Map
    test    R2Result[4], R2_MAPPED
    jz      short R2_Run_Done
    mov     ecx, R2Count
    mov     R2Result[20], ecx
    mov     esi, OFFSET32 R2Ops
    xor     ebx, ebx
    jecxz   short R2_Run_All
R2_Run_Next:
    call    R2_Execute_One
    jc      short R2_Run_Refused
    add     esi, R2_OP_DWORDS * 4
    inc     ebx
    cmp     ebx, ecx
    jb      short R2_Run_Next
R2_Run_All:
    or      R2Result[4], R2_RAN
    jmp     short R2_Run_Count
R2_Run_Refused:
    or      R2Result[4], R2_REFUSED
    mov     R2Result[20], ebx
R2_Run_Count:
    mov     R2Result[16], ebx
R2_Run_Done:
    popad
    ret
EndProc R2_Run

BeginProc R2_W32_DeviceIoControl
    cmp     ecx, DIOC_OPEN
    je      R2_Dioc_Ok
    cmp     ecx, DIOC_CLOSEHANDLE
    je      R2_Dioc_Ok
    cmp     ecx, R2_DIOC_RUN
    jne     R2_Dioc_Fail

    pushad
    mov     ebx, esi
    mov     ebp, [ebx.lpvInBuffer]
    test    ebp, ebp
    jz      R2_Dioc_Copy_Fail
    mov     ecx, [ebx.cbInBuffer]
    cmp     ecx, R2_IN_MIN_BYTES
    jb      R2_Dioc_Copy_Fail
    mov     edi, [ebx.lpvOutBuffer]
    test    edi, edi
    jz      R2_Dioc_Copy_Fail
    cmp     [ebx.cbOutBuffer], R2_RESULT_DWORDS * 4
    jb      R2_Dioc_Copy_Fail

    ; The count must fit both the op table and the bytes supplied.
    mov     eax, [ebp]
    cmp     eax, R2_OP_MAX
    ja      R2_Dioc_Copy_Fail
    mov     edx, eax
    imul    edx, R2_OP_DWORDS * 4
    add     edx, 4
    cmp     ecx, edx
    jb      R2_Dioc_Copy_Fail
    mov     R2Count, eax

    mov     R2Result[4], 0
    mov     R2Result[16], 0
    mov     R2Result[20], 0
    lea     esi, [ebp+4]
    mov     edi, OFFSET32 R2Ops
    mov     ecx, eax
    imul    ecx, R2_OP_DWORDS
    cld
    rep     movsd

    call    R2_Run

    mov     edi, [ebx.lpvOutBuffer]
    mov     esi, OFFSET32 R2Result
    mov     ecx, R2_RESULT_DWORDS
    cld
    rep     movsd
    mov     eax, [ebx.lpcbBytesReturned]
    test    eax, eax
    jz      short R2_Dioc_Copy_Done
    mov     dword ptr [eax], R2_RESULT_DWORDS * 4
R2_Dioc_Copy_Done:
    popad
R2_Dioc_Ok:
    xor     eax, eax
    ret

R2_Dioc_Copy_Fail:
    popad
R2_Dioc_Fail:
    mov     eax, 1
    ret
EndProc R2_W32_DeviceIoControl

BeginProc R2_Dynamic_Init
    clc
    ret
EndProc R2_Dynamic_Init

BeginProc R2_Dynamic_Exit
    clc
    ret
EndProc R2_Dynamic_Exit

Begin_Control_Dispatch R2
    Control_Dispatch Sys_Dynamic_Device_Init, R2_Dynamic_Init
    Control_Dispatch Sys_Dynamic_Device_Exit, R2_Dynamic_Exit
    Control_Dispatch W32_DeviceIoControl, R2_W32_DeviceIoControl
End_Control_Dispatch R2

VxD_LOCKED_CODE_ENDS

end
