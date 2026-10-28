; AMD Carrizo (Radeon R7E, GCN 3) SDMA0 write probe VxD.
;
; Phase 3 of docs\plans\ati-carrizo-gfx8.md. Loaded dynamically by
; V9XCZDMA.EXE, which decides every access; this VxD only executes an op
; list against the chip and refuses anything outside it. The Evergreen DMA
; probe (evergreen_dma.asm) is the model; what is new is that Carrizo's DMA
; engine runs microcode, so the op list can stream a dword array the caller
; supplies into SDMA0's microcode port, and can read-modify-write the
; engine's control registers as Linux does.
;
; It finds the chip with configuration mechanism 1 (1002:9874 only), maps
; BAR5 (256 KiB of registers) and a 64 KiB scratch window of the carve-out
; through BAR0, and runs:
;
;   MMIO read32    - SRBM status and soft reset, MC_VM_FB_LOCATION,
;                    CONFIG_MEMSIZE, GRBM_STATUS, RLC_GPM_STAT and SDMA0's
;                    register block.
;   MMIO write32   - SDMA0's control, ring and microcode-address registers
;                    that Linux v4.9 sdma_v3_0.c writes (golden settings,
;                    load_microcode, gfx_resume, enable), the two HDP
;                    coherency registers, and SRBM_SOFT_RESET with 0 or
;                    SDMA0's bit alone. SDMA1, the CP, the RLC and the
;                    memory controller are not written.
;   MMIO rmw       - the same offsets: clear one mask, set another.
;   ucode          - dwords from the caller's data array to SDMA0_UCODE_DATA,
;                    the only port this op writes.
;   delay          - a count of SRBM_STATUS reads, at most CD_DELAY_MAX.
;   VRAM ops       - write, read, fill, pattern, read block, all at aligned
;                    offsets inside the scratch window.
;   wait           - a VRAM dword equal to a value, or an MMIO dword under a
;                    mask equal to a value, at most the op's limit reads.
;   save/restore   - the whole scratch window to and from a copy here.
;
; The scratch window is BAR0 + 0F000000h, 240 MiB in. The driver uses the
; first 16 MiB, the VBE reports 48 MiB, and the carve-out is 512 MiB, so the
; window is memory nothing else touches. It is mapped only when BAR0 is a
; 64-bit memory BAR below 4 GiB, aligned to 256 MiB, on a chip whose
; CONFIG_MEMSIZE says 256 MiB or more: M-XP7053's BAR0 is E0000000 with
; BAR2 at F0000000 and 512 MiB of carve-out
; (docs\decisions\2026-10-28-carrizo-register-probe.md). Whether BAR0
; reaches 240 MiB was not measured; the publisher's first ops write and
; read back a pattern there before the engine is touched.
;
; Offsets: Linux amdgpu's oss_3_0_d.h, bif_5_0_d.h, gmc_8_1_d.h and
; gfx_8_0_d.h (build\external\carrizo\linux\), dword indices times four.

.386p

.xlist
include VMM.INC
include VWIN32.INC
.list

Declare_Virtual_Device CZDMA, 1, 0, CD_Control, \
                       Undefined_Device_ID, Undefined_Init_Order, , ,

CD_MAGIC                equ 414d4443h ; "CDMA"
CD_DIOC_RUN             equ 1
CD_VENDOR_ATI           equ 1002h
CD_DEVICE_CARRIZO       equ 9874h
CD_OP_MAX               equ 512
CD_DATA_MAX             equ 4096
CD_BLOCK_DWORDS         equ 4096
CD_TIMEOUT              equ 0ffffffffh

CD_CFG_BAR0             equ 10h
CD_CFG_BAR0_HIGH        equ 14h
CD_CFG_BAR5             equ 24h
CD_BAR_IO               equ 00000001h
CD_BAR_TYPE_MASK        equ 00000006h
CD_BAR_TYPE_64          equ 00000004h
CD_MMIO_BYTES           equ 00040000h
CD_MMIO_ALIGN           equ 0003ffffh
CD_LFB_ALIGN            equ 0fffffffh
CD_SCRATCH_OFFSET       equ 0f000000h
CD_SCRATCH_BYTES        equ 00010000h
CD_CONFIG_MEMSIZE       equ 5428h
CD_MEMSIZE_MIN_MIB      equ 256
CD_MEMSIZE_MAX_MIB      equ 4096

; Op codes; the layout is four dwords: op, a, b, c.
CD_OP_MMIO_READ         equ 1   ; a offset
CD_OP_MMIO_WRITE        equ 2   ; a offset, b value
CD_OP_VRAM_WRITE        equ 3   ; a offset, b value
CD_OP_VRAM_READ         equ 4   ; a offset
CD_OP_VRAM_FILL         equ 5   ; a offset, b dwords, c value
CD_OP_VRAM_BLOCK        equ 6   ; a offset, b dwords -> block area
CD_OP_WAIT_VRAM         equ 7   ; a offset, b value, c limit
CD_OP_WAIT_MMIO         equ 8   ; a offset, b mask, c value; limit fixed
CD_OP_SAVE              equ 9
CD_OP_RESTORE           equ 10
CD_OP_VRAM_PATTERN      equ 11  ; a offset, b dwords, c seed
CD_OP_DELAY             equ 12  ; a SRBM_STATUS reads
CD_OP_MMIO_RMW          equ 13  ; a offset, b mask to clear, c mask to set
CD_OP_UCODE             equ 14  ; a port, b first data dword, c dwords
CD_DELAY_MAX            equ 100000
CD_SRBM_STATUS          equ 0e50h
CD_SRBM_SOFT_RESET      equ 0e60h
CD_SOFT_RESET_SDMA0     equ 00100000h
CD_SDMA0_UCODE_DATA     equ 0d004h
CD_OP_BYTES             equ 16
CD_WAIT_MMIO_LIMIT      equ 2000000
CD_PATTERN_STEP         equ 9e3779b9h

; Status bits.
CD_ST_FOUND             equ 00000001h
CD_ST_MAPPED            equ 00000002h
CD_ST_IDENTITY          equ 00000004h
CD_ST_REFUSED           equ 00000008h
CD_ST_COMPLETE          equ 00000010h
CD_ST_BAD_BAR           equ 00000020h
CD_ST_TIMEOUT           equ 00000040h
CD_ST_SAVED             equ 00000080h

; Input: op count, data count, the ops, then the data dwords.
CD_IN_COUNT             equ 0
CD_IN_DATA_COUNT        equ 4
CD_IN_OPS               equ 8

; Output layout, in dwords. Must match struct v9x_czdma_result in
; carrizo_dma_win32.c.
CD_HEADER_DWORDS        equ 16
CD_RESULT_DWORDS        equ (CD_HEADER_DWORDS + CD_OP_MAX + CD_BLOCK_DWORDS)

VxD_LOCKED_DATA_SEG

; Reads accepted: start, limit (exclusive). SDMA0_UCODE_ADDR/_DATA (D000h,
; D004h) and SDMA_PGFSM_WRITE/_READ (D060h-D067h) are left out.
CDReadWindows label dword
    dd 0e4ch, 0e54h     ; SRBM_STATUS2, SRBM_STATUS
    dd 0e60h, 0e64h     ; SRBM_SOFT_RESET
    dd 2024h, 2028h     ; MC_VM_FB_LOCATION
    dd 2f30h, 2f34h     ; HDP_DEBUG0
    dd 5428h, 542ch     ; CONFIG_MEMSIZE
    dd 8010h, 8014h     ; GRBM_STATUS
    dd 3b100h, 3b104h   ; RLC_GPM_STAT
    dd 0d008h, 0d01ch   ; SDMA0_POWER_CNTL .. SDMA0_TILING_CONFIG
    dd 0d024h, 0d03ch   ; SDMA0_SEM_WAIT_FAIL_TIMER_CNTL .. STATUS1_REG
    dd 0d048h, 0d04ch   ; SDMA0_F32_CNTL
    dd 0d070h, 0d078h   ; SDMA0_ID, SDMA0_VERSION
    dd 0d200h, 0d24ch   ; SDMA0_GFX_RB_CNTL .. SDMA0_GFX_DOORBELL
    dd 0d29ch, 0d2a4h   ; SDMA0_GFX_VIRTUAL_ADDR, SDMA0_GFX_APE1_CNTL
    dd 0d428h, 0d42ch   ; SDMA0_RLC0_IB_CNTL
    dd 0d628h, 0d62ch   ; SDMA0_RLC1_IB_CNTL
CD_READ_WINDOW_COUNT    equ 15

; Writes accepted: exact offsets.
CDWriteOffsets label dword
    dd 5480h            ; HDP_MEM_COHERENCY_FLUSH_CNTL
    dd 2f30h            ; HDP_DEBUG0, the HDP read-cache invalidate
    dd 0e60h            ; SRBM_SOFT_RESET, value checked in CD_Execute
    dd 0d000h           ; SDMA0_UCODE_ADDR
    dd 0d008h           ; SDMA0_POWER_CNTL
    dd 0d00ch           ; SDMA0_CLK_CTRL
    dd 0d010h           ; SDMA0_CNTL
    dd 0d014h           ; SDMA0_CHICKEN_BITS
    dd 0d018h           ; SDMA0_TILING_CONFIG
    dd 0d024h           ; SDMA0_SEM_WAIT_FAIL_TIMER_CNTL
    dd 0d048h           ; SDMA0_F32_CNTL
    dd 0d200h           ; SDMA0_GFX_RB_CNTL
    dd 0d204h           ; SDMA0_GFX_RB_BASE
    dd 0d208h           ; SDMA0_GFX_RB_BASE_HI
    dd 0d20ch           ; SDMA0_GFX_RB_RPTR
    dd 0d210h           ; SDMA0_GFX_RB_WPTR
    dd 0d220h           ; SDMA0_GFX_RB_RPTR_ADDR_HI
    dd 0d224h           ; SDMA0_GFX_RB_RPTR_ADDR_LO
    dd 0d228h           ; SDMA0_GFX_IB_CNTL
    dd 0d22ch           ; SDMA0_GFX_IB_RPTR
    dd 0d230h           ; SDMA0_GFX_IB_OFFSET
    dd 0d248h           ; SDMA0_GFX_DOORBELL
    dd 0d428h           ; SDMA0_RLC0_IB_CNTL
    dd 0d628h           ; SDMA0_RLC1_IB_CNTL
CD_WRITE_OFFSET_COUNT   equ 24

CDResult label dword
    dd CD_MAGIC                 ; 00 magic
    dd 0                        ; 01 status
    dd 0                        ; 02 id dword
    dd 0                        ; 03 BAR0 low
    dd 0                        ; 04 BAR0 high
    dd 0                        ; 05 BAR5
    dd 0                        ; 06 reserved
    dd 0                        ; 07 CONFIG_MEMSIZE
    dd 0                        ; 08 ops executed
    dd 0                        ; 09 index of the refused op
    dd 0                        ; 10 block dwords used
    dd 0                        ; 11 configuration address
    dd 0                        ; 12 microcode dwords written
    dd 0                        ; 13 reserved
    dd 0                        ; 14 reserved
    dd 0                        ; 15 reserved
CDOpResults  dd CD_OP_MAX dup (0)
CDBlock      dd CD_BLOCK_DWORDS dup (0)
CDSaved      dd (CD_SCRATCH_BYTES / 4) dup (0)
CDMmio       dd 0
CDScratch    dd 0
CDConfig     dd 0
CDData       dd 0
CDDataCount  dd 0
VxD_LOCKED_DATA_ENDS

VxD_LOCKED_CODE_SEG

; EAX = PCI configuration address. Returns EAX = configuration dword.
; 0CF8h saved and restored with interrupts off.
BeginProc CD_Pci_Read
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
EndProc CD_Pci_Read

; Scans buses 0-15, device slots 0-31, function 0, for 1002:9874.
; Returns CF clear and EAX = configuration address, or CF set.
BeginProc CD_Find
    push    ebx
    push    ecx
    mov     ebx, 80000000h
CD_Find_Next:
    mov     eax, ebx
    call    CD_Pci_Read
    cmp     eax, (CD_DEVICE_CARRIZO shl 16) or CD_VENDOR_ATI
    je      short CD_Find_Found
    add     ebx, 800h
    cmp     ebx, 80100000h
    jb      short CD_Find_Next
    pop     ecx
    pop     ebx
    stc
    ret
CD_Find_Found:
    mov     CDResult[8], eax
    mov     eax, ebx
    pop     ecx
    pop     ebx
    clc
    ret
EndProc CD_Find

; Finds and maps the chip once. Returns CF clear when both windows are
; mapped and the identity check passed.
BeginProc CD_Map
    pushad
    cmp     CDScratch, 0
    jne     CD_Map_Ok

    call    CD_Find
    jc      CD_Map_Fail
    mov     CDConfig, eax
    mov     CDResult[44], eax
    or      CDResult[4], CD_ST_FOUND

    ; BAR5: 32-bit memory, 256 KiB aligned. It cannot be 64-bit: its high
    ; half would be 28h, the CardBus CIS pointer.
    mov     eax, CDConfig
    or      eax, CD_CFG_BAR5
    call    CD_Pci_Read
    mov     CDResult[20], eax
    mov     esi, eax
    test    esi, CD_BAR_IO
    jnz     CD_Map_Bad_Bar
    test    esi, CD_BAR_TYPE_MASK
    jnz     CD_Map_Bad_Bar
    and     esi, NOT 0fh
    jz      CD_Map_Bad_Bar
    test    esi, CD_MMIO_ALIGN
    jnz     CD_Map_Bad_Bar

    ; BAR0: memory, 64-bit, high half zero, 256 MiB aligned.
    mov     eax, CDConfig
    or      eax, CD_CFG_BAR0
    call    CD_Pci_Read
    mov     CDResult[12], eax
    mov     edi, eax
    mov     eax, CDConfig
    or      eax, CD_CFG_BAR0_HIGH
    call    CD_Pci_Read
    mov     CDResult[16], eax
    test    edi, CD_BAR_IO
    jnz     CD_Map_Bad_Bar
    mov     edx, edi
    and     edx, CD_BAR_TYPE_MASK
    cmp     edx, CD_BAR_TYPE_64
    jne     CD_Map_Bad_Bar
    test    eax, eax
    jnz     CD_Map_Bad_Bar
    and     edi, NOT 0fh
    jz      CD_Map_Bad_Bar
    test    edi, CD_LFB_ALIGN
    jnz     CD_Map_Bad_Bar

    VMMcall _MapPhysToLinear,<esi,CD_MMIO_BYTES,0>
    cmp     eax, 0ffffffffh
    je      CD_Map_Fail
    mov     CDMmio, eax

    ; The identity check before the scratch window is mapped: at least
    ; 256 MiB of carve-out, so BAR0 + 240 MiB is inside it.
    mov     edx, [eax+CD_CONFIG_MEMSIZE]
    mov     CDResult[28], edx
    cmp     edx, CD_MEMSIZE_MIN_MIB
    jb      CD_Map_Fail
    cmp     edx, CD_MEMSIZE_MAX_MIB
    ja      CD_Map_Fail
    or      CDResult[4], CD_ST_IDENTITY

    add     edi, CD_SCRATCH_OFFSET
    VMMcall _MapPhysToLinear,<edi,CD_SCRATCH_BYTES,0>
    cmp     eax, 0ffffffffh
    je      CD_Map_Fail
    mov     CDScratch, eax
    or      CDResult[4], CD_ST_MAPPED

CD_Map_Ok:
    popad
    clc
    ret
CD_Map_Bad_Bar:
    or      CDResult[4], CD_ST_BAD_BAR
CD_Map_Fail:
    popad
    stc
    ret
EndProc CD_Map

; EDX = offset. CF clear if readable.
BeginProc CD_Read_Allowed
    push    ecx
    push    esi
    test    edx, 3
    jnz     short CD_Read_Refused
    mov     esi, OFFSET32 CDReadWindows
    mov     ecx, CD_READ_WINDOW_COUNT
CD_Read_Next:
    cmp     edx, [esi]
    jb      short CD_Read_Skip
    cmp     edx, [esi+4]
    jb      short CD_Read_Accepted
CD_Read_Skip:
    add     esi, 8
    dec     ecx
    jnz     short CD_Read_Next
CD_Read_Refused:
    pop     esi
    pop     ecx
    stc
    ret
CD_Read_Accepted:
    pop     esi
    pop     ecx
    clc
    ret
EndProc CD_Read_Allowed

; EDX = offset. CF clear if writable.
BeginProc CD_Write_Allowed
    push    ecx
    push    esi
    mov     esi, OFFSET32 CDWriteOffsets
    mov     ecx, CD_WRITE_OFFSET_COUNT
CD_Write_Next:
    cmp     edx, [esi]
    je      short CD_Write_Accepted
    add     esi, 4
    dec     ecx
    jnz     short CD_Write_Next
    pop     esi
    pop     ecx
    stc
    ret
CD_Write_Accepted:
    pop     esi
    pop     ecx
    clc
    ret
EndProc CD_Write_Allowed

; EDX = scratch offset, ECX = dwords (at least 1). CF clear if the run lies
; inside the window and is aligned.
BeginProc CD_Vram_Allowed
    push    eax
    test    edx, 3
    jnz     short CD_Vram_Refused
    test    ecx, ecx
    jz      short CD_Vram_Refused
    cmp     ecx, CD_SCRATCH_BYTES / 4
    ja      short CD_Vram_Refused
    cmp     edx, CD_SCRATCH_BYTES
    jae     short CD_Vram_Refused
    mov     eax, ecx
    shl     eax, 2
    add     eax, edx
    cmp     eax, CD_SCRATCH_BYTES
    ja      short CD_Vram_Refused
    pop     eax
    clc
    ret
CD_Vram_Refused:
    pop     eax
    stc
    ret
EndProc CD_Vram_Allowed

; ESI = op (four dwords). Returns EAX = the op's result, CF set if refused.
BeginProc CD_Execute
    push    ebx
    push    ecx
    push    edx
    push    edi
    mov     eax, [esi]
    mov     edx, [esi+4]

    cmp     eax, CD_OP_MMIO_READ
    jne     short CD_Exec_Not_Read
    call    CD_Read_Allowed
    jc      CD_Exec_Refused
    mov     ebx, CDMmio
    mov     eax, [ebx+edx]
    jmp     CD_Exec_Done
CD_Exec_Not_Read:

    cmp     eax, CD_OP_MMIO_WRITE
    jne     short CD_Exec_Not_Write
    call    CD_Write_Allowed
    jc      CD_Exec_Refused
    mov     eax, [esi+8]
    ; SRBM_SOFT_RESET takes SDMA0's bit or nothing: no other block of the
    ; chip is ever reset from here.
    cmp     edx, CD_SRBM_SOFT_RESET
    jne     short CD_Exec_Write_Value_Ok
    test    eax, NOT CD_SOFT_RESET_SDMA0
    jnz     CD_Exec_Refused
CD_Exec_Write_Value_Ok:
    mov     ebx, CDMmio
    mov     [ebx+edx], eax
    ; Post the write before the next op.
    mov     eax, [ebx+CD_CONFIG_MEMSIZE]
    xor     eax, eax
    jmp     CD_Exec_Done
CD_Exec_Not_Write:

    cmp     eax, CD_OP_MMIO_RMW
    jne     short CD_Exec_Not_Rmw
    ; Not SRBM_SOFT_RESET: a read-modify-write could carry another
    ; block's bit through.
    cmp     edx, CD_SRBM_SOFT_RESET
    je      CD_Exec_Refused
    call    CD_Write_Allowed
    jc      CD_Exec_Refused
    mov     ebx, CDMmio
    mov     eax, [ebx+edx]
    mov     ecx, [esi+8]
    not     ecx
    and     eax, ecx
    or      eax, [esi+12]
    mov     [ebx+edx], eax
    mov     ecx, [ebx+CD_CONFIG_MEMSIZE]
    jmp     CD_Exec_Done
CD_Exec_Not_Rmw:

    cmp     eax, CD_OP_UCODE
    jne     short CD_Exec_Not_Ucode
    cmp     edx, CD_SDMA0_UCODE_DATA
    jne     CD_Exec_Refused
    mov     eax, [esi+8]
    mov     ecx, [esi+12]
    test    ecx, ecx
    jz      CD_Exec_Refused
    cmp     eax, CDDataCount
    jae     CD_Exec_Refused
    mov     ebx, CDDataCount
    sub     ebx, eax
    cmp     ecx, ebx
    ja      CD_Exec_Refused
    push    esi
    mov     esi, CDData
    lea     esi, [esi+eax*4]
    mov     ebx, CDMmio
    add     ebx, edx
    mov     eax, ecx
CD_Exec_Ucode_Next:
    mov     edi, [esi]
    mov     [ebx], edi
    add     esi, 4
    dec     ecx
    jnz     short CD_Exec_Ucode_Next
    pop     esi
    add     CDResult[48], eax
    jmp     CD_Exec_Done
CD_Exec_Not_Ucode:

    cmp     eax, CD_OP_VRAM_WRITE
    jne     short CD_Exec_Not_Vram_Write
    mov     ecx, 1
    call    CD_Vram_Allowed
    jc      CD_Exec_Refused
    mov     ebx, CDScratch
    mov     eax, [esi+8]
    mov     [ebx+edx], eax
    xor     eax, eax
    jmp     CD_Exec_Done
CD_Exec_Not_Vram_Write:

    cmp     eax, CD_OP_VRAM_READ
    jne     short CD_Exec_Not_Vram_Read
    mov     ecx, 1
    call    CD_Vram_Allowed
    jc      CD_Exec_Refused
    mov     ebx, CDScratch
    mov     eax, [ebx+edx]
    jmp     CD_Exec_Done
CD_Exec_Not_Vram_Read:

    cmp     eax, CD_OP_VRAM_FILL
    jne     short CD_Exec_Not_Fill
    mov     ecx, [esi+8]
    call    CD_Vram_Allowed
    jc      CD_Exec_Refused
    mov     edi, CDScratch
    add     edi, edx
    mov     eax, [esi+12]
    cld
    rep     stosd
    xor     eax, eax
    jmp     CD_Exec_Done
CD_Exec_Not_Fill:

    cmp     eax, CD_OP_VRAM_PATTERN
    jne     short CD_Exec_Not_Pattern
    mov     ecx, [esi+8]
    call    CD_Vram_Allowed
    jc      CD_Exec_Refused
    mov     edi, CDScratch
    add     edi, edx
    mov     eax, [esi+12]
CD_Exec_Pattern_Next:
    mov     [edi], eax
    add     eax, CD_PATTERN_STEP
    add     edi, 4
    dec     ecx
    jnz     short CD_Exec_Pattern_Next
    xor     eax, eax
    jmp     CD_Exec_Done
CD_Exec_Not_Pattern:

    cmp     eax, CD_OP_VRAM_BLOCK
    jne     short CD_Exec_Not_Block
    mov     ecx, [esi+8]
    call    CD_Vram_Allowed
    jc      CD_Exec_Refused
    mov     eax, CDResult[40]
    mov     ebx, eax
    add     ebx, ecx
    cmp     ebx, CD_BLOCK_DWORDS
    ja      CD_Exec_Refused
    mov     CDResult[40], ebx
    push    esi
    mov     esi, CDScratch
    add     esi, edx
    lea     edi, CDBlock[eax*4]
    cld
    rep     movsd
    pop     esi
    jmp     CD_Exec_Done
CD_Exec_Not_Block:

    cmp     eax, CD_OP_WAIT_VRAM
    jne     short CD_Exec_Not_Wait_Vram
    mov     ecx, 1
    call    CD_Vram_Allowed
    jc      CD_Exec_Refused
    mov     ebx, CDScratch
    add     ebx, edx
    mov     edx, [esi+8]
    mov     ecx, [esi+12]
    xor     eax, eax
CD_Exec_Wait_Vram_Next:
    cmp     [ebx], edx
    je      CD_Exec_Done
    inc     eax
    cmp     eax, ecx
    jb      short CD_Exec_Wait_Vram_Next
    or      CDResult[4], CD_ST_TIMEOUT
    mov     eax, CD_TIMEOUT
    jmp     CD_Exec_Done
CD_Exec_Not_Wait_Vram:

    cmp     eax, CD_OP_WAIT_MMIO
    jne     short CD_Exec_Not_Wait_Mmio
    call    CD_Read_Allowed
    jc      CD_Exec_Refused
    mov     ebx, CDMmio
    add     ebx, edx
    xor     eax, eax
CD_Exec_Wait_Mmio_Next:
    mov     ecx, [ebx]
    and     ecx, [esi+8]
    cmp     ecx, [esi+12]
    je      CD_Exec_Done
    inc     eax
    cmp     eax, CD_WAIT_MMIO_LIMIT
    jb      short CD_Exec_Wait_Mmio_Next
    or      CDResult[4], CD_ST_TIMEOUT
    mov     eax, CD_TIMEOUT
    jmp     CD_Exec_Done
CD_Exec_Not_Wait_Mmio:

    cmp     eax, CD_OP_DELAY
    jne     short CD_Exec_Not_Delay
    mov     ecx, edx
    test    ecx, ecx
    jz      CD_Exec_Refused
    cmp     ecx, CD_DELAY_MAX
    ja      CD_Exec_Refused
    mov     ebx, CDMmio
CD_Exec_Delay_Next:
    mov     eax, [ebx+CD_SRBM_STATUS]
    dec     ecx
    jnz     short CD_Exec_Delay_Next
    jmp     CD_Exec_Done
CD_Exec_Not_Delay:

    cmp     eax, CD_OP_SAVE
    jne     short CD_Exec_Not_Save
    push    esi
    mov     esi, CDScratch
    mov     edi, OFFSET32 CDSaved
    mov     ecx, CD_SCRATCH_BYTES / 4
    cld
    rep     movsd
    pop     esi
    or      CDResult[4], CD_ST_SAVED
    xor     eax, eax
    jmp     short CD_Exec_Done
CD_Exec_Not_Save:

    cmp     eax, CD_OP_RESTORE
    jne     short CD_Exec_Refused
    ; Only what was saved in this run goes back.
    test    CDResult[4], CD_ST_SAVED
    jz      short CD_Exec_Refused
    push    esi
    mov     esi, OFFSET32 CDSaved
    mov     edi, CDScratch
    mov     ecx, CD_SCRATCH_BYTES / 4
    cld
    rep     movsd
    pop     esi
    xor     eax, eax

CD_Exec_Done:
    pop     edi
    pop     edx
    pop     ecx
    pop     ebx
    clc
    ret
CD_Exec_Refused:
    pop     edi
    pop     edx
    pop     ecx
    pop     ebx
    stc
    ret
EndProc CD_Execute

; EBP = DIOC input buffer, ECX = its byte count. The ops come first, the
; data array after them; both counts must fit the buffer.
BeginProc CD_Run
    pushad
    mov     eax, [ebp+CD_IN_COUNT]
    cmp     eax, CD_OP_MAX
    ja      CD_Run_Refused
    mov     edx, [ebp+CD_IN_DATA_COUNT]
    cmp     edx, CD_DATA_MAX
    ja      CD_Run_Refused
    sub     ecx, CD_IN_OPS
    jb      CD_Run_Refused
    mov     ebx, eax
    shl     ebx, 4
    sub     ecx, ebx
    jb      CD_Run_Refused
    shr     ecx, 2
    cmp     edx, ecx
    ja      CD_Run_Refused
    lea     ebx, [ebp+CD_IN_OPS+ebx]
    mov     CDData, ebx
    mov     CDDataCount, edx
    call    CD_Map
    jc      short CD_Run_Done

    mov     ecx, [ebp+CD_IN_COUNT]
    lea     esi, [ebp+CD_IN_OPS]
    mov     edi, OFFSET32 CDOpResults
    xor     ebx, ebx
    jecxz   short CD_Run_Complete
CD_Run_Next:
    call    CD_Execute
    jc      short CD_Run_Refused_At
    mov     [edi], eax
    inc     ebx
    mov     CDResult[32], ebx
    add     esi, CD_OP_BYTES
    add     edi, 4
    dec     ecx
    jnz     short CD_Run_Next
CD_Run_Complete:
    or      CDResult[4], CD_ST_COMPLETE
    jmp     short CD_Run_Done
CD_Run_Refused_At:
    mov     CDResult[36], ebx
CD_Run_Refused:
    or      CDResult[4], CD_ST_REFUSED
CD_Run_Done:
    mov     CDData, 0
    mov     CDDataCount, 0
    popad
    ret
EndProc CD_Run

BeginProc CD_Reset_Result
    pushad
    mov     edi, OFFSET32 CDResult + 4
    mov     ecx, CD_RESULT_DWORDS - 1
    xor     eax, eax
    cld
    rep     stosd
    popad
    ret
EndProc CD_Reset_Result

BeginProc CD_W32_DeviceIoControl
    cmp     ecx, DIOC_OPEN
    je      CD_Dioc_Ok
    cmp     ecx, DIOC_CLOSEHANDLE
    je      CD_Dioc_Ok
    cmp     ecx, CD_DIOC_RUN
    jne     CD_Dioc_Fail

    pushad
    mov     ebx, esi
    mov     ebp, [ebx.lpvInBuffer]
    test    ebp, ebp
    jz      short CD_Dioc_Copy_Fail
    mov     ecx, [ebx.cbInBuffer]
    cmp     ecx, CD_IN_OPS
    jb      short CD_Dioc_Copy_Fail
    mov     edi, [ebx.lpvOutBuffer]
    test    edi, edi
    jz      short CD_Dioc_Copy_Fail
    cmp     [ebx.cbOutBuffer], CD_RESULT_DWORDS * 4
    jb      short CD_Dioc_Copy_Fail

    call    CD_Reset_Result
    mov     ecx, [ebx.cbInBuffer]
    call    CD_Run

    mov     edi, [ebx.lpvOutBuffer]
    mov     esi, OFFSET32 CDResult
    mov     ecx, CD_RESULT_DWORDS
    cld
    rep     movsd
    mov     eax, [ebx.lpcbBytesReturned]
    test    eax, eax
    jz      short CD_Dioc_Copy_Done
    mov     dword ptr [eax], CD_RESULT_DWORDS * 4
CD_Dioc_Copy_Done:
    popad
CD_Dioc_Ok:
    xor     eax, eax
    ret

CD_Dioc_Copy_Fail:
    popad
CD_Dioc_Fail:
    mov     eax, 1
    ret
EndProc CD_W32_DeviceIoControl

BeginProc CD_Dynamic_Init
    clc
    ret
EndProc CD_Dynamic_Init

BeginProc CD_Dynamic_Exit
    clc
    ret
EndProc CD_Dynamic_Exit

Begin_Control_Dispatch CD
    Control_Dispatch Sys_Dynamic_Device_Init, CD_Dynamic_Init
    Control_Dispatch Sys_Dynamic_Device_Exit, CD_Dynamic_Exit
    Control_Dispatch W32_DeviceIoControl, CD_W32_DeviceIoControl
End_Control_Dispatch CD

VxD_LOCKED_CODE_ENDS

end
