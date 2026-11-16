; AMD Oland (GCN 1, Southern Islands) graphics command processor probe VxD.
;
; Phase 6 of docs\plans\ati-oland-gfx6.md, first step: a copy of the
; Mullins CP probe (cik_cp.asm, itself a copy of carrizo_cp.asm) for SI.
; What changed, from Linux amdgpu gfx_v6_0.c and radeon sid.h:
;
; - The device: 1002:6610 only, the id read from W9QI0DAD's card
;   (docs\decisions\2026-11-16-oland-first-survey.md). Its registers are
;   BAR2, a 64-bit memory BAR whose high half (BAR3) must be zero, not
;   BAR5 as on the APUs.
; - The RLC's microcode port is indexed: SI's rlc_resume writes
;   RLC_UCODE_ADDR (C32Ch) with each dword's index before RLC_UCODE_DATA
;   (C330h). The ucode op does that for the RLC port; PFP, CE and ME
;   still stream through their auto-incrementing ports (C150h-C16Fh).
; - SI's registers: SCRATCH_REG0 is a config register at 8500h, the RLC's
;   status at C34Ch and its SERDES busy pair at C464h/C468h; there is no
;   CP_RB0_BASE_HI, CP_DEVICE_ID, CP_MAX_CONTEXT or CP_ENDIAN_SWAP (SI's
;   ME_INITIALIZE packet carries those), and no MEC.
; - The gpu_init table windows (SCInitWindows) are the graphics block and
;   the three address-configuration copies; see v9x_si_gpu_init_ops.
;
; Loaded dynamically by V9XSICP.EXE, which decides every access; this VxD
; only executes an op list against the chip and refuses anything outside
; it.
;
; It finds the chip with configuration mechanism 1, maps BAR2 (256 KiB of
; registers) and a 64 KiB scratch window of VRAM through BAR0, and runs:
;
;   MMIO read32    - GRBM and SRBM status, MC_VM_FB_LOCATION, CONFIG_MEMSIZE,
;                    the CP's ring and state registers, the RLC's control
;                    and status, and SCRATCH_REG0.
;   MMIO write32   - the CP and RLC registers that gfx_v6_0.c writes in
;                    rlc_resume, cp_gfx_load_microcode and cp_gfx_resume
;                    (ring 0 only), the microcode address registers,
;                    SCRATCH_REG0, the two HDP coherency registers, and
;                    GRBM_SOFT_RESET with 0 or the RLC's bit alone. The DMA
;                    engines, the memory controller and the display are not
;                    written.
;   MMIO rmw       - the same offsets: clear one mask, set another; not
;                    GRBM_SOFT_RESET.
;   ucode          - dwords from the caller's data array to one of the four
;                    microcode data ports (PFP, ME, CE streamed; the RLC's
;                    indexed), the only ports this op writes.
;   mmio table     - as cik_cp.asm's, against SCInitWindows (see below).
;   delay          - a count of SRBM_STATUS reads, at most SC_DELAY_MAX.
;   VRAM ops       - write, read, fill, pattern, read block, and a copy of a
;                    run of the caller's data array, all at aligned offsets
;                    inside the scratch window.
;   wait           - a VRAM dword equal to a value, or an MMIO dword under a
;                    mask equal to a value, at most the op's limit reads.
;   save/restore   - the whole scratch window to and from a copy here.
;
; The scratch window is BAR0 + 0F800000h, 248 MiB in: past the driver's
; heap (16 MiB) and its DMA ring window (240 MiB, eng_vi.c's Oland mode),
; which may be running while this probe does. W9QI0DAD's BAR0 is a 64-bit
; prefetchable 256 MiB window at D0000000 that starts at VRAM's first byte,
; with 2 GiB behind it. It is mapped only when BAR0 is a 64-bit memory BAR
; below 4 GiB, aligned to 256 MiB, on a chip whose CONFIG_MEMSIZE says
; 256 MiB or more. The publisher writes and reads back a pattern there
; before the engine is touched.
;
; Offsets: radeon sid.h (byte offsets) and amdgpu gfx_6_0_d.h
; (build\external\carrizo\linux\), checked against each other.
.386p

.xlist
include VMM.INC
include VWIN32.INC
.list

Declare_Virtual_Device SICP, 1, 0, SC_Control, \
                       Undefined_Device_ID, Undefined_Init_Order, , ,

SC_MAGIC                equ 50434953h ; "SICP"
SC_DIOC_RUN             equ 1
SC_VENDOR_ATI           equ 1002h
SC_DEVICE_OLAND         equ 6610h
SC_OP_MAX               equ 512
; PFP, CE and ME (2,144 dwords each) and the RLC (2,048): oland_*.bin.
SC_DATA_MAX             equ 16384
SC_BLOCK_DWORDS         equ 4096
SC_TIMEOUT              equ 0ffffffffh

SC_CFG_BAR0             equ 10h
SC_CFG_BAR0_HIGH        equ 14h
SC_CFG_BAR2             equ 18h
SC_CFG_BAR2_HIGH        equ 1ch
SC_BAR_IO               equ 00000001h
SC_BAR_TYPE_MASK        equ 00000006h
SC_BAR_TYPE_64          equ 00000004h
SC_MMIO_BYTES           equ 00040000h
SC_MMIO_ALIGN           equ 0003ffffh
SC_LFB_ALIGN            equ 0fffffffh
SC_SCRATCH_OFFSET       equ 0f800000h
SC_SCRATCH_BYTES        equ 00010000h
SC_CONFIG_MEMSIZE       equ 5428h
SC_MEMSIZE_MIN_MIB      equ 256
SC_MEMSIZE_MAX_MIB      equ 4096

; Op codes; the layout is four dwords: op, a, b, c.
SC_OP_MMIO_READ         equ 1   ; a offset
SC_OP_MMIO_WRITE        equ 2   ; a offset, b value
SC_OP_VRAM_WRITE        equ 3   ; a offset, b value
SC_OP_VRAM_READ         equ 4   ; a offset
SC_OP_VRAM_FILL         equ 5   ; a offset, b dwords, c value
SC_OP_VRAM_BLOCK        equ 6   ; a offset, b dwords -> block area
SC_OP_WAIT_VRAM         equ 7   ; a offset, b value, c limit
SC_OP_WAIT_MMIO         equ 8   ; a offset, b mask, c value; limit fixed
SC_OP_SAVE              equ 9
SC_OP_RESTORE           equ 10
SC_OP_VRAM_PATTERN      equ 11  ; a offset, b dwords, c seed
SC_OP_DELAY             equ 12  ; a SRBM_STATUS reads
SC_OP_MMIO_RMW          equ 13  ; a offset, b mask to clear, c mask to set
SC_OP_UCODE             equ 14  ; a port, b first data dword, c dwords
SC_OP_VRAM_DATA         equ 15  ; a offset, b first data dword, c dwords
SC_OP_MMIO_TABLE        equ 16  ; a first data dword, b entries
SC_DELAY_MAX            equ 100000
SC_SRBM_STATUS          equ 0e50h
SC_GRBM_SOFT_RESET      equ 8020h
SC_SOFT_RESET_RLC       equ 00000004h
SC_PFP_UCODE_DATA       equ 0c154h
SC_ME_RAM_DATA          equ 0c160h
SC_CE_UCODE_DATA        equ 0c16ch
SC_RLC_UCODE_ADDR       equ 0c32ch
SC_RLC_UCODE_DATA       equ 0c330h
SC_OP_BYTES             equ 16
SC_WAIT_MMIO_LIMIT      equ 2000000
SC_PATTERN_STEP         equ 9e3779b9h

; Status bits.
SC_ST_FOUND             equ 00000001h
SC_ST_MAPPED            equ 00000002h
SC_ST_IDENTITY          equ 00000004h
SC_ST_REFUSED           equ 00000008h
SC_ST_COMPLETE          equ 00000010h
SC_ST_BAD_BAR           equ 00000020h
SC_ST_TIMEOUT           equ 00000040h
SC_ST_SAVED             equ 00000080h

; Input: op count, data count, the ops, then the data dwords.
SC_IN_COUNT             equ 0
SC_IN_DATA_COUNT        equ 4
SC_IN_OPS               equ 8

; Output layout, in dwords. Must match struct v9x_sicp_result in
; si_cp_win32.c.
SC_HEADER_DWORDS        equ 16
SC_RESULT_DWORDS        equ (SC_HEADER_DWORDS + SC_OP_MAX + SC_BLOCK_DWORDS)

VxD_LOCKED_DATA_SEG

; Reads accepted: start, limit (exclusive). The microcode data ports
; (C154h, C160h, C16Ch, C330h) are left out: a read through a data port
; advances its address.
SCReadWindows label dword
    dd 0e4ch, 0e54h     ; SRBM_STATUS2, SRBM_STATUS
    dd 2024h, 2028h     ; MC_VM_FB_LOCATION
    dd 2f30h, 2f34h     ; HDP_DEBUG0
    dd 5428h, 542ch     ; CONFIG_MEMSIZE
    dd 8008h, 8018h     ; GRBM_STATUS2, GRBM_PWR_CNTL, GRBM_STATUS, _SE0
    dd 8020h, 8024h     ; GRBM_SOFT_RESET
    dd 802ch, 8030h     ; GRBM_GFX_INDEX
    dd 8500h, 8504h     ; SCRATCH_REG0
    dd 8540h, 8548h     ; SCRATCH_UMSK, SCRATCH_ADDR
    dd 85bch, 85c0h     ; CP_SEM_WAIT_TIMER
    dd 85c8h, 85cch     ; CP_SEM_INCOMPLETE_TIMER_CNTL
    dd 8674h, 8684h     ; CP_STALLED_STAT1 .. CP_STAT
    dd 86d8h, 86dch     ; CP_ME_CNTL
    dd 8700h, 8708h     ; CP_RB0_RPTR, CP_RB_WPTR_DELAY
    dd 89bch, 89c4h     ; CC_ and GC_USER_SHADER_ARRAY_CONFIG
    dd 98f4h, 98fch     ; CC_RB_BACKEND_DISABLE, GB_ADDR_CONFIG
    dd 9910h, 9990h     ; GB_TILE_MODE0-31
    dd 8000h, 8004h     ; GRBM_CNTL
    dd 8bcch, 8bd0h     ; PA_SC_FIFO_SIZE
    dd 90e8h, 90ech     ; SPI_STATIC_THREAD_MGMT_3
    dd 9148h, 9150h     ; CGTS_TCC_DISABLE, CGTS_USER_TCC_DISABLE
    dd 2f48h, 2f4ch     ; HDP_ADDR_CONFIG
    dd 0d0b8h, 0d0bch   ; DMA0_TILING_CONFIG
    dd 28350h, 28354h   ; PA_SC_RASTER_CONFIG
    dd 0c100h, 0c118h   ; CP_RB0_BASE .. CP_RB0_WPTR
    dd 0c144h, 0c148h   ; CP_RB_VMID
    dd 0c150h, 0c154h   ; CP_PFP_UCODE_ADDR
    dd 0c158h, 0c160h   ; CP_ME_RAM_RADDR, CP_ME_RAM_WADDR
    dd 0c168h, 0c16ch   ; CP_CE_UCODE_ADDR
    dd 0c1a8h, 0c1ach   ; CP_INT_CNTL_RING0
    dd 0c1fch, 0c200h   ; CP_DEBUG
    dd 0c300h, 0c31ch   ; RLC_CNTL .. RLC_LB_CNTR_INIT
    dd 0c32ch, 0c330h   ; RLC_UCODE_ADDR
    dd 0c344h, 0c350h   ; RLC_MC_CNTL, RLC_UCODE_CNTL, RLC_STAT
    dd 0c35ch, 0c360h   ; RLC_PG_CNTL
    dd 0c400h, 0c408h   ; RLC_CGTT_MGCG_OVERRIDE, RLC_CGCG_CGLS_CTRL
    dd 0c41ch, 0c420h   ; RLC_LB_INIT_CU_MASK
    dd 0c464h, 0c46ch   ; RLC_SERDES_MASTER_BUSY_0, _1
SC_READ_WINDOW_COUNT    equ 38

; Writes accepted: exact offsets. gfx_v6_0.c's rlc_resume, rlc_stop/start,
; cp_gfx_enable, cp_gfx_load_microcode and cp_gfx_resume (ring 0), and the
; HDP flush and invalidate (si.c).
SCWriteOffsets label dword
    dd 5480h            ; HDP_MEM_COHERENCY_FLUSH_CNTL
    dd 2f30h            ; HDP_DEBUG0, the HDP read-cache invalidate
    dd 8020h            ; GRBM_SOFT_RESET, value checked in SC_Execute
    dd 8500h            ; SCRATCH_REG0
    dd 8540h            ; SCRATCH_UMSK
    dd 8544h            ; SCRATCH_ADDR
    dd 85bch            ; CP_SEM_WAIT_TIMER
    dd 85c8h            ; CP_SEM_INCOMPLETE_TIMER_CNTL
    dd 86d8h            ; CP_ME_CNTL
    dd 8700h            ; CP_RB0_RPTR (cp_gfx_resume writes it)
    dd 8704h            ; CP_RB_WPTR_DELAY
    dd 0c100h           ; CP_RB0_BASE
    dd 0c104h           ; CP_RB0_CNTL
    dd 0c10ch           ; CP_RB0_RPTR_ADDR
    dd 0c110h           ; CP_RB0_RPTR_ADDR_HI
    dd 0c114h           ; CP_RB0_WPTR
    dd 0c150h           ; CP_PFP_UCODE_ADDR
    dd 0c158h           ; CP_ME_RAM_RADDR
    dd 0c15ch           ; CP_ME_RAM_WADDR
    dd 0c168h           ; CP_CE_UCODE_ADDR
    dd 0c1a8h           ; CP_INT_CNTL_RING0
    dd 0c1fch           ; CP_DEBUG
    dd 0c300h           ; RLC_CNTL
    dd 0c304h           ; RLC_RL_BASE
    dd 0c308h           ; RLC_RL_SIZE
    dd 0c30ch           ; RLC_LB_CNTL
    dd 0c314h           ; RLC_LB_CNTR_MAX
    dd 0c318h           ; RLC_LB_CNTR_INIT
    dd 0c32ch           ; RLC_UCODE_ADDR
    dd 0c344h           ; RLC_MC_CNTL
    dd 0c348h           ; RLC_UCODE_CNTL
    dd 0c35ch           ; RLC_PG_CNTL
    dd 0c404h           ; RLC_CGCG_CGLS_CTRL
    dd 0c41ch           ; RLC_LB_INIT_CU_MASK
SC_WRITE_OFFSET_COUNT   equ 34

; The table op's registers: start, limit (exclusive). The graphics block,
; which v9x_si_gpu_init_ops (si_gpu_init.c and its generated tables) stays
; inside - tests\host\test_si_gpu.c checks every entry against this list -
; and the three address-configuration copies constants_init writes outside
; it. GRBM_SOFT_RESET (8020h) and the RLC's microcode port (C32Ch-C333h)
; are left out, so no table can reset a block or reach the RLC's RAM.
SCInitWindows label dword
    dd 2f48h, 2f4ch     ; HDP_ADDR_CONFIG
    dd 8000h, 8020h     ; GRBM_CNTL .. below GRBM_SOFT_RESET
    dd 8024h, 0c000h    ; the rest of the graphics config space
    dd 0c300h, 0c32ch   ; RLC, below its microcode port
    dd 0c334h, 0c500h   ; RLC, above it (load balance, clock gating)
    dd 0d0b8h, 0d0bch   ; DMA0_TILING_CONFIG
    dd 0d8b8h, 0d8bch   ; DMA1_TILING_CONFIG
    dd 28000h, 29000h   ; context registers (PA_SC_RASTER_CONFIG, _MODE_CNTL_1)
SC_INIT_WINDOW_COUNT    equ 8
SCResult label dword
    dd SC_MAGIC                 ; 00 magic
    dd 0                        ; 01 status
    dd 0                        ; 02 id dword
    dd 0                        ; 03 BAR0 low
    dd 0                        ; 04 BAR0 high
    dd 0                        ; 05 BAR2
    dd 0                        ; 06 BAR2 high (BAR3)
    dd 0                        ; 07 CONFIG_MEMSIZE
    dd 0                        ; 08 ops executed
    dd 0                        ; 09 index of the refused op
    dd 0                        ; 10 block dwords used
    dd 0                        ; 11 configuration address
    dd 0                        ; 12 microcode dwords written
    dd 0                        ; 13 reserved
    dd 0                        ; 14 reserved
    dd 0                        ; 15 reserved
SCOpResults  dd SC_OP_MAX dup (0)
SCBlock      dd SC_BLOCK_DWORDS dup (0)
SCSaved      dd (SC_SCRATCH_BYTES / 4) dup (0)
SCMmio       dd 0
SCScratch    dd 0
SCConfig     dd 0
SCData       dd 0
SCDataCount  dd 0
VxD_LOCKED_DATA_ENDS

VxD_LOCKED_CODE_SEG

; EAX = PCI configuration address. Returns EAX = configuration dword.
; 0CF8h saved and restored with interrupts off.
BeginProc SC_Pci_Read
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
EndProc SC_Pci_Read

; Scans buses 0-15, device slots 0-31, function 0, for 1002:6610.
; Returns CF clear and EAX = configuration address, or CF set.
BeginProc SC_Find
    push    ebx
    push    ecx
    mov     ebx, 80000000h
SC_Find_Next:
    mov     eax, ebx
    call    SC_Pci_Read
    cmp     eax, (SC_DEVICE_OLAND shl 16) or SC_VENDOR_ATI
    je      short SC_Find_Found
    add     ebx, 800h
    cmp     ebx, 80100000h
    jb      short SC_Find_Next
    pop     ecx
    pop     ebx
    stc
    ret
SC_Find_Found:
    mov     SCResult[8], eax
    mov     eax, ebx
    pop     ecx
    pop     ebx
    clc
    ret
EndProc SC_Find

; Finds and maps the chip once. Returns CF clear when both windows are
; mapped and the identity check passed.
BeginProc SC_Map
    pushad
    cmp     SCScratch, 0
    jne     SC_Map_Ok

    call    SC_Find
    jc      SC_Map_Fail
    mov     SCConfig, eax
    mov     SCResult[44], eax
    or      SCResult[4], SC_ST_FOUND

    ; BAR2: memory, 64-bit with its high half (BAR3) zero, 256 KiB
    ; aligned (W9QI0DAD: FDD80004, high 0).
    mov     eax, SCConfig
    or      eax, SC_CFG_BAR2
    call    SC_Pci_Read
    mov     SCResult[20], eax
    mov     esi, eax
    mov     eax, SCConfig
    or      eax, SC_CFG_BAR2_HIGH
    call    SC_Pci_Read
    mov     SCResult[24], eax
    test    eax, eax
    jnz     SC_Map_Bad_Bar
    test    esi, SC_BAR_IO
    jnz     SC_Map_Bad_Bar
    mov     edx, esi
    and     edx, SC_BAR_TYPE_MASK
    cmp     edx, SC_BAR_TYPE_64
    jne     SC_Map_Bad_Bar
    and     esi, NOT 0fh
    jz      SC_Map_Bad_Bar
    test    esi, SC_MMIO_ALIGN
    jnz     SC_Map_Bad_Bar

    ; BAR0: memory, 64-bit, high half zero, 256 MiB aligned.
    mov     eax, SCConfig
    or      eax, SC_CFG_BAR0
    call    SC_Pci_Read
    mov     SCResult[12], eax
    mov     edi, eax
    mov     eax, SCConfig
    or      eax, SC_CFG_BAR0_HIGH
    call    SC_Pci_Read
    mov     SCResult[16], eax
    test    edi, SC_BAR_IO
    jnz     SC_Map_Bad_Bar
    mov     edx, edi
    and     edx, SC_BAR_TYPE_MASK
    cmp     edx, SC_BAR_TYPE_64
    jne     SC_Map_Bad_Bar
    test    eax, eax
    jnz     SC_Map_Bad_Bar
    and     edi, NOT 0fh
    jz      SC_Map_Bad_Bar
    test    edi, SC_LFB_ALIGN
    jnz     SC_Map_Bad_Bar

    VMMcall _MapPhysToLinear,<esi,SC_MMIO_BYTES,0>
    cmp     eax, 0ffffffffh
    je      SC_Map_Fail
    mov     SCMmio, eax

    ; The identity check before the scratch window is mapped: at least
    ; 256 MiB of VRAM, so BAR0 + 248 MiB is inside it.
    mov     edx, [eax+SC_CONFIG_MEMSIZE]
    mov     SCResult[28], edx
    cmp     edx, SC_MEMSIZE_MIN_MIB
    jb      SC_Map_Fail
    cmp     edx, SC_MEMSIZE_MAX_MIB
    ja      SC_Map_Fail
    or      SCResult[4], SC_ST_IDENTITY

    add     edi, SC_SCRATCH_OFFSET
    VMMcall _MapPhysToLinear,<edi,SC_SCRATCH_BYTES,0>
    cmp     eax, 0ffffffffh
    je      SC_Map_Fail
    mov     SCScratch, eax
    or      SCResult[4], SC_ST_MAPPED

SC_Map_Ok:
    popad
    clc
    ret
SC_Map_Bad_Bar:
    or      SCResult[4], SC_ST_BAD_BAR
SC_Map_Fail:
    popad
    stc
    ret
EndProc SC_Map

; EDX = offset. CF clear if readable.
BeginProc SC_Read_Allowed
    push    ecx
    push    esi
    test    edx, 3
    jnz     short SC_Read_Refused
    mov     esi, OFFSET32 SCReadWindows
    mov     ecx, SC_READ_WINDOW_COUNT
SC_Read_Next:
    cmp     edx, [esi]
    jb      short SC_Read_Skip
    cmp     edx, [esi+4]
    jb      short SC_Read_Accepted
SC_Read_Skip:
    add     esi, 8
    dec     ecx
    jnz     short SC_Read_Next
SC_Read_Refused:
    pop     esi
    pop     ecx
    stc
    ret
SC_Read_Accepted:
    pop     esi
    pop     ecx
    clc
    ret
EndProc SC_Read_Allowed

; EDX = offset. CF clear if writable.
BeginProc SC_Write_Allowed
    push    ecx
    push    esi
    mov     esi, OFFSET32 SCWriteOffsets
    mov     ecx, SC_WRITE_OFFSET_COUNT
SC_Write_Next:
    cmp     edx, [esi]
    je      short SC_Write_Accepted
    add     esi, 4
    dec     ecx
    jnz     short SC_Write_Next
    pop     esi
    pop     ecx
    stc
    ret
SC_Write_Accepted:
    pop     esi
    pop     ecx
    clc
    ret
EndProc SC_Write_Allowed

; EDX = offset. CF clear if the table op may write it.
BeginProc SC_Init_Allowed
    push    ecx
    push    esi
    test    edx, 3
    jnz     short SC_Init_Refused
    mov     esi, OFFSET32 SCInitWindows
    mov     ecx, SC_INIT_WINDOW_COUNT
SC_Init_Next:
    cmp     edx, [esi]
    jb      short SC_Init_Skip
    cmp     edx, [esi+4]
    jb      short SC_Init_Accepted
SC_Init_Skip:
    add     esi, 8
    dec     ecx
    jnz     short SC_Init_Next
SC_Init_Refused:
    pop     esi
    pop     ecx
    stc
    ret
SC_Init_Accepted:
    pop     esi
    pop     ecx
    clc
    ret
EndProc SC_Init_Allowed

; EDX = scratch offset, ECX = dwords (at least 1). CF clear if the run lies
; inside the window and is aligned.
BeginProc SC_Vram_Allowed
    push    eax
    test    edx, 3
    jnz     short SC_Vram_Refused
    test    ecx, ecx
    jz      short SC_Vram_Refused
    cmp     ecx, SC_SCRATCH_BYTES / 4
    ja      short SC_Vram_Refused
    cmp     edx, SC_SCRATCH_BYTES
    jae     short SC_Vram_Refused
    mov     eax, ecx
    shl     eax, 2
    add     eax, edx
    cmp     eax, SC_SCRATCH_BYTES
    ja      short SC_Vram_Refused
    pop     eax
    clc
    ret
SC_Vram_Refused:
    pop     eax
    stc
    ret
EndProc SC_Vram_Allowed

; ESI = op (four dwords). Returns EAX = the op's result, CF set if refused.
BeginProc SC_Execute
    push    ebx
    push    ecx
    push    edx
    push    edi
    mov     eax, [esi]
    mov     edx, [esi+4]

    cmp     eax, SC_OP_MMIO_READ
    jne     short SC_Exec_Not_Read
    call    SC_Read_Allowed
    jc      SC_Exec_Refused
    mov     ebx, SCMmio
    mov     eax, [ebx+edx]
    jmp     SC_Exec_Done
SC_Exec_Not_Read:

    cmp     eax, SC_OP_MMIO_WRITE
    jne     short SC_Exec_Not_Write
    call    SC_Write_Allowed
    jc      SC_Exec_Refused
    mov     eax, [esi+8]
    ; GRBM_SOFT_RESET takes the RLC's bit or nothing: rlc_reset's pulse.
    ; No other block of the chip is ever reset from here.
    cmp     edx, SC_GRBM_SOFT_RESET
    jne     short SC_Exec_Write_Value_Ok
    test    eax, NOT SC_SOFT_RESET_RLC
    jnz     SC_Exec_Refused
SC_Exec_Write_Value_Ok:
    mov     ebx, SCMmio
    mov     [ebx+edx], eax
    ; Post the write before the next op.
    mov     eax, [ebx+SC_CONFIG_MEMSIZE]
    xor     eax, eax
    jmp     SC_Exec_Done
SC_Exec_Not_Write:

    cmp     eax, SC_OP_MMIO_RMW
    jne     short SC_Exec_Not_Rmw
    ; Not GRBM_SOFT_RESET: a read-modify-write could carry another
    ; block's bit through.
    cmp     edx, SC_GRBM_SOFT_RESET
    je      SC_Exec_Refused
    call    SC_Write_Allowed
    jc      SC_Exec_Refused
    mov     ebx, SCMmio
    mov     eax, [ebx+edx]
    mov     ecx, [esi+8]
    not     ecx
    and     eax, ecx
    or      eax, [esi+12]
    mov     [ebx+edx], eax
    mov     ecx, [ebx+SC_CONFIG_MEMSIZE]
    jmp     SC_Exec_Done
SC_Exec_Not_Rmw:

    cmp     eax, SC_OP_UCODE
    jne     SC_Exec_Not_Ucode
    cmp     edx, SC_PFP_UCODE_DATA
    je      short SC_Exec_Ucode_Port_Ok
    cmp     edx, SC_ME_RAM_DATA
    je      short SC_Exec_Ucode_Port_Ok
    cmp     edx, SC_CE_UCODE_DATA
    je      short SC_Exec_Ucode_Port_Ok
    cmp     edx, SC_RLC_UCODE_DATA
    jne     SC_Exec_Refused
SC_Exec_Ucode_Port_Ok:
    mov     eax, [esi+8]
    mov     ecx, [esi+12]
    test    ecx, ecx
    jz      SC_Exec_Refused
    cmp     eax, SCDataCount
    jae     SC_Exec_Refused
    mov     ebx, SCDataCount
    sub     ebx, eax
    cmp     ecx, ebx
    ja      SC_Exec_Refused
    push    esi
    mov     esi, SCData
    lea     esi, [esi+eax*4]
    mov     ebx, SCMmio
    cmp     edx, SC_RLC_UCODE_DATA
    je      short SC_Exec_Ucode_Indexed
    add     ebx, edx
    mov     eax, ecx
SC_Exec_Ucode_Next:
    mov     edi, [esi]
    mov     [ebx], edi
    add     esi, 4
    dec     ecx
    jnz     short SC_Exec_Ucode_Next
    pop     esi
    add     SCResult[48], eax
    jmp     SC_Exec_Done
    ; SI's RLC port does not advance: gfx_v6_0_rlc_resume writes
    ; RLC_UCODE_ADDR with each dword's index, then RLC_UCODE_DATA.
SC_Exec_Ucode_Indexed:
    mov     eax, ecx
    xor     edx, edx
SC_Exec_Ucode_Indexed_Next:
    mov     [ebx+SC_RLC_UCODE_ADDR], edx
    mov     edi, [esi]
    mov     [ebx+SC_RLC_UCODE_DATA], edi
    add     esi, 4
    inc     edx
    dec     ecx
    jnz     short SC_Exec_Ucode_Indexed_Next
    pop     esi
    add     SCResult[48], eax
    jmp     SC_Exec_Done
SC_Exec_Not_Ucode:

    cmp     eax, SC_OP_MMIO_TABLE
    jne     SC_Exec_Not_Table
    ; EDX = first data dword, ECX = entries; the run must lie in the data.
    mov     ecx, [esi+8]
    test    ecx, ecx
    jz      SC_Exec_Refused
    cmp     ecx, SC_DATA_MAX / 3
    ja      SC_Exec_Refused
    cmp     edx, SCDataCount
    jae     SC_Exec_Refused
    mov     ebx, SCDataCount
    sub     ebx, edx
    lea     eax, [ecx+ecx*2]
    cmp     eax, ebx
    ja      SC_Exec_Refused
    push    esi
    mov     esi, SCData
    lea     esi, [esi+edx*4]
    ; Every offset first, so a refused table writes nothing.
    push    esi
    push    ecx
SC_Exec_Table_Check:
    mov     edx, [esi]
    call    SC_Init_Allowed
    jc      short SC_Exec_Table_Bad
    add     esi, 12
    dec     ecx
    jnz     short SC_Exec_Table_Check
    pop     ecx
    pop     esi
    mov     ebx, SCMmio
    mov     edi, ecx
SC_Exec_Table_Next:
    mov     edx, [esi]
    mov     eax, [esi+8]
    cmp     dword ptr [esi+4], 0ffffffffh
    je      short SC_Exec_Table_Store
    mov     eax, [esi+4]
    not     eax
    and     eax, [ebx+edx]
    or      eax, [esi+8]
SC_Exec_Table_Store:
    mov     [ebx+edx], eax
    ; Post each write before the next: SRBM_GFX_CNTL's VMID select must
    ; land before the SH_MEM writes it banks.
    mov     eax, [ebx+SC_CONFIG_MEMSIZE]
    add     esi, 12
    dec     ecx
    jnz     short SC_Exec_Table_Next
    pop     esi
    mov     eax, edi
    jmp     SC_Exec_Done
SC_Exec_Table_Bad:
    pop     ecx
    pop     esi
    pop     esi
    jmp     SC_Exec_Refused
SC_Exec_Not_Table:

    cmp     eax, SC_OP_VRAM_DATA
    jne     short SC_Exec_Not_Vram_Data
    mov     ecx, [esi+12]
    call    SC_Vram_Allowed
    jc      SC_Exec_Refused
    mov     eax, [esi+8]
    cmp     eax, SCDataCount
    jae     SC_Exec_Refused
    mov     ebx, SCDataCount
    sub     ebx, eax
    cmp     ecx, ebx
    ja      SC_Exec_Refused
    push    esi
    mov     esi, SCData
    lea     esi, [esi+eax*4]
    mov     edi, SCScratch
    add     edi, edx
    cld
    rep     movsd
    pop     esi
    xor     eax, eax
    jmp     SC_Exec_Done
SC_Exec_Not_Vram_Data:

    cmp     eax, SC_OP_VRAM_WRITE
    jne     short SC_Exec_Not_Vram_Write
    mov     ecx, 1
    call    SC_Vram_Allowed
    jc      SC_Exec_Refused
    mov     ebx, SCScratch
    mov     eax, [esi+8]
    mov     [ebx+edx], eax
    xor     eax, eax
    jmp     SC_Exec_Done
SC_Exec_Not_Vram_Write:

    cmp     eax, SC_OP_VRAM_READ
    jne     short SC_Exec_Not_Vram_Read
    mov     ecx, 1
    call    SC_Vram_Allowed
    jc      SC_Exec_Refused
    mov     ebx, SCScratch
    mov     eax, [ebx+edx]
    jmp     SC_Exec_Done
SC_Exec_Not_Vram_Read:

    cmp     eax, SC_OP_VRAM_FILL
    jne     short SC_Exec_Not_Fill
    mov     ecx, [esi+8]
    call    SC_Vram_Allowed
    jc      SC_Exec_Refused
    mov     edi, SCScratch
    add     edi, edx
    mov     eax, [esi+12]
    cld
    rep     stosd
    xor     eax, eax
    jmp     SC_Exec_Done
SC_Exec_Not_Fill:

    cmp     eax, SC_OP_VRAM_PATTERN
    jne     short SC_Exec_Not_Pattern
    mov     ecx, [esi+8]
    call    SC_Vram_Allowed
    jc      SC_Exec_Refused
    mov     edi, SCScratch
    add     edi, edx
    mov     eax, [esi+12]
SC_Exec_Pattern_Next:
    mov     [edi], eax
    add     eax, SC_PATTERN_STEP
    add     edi, 4
    dec     ecx
    jnz     short SC_Exec_Pattern_Next
    xor     eax, eax
    jmp     SC_Exec_Done
SC_Exec_Not_Pattern:

    cmp     eax, SC_OP_VRAM_BLOCK
    jne     short SC_Exec_Not_Block
    mov     ecx, [esi+8]
    call    SC_Vram_Allowed
    jc      SC_Exec_Refused
    mov     eax, SCResult[40]
    mov     ebx, eax
    add     ebx, ecx
    cmp     ebx, SC_BLOCK_DWORDS
    ja      SC_Exec_Refused
    mov     SCResult[40], ebx
    push    esi
    mov     esi, SCScratch
    add     esi, edx
    lea     edi, SCBlock[eax*4]
    cld
    rep     movsd
    pop     esi
    jmp     SC_Exec_Done
SC_Exec_Not_Block:

    cmp     eax, SC_OP_WAIT_VRAM
    jne     short SC_Exec_Not_Wait_Vram
    mov     ecx, 1
    call    SC_Vram_Allowed
    jc      SC_Exec_Refused
    mov     ebx, SCScratch
    add     ebx, edx
    mov     edx, [esi+8]
    mov     ecx, [esi+12]
    xor     eax, eax
SC_Exec_Wait_Vram_Next:
    cmp     [ebx], edx
    je      SC_Exec_Done
    inc     eax
    cmp     eax, ecx
    jb      short SC_Exec_Wait_Vram_Next
    or      SCResult[4], SC_ST_TIMEOUT
    mov     eax, SC_TIMEOUT
    jmp     SC_Exec_Done
SC_Exec_Not_Wait_Vram:

    cmp     eax, SC_OP_WAIT_MMIO
    jne     short SC_Exec_Not_Wait_Mmio
    call    SC_Read_Allowed
    jc      SC_Exec_Refused
    mov     ebx, SCMmio
    add     ebx, edx
    xor     eax, eax
SC_Exec_Wait_Mmio_Next:
    mov     ecx, [ebx]
    and     ecx, [esi+8]
    cmp     ecx, [esi+12]
    je      SC_Exec_Done
    inc     eax
    cmp     eax, SC_WAIT_MMIO_LIMIT
    jb      short SC_Exec_Wait_Mmio_Next
    or      SCResult[4], SC_ST_TIMEOUT
    mov     eax, SC_TIMEOUT
    jmp     SC_Exec_Done
SC_Exec_Not_Wait_Mmio:

    cmp     eax, SC_OP_DELAY
    jne     short SC_Exec_Not_Delay
    mov     ecx, edx
    test    ecx, ecx
    jz      SC_Exec_Refused
    cmp     ecx, SC_DELAY_MAX
    ja      SC_Exec_Refused
    mov     ebx, SCMmio
SC_Exec_Delay_Next:
    mov     eax, [ebx+SC_SRBM_STATUS]
    dec     ecx
    jnz     short SC_Exec_Delay_Next
    jmp     SC_Exec_Done
SC_Exec_Not_Delay:

    cmp     eax, SC_OP_SAVE
    jne     short SC_Exec_Not_Save
    push    esi
    mov     esi, SCScratch
    mov     edi, OFFSET32 SCSaved
    mov     ecx, SC_SCRATCH_BYTES / 4
    cld
    rep     movsd
    pop     esi
    or      SCResult[4], SC_ST_SAVED
    xor     eax, eax
    jmp     short SC_Exec_Done
SC_Exec_Not_Save:

    cmp     eax, SC_OP_RESTORE
    jne     short SC_Exec_Refused
    ; Only what was saved in this run goes back.
    test    SCResult[4], SC_ST_SAVED
    jz      short SC_Exec_Refused
    push    esi
    mov     esi, OFFSET32 SCSaved
    mov     edi, SCScratch
    mov     ecx, SC_SCRATCH_BYTES / 4
    cld
    rep     movsd
    pop     esi
    xor     eax, eax

SC_Exec_Done:
    pop     edi
    pop     edx
    pop     ecx
    pop     ebx
    clc
    ret
SC_Exec_Refused:
    pop     edi
    pop     edx
    pop     ecx
    pop     ebx
    stc
    ret
EndProc SC_Execute

; EBP = DIOC input buffer, ECX = its byte count. The ops come first, the
; data array after them; both counts must fit the buffer.
BeginProc SC_Run
    pushad
    mov     eax, [ebp+SC_IN_COUNT]
    cmp     eax, SC_OP_MAX
    ja      SC_Run_Refused
    mov     edx, [ebp+SC_IN_DATA_COUNT]
    cmp     edx, SC_DATA_MAX
    ja      SC_Run_Refused
    sub     ecx, SC_IN_OPS
    jb      SC_Run_Refused
    mov     ebx, eax
    shl     ebx, 4
    sub     ecx, ebx
    jb      SC_Run_Refused
    shr     ecx, 2
    cmp     edx, ecx
    ja      SC_Run_Refused
    lea     ebx, [ebp+SC_IN_OPS+ebx]
    mov     SCData, ebx
    mov     SCDataCount, edx
    call    SC_Map
    jc      short SC_Run_Done

    mov     ecx, [ebp+SC_IN_COUNT]
    lea     esi, [ebp+SC_IN_OPS]
    mov     edi, OFFSET32 SCOpResults
    xor     ebx, ebx
    jecxz   short SC_Run_Complete
SC_Run_Next:
    call    SC_Execute
    jc      short SC_Run_Refused_At
    mov     [edi], eax
    inc     ebx
    mov     SCResult[32], ebx
    add     esi, SC_OP_BYTES
    add     edi, 4
    dec     ecx
    jnz     short SC_Run_Next
SC_Run_Complete:
    or      SCResult[4], SC_ST_COMPLETE
    jmp     short SC_Run_Done
SC_Run_Refused_At:
    mov     SCResult[36], ebx
SC_Run_Refused:
    or      SCResult[4], SC_ST_REFUSED
SC_Run_Done:
    mov     SCData, 0
    mov     SCDataCount, 0
    popad
    ret
EndProc SC_Run

BeginProc SC_Reset_Result
    pushad
    mov     edi, OFFSET32 SCResult + 4
    mov     ecx, SC_RESULT_DWORDS - 1
    xor     eax, eax
    cld
    rep     stosd
    popad
    ret
EndProc SC_Reset_Result

BeginProc SC_W32_DeviceIoControl
    cmp     ecx, DIOC_OPEN
    je      SC_Dioc_Ok
    cmp     ecx, DIOC_CLOSEHANDLE
    je      SC_Dioc_Ok
    cmp     ecx, SC_DIOC_RUN
    jne     SC_Dioc_Fail

    pushad
    mov     ebx, esi
    mov     ebp, [ebx.lpvInBuffer]
    test    ebp, ebp
    jz      short SC_Dioc_Copy_Fail
    mov     ecx, [ebx.cbInBuffer]
    cmp     ecx, SC_IN_OPS
    jb      short SC_Dioc_Copy_Fail
    mov     edi, [ebx.lpvOutBuffer]
    test    edi, edi
    jz      short SC_Dioc_Copy_Fail
    cmp     [ebx.cbOutBuffer], SC_RESULT_DWORDS * 4
    jb      short SC_Dioc_Copy_Fail

    call    SC_Reset_Result
    mov     ecx, [ebx.cbInBuffer]
    call    SC_Run

    mov     edi, [ebx.lpvOutBuffer]
    mov     esi, OFFSET32 SCResult
    mov     ecx, SC_RESULT_DWORDS
    cld
    rep     movsd
    mov     eax, [ebx.lpcbBytesReturned]
    test    eax, eax
    jz      short SC_Dioc_Copy_Done
    mov     dword ptr [eax], SC_RESULT_DWORDS * 4
SC_Dioc_Copy_Done:
    popad
SC_Dioc_Ok:
    xor     eax, eax
    ret

SC_Dioc_Copy_Fail:
    popad
SC_Dioc_Fail:
    mov     eax, 1
    ret
EndProc SC_W32_DeviceIoControl

BeginProc SC_Dynamic_Init
    clc
    ret
EndProc SC_Dynamic_Init

BeginProc SC_Dynamic_Exit
    clc
    ret
EndProc SC_Dynamic_Exit

Begin_Control_Dispatch SC
    Control_Dispatch Sys_Dynamic_Device_Init, SC_Dynamic_Init
    Control_Dispatch Sys_Dynamic_Device_Exit, SC_Dynamic_Exit
    Control_Dispatch W32_DeviceIoControl, SC_W32_DeviceIoControl
End_Control_Dispatch SC

VxD_LOCKED_CODE_ENDS

end
