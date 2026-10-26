; AMD Evergreen (Radeon HD 5450, Cedar) async DMA write probe VxD.
;
; Phase 3 of docs\plans\ati-evergreen-cedar.md, first step. Loaded
; dynamically by V9XEGDMA.EXE, which decides every access; this VxD only
; executes an op list against the card and refuses anything outside it.
; The Rage 128's R1282D.VXD (r128_2d.asm) is the model.
;
; It finds the card with configuration mechanism 1 (1002:68F9 only), maps
; BAR2 (128 KiB of registers) and a 64 KiB scratch window of VRAM through
; BAR0, and runs:
;
;   MMIO read32    - SRBM_STATUS/SOFT_RESET/STATUS2, MC_VM_FB_LOCATION,
;                    CONFIG_MEMSIZE, GRBM_STATUS and the DMA ring block.
;   MMIO write32   - the DMA ring registers r600_dma_resume writes,
;                    HDP_MEM_COHERENCY_FLUSH_CNTL, and SRBM_SOFT_RESET with
;                    0 or the DMA bit alone (evergreen_gpu_soft_reset's
;                    pulse, without the memory controller stop Linux puts
;                    round every reset). Nothing else.
;   delay          - a count of SRBM_STATUS reads, at most ED_DELAY_MAX.
;   VRAM ops       - write, read, fill, pattern, read block, all at aligned
;                    offsets inside the scratch window.
;   wait           - a VRAM dword equal to a value, or an MMIO dword under a
;                    mask equal to a value, at most the op's limit reads.
;   save/restore   - the whole scratch window to and from a copy here.
;
; The scratch window is BAR0 + 0F000000h, 240 MiB in. The driver uses the
; first 16 MiB (what VBE 4F00h reports), so the window is VRAM nothing else
; touches. It is mapped only when BAR0 is a 64-bit memory BAR below 4 GiB,
; aligned to 256 MiB, on a card whose CONFIG_MEMSIZE says 256 MiB or more:
; M-BXMFA8's BAR0 is D0000000-DFFFFFFF and its VRAM 1024 MiB
; (docs\decisions\2026-10-26-cedar-register-probe.md).
;
; Offsets: Linux radeon's evergreend.h and r600d.h
; (build\external\evergreen\linux-radeon\).

.386p

.xlist
include VMM.INC
include VWIN32.INC
.list

Declare_Virtual_Device EGDMA, 1, 0, ED_Control, \
                       Undefined_Device_ID, Undefined_Init_Order, , ,

ED_MAGIC                equ 414d4445h ; "EDMA"
ED_DIOC_RUN             equ 1
ED_VENDOR_ATI           equ 1002h
ED_DEVICE_CEDAR         equ 68f9h
ED_OP_MAX               equ 512
ED_BLOCK_DWORDS         equ 4096
ED_TIMEOUT              equ 0ffffffffh

ED_CFG_BAR0             equ 10h
ED_CFG_BAR0_HIGH        equ 14h
ED_CFG_BAR2             equ 18h
ED_CFG_BAR2_HIGH        equ 1ch
ED_BAR_IO               equ 00000001h
ED_BAR_TYPE_MASK        equ 00000006h
ED_BAR_TYPE_64          equ 00000004h
ED_MMIO_BYTES           equ 00020000h
ED_MMIO_ALIGN           equ 0001ffffh
ED_LFB_ALIGN            equ 0fffffffh
ED_SCRATCH_OFFSET       equ 0f000000h
ED_SCRATCH_BYTES        equ 00010000h
ED_CONFIG_MEMSIZE       equ 5428h
ED_MEMSIZE_MIN_MIB      equ 256
ED_MEMSIZE_MAX_MIB      equ 4096

; Op codes; the layout is four dwords: op, a, b, c.
ED_OP_MMIO_READ         equ 1   ; a offset
ED_OP_MMIO_WRITE        equ 2   ; a offset, b value
ED_OP_VRAM_WRITE        equ 3   ; a offset, b value
ED_OP_VRAM_READ         equ 4   ; a offset
ED_OP_VRAM_FILL         equ 5   ; a offset, b dwords, c value
ED_OP_VRAM_BLOCK        equ 6   ; a offset, b dwords -> block area
ED_OP_WAIT_VRAM         equ 7   ; a offset, b value, c limit
ED_OP_WAIT_MMIO         equ 8   ; a offset, b mask, c value; limit fixed
ED_OP_SAVE              equ 9
ED_OP_RESTORE           equ 10
ED_OP_VRAM_PATTERN      equ 11  ; a offset, b dwords, c seed
ED_OP_DELAY             equ 12  ; a SRBM_STATUS reads
ED_DELAY_MAX            equ 100000
ED_SRBM_STATUS          equ 0e50h
ED_SRBM_SOFT_RESET      equ 0e60h
ED_SOFT_RESET_DMA       equ 00100000h
ED_OP_BYTES             equ 16
ED_WAIT_MMIO_LIMIT      equ 2000000
ED_PATTERN_STEP         equ 9e3779b9h

; Status bits.
ED_ST_FOUND             equ 00000001h
ED_ST_MAPPED            equ 00000002h
ED_ST_IDENTITY          equ 00000004h
ED_ST_REFUSED           equ 00000008h
ED_ST_COMPLETE          equ 00000010h
ED_ST_BAD_BAR           equ 00000020h
ED_ST_TIMEOUT           equ 00000040h
ED_ST_SAVED             equ 00000080h

; Input: count, then count ops.
ED_IN_COUNT             equ 0
ED_IN_OPS               equ 4

; Output layout, in dwords. Must match struct v9x_egdma_result in
; evergreen_dma_win32.c.
ED_HEADER_DWORDS        equ 16
ED_RESULT_DWORDS        equ (ED_HEADER_DWORDS + ED_OP_MAX + ED_BLOCK_DWORDS)

VxD_LOCKED_DATA_SEG

; Reads accepted: start, limit (exclusive).
EDReadWindows label dword
    dd 0e50h, 0e54h     ; SRBM_STATUS
    dd 0e60h, 0e64h     ; SRBM_SOFT_RESET
    dd 0ec4h, 0ec8h     ; SRBM_STATUS2
    dd 2024h, 2028h     ; MC_VM_FB_LOCATION
    dd 5428h, 542ch     ; CONFIG_MEMSIZE
    dd 8010h, 8014h     ; GRBM_STATUS
    dd 0d000h, 0d010h   ; DMA_RB_CNTL, _BASE, _RPTR, _WPTR
    dd 0d01ch, 0d030h   ; DMA_RB_RPTR_ADDR_HI/LO, DMA_IB_CNTL, DMA_IB_RPTR, DMA_CNTL
    dd 0d034h, 0d038h   ; DMA_STATUS_REG
    dd 0d044h, 0d04ch   ; DMA_SEM_*_TIMER_CNTL
    dd 0d0b8h, 0d0c0h   ; DMA_TILING_CONFIG, DMA_MODE
ED_READ_WINDOW_COUNT    equ 11

; Writes accepted: exact offsets.
EDWriteOffsets label dword
    dd 5480h            ; HDP_MEM_COHERENCY_FLUSH_CNTL
    dd 0e60h            ; SRBM_SOFT_RESET, value checked in ED_Execute
    dd 0d000h           ; DMA_RB_CNTL
    dd 0d004h           ; DMA_RB_BASE
    dd 0d008h           ; DMA_RB_RPTR
    dd 0d00ch           ; DMA_RB_WPTR
    dd 0d01ch           ; DMA_RB_RPTR_ADDR_HI
    dd 0d020h           ; DMA_RB_RPTR_ADDR_LO
    dd 0d024h           ; DMA_IB_CNTL
    dd 0d02ch           ; DMA_CNTL
    dd 0d044h           ; DMA_SEM_INCOMPLETE_TIMER_CNTL
    dd 0d048h           ; DMA_SEM_WAIT_FAIL_TIMER_CNTL
    dd 0d0bch           ; DMA_MODE
ED_WRITE_OFFSET_COUNT   equ 13

EDResult label dword
    dd ED_MAGIC                 ; 00 magic
    dd 0                        ; 01 status
    dd 0                        ; 02 id dword
    dd 0                        ; 03 BAR0 low
    dd 0                        ; 04 BAR0 high
    dd 0                        ; 05 BAR2 low
    dd 0                        ; 06 BAR2 high
    dd 0                        ; 07 CONFIG_MEMSIZE
    dd 0                        ; 08 ops executed
    dd 0                        ; 09 index of the refused op
    dd 0                        ; 10 block dwords used
    dd 0                        ; 11 configuration address
    dd 0                        ; 12 reserved
    dd 0                        ; 13 reserved
    dd 0                        ; 14 reserved
    dd 0                        ; 15 reserved
EDOpResults  dd ED_OP_MAX dup (0)
EDBlock      dd ED_BLOCK_DWORDS dup (0)
EDSaved      dd (ED_SCRATCH_BYTES / 4) dup (0)
EDMmio       dd 0
EDScratch    dd 0
EDConfig     dd 0
VxD_LOCKED_DATA_ENDS

VxD_LOCKED_CODE_SEG

; EAX = PCI configuration address. Returns EAX = configuration dword.
; 0CF8h saved and restored with interrupts off.
BeginProc ED_Pci_Read
    push    ebx
    push    ecx
    push    edx
    pushfd
    cli
    mov     ebx, eax
    mov     dx, 0cf8h
    in      eax, dx
    mov     ecx, eax
    mov     eax, ebx
    out     dx, eax
    mov     dx, 0cfch
    in      eax, dx
    mov     ebx, eax
    mov     dx, 0cf8h
    mov     eax, ecx
    out     dx, eax
    mov     eax, ebx
    popfd
    pop     edx
    pop     ecx
    pop     ebx
    ret
EndProc ED_Pci_Read

; Scans buses 0-15, device slots 0-31, function 0, for 1002:68F9.
; Returns CF clear and EAX = configuration address, or CF set.
BeginProc ED_Find
    push    ebx
    push    ecx
    mov     ebx, 80000000h
ED_Find_Next:
    mov     eax, ebx
    call    ED_Pci_Read
    cmp     eax, (ED_DEVICE_CEDAR shl 16) or ED_VENDOR_ATI
    je      short ED_Find_Found
    add     ebx, 800h
    cmp     ebx, 80100000h
    jb      short ED_Find_Next
    pop     ecx
    pop     ebx
    stc
    ret
ED_Find_Found:
    mov     EDResult[8], eax
    mov     eax, ebx
    pop     ecx
    pop     ebx
    clc
    ret
EndProc ED_Find

; Finds and maps the card once. Returns CF clear when both windows are
; mapped and the identity check passed.
BeginProc ED_Map
    pushad
    cmp     EDScratch, 0
    jne     ED_Map_Ok

    call    ED_Find
    jc      ED_Map_Fail
    mov     EDConfig, eax
    mov     EDResult[44], eax
    or      EDResult[4], ED_ST_FOUND

    ; BAR2: memory, 64-bit, high half zero, 128 KiB aligned.
    mov     eax, EDConfig
    or      eax, ED_CFG_BAR2
    call    ED_Pci_Read
    mov     EDResult[20], eax
    mov     esi, eax
    mov     eax, EDConfig
    or      eax, ED_CFG_BAR2_HIGH
    call    ED_Pci_Read
    mov     EDResult[24], eax
    test    esi, ED_BAR_IO
    jnz     ED_Map_Bad_Bar
    mov     edx, esi
    and     edx, ED_BAR_TYPE_MASK
    cmp     edx, ED_BAR_TYPE_64
    jne     ED_Map_Bad_Bar
    test    eax, eax
    jnz     ED_Map_Bad_Bar
    and     esi, NOT 0fh
    jz      ED_Map_Bad_Bar
    test    esi, ED_MMIO_ALIGN
    jnz     ED_Map_Bad_Bar

    ; BAR0: memory, 64-bit, high half zero, 256 MiB aligned.
    mov     eax, EDConfig
    or      eax, ED_CFG_BAR0
    call    ED_Pci_Read
    mov     EDResult[12], eax
    mov     edi, eax
    mov     eax, EDConfig
    or      eax, ED_CFG_BAR0_HIGH
    call    ED_Pci_Read
    mov     EDResult[16], eax
    test    edi, ED_BAR_IO
    jnz     ED_Map_Bad_Bar
    mov     edx, edi
    and     edx, ED_BAR_TYPE_MASK
    cmp     edx, ED_BAR_TYPE_64
    jne     ED_Map_Bad_Bar
    test    eax, eax
    jnz     ED_Map_Bad_Bar
    and     edi, NOT 0fh
    jz      ED_Map_Bad_Bar
    test    edi, ED_LFB_ALIGN
    jnz     ED_Map_Bad_Bar

    VMMcall _MapPhysToLinear,<esi,ED_MMIO_BYTES,0>
    cmp     eax, 0ffffffffh
    je      ED_Map_Fail
    mov     EDMmio, eax

    ; The identity check before the scratch window is mapped: at least
    ; 256 MiB of VRAM, so BAR0 + 240 MiB is VRAM.
    mov     edx, [eax+ED_CONFIG_MEMSIZE]
    mov     EDResult[28], edx
    cmp     edx, ED_MEMSIZE_MIN_MIB
    jb      ED_Map_Fail
    cmp     edx, ED_MEMSIZE_MAX_MIB
    ja      ED_Map_Fail
    or      EDResult[4], ED_ST_IDENTITY

    add     edi, ED_SCRATCH_OFFSET
    VMMcall _MapPhysToLinear,<edi,ED_SCRATCH_BYTES,0>
    cmp     eax, 0ffffffffh
    je      ED_Map_Fail
    mov     EDScratch, eax
    or      EDResult[4], ED_ST_MAPPED

ED_Map_Ok:
    popad
    clc
    ret
ED_Map_Bad_Bar:
    or      EDResult[4], ED_ST_BAD_BAR
ED_Map_Fail:
    popad
    stc
    ret
EndProc ED_Map

; EDX = offset. CF clear if readable.
BeginProc ED_Read_Allowed
    push    ecx
    push    esi
    test    edx, 3
    jnz     short ED_Read_Refused
    mov     esi, OFFSET32 EDReadWindows
    mov     ecx, ED_READ_WINDOW_COUNT
ED_Read_Next:
    cmp     edx, [esi]
    jb      short ED_Read_Skip
    cmp     edx, [esi+4]
    jb      short ED_Read_Accepted
ED_Read_Skip:
    add     esi, 8
    dec     ecx
    jnz     short ED_Read_Next
ED_Read_Refused:
    pop     esi
    pop     ecx
    stc
    ret
ED_Read_Accepted:
    pop     esi
    pop     ecx
    clc
    ret
EndProc ED_Read_Allowed

; EDX = offset. CF clear if writable.
BeginProc ED_Write_Allowed
    push    ecx
    push    esi
    mov     esi, OFFSET32 EDWriteOffsets
    mov     ecx, ED_WRITE_OFFSET_COUNT
ED_Write_Next:
    cmp     edx, [esi]
    je      short ED_Write_Accepted
    add     esi, 4
    dec     ecx
    jnz     short ED_Write_Next
    pop     esi
    pop     ecx
    stc
    ret
ED_Write_Accepted:
    pop     esi
    pop     ecx
    clc
    ret
EndProc ED_Write_Allowed

; EDX = scratch offset, ECX = dwords (at least 1). CF clear if the run lies
; inside the window and is aligned.
BeginProc ED_Vram_Allowed
    push    eax
    test    edx, 3
    jnz     short ED_Vram_Refused
    test    ecx, ecx
    jz      short ED_Vram_Refused
    cmp     ecx, ED_SCRATCH_BYTES / 4
    ja      short ED_Vram_Refused
    cmp     edx, ED_SCRATCH_BYTES
    jae     short ED_Vram_Refused
    mov     eax, ecx
    shl     eax, 2
    add     eax, edx
    cmp     eax, ED_SCRATCH_BYTES
    ja      short ED_Vram_Refused
    pop     eax
    clc
    ret
ED_Vram_Refused:
    pop     eax
    stc
    ret
EndProc ED_Vram_Allowed

; ESI = op (four dwords). Returns EAX = the op's result, CF set if refused.
BeginProc ED_Execute
    push    ebx
    push    ecx
    push    edx
    push    edi
    mov     eax, [esi]
    mov     edx, [esi+4]

    cmp     eax, ED_OP_MMIO_READ
    jne     short ED_Exec_Not_Read
    call    ED_Read_Allowed
    jc      ED_Exec_Refused
    mov     ebx, EDMmio
    mov     eax, [ebx+edx]
    jmp     ED_Exec_Done
ED_Exec_Not_Read:

    cmp     eax, ED_OP_MMIO_WRITE
    jne     short ED_Exec_Not_Write
    call    ED_Write_Allowed
    jc      ED_Exec_Refused
    mov     eax, [esi+8]
    ; SRBM_SOFT_RESET takes the DMA bit or nothing: no other block of the
    ; card is ever reset from here.
    cmp     edx, ED_SRBM_SOFT_RESET
    jne     short ED_Exec_Write_Value_Ok
    test    eax, NOT ED_SOFT_RESET_DMA
    jnz     ED_Exec_Refused
ED_Exec_Write_Value_Ok:
    mov     ebx, EDMmio
    mov     [ebx+edx], eax
    ; Post the write before the next op.
    mov     eax, [ebx+ED_CONFIG_MEMSIZE]
    xor     eax, eax
    jmp     ED_Exec_Done
ED_Exec_Not_Write:

    cmp     eax, ED_OP_VRAM_WRITE
    jne     short ED_Exec_Not_Vram_Write
    mov     ecx, 1
    call    ED_Vram_Allowed
    jc      ED_Exec_Refused
    mov     ebx, EDScratch
    mov     eax, [esi+8]
    mov     [ebx+edx], eax
    xor     eax, eax
    jmp     ED_Exec_Done
ED_Exec_Not_Vram_Write:

    cmp     eax, ED_OP_VRAM_READ
    jne     short ED_Exec_Not_Vram_Read
    mov     ecx, 1
    call    ED_Vram_Allowed
    jc      ED_Exec_Refused
    mov     ebx, EDScratch
    mov     eax, [ebx+edx]
    jmp     ED_Exec_Done
ED_Exec_Not_Vram_Read:

    cmp     eax, ED_OP_VRAM_FILL
    jne     short ED_Exec_Not_Fill
    mov     ecx, [esi+8]
    call    ED_Vram_Allowed
    jc      ED_Exec_Refused
    mov     edi, EDScratch
    add     edi, edx
    mov     eax, [esi+12]
    cld
    rep     stosd
    xor     eax, eax
    jmp     ED_Exec_Done
ED_Exec_Not_Fill:

    cmp     eax, ED_OP_VRAM_PATTERN
    jne     short ED_Exec_Not_Pattern
    mov     ecx, [esi+8]
    call    ED_Vram_Allowed
    jc      ED_Exec_Refused
    mov     edi, EDScratch
    add     edi, edx
    mov     eax, [esi+12]
ED_Exec_Pattern_Next:
    mov     [edi], eax
    add     eax, ED_PATTERN_STEP
    add     edi, 4
    dec     ecx
    jnz     short ED_Exec_Pattern_Next
    xor     eax, eax
    jmp     ED_Exec_Done
ED_Exec_Not_Pattern:

    cmp     eax, ED_OP_VRAM_BLOCK
    jne     short ED_Exec_Not_Block
    mov     ecx, [esi+8]
    call    ED_Vram_Allowed
    jc      ED_Exec_Refused
    mov     eax, EDResult[40]
    mov     ebx, eax
    add     ebx, ecx
    cmp     ebx, ED_BLOCK_DWORDS
    ja      ED_Exec_Refused
    mov     EDResult[40], ebx
    push    esi
    mov     esi, EDScratch
    add     esi, edx
    lea     edi, EDBlock[eax*4]
    cld
    rep     movsd
    pop     esi
    jmp     ED_Exec_Done
ED_Exec_Not_Block:

    cmp     eax, ED_OP_WAIT_VRAM
    jne     short ED_Exec_Not_Wait_Vram
    mov     ecx, 1
    call    ED_Vram_Allowed
    jc      ED_Exec_Refused
    mov     ebx, EDScratch
    add     ebx, edx
    mov     edx, [esi+8]
    mov     ecx, [esi+12]
    xor     eax, eax
ED_Exec_Wait_Vram_Next:
    cmp     [ebx], edx
    je      ED_Exec_Done
    inc     eax
    cmp     eax, ecx
    jb      short ED_Exec_Wait_Vram_Next
    or      EDResult[4], ED_ST_TIMEOUT
    mov     eax, ED_TIMEOUT
    jmp     ED_Exec_Done
ED_Exec_Not_Wait_Vram:

    cmp     eax, ED_OP_WAIT_MMIO
    jne     short ED_Exec_Not_Wait_Mmio
    call    ED_Read_Allowed
    jc      ED_Exec_Refused
    mov     ebx, EDMmio
    add     ebx, edx
    xor     eax, eax
ED_Exec_Wait_Mmio_Next:
    mov     ecx, [ebx]
    and     ecx, [esi+8]
    cmp     ecx, [esi+12]
    je      ED_Exec_Done
    inc     eax
    cmp     eax, ED_WAIT_MMIO_LIMIT
    jb      short ED_Exec_Wait_Mmio_Next
    or      EDResult[4], ED_ST_TIMEOUT
    mov     eax, ED_TIMEOUT
    jmp     ED_Exec_Done
ED_Exec_Not_Wait_Mmio:

    cmp     eax, ED_OP_DELAY
    jne     short ED_Exec_Not_Delay
    mov     ecx, edx
    test    ecx, ecx
    jz      ED_Exec_Refused
    cmp     ecx, ED_DELAY_MAX
    ja      ED_Exec_Refused
    mov     ebx, EDMmio
ED_Exec_Delay_Next:
    mov     eax, [ebx+ED_SRBM_STATUS]
    dec     ecx
    jnz     short ED_Exec_Delay_Next
    jmp     ED_Exec_Done
ED_Exec_Not_Delay:

    cmp     eax, ED_OP_SAVE
    jne     short ED_Exec_Not_Save
    push    esi
    mov     esi, EDScratch
    mov     edi, OFFSET32 EDSaved
    mov     ecx, ED_SCRATCH_BYTES / 4
    cld
    rep     movsd
    pop     esi
    or      EDResult[4], ED_ST_SAVED
    xor     eax, eax
    jmp     short ED_Exec_Done
ED_Exec_Not_Save:

    cmp     eax, ED_OP_RESTORE
    jne     short ED_Exec_Refused
    ; Only what was saved in this run goes back.
    test    EDResult[4], ED_ST_SAVED
    jz      short ED_Exec_Refused
    push    esi
    mov     esi, OFFSET32 EDSaved
    mov     edi, EDScratch
    mov     ecx, ED_SCRATCH_BYTES / 4
    cld
    rep     movsd
    pop     esi
    xor     eax, eax

ED_Exec_Done:
    pop     edi
    pop     edx
    pop     ecx
    pop     ebx
    clc
    ret
ED_Exec_Refused:
    pop     edi
    pop     edx
    pop     ecx
    pop     ebx
    stc
    ret
EndProc ED_Execute

; EBP = DIOC input buffer, ECX = its byte count.
BeginProc ED_Run
    pushad
    mov     eax, [ebp+ED_IN_COUNT]
    cmp     eax, ED_OP_MAX
    ja      short ED_Run_Refused
    sub     ecx, ED_IN_OPS
    shr     ecx, 4
    cmp     eax, ecx
    ja      short ED_Run_Refused
    call    ED_Map
    jc      short ED_Run_Done

    mov     ecx, [ebp+ED_IN_COUNT]
    lea     esi, [ebp+ED_IN_OPS]
    mov     edi, OFFSET32 EDOpResults
    xor     ebx, ebx
    jecxz   short ED_Run_Complete
ED_Run_Next:
    call    ED_Execute
    jc      short ED_Run_Refused_At
    mov     [edi], eax
    inc     ebx
    mov     EDResult[32], ebx
    add     esi, ED_OP_BYTES
    add     edi, 4
    dec     ecx
    jnz     short ED_Run_Next
ED_Run_Complete:
    or      EDResult[4], ED_ST_COMPLETE
    jmp     short ED_Run_Done
ED_Run_Refused_At:
    mov     EDResult[36], ebx
ED_Run_Refused:
    or      EDResult[4], ED_ST_REFUSED
ED_Run_Done:
    popad
    ret
EndProc ED_Run

BeginProc ED_Reset_Result
    pushad
    mov     edi, OFFSET32 EDResult + 4
    mov     ecx, ED_RESULT_DWORDS - 1
    xor     eax, eax
    cld
    rep     stosd
    popad
    ret
EndProc ED_Reset_Result

BeginProc ED_W32_DeviceIoControl
    cmp     ecx, DIOC_OPEN
    je      ED_Dioc_Ok
    cmp     ecx, DIOC_CLOSEHANDLE
    je      ED_Dioc_Ok
    cmp     ecx, ED_DIOC_RUN
    jne     ED_Dioc_Fail

    pushad
    mov     ebx, esi
    mov     ebp, [ebx.lpvInBuffer]
    test    ebp, ebp
    jz      short ED_Dioc_Copy_Fail
    mov     ecx, [ebx.cbInBuffer]
    cmp     ecx, ED_IN_OPS
    jb      short ED_Dioc_Copy_Fail
    mov     edi, [ebx.lpvOutBuffer]
    test    edi, edi
    jz      short ED_Dioc_Copy_Fail
    cmp     [ebx.cbOutBuffer], ED_RESULT_DWORDS * 4
    jb      short ED_Dioc_Copy_Fail

    call    ED_Reset_Result
    mov     ecx, [ebx.cbInBuffer]
    call    ED_Run

    mov     edi, [ebx.lpvOutBuffer]
    mov     esi, OFFSET32 EDResult
    mov     ecx, ED_RESULT_DWORDS
    cld
    rep     movsd
    mov     eax, [ebx.lpcbBytesReturned]
    test    eax, eax
    jz      short ED_Dioc_Copy_Done
    mov     dword ptr [eax], ED_RESULT_DWORDS * 4
ED_Dioc_Copy_Done:
    popad
ED_Dioc_Ok:
    xor     eax, eax
    ret

ED_Dioc_Copy_Fail:
    popad
ED_Dioc_Fail:
    mov     eax, 1
    ret
EndProc ED_W32_DeviceIoControl

BeginProc ED_Dynamic_Init
    clc
    ret
EndProc ED_Dynamic_Init

BeginProc ED_Dynamic_Exit
    clc
    ret
EndProc ED_Dynamic_Exit

Begin_Control_Dispatch ED
    Control_Dispatch Sys_Dynamic_Device_Init, ED_Dynamic_Init
    Control_Dispatch Sys_Dynamic_Device_Exit, ED_Dynamic_Exit
    Control_Dispatch W32_DeviceIoControl, ED_W32_DeviceIoControl
End_Control_Dispatch ED

VxD_LOCKED_CODE_ENDS

end
