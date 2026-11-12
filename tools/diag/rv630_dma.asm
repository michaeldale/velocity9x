; ATI Radeon HD 2600 XT (RV630, 1002:9588) async DMA write probe VxD.
;
; Phase 3 of docs\plans\ati-rv630-hd2600xt.md, first step. Loaded
; dynamically by V9XRVDMA.EXE, which decides every access; this VxD only
; executes an op list against the card and refuses anything outside it.
; The Evergreen one (evergreen_dma.asm) is the model. What differs is the
; device, the register BAR's size, CONFIG_MEMSIZE's unit, the register
; lists and the DMA soft-reset bit, all from Linux r600d.h and r600_dma.c.
;
; It finds the card with configuration mechanism 1 (1002:9588), maps BAR2
; (64 KiB of registers) and a 64 KiB scratch window of VRAM through BAR0,
; and runs:
;
;   MMIO read32    - SRBM_STATUS/SOFT_RESET, MC_VM_FB_LOCATION,
;                    CONFIG_MEMSIZE, GRBM_STATUS and the DMA ring block.
;   MMIO write32   - the DMA ring registers r600_dma_resume writes (R6xx
;                    has no DMA_MODE: Linux writes it from RV770 on),
;                    HDP_MEM_COHERENCY_FLUSH_CNTL, and SRBM_SOFT_RESET
;                    changed in the DMA bit alone (bit 12 on R6xx, r600d.h
;                    SOFT_RESET_DMA; RV770 moved it to bit 20), without the
;                    memory controller stop Linux puts round every reset.
;                    Nothing else.
;   delay          - a count of SRBM_STATUS reads, at most RD_DELAY_MAX.
;   VRAM ops       - write, read, fill, pattern, read block, all at aligned
;                    offsets inside the scratch window.
;   wait           - a VRAM dword equal to a value, or an MMIO dword under a
;                    mask equal to a value, at most the op's limit reads.
;   save/restore   - the whole scratch window to and from a copy here.
;
; The scratch window is BAR0 + 0F000000h, 240 MiB in. The driver uses the
; first 16 MiB (what VBE 4F00h reports), so the window is VRAM nothing else
; touches. It is mapped only when BAR0 is a memory BAR below 4 GiB (64-bit
; with a zero high half, or 32-bit), aligned to 256 MiB, on a card whose
; CONFIG_MEMSIZE (in bytes on R6xx, r600_mc_init) says 256 MiB or more:
; M-NZHWBR's BAR0 is E0000000 and its VRAM 256 MiB
; (docs\decisions\2026-11-13-rv630-register-probe.md), so the window is its
; last 16 MiB.
;
; Offsets: Linux radeon's r600d.h (build\external\evergreen\linux-radeon\).

.386p

.xlist
include VMM.INC
include VWIN32.INC
.list

Declare_Virtual_Device RVDMA, 1, 0, RD_Control, \
                       Undefined_Device_ID, Undefined_Init_Order, , ,

RD_MAGIC                equ 414d4452h ; "RDMA"
RD_DIOC_RUN             equ 1
RD_VENDOR_ATI           equ 1002h
RD_DEVICE_RV630         equ 9588h
RD_OP_MAX              equ 512
RD_BLOCK_DWORDS         equ 4096
RD_TIMEOUT              equ 0ffffffffh

RD_CFG_BAR0             equ 10h
RD_CFG_BAR0_HIGH        equ 14h
RD_CFG_BAR2             equ 18h
RD_CFG_BAR2_HIGH        equ 1ch
RD_BAR_IO               equ 00000001h
RD_BAR_TYPE_MASK        equ 00000006h
RD_BAR_TYPE_64          equ 00000004h
RD_BAR_TYPE_32          equ 00000000h
RD_MMIO_BYTES           equ 00010000h
RD_MMIO_ALIGN           equ 0000ffffh
RD_LFB_ALIGN            equ 0fffffffh
RD_SCRATCH_OFFSET       equ 0f000000h
RD_SCRATCH_BYTES        equ 00010000h
RD_CONFIG_MEMSIZE       equ 5428h
RD_MEMSIZE_MIN          equ 10000000h
RD_MEMSIZE_MAX          equ 40000000h

; Op codes; the layout is four dwords: op, a, b, c.
RD_OP_MMIO_READ         equ 1   ; a offset
RD_OP_MMIO_WRITE        equ 2   ; a offset, b value
RD_OP_VRAM_WRITE        equ 3   ; a offset, b value
RD_OP_VRAM_READ         equ 4   ; a offset
RD_OP_VRAM_FILL         equ 5   ; a offset, b dwords, c value
RD_OP_VRAM_BLOCK        equ 6   ; a offset, b dwords -> block area
RD_OP_WAIT_VRAM         equ 7   ; a offset, b value, c limit
RD_OP_WAIT_MMIO         equ 8   ; a offset, b mask, c value; limit fixed
RD_OP_SAVE              equ 9
RD_OP_RESTORE           equ 10
RD_OP_VRAM_PATTERN      equ 11  ; a offset, b dwords, c seed
RD_OP_DELAY             equ 12  ; a SRBM_STATUS reads
RD_DELAY_MAX            equ 100000
RD_SRBM_STATUS          equ 0e50h
RD_SRBM_SOFT_RESET      equ 0e60h
RD_SOFT_RESET_DMA       equ 00001000h
RD_OP_BYTES             equ 16
RD_WAIT_MMIO_LIMIT      equ 2000000
RD_PATTERN_STEP         equ 9e3779b9h

; Status bits.
RD_ST_FOUND             equ 00000001h
RD_ST_MAPPED            equ 00000002h
RD_ST_IDENTITY          equ 00000004h
RD_ST_REFUSED           equ 00000008h
RD_ST_COMPLETE          equ 00000010h
RD_ST_BAD_BAR           equ 00000020h
RD_ST_TIMEOUT           equ 00000040h
RD_ST_SAVED             equ 00000080h

; Input: count, then count ops.
RD_IN_COUNT             equ 0
RD_IN_OPS               equ 4

; Output layout, in dwords. Must match struct v9x_rvdma_result in
; rv630_dma_win32.c.
RD_HEADER_DWORDS        equ 16
RD_RESULT_DWORDS        equ (RD_HEADER_DWORDS + RD_OP_MAX + RD_BLOCK_DWORDS)

VxD_LOCKED_DATA_SEG

; Reads accepted: start, limit (exclusive).
RDReadWindows label dword
    dd 0e50h, 0e54h     ; SRBM_STATUS
    dd 0e60h, 0e64h     ; SRBM_SOFT_RESET
    dd 2180h, 2184h     ; MC_VM_FB_LOCATION
    dd 5428h, 542ch     ; CONFIG_MEMSIZE
    dd 8010h, 8014h     ; GRBM_STATUS
    dd 0d000h, 0d010h   ; DMA_RB_CNTL, _BASE, _RPTR, _WPTR
    dd 0d01ch, 0d030h   ; DMA_RB_RPTR_ADDR_HI/LO, DMA_IB_CNTL, DMA_IB_RPTR, DMA_CNTL
    dd 0d034h, 0d038h   ; DMA_STATUS_REG
    dd 0d044h, 0d04ch   ; DMA_SEM_*_TIMER_CNTL
RD_READ_WINDOW_COUNT    equ 9

; Writes accepted: exact offsets.
RDWriteOffsets label dword
    dd 5480h            ; HDP_MEM_COHERENCY_FLUSH_CNTL
    dd 0e60h            ; SRBM_SOFT_RESET, value checked in RD_Execute
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
RD_WRITE_OFFSET_COUNT   equ 12

RDResult label dword
    dd RD_MAGIC                 ; 00 magic
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
RDOpResults  dd RD_OP_MAX dup (0)
RDBlock      dd RD_BLOCK_DWORDS dup (0)
RDSaved      dd (RD_SCRATCH_BYTES / 4) dup (0)
RDMmio       dd 0
RDScratch    dd 0
RDConfig     dd 0
VxD_LOCKED_DATA_ENDS

VxD_LOCKED_CODE_SEG

; EAX = PCI configuration address. Returns EAX = configuration dword.
; 0CF8h saved and restored with interrupts off.
BeginProc RD_Pci_Read
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
EndProc RD_Pci_Read

; Scans buses 0-15, device slots 0-31, function 0, for 1002:9588.
; Returns CF clear and EAX = configuration address, or CF set.
BeginProc RD_Find
    push    ebx
    push    ecx
    mov     ebx, 80000000h
RD_Find_Next:
    mov     eax, ebx
    call    RD_Pci_Read
    cmp     eax, (RD_DEVICE_RV630 shl 16) or RD_VENDOR_ATI
    je      short RD_Find_Found
    add     ebx, 800h
    cmp     ebx, 80100000h
    jb      short RD_Find_Next
    pop     ecx
    pop     ebx
    stc
    ret
RD_Find_Found:
    mov     RDResult[8], eax
    mov     eax, ebx
    pop     ecx
    pop     ebx
    clc
    ret
EndProc RD_Find

; Finds and maps the card once. Returns CF clear when both windows are
; mapped and the identity check passed.
BeginProc RD_Map
    pushad
    cmp     RDScratch, 0
    jne     RD_Map_Ok

    call    RD_Find
    jc      RD_Map_Fail
    mov     RDConfig, eax
    mov     RDResult[44], eax
    or      RDResult[4], RD_ST_FOUND

    ; BAR2: memory, 64-bit with the high half zero or 32-bit, 64 KiB
    ; aligned. On a 32-bit BAR the next dword is another BAR, not a high
    ; half, and is reported but not checked.
    mov     eax, RDConfig
    or      eax, RD_CFG_BAR2
    call    RD_Pci_Read
    mov     RDResult[20], eax
    mov     esi, eax
    mov     eax, RDConfig
    or      eax, RD_CFG_BAR2_HIGH
    call    RD_Pci_Read
    mov     RDResult[24], eax
    test    esi, RD_BAR_IO
    jnz     RD_Map_Bad_Bar
    mov     edx, esi
    and     edx, RD_BAR_TYPE_MASK
    cmp     edx, RD_BAR_TYPE_32
    je      short RD_Map_Bar2_Low
    cmp     edx, RD_BAR_TYPE_64
    jne     RD_Map_Bad_Bar
    test    eax, eax
    jnz     RD_Map_Bad_Bar
RD_Map_Bar2_Low:
    and     esi, NOT 0fh
    jz      RD_Map_Bad_Bar
    test    esi, RD_MMIO_ALIGN
    jnz     RD_Map_Bad_Bar

    ; BAR0: memory, 64-bit with the high half zero or 32-bit, 256 MiB
    ; aligned.
    mov     eax, RDConfig
    or      eax, RD_CFG_BAR0
    call    RD_Pci_Read
    mov     RDResult[12], eax
    mov     edi, eax
    mov     eax, RDConfig
    or      eax, RD_CFG_BAR0_HIGH
    call    RD_Pci_Read
    mov     RDResult[16], eax
    test    edi, RD_BAR_IO
    jnz     RD_Map_Bad_Bar
    mov     edx, edi
    and     edx, RD_BAR_TYPE_MASK
    cmp     edx, RD_BAR_TYPE_32
    je      short RD_Map_Bar0_Low
    cmp     edx, RD_BAR_TYPE_64
    jne     RD_Map_Bad_Bar
    test    eax, eax
    jnz     RD_Map_Bad_Bar
RD_Map_Bar0_Low:
    and     edi, NOT 0fh
    jz      RD_Map_Bad_Bar
    test    edi, RD_LFB_ALIGN
    jnz     RD_Map_Bad_Bar

    VMMcall _MapPhysToLinear,<esi,RD_MMIO_BYTES,0>
    cmp     eax, 0ffffffffh
    je      RD_Map_Fail
    mov     RDMmio, eax

    ; The identity check before the scratch window is mapped: at least
    ; 256 MiB of VRAM, so BAR0 + 240 MiB is VRAM. R6xx's CONFIG_MEMSIZE
    ; is in bytes.
    mov     edx, [eax+RD_CONFIG_MEMSIZE]
    mov     RDResult[28], edx
    cmp     edx, RD_MEMSIZE_MIN
    jb      RD_Map_Fail
    cmp     edx, RD_MEMSIZE_MAX
    ja      RD_Map_Fail
    or      RDResult[4], RD_ST_IDENTITY

    add     edi, RD_SCRATCH_OFFSET
    VMMcall _MapPhysToLinear,<edi,RD_SCRATCH_BYTES,0>
    cmp     eax, 0ffffffffh
    je      RD_Map_Fail
    mov     RDScratch, eax
    or      RDResult[4], RD_ST_MAPPED

RD_Map_Ok:
    popad
    clc
    ret
RD_Map_Bad_Bar:
    or      RDResult[4], RD_ST_BAD_BAR
RD_Map_Fail:
    popad
    stc
    ret
EndProc RD_Map

; EDX = offset. CF clear if readable.
BeginProc RD_Read_Allowed
    push    ecx
    push    esi
    test    edx, 3
    jnz     short RD_Read_Refused
    mov     esi, OFFSET32 RDReadWindows
    mov     ecx, RD_READ_WINDOW_COUNT
RD_Read_Next:
    cmp     edx, [esi]
    jb      short RD_Read_Skip
    cmp     edx, [esi+4]
    jb      short RD_Read_Accepted
RD_Read_Skip:
    add     esi, 8
    dec     ecx
    jnz     short RD_Read_Next
RD_Read_Refused:
    pop     esi
    pop     ecx
    stc
    ret
RD_Read_Accepted:
    pop     esi
    pop     ecx
    clc
    ret
EndProc RD_Read_Allowed

; EDX = offset. CF clear if writable.
BeginProc RD_Write_Allowed
    push    ecx
    push    esi
    mov     esi, OFFSET32 RDWriteOffsets
    mov     ecx, RD_WRITE_OFFSET_COUNT
RD_Write_Next:
    cmp     edx, [esi]
    je      short RD_Write_Accepted
    add     esi, 4
    dec     ecx
    jnz     short RD_Write_Next
    pop     esi
    pop     ecx
    stc
    ret
RD_Write_Accepted:
    pop     esi
    pop     ecx
    clc
    ret
EndProc RD_Write_Allowed

; EDX = scratch offset, ECX = dwords (at least 1). CF clear if the run lies
; inside the window and is aligned.
BeginProc RD_Vram_Allowed
    push    eax
    test    edx, 3
    jnz     short RD_Vram_Refused
    test    ecx, ecx
    jz      short RD_Vram_Refused
    cmp     ecx, RD_SCRATCH_BYTES / 4
    ja      short RD_Vram_Refused
    cmp     edx, RD_SCRATCH_BYTES
    jae     short RD_Vram_Refused
    mov     eax, ecx
    shl     eax, 2
    add     eax, edx
    cmp     eax, RD_SCRATCH_BYTES
    ja      short RD_Vram_Refused
    pop     eax
    clc
    ret
RD_Vram_Refused:
    pop     eax
    stc
    ret
EndProc RD_Vram_Allowed

; ESI = op (four dwords). Returns EAX = the op's result, CF set if refused.
BeginProc RD_Execute
    push    ebx
    push    ecx
    push    edx
    push    edi
    mov     eax, [esi]
    mov     edx, [esi+4]

    cmp     eax, RD_OP_MMIO_READ
    jne     short RD_Exec_Not_Read
    call    RD_Read_Allowed
    jc      RD_Exec_Refused
    mov     ebx, RDMmio
    mov     eax, [ebx+edx]
    jmp     RD_Exec_Done
RD_Exec_Not_Read:

    cmp     eax, RD_OP_MMIO_WRITE
    jne     short RD_Exec_Not_Write
    call    RD_Write_Allowed
    jc      RD_Exec_Refused
    mov     eax, [esi+8]
    ; SRBM_SOFT_RESET may change only in the DMA bit: no other block of the
    ; card is reset or released from here. Every other bit must be written
    ; as it reads now, as Linux's read-modify-write in r600_gpu_soft_reset
    ; keeps it. M-NZHWBR's register probe read the register as 0.
    cmp     edx, RD_SRBM_SOFT_RESET
    jne     short RD_Exec_Write_Value_Ok
    push    ebx
    mov     ebx, RDMmio
    mov     ebx, [ebx+edx]
    xor     ebx, eax
    test    ebx, NOT RD_SOFT_RESET_DMA
    pop     ebx
    jnz     RD_Exec_Refused
RD_Exec_Write_Value_Ok:
    mov     ebx, RDMmio
    mov     [ebx+edx], eax
    ; Post the write before the next op.
    mov     eax, [ebx+RD_CONFIG_MEMSIZE]
    xor     eax, eax
    jmp     RD_Exec_Done
RD_Exec_Not_Write:

    cmp     eax, RD_OP_VRAM_WRITE
    jne     short RD_Exec_Not_Vram_Write
    mov     ecx, 1
    call    RD_Vram_Allowed
    jc      RD_Exec_Refused
    mov     ebx, RDScratch
    mov     eax, [esi+8]
    mov     [ebx+edx], eax
    xor     eax, eax
    jmp     RD_Exec_Done
RD_Exec_Not_Vram_Write:

    cmp     eax, RD_OP_VRAM_READ
    jne     short RD_Exec_Not_Vram_Read
    mov     ecx, 1
    call    RD_Vram_Allowed
    jc      RD_Exec_Refused
    mov     ebx, RDScratch
    mov     eax, [ebx+edx]
    jmp     RD_Exec_Done
RD_Exec_Not_Vram_Read:

    cmp     eax, RD_OP_VRAM_FILL
    jne     short RD_Exec_Not_Fill
    mov     ecx, [esi+8]
    call    RD_Vram_Allowed
    jc      RD_Exec_Refused
    mov     edi, RDScratch
    add     edi, edx
    mov     eax, [esi+12]
    cld
    rep     stosd
    xor     eax, eax
    jmp     RD_Exec_Done
RD_Exec_Not_Fill:

    cmp     eax, RD_OP_VRAM_PATTERN
    jne     short RD_Exec_Not_Pattern
    mov     ecx, [esi+8]
    call    RD_Vram_Allowed
    jc      RD_Exec_Refused
    mov     edi, RDScratch
    add     edi, edx
    mov     eax, [esi+12]
RD_Exec_Pattern_Next:
    mov     [edi], eax
    add     eax, RD_PATTERN_STEP
    add     edi, 4
    dec     ecx
    jnz     short RD_Exec_Pattern_Next
    xor     eax, eax
    jmp     RD_Exec_Done
RD_Exec_Not_Pattern:

    cmp     eax, RD_OP_VRAM_BLOCK
    jne     short RD_Exec_Not_Block
    mov     ecx, [esi+8]
    call    RD_Vram_Allowed
    jc      RD_Exec_Refused
    mov     eax, RDResult[40]
    mov     ebx, eax
    add     ebx, ecx
    cmp     ebx, RD_BLOCK_DWORDS
    ja      RD_Exec_Refused
    mov     RDResult[40], ebx
    push    esi
    mov     esi, RDScratch
    add     esi, edx
    lea     edi, RDBlock[eax*4]
    cld
    rep     movsd
    pop     esi
    jmp     RD_Exec_Done
RD_Exec_Not_Block:

    cmp     eax, RD_OP_WAIT_VRAM
    jne     short RD_Exec_Not_Wait_Vram
    mov     ecx, 1
    call    RD_Vram_Allowed
    jc      RD_Exec_Refused
    mov     ebx, RDScratch
    add     ebx, edx
    mov     edx, [esi+8]
    mov     ecx, [esi+12]
    xor     eax, eax
RD_Exec_Wait_Vram_Next:
    cmp     [ebx], edx
    je      RD_Exec_Done
    inc     eax
    cmp     eax, ecx
    jb      short RD_Exec_Wait_Vram_Next
    or      RDResult[4], RD_ST_TIMEOUT
    mov     eax, RD_TIMEOUT
    jmp     RD_Exec_Done
RD_Exec_Not_Wait_Vram:

    cmp     eax, RD_OP_WAIT_MMIO
    jne     short RD_Exec_Not_Wait_Mmio
    call    RD_Read_Allowed
    jc      RD_Exec_Refused
    mov     ebx, RDMmio
    add     ebx, edx
    xor     eax, eax
RD_Exec_Wait_Mmio_Next:
    mov     ecx, [ebx]
    and     ecx, [esi+8]
    cmp     ecx, [esi+12]
    je      RD_Exec_Done
    inc     eax
    cmp     eax, RD_WAIT_MMIO_LIMIT
    jb      short RD_Exec_Wait_Mmio_Next
    or      RDResult[4], RD_ST_TIMEOUT
    mov     eax, RD_TIMEOUT
    jmp     RD_Exec_Done
RD_Exec_Not_Wait_Mmio:

    cmp     eax, RD_OP_DELAY
    jne     short RD_Exec_Not_Delay
    mov     ecx, edx
    test    ecx, ecx
    jz      RD_Exec_Refused
    cmp     ecx, RD_DELAY_MAX
    ja      RD_Exec_Refused
    mov     ebx, RDMmio
RD_Exec_Delay_Next:
    mov     eax, [ebx+RD_SRBM_STATUS]
    dec     ecx
    jnz     short RD_Exec_Delay_Next
    jmp     RD_Exec_Done
RD_Exec_Not_Delay:

    cmp     eax, RD_OP_SAVE
    jne     short RD_Exec_Not_Save
    push    esi
    mov     esi, RDScratch
    mov     edi, OFFSET32 RDSaved
    mov     ecx, RD_SCRATCH_BYTES / 4
    cld
    rep     movsd
    pop     esi
    or      RDResult[4], RD_ST_SAVED
    xor     eax, eax
    jmp     short RD_Exec_Done
RD_Exec_Not_Save:

    cmp     eax, RD_OP_RESTORE
    jne     short RD_Exec_Refused
    ; Only what was saved in this run goes back.
    test    RDResult[4], RD_ST_SAVED
    jz      short RD_Exec_Refused
    push    esi
    mov     esi, OFFSET32 RDSaved
    mov     edi, RDScratch
    mov     ecx, RD_SCRATCH_BYTES / 4
    cld
    rep     movsd
    pop     esi
    xor     eax, eax

RD_Exec_Done:
    pop     edi
    pop     edx
    pop     ecx
    pop     ebx
    clc
    ret
RD_Exec_Refused:
    pop     edi
    pop     edx
    pop     ecx
    pop     ebx
    stc
    ret
EndProc RD_Execute

; EBP = DIOC input buffer, ECX = its byte count.
BeginProc RD_Run
    pushad
    mov     eax, [ebp+RD_IN_COUNT]
    cmp     eax, RD_OP_MAX
    ja      short RD_Run_Refused
    sub     ecx, RD_IN_OPS
    shr     ecx, 4
    cmp     eax, ecx
    ja      short RD_Run_Refused
    call    RD_Map
    jc      short RD_Run_Done

    mov     ecx, [ebp+RD_IN_COUNT]
    lea     esi, [ebp+RD_IN_OPS]
    mov     edi, OFFSET32 RDOpResults
    xor     ebx, ebx
    jecxz   short RD_Run_Complete
RD_Run_Next:
    call    RD_Execute
    jc      short RD_Run_Refused_At
    mov     [edi], eax
    inc     ebx
    mov     RDResult[32], ebx
    add     esi, RD_OP_BYTES
    add     edi, 4
    dec     ecx
    jnz     short RD_Run_Next
RD_Run_Complete:
    or      RDResult[4], RD_ST_COMPLETE
    jmp     short RD_Run_Done
RD_Run_Refused_At:
    mov     RDResult[36], ebx
RD_Run_Refused:
    or      RDResult[4], RD_ST_REFUSED
RD_Run_Done:
    popad
    ret
EndProc RD_Run

BeginProc RD_Reset_Result
    pushad
    mov     edi, OFFSET32 RDResult + 4
    mov     ecx, RD_RESULT_DWORDS - 1
    xor     eax, eax
    cld
    rep     stosd
    popad
    ret
EndProc RD_Reset_Result

BeginProc RD_W32_DeviceIoControl
    cmp     ecx, DIOC_OPEN
    je      RD_Dioc_Ok
    cmp     ecx, DIOC_CLOSEHANDLE
    je      RD_Dioc_Ok
    cmp     ecx, RD_DIOC_RUN
    jne     RD_Dioc_Fail

    pushad
    mov     ebx, esi
    mov     ebp, [ebx.lpvInBuffer]
    test    ebp, ebp
    jz      short RD_Dioc_Copy_Fail
    mov     ecx, [ebx.cbInBuffer]
    cmp     ecx, RD_IN_OPS
    jb      short RD_Dioc_Copy_Fail
    mov     edi, [ebx.lpvOutBuffer]
    test    edi, edi
    jz      short RD_Dioc_Copy_Fail
    cmp     [ebx.cbOutBuffer], RD_RESULT_DWORDS * 4
    jb      short RD_Dioc_Copy_Fail

    call    RD_Reset_Result
    mov     ecx, [ebx.cbInBuffer]
    call    RD_Run

    mov     edi, [ebx.lpvOutBuffer]
    mov     esi, OFFSET32 RDResult
    mov     ecx, RD_RESULT_DWORDS
    cld
    rep     movsd
    mov     eax, [ebx.lpcbBytesReturned]
    test    eax, eax
    jz      short RD_Dioc_Copy_Done
    mov     dword ptr [eax], RD_RESULT_DWORDS * 4
RD_Dioc_Copy_Done:
    popad
RD_Dioc_Ok:
    xor     eax, eax
    ret

RD_Dioc_Copy_Fail:
    popad
RD_Dioc_Fail:
    mov     eax, 1
    ret
EndProc RD_W32_DeviceIoControl

BeginProc RD_Dynamic_Init
    clc
    ret
EndProc RD_Dynamic_Init

BeginProc RD_Dynamic_Exit
    clc
    ret
EndProc RD_Dynamic_Exit

Begin_Control_Dispatch RD
    Control_Dispatch Sys_Dynamic_Device_Init, RD_Dynamic_Init
    Control_Dispatch Sys_Dynamic_Device_Exit, RD_Dynamic_Exit
    Control_Dispatch W32_DeviceIoControl, RD_W32_DeviceIoControl
End_Control_Dispatch RD

VxD_LOCKED_CODE_ENDS

end
