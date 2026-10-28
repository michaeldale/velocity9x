; AMD Carrizo (Radeon R7E, GCN 3) graphics command processor probe VxD.
;
; Phase 1 of docs\plans\ati-carrizo-gfx8-3d.md. Loaded dynamically by
; V9XCZCP.EXE, which decides every access; this VxD only executes an op
; list against the chip and refuses anything outside it. CZDMA.VXD
; (carrizo_dma.asm) is the model, over the RLC and the graphics CP instead
; of SDMA0.
;
; It finds the chip with configuration mechanism 1 (1002:9874 only), maps
; BAR5 (256 KiB of registers) and a 64 KiB scratch window of the carve-out
; through BAR0, and runs:
;
;   MMIO read32    - GRBM and SRBM status, MC_VM_FB_LOCATION, CONFIG_MEMSIZE,
;                    the CP's ring and state registers, the RLC's control
;                    and status, and SCRATCH_REG0.
;   MMIO write32   - the CP and RLC registers that Linux v4.9 gfx_v8_0.c
;                    writes in rlc_resume, cp_gfx_load_microcode,
;                    cp_gfx_resume and cp_gfx_start, the four microcode
;                    address registers, SCRATCH_REG0, the two HDP coherency
;                    registers, and GRBM_SOFT_RESET with 0 or the RLC's bit
;                    alone. SDMA, the MEC, the memory controller and the
;                    display are not written.
;   MMIO rmw       - the same offsets: clear one mask, set another; not
;                    GRBM_SOFT_RESET.
;   ucode          - dwords from the caller's data array to one of the four
;                    microcode data ports (PFP, ME, CE, RLC GPM), the only
;                    ports this op writes.
;   delay          - a count of SRBM_STATUS reads, at most CC_DELAY_MAX.
;   VRAM ops       - write, read, fill, pattern, read block, and a copy of a
;                    run of the caller's data array (rings and shaders too
;                    long for one op a dword), all at aligned offsets inside
;                    the scratch window.
;   wait           - a VRAM dword equal to a value, or an MMIO dword under a
;                    mask equal to a value, at most the op's limit reads.
;   save/restore   - the whole scratch window to and from a copy here.
;
; The scratch window is BAR0 + 0F800000h, 248 MiB in: past the driver's
; heap (16 MiB) and its SDMA ring window (240-244 MiB, V9XMINI_FN_VI_MAP),
; which may be running while this probe does. It is mapped only when BAR0
; is a 64-bit memory BAR below 4 GiB, aligned to 256 MiB, on a chip whose
; CONFIG_MEMSIZE says 256 MiB or more (M-XP7053: E0000000, 512 MiB,
; docs\decisions\2026-10-28-carrizo-register-probe.md). The publisher writes
; and reads back a pattern there before the engine is touched.
;
; Offsets: Linux amdgpu's gfx_8_0_d.h, oss_3_0_d.h, bif_5_0_d.h and
; gmc_8_1_d.h (build\external\carrizo\linux\), dword indices times four.

.386p

.xlist
include VMM.INC
include VWIN32.INC
.list

Declare_Virtual_Device CZCP, 1, 0, CC_Control, \
                       Undefined_Device_ID, Undefined_Init_Order, , ,

CC_MAGIC                equ 50435a43h ; "CZCP"
CC_DIOC_RUN             equ 1
CC_VENDOR_ATI           equ 1002h
CC_DEVICE_CARRIZO       equ 9874h
CC_OP_MAX               equ 512
; PFP, CE and ME (4,192 + 2,144 + 4,192 dwords) and the RLC (3,072).
CC_DATA_MAX             equ 16384
CC_BLOCK_DWORDS         equ 4096
CC_TIMEOUT              equ 0ffffffffh

CC_CFG_BAR0             equ 10h
CC_CFG_BAR0_HIGH        equ 14h
CC_CFG_BAR5             equ 24h
CC_BAR_IO               equ 00000001h
CC_BAR_TYPE_MASK        equ 00000006h
CC_BAR_TYPE_64          equ 00000004h
CC_MMIO_BYTES           equ 00040000h
CC_MMIO_ALIGN           equ 0003ffffh
CC_LFB_ALIGN            equ 0fffffffh
CC_SCRATCH_OFFSET       equ 0f800000h
CC_SCRATCH_BYTES        equ 00010000h
CC_CONFIG_MEMSIZE       equ 5428h
CC_MEMSIZE_MIN_MIB      equ 256
CC_MEMSIZE_MAX_MIB      equ 4096

; Op codes; the layout is four dwords: op, a, b, c.
CC_OP_MMIO_READ         equ 1   ; a offset
CC_OP_MMIO_WRITE        equ 2   ; a offset, b value
CC_OP_VRAM_WRITE        equ 3   ; a offset, b value
CC_OP_VRAM_READ         equ 4   ; a offset
CC_OP_VRAM_FILL         equ 5   ; a offset, b dwords, c value
CC_OP_VRAM_BLOCK        equ 6   ; a offset, b dwords -> block area
CC_OP_WAIT_VRAM         equ 7   ; a offset, b value, c limit
CC_OP_WAIT_MMIO         equ 8   ; a offset, b mask, c value; limit fixed
CC_OP_SAVE              equ 9
CC_OP_RESTORE           equ 10
CC_OP_VRAM_PATTERN      equ 11  ; a offset, b dwords, c seed
CC_OP_DELAY             equ 12  ; a SRBM_STATUS reads
CC_OP_MMIO_RMW          equ 13  ; a offset, b mask to clear, c mask to set
CC_OP_UCODE             equ 14  ; a port, b first data dword, c dwords
CC_OP_VRAM_DATA         equ 15  ; a offset, b first data dword, c dwords
CC_DELAY_MAX            equ 100000
CC_SRBM_STATUS          equ 0e50h
CC_GRBM_SOFT_RESET      equ 8020h
CC_SOFT_RESET_RLC       equ 00000004h
CC_PFP_UCODE_DATA       equ 3e054h
CC_ME_RAM_DATA          equ 3e05ch
CC_CE_UCODE_DATA        equ 3e064h
CC_RLC_GPM_UCODE_DATA   equ 3e0f4h
CC_OP_BYTES             equ 16
CC_WAIT_MMIO_LIMIT      equ 2000000
CC_PATTERN_STEP         equ 9e3779b9h

; Status bits.
CC_ST_FOUND             equ 00000001h
CC_ST_MAPPED            equ 00000002h
CC_ST_IDENTITY          equ 00000004h
CC_ST_REFUSED           equ 00000008h
CC_ST_COMPLETE          equ 00000010h
CC_ST_BAD_BAR           equ 00000020h
CC_ST_TIMEOUT           equ 00000040h
CC_ST_SAVED             equ 00000080h

; Input: op count, data count, the ops, then the data dwords.
CC_IN_COUNT             equ 0
CC_IN_DATA_COUNT        equ 4
CC_IN_OPS               equ 8

; Output layout, in dwords. Must match struct v9x_czcp_result in
; carrizo_cp_win32.c.
CC_HEADER_DWORDS        equ 16
CC_RESULT_DWORDS        equ (CC_HEADER_DWORDS + CC_OP_MAX + CC_BLOCK_DWORDS)

VxD_LOCKED_DATA_SEG

; Reads accepted: start, limit (exclusive). The four microcode address and
; data ports (3E050h-3E067h, 3E0F0h-3E0F7h) are left out: a read through
; a data port advances its address.
CCReadWindows label dword
    dd 0e4ch, 0e54h     ; SRBM_STATUS2, SRBM_STATUS
    dd 2024h, 2028h     ; MC_VM_FB_LOCATION
    dd 2f30h, 2f34h     ; HDP_DEBUG0
    dd 5428h, 542ch     ; CONFIG_MEMSIZE
    dd 8008h, 8014h     ; GRBM_STATUS2, GRBM_PWR_CNTL, GRBM_STATUS
    dd 8020h, 8024h     ; GRBM_SOFT_RESET
    dd 8234h, 8238h     ; CP_MEC_CNTL
    dd 98f8h, 98fch     ; GB_ADDR_CONFIG
    dd 9910h, 99d0h     ; GB_TILE_MODE0-31, GB_MACROTILE_MODE0-15
    dd 8674h, 8684h     ; CP_STALLED_STAT1 .. CP_STAT
    dd 86d8h, 86dch     ; CP_ME_CNTL
    dd 8700h, 870ch     ; CP_RB0_RPTR, CP_RB_WPTR_DELAY, CP_RB_WPTR_POLL_CNTL
    dd 0c100h, 0c118h   ; CP_RB0_BASE .. CP_RB0_WPTR
    dd 0c12ch, 0c130h   ; CP_DEVICE_ID
    dd 0c140h, 0c148h   ; CP_ENDIAN_SWAP, CP_RB_VMID
    dd 0c164h, 0c168h   ; CP_RB_DOORBELL_CONTROL
    dd 0c1a8h, 0c1ach   ; CP_INT_CNTL_RING0
    dd 0c2b8h, 0c2bch   ; CP_MAX_CONTEXT
    dd 0c2c4h, 0c2c8h   ; CP_RB0_BASE_HI
    dd 30100h, 30104h   ; SCRATCH_REG0
    dd 3b000h, 3b004h   ; RLC_CNTL
    dd 3b010h, 3b014h   ; RLC_STAT
    dd 3b100h, 3b104h   ; RLC_GPM_STAT
    dd 3b10ch, 3b110h   ; RLC_PG_CNTL
    dd 3b124h, 3b128h   ; RLC_CGCG_CGLS_CTRL
    dd 3b184h, 3b18ch   ; RLC_SERDES_CU_MASTER_BUSY, _NONCU_MASTER_BUSY
CC_READ_WINDOW_COUNT    equ 26

; Writes accepted: exact offsets.
CCWriteOffsets label dword
    dd 5480h            ; HDP_MEM_COHERENCY_FLUSH_CNTL
    dd 2f30h            ; HDP_DEBUG0, the HDP read-cache invalidate
    dd 8020h            ; GRBM_SOFT_RESET, value checked in CC_Execute
    dd 86d8h            ; CP_ME_CNTL
    dd 8704h            ; CP_RB_WPTR_DELAY
    ; GB_TILE_MODE8, the linear-aligned entry the first draw's colour
    ; buffer names. The BIOS leaves the table unprogrammed (M-XP7053 read
    ; 00C0077Ch); Linux's gfx_v8_0_tiling_mode_table_init writes it.
    dd 9930h
    dd 0c100h           ; CP_RB0_BASE
    dd 0c104h           ; CP_RB0_CNTL
    dd 0c10ch           ; CP_RB0_RPTR_ADDR
    dd 0c110h           ; CP_RB0_RPTR_ADDR_HI
    dd 0c114h           ; CP_RB0_WPTR
    dd 0c12ch           ; CP_DEVICE_ID
    dd 0c140h           ; CP_ENDIAN_SWAP
    dd 0c144h           ; CP_RB_VMID
    dd 0c164h           ; CP_RB_DOORBELL_CONTROL
    dd 0c1a8h           ; CP_INT_CNTL_RING0
    dd 0c2b8h           ; CP_MAX_CONTEXT
    dd 0c2c4h           ; CP_RB0_BASE_HI
    dd 30100h           ; SCRATCH_REG0
    dd 3b000h           ; RLC_CNTL
    dd 3b10ch           ; RLC_PG_CNTL
    dd 3b124h           ; RLC_CGCG_CGLS_CTRL
    dd 3e050h           ; CP_PFP_UCODE_ADDR
    dd 3e058h           ; CP_ME_RAM_WADDR
    dd 3e060h           ; CP_CE_UCODE_ADDR
    dd 3e0f0h           ; RLC_GPM_UCODE_ADDR
CC_WRITE_OFFSET_COUNT   equ 26

CCResult label dword
    dd CC_MAGIC                 ; 00 magic
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
CCOpResults  dd CC_OP_MAX dup (0)
CCBlock      dd CC_BLOCK_DWORDS dup (0)
CCSaved      dd (CC_SCRATCH_BYTES / 4) dup (0)
CCMmio       dd 0
CCScratch    dd 0
CCConfig     dd 0
CCData       dd 0
CCDataCount  dd 0
VxD_LOCKED_DATA_ENDS

VxD_LOCKED_CODE_SEG

; EAX = PCI configuration address. Returns EAX = configuration dword.
; 0CF8h saved and restored with interrupts off.
BeginProc CC_Pci_Read
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
EndProc CC_Pci_Read

; Scans buses 0-15, device slots 0-31, function 0, for 1002:9874.
; Returns CF clear and EAX = configuration address, or CF set.
BeginProc CC_Find
    push    ebx
    push    ecx
    mov     ebx, 80000000h
CC_Find_Next:
    mov     eax, ebx
    call    CC_Pci_Read
    cmp     eax, (CC_DEVICE_CARRIZO shl 16) or CC_VENDOR_ATI
    je      short CC_Find_Found
    add     ebx, 800h
    cmp     ebx, 80100000h
    jb      short CC_Find_Next
    pop     ecx
    pop     ebx
    stc
    ret
CC_Find_Found:
    mov     CCResult[8], eax
    mov     eax, ebx
    pop     ecx
    pop     ebx
    clc
    ret
EndProc CC_Find

; Finds and maps the chip once. Returns CF clear when both windows are
; mapped and the identity check passed.
BeginProc CC_Map
    pushad
    cmp     CCScratch, 0
    jne     CC_Map_Ok

    call    CC_Find
    jc      CC_Map_Fail
    mov     CCConfig, eax
    mov     CCResult[44], eax
    or      CCResult[4], CC_ST_FOUND

    ; BAR5: 32-bit memory, 256 KiB aligned. It cannot be 64-bit: its high
    ; half would be 28h, the CardBus CIS pointer.
    mov     eax, CCConfig
    or      eax, CC_CFG_BAR5
    call    CC_Pci_Read
    mov     CCResult[20], eax
    mov     esi, eax
    test    esi, CC_BAR_IO
    jnz     CC_Map_Bad_Bar
    test    esi, CC_BAR_TYPE_MASK
    jnz     CC_Map_Bad_Bar
    and     esi, NOT 0fh
    jz      CC_Map_Bad_Bar
    test    esi, CC_MMIO_ALIGN
    jnz     CC_Map_Bad_Bar

    ; BAR0: memory, 64-bit, high half zero, 256 MiB aligned.
    mov     eax, CCConfig
    or      eax, CC_CFG_BAR0
    call    CC_Pci_Read
    mov     CCResult[12], eax
    mov     edi, eax
    mov     eax, CCConfig
    or      eax, CC_CFG_BAR0_HIGH
    call    CC_Pci_Read
    mov     CCResult[16], eax
    test    edi, CC_BAR_IO
    jnz     CC_Map_Bad_Bar
    mov     edx, edi
    and     edx, CC_BAR_TYPE_MASK
    cmp     edx, CC_BAR_TYPE_64
    jne     CC_Map_Bad_Bar
    test    eax, eax
    jnz     CC_Map_Bad_Bar
    and     edi, NOT 0fh
    jz      CC_Map_Bad_Bar
    test    edi, CC_LFB_ALIGN
    jnz     CC_Map_Bad_Bar

    VMMcall _MapPhysToLinear,<esi,CC_MMIO_BYTES,0>
    cmp     eax, 0ffffffffh
    je      CC_Map_Fail
    mov     CCMmio, eax

    ; The identity check before the scratch window is mapped: at least
    ; 256 MiB of carve-out, so BAR0 + 248 MiB is inside it.
    mov     edx, [eax+CC_CONFIG_MEMSIZE]
    mov     CCResult[28], edx
    cmp     edx, CC_MEMSIZE_MIN_MIB
    jb      CC_Map_Fail
    cmp     edx, CC_MEMSIZE_MAX_MIB
    ja      CC_Map_Fail
    or      CCResult[4], CC_ST_IDENTITY

    add     edi, CC_SCRATCH_OFFSET
    VMMcall _MapPhysToLinear,<edi,CC_SCRATCH_BYTES,0>
    cmp     eax, 0ffffffffh
    je      CC_Map_Fail
    mov     CCScratch, eax
    or      CCResult[4], CC_ST_MAPPED

CC_Map_Ok:
    popad
    clc
    ret
CC_Map_Bad_Bar:
    or      CCResult[4], CC_ST_BAD_BAR
CC_Map_Fail:
    popad
    stc
    ret
EndProc CC_Map

; EDX = offset. CF clear if readable.
BeginProc CC_Read_Allowed
    push    ecx
    push    esi
    test    edx, 3
    jnz     short CC_Read_Refused
    mov     esi, OFFSET32 CCReadWindows
    mov     ecx, CC_READ_WINDOW_COUNT
CC_Read_Next:
    cmp     edx, [esi]
    jb      short CC_Read_Skip
    cmp     edx, [esi+4]
    jb      short CC_Read_Accepted
CC_Read_Skip:
    add     esi, 8
    dec     ecx
    jnz     short CC_Read_Next
CC_Read_Refused:
    pop     esi
    pop     ecx
    stc
    ret
CC_Read_Accepted:
    pop     esi
    pop     ecx
    clc
    ret
EndProc CC_Read_Allowed

; EDX = offset. CF clear if writable.
BeginProc CC_Write_Allowed
    push    ecx
    push    esi
    mov     esi, OFFSET32 CCWriteOffsets
    mov     ecx, CC_WRITE_OFFSET_COUNT
CC_Write_Next:
    cmp     edx, [esi]
    je      short CC_Write_Accepted
    add     esi, 4
    dec     ecx
    jnz     short CC_Write_Next
    pop     esi
    pop     ecx
    stc
    ret
CC_Write_Accepted:
    pop     esi
    pop     ecx
    clc
    ret
EndProc CC_Write_Allowed

; EDX = scratch offset, ECX = dwords (at least 1). CF clear if the run lies
; inside the window and is aligned.
BeginProc CC_Vram_Allowed
    push    eax
    test    edx, 3
    jnz     short CC_Vram_Refused
    test    ecx, ecx
    jz      short CC_Vram_Refused
    cmp     ecx, CC_SCRATCH_BYTES / 4
    ja      short CC_Vram_Refused
    cmp     edx, CC_SCRATCH_BYTES
    jae     short CC_Vram_Refused
    mov     eax, ecx
    shl     eax, 2
    add     eax, edx
    cmp     eax, CC_SCRATCH_BYTES
    ja      short CC_Vram_Refused
    pop     eax
    clc
    ret
CC_Vram_Refused:
    pop     eax
    stc
    ret
EndProc CC_Vram_Allowed

; ESI = op (four dwords). Returns EAX = the op's result, CF set if refused.
BeginProc CC_Execute
    push    ebx
    push    ecx
    push    edx
    push    edi
    mov     eax, [esi]
    mov     edx, [esi+4]

    cmp     eax, CC_OP_MMIO_READ
    jne     short CC_Exec_Not_Read
    call    CC_Read_Allowed
    jc      CC_Exec_Refused
    mov     ebx, CCMmio
    mov     eax, [ebx+edx]
    jmp     CC_Exec_Done
CC_Exec_Not_Read:

    cmp     eax, CC_OP_MMIO_WRITE
    jne     short CC_Exec_Not_Write
    call    CC_Write_Allowed
    jc      CC_Exec_Refused
    mov     eax, [esi+8]
    ; GRBM_SOFT_RESET takes the RLC's bit or nothing: rlc_reset's pulse.
    ; No other block of the chip is ever reset from here.
    cmp     edx, CC_GRBM_SOFT_RESET
    jne     short CC_Exec_Write_Value_Ok
    test    eax, NOT CC_SOFT_RESET_RLC
    jnz     CC_Exec_Refused
CC_Exec_Write_Value_Ok:
    mov     ebx, CCMmio
    mov     [ebx+edx], eax
    ; Post the write before the next op.
    mov     eax, [ebx+CC_CONFIG_MEMSIZE]
    xor     eax, eax
    jmp     CC_Exec_Done
CC_Exec_Not_Write:

    cmp     eax, CC_OP_MMIO_RMW
    jne     short CC_Exec_Not_Rmw
    ; Not GRBM_SOFT_RESET: a read-modify-write could carry another
    ; block's bit through.
    cmp     edx, CC_GRBM_SOFT_RESET
    je      CC_Exec_Refused
    call    CC_Write_Allowed
    jc      CC_Exec_Refused
    mov     ebx, CCMmio
    mov     eax, [ebx+edx]
    mov     ecx, [esi+8]
    not     ecx
    and     eax, ecx
    or      eax, [esi+12]
    mov     [ebx+edx], eax
    mov     ecx, [ebx+CC_CONFIG_MEMSIZE]
    jmp     CC_Exec_Done
CC_Exec_Not_Rmw:

    cmp     eax, CC_OP_UCODE
    jne     CC_Exec_Not_Ucode
    cmp     edx, CC_PFP_UCODE_DATA
    je      short CC_Exec_Ucode_Port_Ok
    cmp     edx, CC_ME_RAM_DATA
    je      short CC_Exec_Ucode_Port_Ok
    cmp     edx, CC_CE_UCODE_DATA
    je      short CC_Exec_Ucode_Port_Ok
    cmp     edx, CC_RLC_GPM_UCODE_DATA
    jne     CC_Exec_Refused
CC_Exec_Ucode_Port_Ok:
    mov     eax, [esi+8]
    mov     ecx, [esi+12]
    test    ecx, ecx
    jz      CC_Exec_Refused
    cmp     eax, CCDataCount
    jae     CC_Exec_Refused
    mov     ebx, CCDataCount
    sub     ebx, eax
    cmp     ecx, ebx
    ja      CC_Exec_Refused
    push    esi
    mov     esi, CCData
    lea     esi, [esi+eax*4]
    mov     ebx, CCMmio
    add     ebx, edx
    mov     eax, ecx
CC_Exec_Ucode_Next:
    mov     edi, [esi]
    mov     [ebx], edi
    add     esi, 4
    dec     ecx
    jnz     short CC_Exec_Ucode_Next
    pop     esi
    add     CCResult[48], eax
    jmp     CC_Exec_Done
CC_Exec_Not_Ucode:

    cmp     eax, CC_OP_VRAM_DATA
    jne     short CC_Exec_Not_Vram_Data
    mov     ecx, [esi+12]
    call    CC_Vram_Allowed
    jc      CC_Exec_Refused
    mov     eax, [esi+8]
    cmp     eax, CCDataCount
    jae     CC_Exec_Refused
    mov     ebx, CCDataCount
    sub     ebx, eax
    cmp     ecx, ebx
    ja      CC_Exec_Refused
    push    esi
    mov     esi, CCData
    lea     esi, [esi+eax*4]
    mov     edi, CCScratch
    add     edi, edx
    cld
    rep     movsd
    pop     esi
    xor     eax, eax
    jmp     CC_Exec_Done
CC_Exec_Not_Vram_Data:

    cmp     eax, CC_OP_VRAM_WRITE
    jne     short CC_Exec_Not_Vram_Write
    mov     ecx, 1
    call    CC_Vram_Allowed
    jc      CC_Exec_Refused
    mov     ebx, CCScratch
    mov     eax, [esi+8]
    mov     [ebx+edx], eax
    xor     eax, eax
    jmp     CC_Exec_Done
CC_Exec_Not_Vram_Write:

    cmp     eax, CC_OP_VRAM_READ
    jne     short CC_Exec_Not_Vram_Read
    mov     ecx, 1
    call    CC_Vram_Allowed
    jc      CC_Exec_Refused
    mov     ebx, CCScratch
    mov     eax, [ebx+edx]
    jmp     CC_Exec_Done
CC_Exec_Not_Vram_Read:

    cmp     eax, CC_OP_VRAM_FILL
    jne     short CC_Exec_Not_Fill
    mov     ecx, [esi+8]
    call    CC_Vram_Allowed
    jc      CC_Exec_Refused
    mov     edi, CCScratch
    add     edi, edx
    mov     eax, [esi+12]
    cld
    rep     stosd
    xor     eax, eax
    jmp     CC_Exec_Done
CC_Exec_Not_Fill:

    cmp     eax, CC_OP_VRAM_PATTERN
    jne     short CC_Exec_Not_Pattern
    mov     ecx, [esi+8]
    call    CC_Vram_Allowed
    jc      CC_Exec_Refused
    mov     edi, CCScratch
    add     edi, edx
    mov     eax, [esi+12]
CC_Exec_Pattern_Next:
    mov     [edi], eax
    add     eax, CC_PATTERN_STEP
    add     edi, 4
    dec     ecx
    jnz     short CC_Exec_Pattern_Next
    xor     eax, eax
    jmp     CC_Exec_Done
CC_Exec_Not_Pattern:

    cmp     eax, CC_OP_VRAM_BLOCK
    jne     short CC_Exec_Not_Block
    mov     ecx, [esi+8]
    call    CC_Vram_Allowed
    jc      CC_Exec_Refused
    mov     eax, CCResult[40]
    mov     ebx, eax
    add     ebx, ecx
    cmp     ebx, CC_BLOCK_DWORDS
    ja      CC_Exec_Refused
    mov     CCResult[40], ebx
    push    esi
    mov     esi, CCScratch
    add     esi, edx
    lea     edi, CCBlock[eax*4]
    cld
    rep     movsd
    pop     esi
    jmp     CC_Exec_Done
CC_Exec_Not_Block:

    cmp     eax, CC_OP_WAIT_VRAM
    jne     short CC_Exec_Not_Wait_Vram
    mov     ecx, 1
    call    CC_Vram_Allowed
    jc      CC_Exec_Refused
    mov     ebx, CCScratch
    add     ebx, edx
    mov     edx, [esi+8]
    mov     ecx, [esi+12]
    xor     eax, eax
CC_Exec_Wait_Vram_Next:
    cmp     [ebx], edx
    je      CC_Exec_Done
    inc     eax
    cmp     eax, ecx
    jb      short CC_Exec_Wait_Vram_Next
    or      CCResult[4], CC_ST_TIMEOUT
    mov     eax, CC_TIMEOUT
    jmp     CC_Exec_Done
CC_Exec_Not_Wait_Vram:

    cmp     eax, CC_OP_WAIT_MMIO
    jne     short CC_Exec_Not_Wait_Mmio
    call    CC_Read_Allowed
    jc      CC_Exec_Refused
    mov     ebx, CCMmio
    add     ebx, edx
    xor     eax, eax
CC_Exec_Wait_Mmio_Next:
    mov     ecx, [ebx]
    and     ecx, [esi+8]
    cmp     ecx, [esi+12]
    je      CC_Exec_Done
    inc     eax
    cmp     eax, CC_WAIT_MMIO_LIMIT
    jb      short CC_Exec_Wait_Mmio_Next
    or      CCResult[4], CC_ST_TIMEOUT
    mov     eax, CC_TIMEOUT
    jmp     CC_Exec_Done
CC_Exec_Not_Wait_Mmio:

    cmp     eax, CC_OP_DELAY
    jne     short CC_Exec_Not_Delay
    mov     ecx, edx
    test    ecx, ecx
    jz      CC_Exec_Refused
    cmp     ecx, CC_DELAY_MAX
    ja      CC_Exec_Refused
    mov     ebx, CCMmio
CC_Exec_Delay_Next:
    mov     eax, [ebx+CC_SRBM_STATUS]
    dec     ecx
    jnz     short CC_Exec_Delay_Next
    jmp     CC_Exec_Done
CC_Exec_Not_Delay:

    cmp     eax, CC_OP_SAVE
    jne     short CC_Exec_Not_Save
    push    esi
    mov     esi, CCScratch
    mov     edi, OFFSET32 CCSaved
    mov     ecx, CC_SCRATCH_BYTES / 4
    cld
    rep     movsd
    pop     esi
    or      CCResult[4], CC_ST_SAVED
    xor     eax, eax
    jmp     short CC_Exec_Done
CC_Exec_Not_Save:

    cmp     eax, CC_OP_RESTORE
    jne     short CC_Exec_Refused
    ; Only what was saved in this run goes back.
    test    CCResult[4], CC_ST_SAVED
    jz      short CC_Exec_Refused
    push    esi
    mov     esi, OFFSET32 CCSaved
    mov     edi, CCScratch
    mov     ecx, CC_SCRATCH_BYTES / 4
    cld
    rep     movsd
    pop     esi
    xor     eax, eax

CC_Exec_Done:
    pop     edi
    pop     edx
    pop     ecx
    pop     ebx
    clc
    ret
CC_Exec_Refused:
    pop     edi
    pop     edx
    pop     ecx
    pop     ebx
    stc
    ret
EndProc CC_Execute

; EBP = DIOC input buffer, ECX = its byte count. The ops come first, the
; data array after them; both counts must fit the buffer.
BeginProc CC_Run
    pushad
    mov     eax, [ebp+CC_IN_COUNT]
    cmp     eax, CC_OP_MAX
    ja      CC_Run_Refused
    mov     edx, [ebp+CC_IN_DATA_COUNT]
    cmp     edx, CC_DATA_MAX
    ja      CC_Run_Refused
    sub     ecx, CC_IN_OPS
    jb      CC_Run_Refused
    mov     ebx, eax
    shl     ebx, 4
    sub     ecx, ebx
    jb      CC_Run_Refused
    shr     ecx, 2
    cmp     edx, ecx
    ja      CC_Run_Refused
    lea     ebx, [ebp+CC_IN_OPS+ebx]
    mov     CCData, ebx
    mov     CCDataCount, edx
    call    CC_Map
    jc      short CC_Run_Done

    mov     ecx, [ebp+CC_IN_COUNT]
    lea     esi, [ebp+CC_IN_OPS]
    mov     edi, OFFSET32 CCOpResults
    xor     ebx, ebx
    jecxz   short CC_Run_Complete
CC_Run_Next:
    call    CC_Execute
    jc      short CC_Run_Refused_At
    mov     [edi], eax
    inc     ebx
    mov     CCResult[32], ebx
    add     esi, CC_OP_BYTES
    add     edi, 4
    dec     ecx
    jnz     short CC_Run_Next
CC_Run_Complete:
    or      CCResult[4], CC_ST_COMPLETE
    jmp     short CC_Run_Done
CC_Run_Refused_At:
    mov     CCResult[36], ebx
CC_Run_Refused:
    or      CCResult[4], CC_ST_REFUSED
CC_Run_Done:
    mov     CCData, 0
    mov     CCDataCount, 0
    popad
    ret
EndProc CC_Run

BeginProc CC_Reset_Result
    pushad
    mov     edi, OFFSET32 CCResult + 4
    mov     ecx, CC_RESULT_DWORDS - 1
    xor     eax, eax
    cld
    rep     stosd
    popad
    ret
EndProc CC_Reset_Result

BeginProc CC_W32_DeviceIoControl
    cmp     ecx, DIOC_OPEN
    je      CC_Dioc_Ok
    cmp     ecx, DIOC_CLOSEHANDLE
    je      CC_Dioc_Ok
    cmp     ecx, CC_DIOC_RUN
    jne     CC_Dioc_Fail

    pushad
    mov     ebx, esi
    mov     ebp, [ebx.lpvInBuffer]
    test    ebp, ebp
    jz      short CC_Dioc_Copy_Fail
    mov     ecx, [ebx.cbInBuffer]
    cmp     ecx, CC_IN_OPS
    jb      short CC_Dioc_Copy_Fail
    mov     edi, [ebx.lpvOutBuffer]
    test    edi, edi
    jz      short CC_Dioc_Copy_Fail
    cmp     [ebx.cbOutBuffer], CC_RESULT_DWORDS * 4
    jb      short CC_Dioc_Copy_Fail

    call    CC_Reset_Result
    mov     ecx, [ebx.cbInBuffer]
    call    CC_Run

    mov     edi, [ebx.lpvOutBuffer]
    mov     esi, OFFSET32 CCResult
    mov     ecx, CC_RESULT_DWORDS
    cld
    rep     movsd
    mov     eax, [ebx.lpcbBytesReturned]
    test    eax, eax
    jz      short CC_Dioc_Copy_Done
    mov     dword ptr [eax], CC_RESULT_DWORDS * 4
CC_Dioc_Copy_Done:
    popad
CC_Dioc_Ok:
    xor     eax, eax
    ret

CC_Dioc_Copy_Fail:
    popad
CC_Dioc_Fail:
    mov     eax, 1
    ret
EndProc CC_W32_DeviceIoControl

BeginProc CC_Dynamic_Init
    clc
    ret
EndProc CC_Dynamic_Init

BeginProc CC_Dynamic_Exit
    clc
    ret
EndProc CC_Dynamic_Exit

Begin_Control_Dispatch CC
    Control_Dispatch Sys_Dynamic_Device_Init, CC_Dynamic_Init
    Control_Dispatch Sys_Dynamic_Device_Exit, CC_Dynamic_Exit
    Control_Dispatch W32_DeviceIoControl, CC_W32_DeviceIoControl
End_Control_Dispatch CC

VxD_LOCKED_CODE_ENDS

end
