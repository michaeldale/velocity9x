; AMD Evergreen (Radeon HD 5450, Cedar) command processor bring-up probe VxD.
;
; Phase 1 of docs\plans\ati-evergreen-cedar-3d.md. Loaded dynamically by
; V9XEGCP.EXE, which decides every access; this VxD only executes an op
; list against the card and refuses anything outside it. EGDMA.VXD
; (evergreen_dma.asm) is the model; this one differs in its allow-lists,
; its window, and a stream op that feeds microcode to a data port.
;
; It finds the card with configuration mechanism 1 (1002:68F9 only), maps
; BAR2 (128 KiB of registers) and a 64 KiB scratch window of VRAM through
; BAR0, and runs:
;
;   MMIO read32    - SRBM and GRBM status, MC_VM_FB_LOCATION,
;                    CONFIG_MEMSIZE, the CP's status, ring and microcode
;                    registers, and the scratch registers.
;   MMIO write32   - the registers Linux evergreen_cp_load_microcode,
;                    evergreen_cp_resume and r600_ring_test write: the CP
;                    ring, CP_ME_CNTL, the microcode address ports, CP_DEBUG,
;                    the semaphore timers, SCRATCH_REG0/UMSK/ADDR,
;                    HDP_MEM_COHERENCY_FLUSH_CNTL, and GRBM_SOFT_RESET with
;                    only the CP, PA, SH, VGT, SPI and SX bits that
;                    evergreen_cp_resume pulses. Nothing else.
;   stream         - a run of the request's data area into
;                    CP_PFP_UCODE_DATA or CP_ME_RAM_DATA, the microcode
;                    ports. No other register takes a stream.
;   VRAM data      - a run of the data area into the scratch window: the
;                    ring, shaders and vertices V9XEG3D lays out.
;   (V9XEG3D adds Linux evergreen_gpu_init's configuration registers for
;   Cedar to both allow-lists; see the lists below.)
;   delay          - a count of SRBM_STATUS reads, at most EC_DELAY_MAX.
;   VRAM ops       - write, read, fill, pattern, read block, all at aligned
;                    offsets inside the scratch window.
;   wait           - a VRAM dword equal to a value, or an MMIO dword under a
;                    mask equal to a value, at most the op's limit reads.
;   save/restore   - the whole scratch window to and from a copy here.
;
; The scratch window is BAR0 + 0F100000h, 241 MiB in: past the 16 MiB heap
; and past the 64 KiB at 240 MiB that the driver's DMA ring now occupies
; (cedar_hw16.c), so this probe and a running DirectDraw never share VRAM.
; It is mapped only when BAR0 is a 64-bit memory BAR below 4 GiB, aligned
; to 256 MiB, on a card whose CONFIG_MEMSIZE says 256 MiB or more.
;
; Offsets: Linux radeon's evergreend.h and r600d.h
; (build\external\evergreen\linux-radeon\).

.386p

.xlist
include VMM.INC
include VWIN32.INC
.list

Declare_Virtual_Device EGCP, 1, 0, EC_Control, \
                       Undefined_Device_ID, Undefined_Init_Order, , ,

EC_MAGIC                equ 50434745h ; "EGCP"
EC_DIOC_RUN             equ 1
EC_VENDOR_ATI           equ 1002h
EC_DEVICE_CEDAR         equ 68f9h
EC_OP_MAX               equ 512
EC_BLOCK_DWORDS         equ 4096
EC_DATA_DWORDS          equ 4096
EC_TIMEOUT              equ 0ffffffffh

EC_CFG_BAR0             equ 10h
EC_CFG_BAR0_HIGH        equ 14h
EC_CFG_BAR2             equ 18h
EC_CFG_BAR2_HIGH        equ 1ch
EC_BAR_IO               equ 00000001h
EC_BAR_TYPE_MASK        equ 00000006h
EC_BAR_TYPE_64          equ 00000004h
EC_MMIO_BYTES           equ 00020000h
EC_MMIO_ALIGN           equ 0001ffffh
EC_LFB_ALIGN            equ 0fffffffh
EC_SCRATCH_OFFSET       equ 0f100000h
EC_SCRATCH_BYTES        equ 00010000h
EC_CONFIG_MEMSIZE       equ 5428h
EC_MEMSIZE_MIN_MIB      equ 256
EC_MEMSIZE_MAX_MIB      equ 4096

; Op codes; the layout is four dwords: op, a, b, c.
EC_OP_MMIO_READ         equ 1   ; a offset
EC_OP_MMIO_WRITE        equ 2   ; a offset, b value
EC_OP_VRAM_WRITE        equ 3   ; a offset, b value
EC_OP_VRAM_READ         equ 4   ; a offset
EC_OP_VRAM_FILL         equ 5   ; a offset, b dwords, c value
EC_OP_VRAM_BLOCK        equ 6   ; a offset, b dwords -> block area
EC_OP_WAIT_VRAM         equ 7   ; a offset, b value, c limit
EC_OP_WAIT_MMIO         equ 8   ; a offset, b mask, c value; limit fixed
EC_OP_SAVE              equ 9
EC_OP_RESTORE           equ 10
EC_OP_VRAM_PATTERN      equ 11  ; a offset, b dwords, c seed
EC_OP_DELAY             equ 12  ; a SRBM_STATUS reads
EC_OP_STREAM            equ 13  ; a port, b first data dword, c dwords
EC_OP_VRAM_DATA         equ 14  ; a window offset, b first data dword, c dwords
EC_DELAY_MAX            equ 100000
EC_SRBM_STATUS          equ 0e50h
EC_GRBM_SOFT_RESET      equ 8020h
; CP | PA | SH | VGT | SPI | SX, evergreen_cp_resume's pulse.
EC_GRBM_RESET_ALLOWED   equ 00004721h
EC_CP_PFP_UCODE_DATA    equ 0c154h
EC_CP_ME_RAM_DATA       equ 0c160h
EC_OP_BYTES             equ 16
EC_WAIT_MMIO_LIMIT      equ 2000000
EC_PATTERN_STEP         equ 9e3779b9h

; Status bits.
EC_ST_FOUND             equ 00000001h
EC_ST_MAPPED            equ 00000002h
EC_ST_IDENTITY          equ 00000004h
EC_ST_REFUSED           equ 00000008h
EC_ST_COMPLETE          equ 00000010h
EC_ST_BAD_BAR           equ 00000020h
EC_ST_TIMEOUT           equ 00000040h
EC_ST_SAVED             equ 00000080h

; Input: count, then EC_OP_MAX ops, then a data count and the data area.
EC_IN_COUNT             equ 0
EC_IN_OPS               equ 4
EC_IN_DATA_COUNT        equ (EC_IN_OPS + EC_OP_MAX * EC_OP_BYTES)
EC_IN_DATA              equ (EC_IN_DATA_COUNT + 4)
EC_IN_BYTES             equ (EC_IN_DATA + EC_DATA_DWORDS * 4)

; Output layout, in dwords. Must match struct v9x_egcp_result in
; evergreen_cp_win32.c.
EC_HEADER_DWORDS        equ 16
EC_RESULT_DWORDS        equ (EC_HEADER_DWORDS + EC_OP_MAX + EC_BLOCK_DWORDS)

VxD_LOCKED_DATA_SEG

; Reads accepted: start, limit (exclusive).
ECReadWindows label dword
    dd 0e50h, 0e54h     ; SRBM_STATUS
    dd 0e60h, 0e64h     ; SRBM_SOFT_RESET
    dd 0ec4h, 0ec8h     ; SRBM_STATUS2
    dd 2024h, 2028h     ; MC_VM_FB_LOCATION
    dd 5428h, 542ch     ; CONFIG_MEMSIZE
    dd 8010h, 8018h     ; GRBM_STATUS, GRBM_STATUS_SE0
    dd 8020h, 8024h     ; GRBM_SOFT_RESET
    dd 8500h, 8520h     ; SCRATCH_REG0-7
    dd 8540h, 8548h     ; SCRATCH_UMSK, SCRATCH_ADDR
    dd 85bch, 85c0h     ; CP_SEM_WAIT_TIMER
    dd 85c8h, 85cch     ; CP_SEM_INCOMPLETE_TIMER_CNTL
    dd 8680h, 8684h     ; CP_STAT
    dd 86d8h, 86dch     ; CP_ME_CNTL
    dd 8700h, 8708h     ; CP_RB_RPTR, CP_RB_WPTR_DELAY
    dd 0c100h, 0c118h   ; CP_RB_BASE .. CP_RB_WPTR
    dd 0c150h, 0c154h   ; CP_PFP_UCODE_ADDR
    dd 0c158h, 0c160h   ; CP_ME_RAM_RADDR, CP_ME_RAM_WADDR
    dd 0c1fch, 0c200h   ; CP_DEBUG
    ; evergreen_gpu_init's registers (V9XEG3D), and what it reads.
    dd 2f48h, 2f50h     ; HDP_ADDR_CONFIG, HDP_MISC_CNTL
    dd 3f90h, 3f98h     ; CGTS_SYS_TCC_DISABLE, CGTS_USER_SYS_TCC_DISABLE
    dd 8000h, 8004h     ; GRBM_CNTL
    dd 8760h, 8768h     ; CP_QUEUE_THRESHOLDS, CP_MEQ_THRESHOLDS
    dd 87fch, 8800h     ; CP_PERFMON_CNTL
    dd 88c4h, 88c8h     ; VGT_CACHE_INVALIDATION
    dd 88d4h, 88d8h     ; VGT_GS_VERTEX_REUSE
    dd 8950h, 8954h     ; CC_GC_SHADER_PIPE_CONFIG
    dd 8958h, 895ch     ; VGT_PRIMITIVE_TYPE
    dd 8974h, 8978h     ; VGT_NUM_INSTANCES
    dd 8a14h, 8a18h     ; PA_CL_ENHANCE
    dd 8a60h, 8a64h     ; PA_SU_LINE_STIPPLE_VALUE
    dd 8b10h, 8b14h     ; PA_SC_LINE_STIPPLE_STATE
    dd 8b24h, 8b28h     ; PA_SC_FORCE_EOV_MAX_CNTS
    dd 8bcch, 8bd0h     ; PA_SC_FIFO_SIZE
    dd 8c00h, 8c10h     ; SQ_CONFIG, SQ_GPR_RESOURCE_MGMT_1-3
    dd 8c18h, 8c2ch     ; SQ_THREAD_RESOURCE_MGMT(_2), SQ_STACK_RESOURCE_MGMT_1-3
    dd 8cf0h, 8cf4h     ; SQ_MS_FIFO_SIZES
    dd 8d8ch, 8d90h     ; SQ_DYN_GPR_CNTL_PS_FLUSH_REQ
    dd 8e2ch, 8e30h     ; SQ_LDS_RESOURCE_MGMT
    dd 900ch, 9010h     ; SX_EXPORT_BUFFER_SIZES
    dd 9058h, 905ch     ; SX_DEBUG_1
    dd 9100h, 9104h     ; SPI_CONFIG_CNTL
    dd 913ch, 9140h     ; SPI_CONFIG_CNTL_1
    dd 9148h, 9150h     ; CGTS_TCC_DISABLE, CGTS_USER_TCC_DISABLE
    dd 9508h, 950ch     ; TA_CNTL_AUX
    dd 98f4h, 9900h     ; CC_RB_BACKEND_DISABLE, GB_ADDR_CONFIG, GB_BACKEND_MAP
    dd 9a20h, 9a40h     ; CB_PERF_CTR0-3_SEL_0/1
    dd 0a008h, 0a00ch   ; SMX_SAR_CTL0
    dd 0a020h, 0a024h   ; SMX_DC_CTL0
EC_READ_WINDOW_COUNT    equ 48

; Writes accepted: exact offsets. GRBM_SOFT_RESET's value is checked in
; EC_Execute.
ECWriteOffsets label dword
    dd 5480h            ; HDP_MEM_COHERENCY_FLUSH_CNTL
    dd 8020h            ; GRBM_SOFT_RESET
    dd 8500h            ; SCRATCH_REG0
    dd 8540h            ; SCRATCH_UMSK
    dd 8544h            ; SCRATCH_ADDR
    dd 85bch            ; CP_SEM_WAIT_TIMER
    dd 85c8h            ; CP_SEM_INCOMPLETE_TIMER_CNTL
    dd 86d8h            ; CP_ME_CNTL
    dd 8704h            ; CP_RB_WPTR_DELAY
    dd 0c100h           ; CP_RB_BASE
    dd 0c104h           ; CP_RB_CNTL
    dd 0c108h           ; CP_RB_RPTR_WR
    dd 0c10ch           ; CP_RB_RPTR_ADDR
    dd 0c110h           ; CP_RB_RPTR_ADDR_HI
    dd 0c114h           ; CP_RB_WPTR
    dd 0c150h           ; CP_PFP_UCODE_ADDR
    dd 0c158h           ; CP_ME_RAM_RADDR
    dd 0c15ch           ; CP_ME_RAM_WADDR
    dd 0c1fch           ; CP_DEBUG
    ; evergreen_gpu_init's writes for Cedar (V9XEG3D). The context
    ; registers it also writes (28000h and up) are left to CLEAR_STATE and
    ; packets, and the address-config copies that the display, host path
    ; and DMA engine use (DMIF_, HDP_, DMA_TILING_CONFIG) are not taken.
    dd 2f4ch            ; HDP_MISC_CNTL
    dd 3f90h            ; CGTS_SYS_TCC_DISABLE
    dd 3f94h            ; CGTS_USER_SYS_TCC_DISABLE
    dd 8000h            ; GRBM_CNTL
    dd 8760h            ; CP_QUEUE_THRESHOLDS
    dd 8764h            ; CP_MEQ_THRESHOLDS
    dd 87fch            ; CP_PERFMON_CNTL
    dd 88c4h            ; VGT_CACHE_INVALIDATION
    dd 88d4h            ; VGT_GS_VERTEX_REUSE
    dd 8974h            ; VGT_NUM_INSTANCES
    dd 8a14h            ; PA_CL_ENHANCE
    dd 8a60h            ; PA_SU_LINE_STIPPLE_VALUE
    dd 8b10h            ; PA_SC_LINE_STIPPLE_STATE
    dd 8b24h            ; PA_SC_FORCE_EOV_MAX_CNTS
    dd 8bcch            ; PA_SC_FIFO_SIZE
    dd 8c00h            ; SQ_CONFIG
    dd 8c04h            ; SQ_GPR_RESOURCE_MGMT_1
    dd 8c08h            ; SQ_GPR_RESOURCE_MGMT_2
    dd 8c0ch            ; SQ_GPR_RESOURCE_MGMT_3
    dd 8c18h            ; SQ_THREAD_RESOURCE_MGMT
    dd 8c1ch            ; SQ_THREAD_RESOURCE_MGMT_2
    dd 8c20h            ; SQ_STACK_RESOURCE_MGMT_1
    dd 8c24h            ; SQ_STACK_RESOURCE_MGMT_2
    dd 8c28h            ; SQ_STACK_RESOURCE_MGMT_3
    dd 8cf0h            ; SQ_MS_FIFO_SIZES
    dd 8d8ch            ; SQ_DYN_GPR_CNTL_PS_FLUSH_REQ
    dd 900ch            ; SX_EXPORT_BUFFER_SIZES
    dd 9058h            ; SX_DEBUG_1
    dd 9100h            ; SPI_CONFIG_CNTL
    dd 913ch            ; SPI_CONFIG_CNTL_1
    dd 9148h            ; CGTS_TCC_DISABLE
    dd 914ch            ; CGTS_USER_TCC_DISABLE
    dd 9508h            ; TA_CNTL_AUX
    dd 98f8h            ; GB_ADDR_CONFIG
    dd 98fch            ; GB_BACKEND_MAP
    dd 9a20h            ; CB_PERF_CTR0_SEL_0
    dd 9a24h            ; CB_PERF_CTR0_SEL_1
    dd 9a28h            ; CB_PERF_CTR1_SEL_0
    dd 9a2ch            ; CB_PERF_CTR1_SEL_1
    dd 9a30h            ; CB_PERF_CTR2_SEL_0
    dd 9a34h            ; CB_PERF_CTR2_SEL_1
    dd 9a38h            ; CB_PERF_CTR3_SEL_0
    dd 9a3ch            ; CB_PERF_CTR3_SEL_1
    dd 0a008h           ; SMX_SAR_CTL0
    dd 0a020h           ; SMX_DC_CTL0
    ; Config registers the CP writes from V9XEG3D's ring, put back by MMIO
    ; afterwards.
    dd 8958h            ; VGT_PRIMITIVE_TYPE
    dd 8e2ch            ; SQ_LDS_RESOURCE_MGMT
EC_WRITE_OFFSET_COUNT   equ 66

ECResult label dword
    dd EC_MAGIC                 ; 00 magic
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
ECOpResults  dd EC_OP_MAX dup (0)
ECBlock      dd EC_BLOCK_DWORDS dup (0)
ECSaved      dd (EC_SCRATCH_BYTES / 4) dup (0)
ECMmio       dd 0
ECScratch    dd 0
ECConfig     dd 0
; The request's data area and its count, for the stream op.
ECData       dd 0
ECDataCount  dd 0
VxD_LOCKED_DATA_ENDS

VxD_LOCKED_CODE_SEG

; EAX = PCI configuration address. Returns EAX = configuration dword.
; 0CF8h saved and restored with interrupts off.
BeginProc EC_Pci_Read
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
EndProc EC_Pci_Read

; Scans buses 0-15, device slots 0-31, function 0, for 1002:68F9.
; Returns CF clear and EAX = configuration address, or CF set.
BeginProc EC_Find
    push    ebx
    push    ecx
    mov     ebx, 80000000h
EC_Find_Next:
    mov     eax, ebx
    call    EC_Pci_Read
    cmp     eax, (EC_DEVICE_CEDAR shl 16) or EC_VENDOR_ATI
    je      short EC_Find_Found
    add     ebx, 800h
    cmp     ebx, 80100000h
    jb      short EC_Find_Next
    pop     ecx
    pop     ebx
    stc
    ret
EC_Find_Found:
    mov     ECResult[8], eax
    mov     eax, ebx
    pop     ecx
    pop     ebx
    clc
    ret
EndProc EC_Find

; Finds and maps the card once. Returns CF clear when both windows are
; mapped and the identity check passed.
BeginProc EC_Map
    pushad
    cmp     ECScratch, 0
    jne     EC_Map_Ok

    call    EC_Find
    jc      EC_Map_Fail
    mov     ECConfig, eax
    mov     ECResult[44], eax
    or      ECResult[4], EC_ST_FOUND

    ; BAR2: memory, 64-bit, high half zero, 128 KiB aligned.
    mov     eax, ECConfig
    or      eax, EC_CFG_BAR2
    call    EC_Pci_Read
    mov     ECResult[20], eax
    mov     esi, eax
    mov     eax, ECConfig
    or      eax, EC_CFG_BAR2_HIGH
    call    EC_Pci_Read
    mov     ECResult[24], eax
    test    esi, EC_BAR_IO
    jnz     EC_Map_Bad_Bar
    mov     edx, esi
    and     edx, EC_BAR_TYPE_MASK
    cmp     edx, EC_BAR_TYPE_64
    jne     EC_Map_Bad_Bar
    test    eax, eax
    jnz     EC_Map_Bad_Bar
    and     esi, NOT 0fh
    jz      EC_Map_Bad_Bar
    test    esi, EC_MMIO_ALIGN
    jnz     EC_Map_Bad_Bar

    ; BAR0: memory, 64-bit, high half zero, 256 MiB aligned.
    mov     eax, ECConfig
    or      eax, EC_CFG_BAR0
    call    EC_Pci_Read
    mov     ECResult[12], eax
    mov     edi, eax
    mov     eax, ECConfig
    or      eax, EC_CFG_BAR0_HIGH
    call    EC_Pci_Read
    mov     ECResult[16], eax
    test    edi, EC_BAR_IO
    jnz     EC_Map_Bad_Bar
    mov     edx, edi
    and     edx, EC_BAR_TYPE_MASK
    cmp     edx, EC_BAR_TYPE_64
    jne     EC_Map_Bad_Bar
    test    eax, eax
    jnz     EC_Map_Bad_Bar
    and     edi, NOT 0fh
    jz      EC_Map_Bad_Bar
    test    edi, EC_LFB_ALIGN
    jnz     EC_Map_Bad_Bar

    VMMcall _MapPhysToLinear,<esi,EC_MMIO_BYTES,0>
    cmp     eax, 0ffffffffh
    je      EC_Map_Fail
    mov     ECMmio, eax

    ; The identity check before the scratch window is mapped: at least
    ; 256 MiB of VRAM, so BAR0 + 240 MiB is VRAM.
    mov     edx, [eax+EC_CONFIG_MEMSIZE]
    mov     ECResult[28], edx
    cmp     edx, EC_MEMSIZE_MIN_MIB
    jb      EC_Map_Fail
    cmp     edx, EC_MEMSIZE_MAX_MIB
    ja      EC_Map_Fail
    or      ECResult[4], EC_ST_IDENTITY

    add     edi, EC_SCRATCH_OFFSET
    VMMcall _MapPhysToLinear,<edi,EC_SCRATCH_BYTES,0>
    cmp     eax, 0ffffffffh
    je      EC_Map_Fail
    mov     ECScratch, eax
    or      ECResult[4], EC_ST_MAPPED

EC_Map_Ok:
    popad
    clc
    ret
EC_Map_Bad_Bar:
    or      ECResult[4], EC_ST_BAD_BAR
EC_Map_Fail:
    popad
    stc
    ret
EndProc EC_Map

; EDX = offset. CF clear if readable.
BeginProc EC_Read_Allowed
    push    ecx
    push    esi
    test    edx, 3
    jnz     short EC_Read_Refused
    mov     esi, OFFSET32 ECReadWindows
    mov     ecx, EC_READ_WINDOW_COUNT
EC_Read_Next:
    cmp     edx, [esi]
    jb      short EC_Read_Skip
    cmp     edx, [esi+4]
    jb      short EC_Read_Accepted
EC_Read_Skip:
    add     esi, 8
    dec     ecx
    jnz     short EC_Read_Next
EC_Read_Refused:
    pop     esi
    pop     ecx
    stc
    ret
EC_Read_Accepted:
    pop     esi
    pop     ecx
    clc
    ret
EndProc EC_Read_Allowed

; EDX = offset. CF clear if writable.
BeginProc EC_Write_Allowed
    push    ecx
    push    esi
    mov     esi, OFFSET32 ECWriteOffsets
    mov     ecx, EC_WRITE_OFFSET_COUNT
EC_Write_Next:
    cmp     edx, [esi]
    je      short EC_Write_Accepted
    add     esi, 4
    dec     ecx
    jnz     short EC_Write_Next
    pop     esi
    pop     ecx
    stc
    ret
EC_Write_Accepted:
    pop     esi
    pop     ecx
    clc
    ret
EndProc EC_Write_Allowed

; EDX = scratch offset, ECX = dwords (at least 1). CF clear if the run lies
; inside the window and is aligned.
BeginProc EC_Vram_Allowed
    push    eax
    test    edx, 3
    jnz     short EC_Vram_Refused
    test    ecx, ecx
    jz      short EC_Vram_Refused
    cmp     ecx, EC_SCRATCH_BYTES / 4
    ja      short EC_Vram_Refused
    cmp     edx, EC_SCRATCH_BYTES
    jae     short EC_Vram_Refused
    mov     eax, ecx
    shl     eax, 2
    add     eax, edx
    cmp     eax, EC_SCRATCH_BYTES
    ja      short EC_Vram_Refused
    pop     eax
    clc
    ret
EC_Vram_Refused:
    pop     eax
    stc
    ret
EndProc EC_Vram_Allowed

; ESI = op (four dwords). Returns EAX = the op's result, CF set if refused.
BeginProc EC_Execute
    push    ebx
    push    ecx
    push    edx
    push    edi
    mov     eax, [esi]
    mov     edx, [esi+4]

    cmp     eax, EC_OP_MMIO_READ
    jne     short EC_Exec_Not_Read
    call    EC_Read_Allowed
    jc      EC_Exec_Refused
    mov     ebx, ECMmio
    mov     eax, [ebx+edx]
    jmp     EC_Exec_Done
EC_Exec_Not_Read:

    cmp     eax, EC_OP_MMIO_WRITE
    jne     short EC_Exec_Not_Write
    call    EC_Write_Allowed
    jc      EC_Exec_Refused
    mov     eax, [esi+8]
    ; GRBM_SOFT_RESET takes only the blocks evergreen_cp_resume resets:
    ; nothing outside the graphics front end is ever reset from here.
    cmp     edx, EC_GRBM_SOFT_RESET
    jne     short EC_Exec_Write_Value_Ok
    test    eax, NOT EC_GRBM_RESET_ALLOWED
    jnz     EC_Exec_Refused
EC_Exec_Write_Value_Ok:
    mov     ebx, ECMmio
    mov     [ebx+edx], eax
    ; Post the write before the next op.
    mov     eax, [ebx+EC_CONFIG_MEMSIZE]
    xor     eax, eax
    jmp     EC_Exec_Done
EC_Exec_Not_Write:

    cmp     eax, EC_OP_VRAM_WRITE
    jne     short EC_Exec_Not_Vram_Write
    mov     ecx, 1
    call    EC_Vram_Allowed
    jc      EC_Exec_Refused
    mov     ebx, ECScratch
    mov     eax, [esi+8]
    mov     [ebx+edx], eax
    xor     eax, eax
    jmp     EC_Exec_Done
EC_Exec_Not_Vram_Write:

    cmp     eax, EC_OP_VRAM_READ
    jne     short EC_Exec_Not_Vram_Read
    mov     ecx, 1
    call    EC_Vram_Allowed
    jc      EC_Exec_Refused
    mov     ebx, ECScratch
    mov     eax, [ebx+edx]
    jmp     EC_Exec_Done
EC_Exec_Not_Vram_Read:

    cmp     eax, EC_OP_VRAM_FILL
    jne     short EC_Exec_Not_Fill
    mov     ecx, [esi+8]
    call    EC_Vram_Allowed
    jc      EC_Exec_Refused
    mov     edi, ECScratch
    add     edi, edx
    mov     eax, [esi+12]
    cld
    rep     stosd
    xor     eax, eax
    jmp     EC_Exec_Done
EC_Exec_Not_Fill:

    cmp     eax, EC_OP_VRAM_PATTERN
    jne     short EC_Exec_Not_Pattern
    mov     ecx, [esi+8]
    call    EC_Vram_Allowed
    jc      EC_Exec_Refused
    mov     edi, ECScratch
    add     edi, edx
    mov     eax, [esi+12]
EC_Exec_Pattern_Next:
    mov     [edi], eax
    add     eax, EC_PATTERN_STEP
    add     edi, 4
    dec     ecx
    jnz     short EC_Exec_Pattern_Next
    xor     eax, eax
    jmp     EC_Exec_Done
EC_Exec_Not_Pattern:

    cmp     eax, EC_OP_VRAM_BLOCK
    jne     short EC_Exec_Not_Block
    mov     ecx, [esi+8]
    call    EC_Vram_Allowed
    jc      EC_Exec_Refused
    mov     eax, ECResult[40]
    mov     ebx, eax
    add     ebx, ecx
    cmp     ebx, EC_BLOCK_DWORDS
    ja      EC_Exec_Refused
    mov     ECResult[40], ebx
    push    esi
    mov     esi, ECScratch
    add     esi, edx
    lea     edi, ECBlock[eax*4]
    cld
    rep     movsd
    pop     esi
    jmp     EC_Exec_Done
EC_Exec_Not_Block:

    cmp     eax, EC_OP_WAIT_VRAM
    jne     short EC_Exec_Not_Wait_Vram
    mov     ecx, 1
    call    EC_Vram_Allowed
    jc      EC_Exec_Refused
    mov     ebx, ECScratch
    add     ebx, edx
    mov     edx, [esi+8]
    mov     ecx, [esi+12]
    xor     eax, eax
EC_Exec_Wait_Vram_Next:
    cmp     [ebx], edx
    je      EC_Exec_Done
    inc     eax
    cmp     eax, ecx
    jb      short EC_Exec_Wait_Vram_Next
    or      ECResult[4], EC_ST_TIMEOUT
    mov     eax, EC_TIMEOUT
    jmp     EC_Exec_Done
EC_Exec_Not_Wait_Vram:

    cmp     eax, EC_OP_WAIT_MMIO
    jne     short EC_Exec_Not_Wait_Mmio
    call    EC_Read_Allowed
    jc      EC_Exec_Refused
    mov     ebx, ECMmio
    add     ebx, edx
    xor     eax, eax
EC_Exec_Wait_Mmio_Next:
    mov     ecx, [ebx]
    and     ecx, [esi+8]
    cmp     ecx, [esi+12]
    je      EC_Exec_Done
    inc     eax
    cmp     eax, EC_WAIT_MMIO_LIMIT
    jb      short EC_Exec_Wait_Mmio_Next
    or      ECResult[4], EC_ST_TIMEOUT
    mov     eax, EC_TIMEOUT
    jmp     EC_Exec_Done
EC_Exec_Not_Wait_Mmio:

    cmp     eax, EC_OP_DELAY
    jne     short EC_Exec_Not_Delay
    mov     ecx, edx
    test    ecx, ecx
    jz      EC_Exec_Refused
    cmp     ecx, EC_DELAY_MAX
    ja      EC_Exec_Refused
    mov     ebx, ECMmio
EC_Exec_Delay_Next:
    mov     eax, [ebx+EC_SRBM_STATUS]
    dec     ecx
    jnz     short EC_Exec_Delay_Next
    jmp     EC_Exec_Done
EC_Exec_Not_Delay:

    ; A run of the data area into one microcode port: a = the port, b =
    ; the first data dword, c = the count.
    cmp     eax, EC_OP_STREAM
    jne     short EC_Exec_Not_Stream
    cmp     edx, EC_CP_PFP_UCODE_DATA
    je      short EC_Exec_Stream_Port_Ok
    cmp     edx, EC_CP_ME_RAM_DATA
    jne     EC_Exec_Refused
EC_Exec_Stream_Port_Ok:
    mov     ebx, [esi+8]
    mov     ecx, [esi+12]
    test    ecx, ecx
    jz      EC_Exec_Refused
    cmp     ebx, ECDataCount
    jae     EC_Exec_Refused
    mov     eax, ECDataCount
    sub     eax, ebx
    cmp     ecx, eax
    ja      EC_Exec_Refused
    push    esi
    mov     esi, ECData
    lea     esi, [esi+ebx*4]
    mov     ebx, ECMmio
    add     ebx, edx
    cld
EC_Exec_Stream_Next:
    lodsd
    mov     [ebx], eax
    dec     ecx
    jnz     short EC_Exec_Stream_Next
    pop     esi
    xor     eax, eax
    jmp     EC_Exec_Done
EC_Exec_Not_Stream:

    ; A run of the data area into the scratch window: a = the window
    ; offset, b = the first data dword, c = the count.
    cmp     eax, EC_OP_VRAM_DATA
    jne     short EC_Exec_Not_Vram_Data
    mov     ecx, [esi+12]
    call    EC_Vram_Allowed
    jc      EC_Exec_Refused
    mov     ebx, [esi+8]
    cmp     ebx, ECDataCount
    jae     EC_Exec_Refused
    mov     eax, ECDataCount
    sub     eax, ebx
    cmp     ecx, eax
    ja      EC_Exec_Refused
    push    esi
    mov     esi, ECData
    lea     esi, [esi+ebx*4]
    mov     edi, ECScratch
    add     edi, edx
    cld
    rep     movsd
    pop     esi
    xor     eax, eax
    jmp     EC_Exec_Done
EC_Exec_Not_Vram_Data:

    cmp     eax, EC_OP_SAVE
    jne     short EC_Exec_Not_Save
    push    esi
    mov     esi, ECScratch
    mov     edi, OFFSET32 ECSaved
    mov     ecx, EC_SCRATCH_BYTES / 4
    cld
    rep     movsd
    pop     esi
    or      ECResult[4], EC_ST_SAVED
    xor     eax, eax
    jmp     short EC_Exec_Done
EC_Exec_Not_Save:

    cmp     eax, EC_OP_RESTORE
    jne     short EC_Exec_Refused
    ; Only what was saved in this run goes back.
    test    ECResult[4], EC_ST_SAVED
    jz      short EC_Exec_Refused
    push    esi
    mov     esi, OFFSET32 ECSaved
    mov     edi, ECScratch
    mov     ecx, EC_SCRATCH_BYTES / 4
    cld
    rep     movsd
    pop     esi
    xor     eax, eax

EC_Exec_Done:
    pop     edi
    pop     edx
    pop     ecx
    pop     ebx
    clc
    ret
EC_Exec_Refused:
    pop     edi
    pop     edx
    pop     ecx
    pop     ebx
    stc
    ret
EndProc EC_Execute

; EBP = DIOC input buffer, ECX = its byte count.
BeginProc EC_Run
    pushad
    ; The request is always the whole layout: the op slots, then the data
    ; count and area.
    cmp     ecx, EC_IN_BYTES
    jb      short EC_Run_Refused
    mov     eax, [ebp+EC_IN_COUNT]
    cmp     eax, EC_OP_MAX
    ja      short EC_Run_Refused
    mov     eax, [ebp+EC_IN_DATA_COUNT]
    cmp     eax, EC_DATA_DWORDS
    ja      short EC_Run_Refused
    mov     ECDataCount, eax
    lea     eax, [ebp+EC_IN_DATA]
    mov     ECData, eax
    call    EC_Map
    jc      short EC_Run_Done

    mov     ecx, [ebp+EC_IN_COUNT]
    lea     esi, [ebp+EC_IN_OPS]
    mov     edi, OFFSET32 ECOpResults
    xor     ebx, ebx
    jecxz   short EC_Run_Complete
EC_Run_Next:
    call    EC_Execute
    jc      short EC_Run_Refused_At
    mov     [edi], eax
    inc     ebx
    mov     ECResult[32], ebx
    add     esi, EC_OP_BYTES
    add     edi, 4
    dec     ecx
    jnz     short EC_Run_Next
EC_Run_Complete:
    or      ECResult[4], EC_ST_COMPLETE
    jmp     short EC_Run_Done
EC_Run_Refused_At:
    mov     ECResult[36], ebx
EC_Run_Refused:
    or      ECResult[4], EC_ST_REFUSED
EC_Run_Done:
    popad
    ret
EndProc EC_Run

BeginProc EC_Reset_Result
    pushad
    mov     edi, OFFSET32 ECResult + 4
    mov     ecx, EC_RESULT_DWORDS - 1
    xor     eax, eax
    cld
    rep     stosd
    popad
    ret
EndProc EC_Reset_Result

BeginProc EC_W32_DeviceIoControl
    cmp     ecx, DIOC_OPEN
    je      EC_Dioc_Ok
    cmp     ecx, DIOC_CLOSEHANDLE
    je      EC_Dioc_Ok
    cmp     ecx, EC_DIOC_RUN
    jne     EC_Dioc_Fail

    pushad
    mov     ebx, esi
    mov     ebp, [ebx.lpvInBuffer]
    test    ebp, ebp
    jz      short EC_Dioc_Copy_Fail
    mov     ecx, [ebx.cbInBuffer]
    cmp     ecx, EC_IN_OPS
    jb      short EC_Dioc_Copy_Fail
    mov     edi, [ebx.lpvOutBuffer]
    test    edi, edi
    jz      short EC_Dioc_Copy_Fail
    cmp     [ebx.cbOutBuffer], EC_RESULT_DWORDS * 4
    jb      short EC_Dioc_Copy_Fail

    call    EC_Reset_Result
    mov     ecx, [ebx.cbInBuffer]
    call    EC_Run

    mov     edi, [ebx.lpvOutBuffer]
    mov     esi, OFFSET32 ECResult
    mov     ecx, EC_RESULT_DWORDS
    cld
    rep     movsd
    mov     eax, [ebx.lpcbBytesReturned]
    test    eax, eax
    jz      short EC_Dioc_Copy_Done
    mov     dword ptr [eax], EC_RESULT_DWORDS * 4
EC_Dioc_Copy_Done:
    popad
EC_Dioc_Ok:
    xor     eax, eax
    ret

EC_Dioc_Copy_Fail:
    popad
EC_Dioc_Fail:
    mov     eax, 1
    ret
EndProc EC_W32_DeviceIoControl

BeginProc EC_Dynamic_Init
    clc
    ret
EndProc EC_Dynamic_Init

BeginProc EC_Dynamic_Exit
    clc
    ret
EndProc EC_Dynamic_Exit

Begin_Control_Dispatch EC
    Control_Dispatch Sys_Dynamic_Device_Init, EC_Dynamic_Init
    Control_Dispatch Sys_Dynamic_Device_Exit, EC_Dynamic_Exit
    Control_Dispatch W32_DeviceIoControl, EC_W32_DeviceIoControl
End_Control_Dispatch EC

VxD_LOCKED_CODE_ENDS

end
