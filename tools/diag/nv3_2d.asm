; NVIDIA RIVA 128 (NV3) 2D engine write probe VxD.
;
; Phase 3 of docs\plans\nvidia-riva128.md. Loaded dynamically by
; V9XN32D.EXE, which decides every access; this VxD only executes an op
; list against the card and refuses anything outside it. The Evergreen DMA
; probe (evergreen_dma.asm) is the model.
;
; It finds PCI 12D2:0018 with configuration mechanism 1, maps BAR0 (16 MiB
; of registers), the 32 KiB RAMIN window at BAR1 + C00000h and a 64 KiB
; scratch window of video memory at BAR1 + 200000h, and runs:
;
;   MMIO read32    - the units N3MM.VXD reads (nv3_mmio_probe.asm), which
;                    are safe once PMC_ENABLE has the engine on.
;   MMIO write32   - exactly the registers Linux rivafb's LoadStateExt
;                    writes on NV3 before it draws (riva_hw.c and
;                    riva_tbl.h: RivaTablePMC, RivaTablePTIMER, PFB
;                    config, nv3TablePFIFO, nv3TablePGRAPH and its per-depth
;                    SURF_CONFIG, the surface offsets and pitches), and
;                    channel 0's object methods in the USER area: method 0
;                    (bind) and 300h-31Ch and 3FCh-404h of each subchannel,
;                    the methods rivafb's ROP, clip, pattern, blit and GDI
;                    rectangle use. Nothing else.
;   RAMIN ops      - write, read and fill inside the RAMIN window, which is
;                    the top 32 KiB of video memory (docs\decisions\
;                    2026-11-02-riva128-register-probe.md).
;   VRAM ops       - write, read, fill, pattern, read block inside the
;                    scratch window.
;   wait           - an MMIO dword under a mask equal to a value; or the
;                    engine idle as rivafb's nv3Busy defines it (PGRAPH
;                    STATUS bit 0 clear and the free count back to the value
;                    LATCH took), at most N2_WAIT_LIMIT reads.
;   save/restore   - the scratch window to and from a copy here.
;
; The scratch window is video memory 2 MiB in: below the 4 MiB the PGRAPH
; surface offset can name (nv3_pgraph.xml's 003FFFF0h mask) and above an
; 800x600 desktop at any depth (1.92 MB at 32 bpp). The tool runs at
; 800x600 only.

.386p

.xlist
include VMM.INC
include VWIN32.INC
.list

Declare_Virtual_Device N32D, 1, 0, N2_Control, \
                       Undefined_Device_ID, Undefined_Init_Order, , ,

N2_MAGIC                equ 4432334eh ; "N32D"
N2_DIOC_RUN             equ 1
N2_ID                   equ 001812d2h ; device 0018h, vendor 12D2h
N2_OP_MAX               equ 512
N2_BLOCK_DWORDS         equ 8192
N2_TIMEOUT              equ 0ffffffffh

N2_CFG_BAR0             equ 10h
N2_CFG_BAR1             equ 14h
N2_BAR_IO               equ 00000001h
N2_BASE_ALIGN           equ 00ffffffh
N2_MMIO_BYTES           equ 01000000h
N2_RAMIN_OFFSET         equ 00c00000h
N2_RAMIN_BYTES          equ 00008000h
N2_SCRATCH_OFFSET       equ 00200000h
N2_SCRATCH_BYTES        equ 00010000h
N2_BOOT_CHIP_MASK       equ 000f0000h
N2_BOOT_CHIP_NV3        equ 00030000h
N2_PGRAPH_STATUS        equ 004006b0h
N2_USER_FREE            equ 00800010h
N2_FREE_MASK            equ 0000ffffh
N2_USER_START           equ 00800000h
N2_USER_LIMIT           equ 00810000h
N2_SUBCHANNEL_MASK      equ 00001fffh

; Op codes; the layout is four dwords: op, a, b, c.
N2_OP_MMIO_READ         equ 1   ; a offset
N2_OP_MMIO_WRITE        equ 2   ; a offset, b value
N2_OP_VRAM_WRITE        equ 3   ; a offset, b value
N2_OP_VRAM_READ         equ 4   ; a offset
N2_OP_VRAM_FILL         equ 5   ; a offset, b dwords, c value
N2_OP_VRAM_BLOCK        equ 6   ; a offset, b dwords -> block area
N2_OP_WAIT_MMIO         equ 7   ; a offset, b mask, c value
N2_OP_WAIT_IDLE         equ 8   ; nv3Busy false
N2_OP_SAVE              equ 9
N2_OP_RESTORE           equ 10
N2_OP_VRAM_PATTERN      equ 11  ; a offset, b dwords, c seed
N2_OP_DELAY             equ 12  ; a PMC_BOOT_0 reads
N2_OP_LATCH_FREE        equ 13  ; the free count rivafb calls FifoEmptyCount
N2_OP_RAMIN_WRITE       equ 14  ; a offset, b value
N2_OP_RAMIN_READ        equ 15  ; a offset
N2_OP_RAMIN_FILL        equ 16  ; a offset, b dwords, c value
N2_DELAY_MAX            equ 100000
N2_WAIT_LIMIT           equ 2000000
N2_OP_BYTES             equ 16
N2_PATTERN_STEP         equ 9e3779b9h

; Status bits.
N2_ST_FOUND             equ 00000001h
N2_ST_MAPPED            equ 00000002h
N2_ST_IDENTITY          equ 00000004h
N2_ST_REFUSED           equ 00000008h
N2_ST_COMPLETE          equ 00000010h
N2_ST_BAD_BAR           equ 00000020h
N2_ST_TIMEOUT           equ 00000040h
N2_ST_SAVED             equ 00000080h

; Input: count, then count ops.
N2_IN_COUNT             equ 0
N2_IN_OPS               equ 4

; Output layout, in dwords. Must match struct v9x_n32d_result in
; nv3_2d_win32.c.
N2_HEADER_DWORDS        equ 16
N2_RESULT_DWORDS        equ (N2_HEADER_DWORDS + N2_OP_MAX + N2_BLOCK_DWORDS)

VxD_LOCKED_DATA_SEG

; Reads accepted: start, limit (exclusive). N3MM.VXD's list.
N2ReadWindows label dword
    dd 000000h, 000004h ; PMC_BOOT_0
    dd 000100h, 000104h ; PMC_INTR_0
    dd 000140h, 000144h ; PMC_INTR_EN_0
    dd 000200h, 000204h ; PMC_ENABLE
    dd 002080h, 002084h ; PFIFO_DEBUG_0
    dd 002100h, 002104h ; PFIFO_INTR_0
    dd 002140h, 002144h ; PFIFO_INTR_EN_0
    dd 002200h, 00221ch ; PFIFO_CONFIG_0, RAMHT, RAMFC, RAMRO
    dd 002400h, 002424h ; PFIFO runout status, put, get
    dd 002500h, 002504h ; PFIFO_CACHES
    dd 003000h, 003004h ; CACHE0_PUSH0
    dd 003200h, 003214h ; CACHE1_PUSH0, PUSH1, PUT
    dd 003240h, 003254h ; CACHE1_PULL0, PULL1, PULL_STATE
    dd 003270h, 003274h ; CACHE1_GET
    dd 003280h, 003300h ; CACHE1 context, 8 registers 10h apart
    dd 009100h, 009104h ; PTIMER_INTR_0
    dd 009140h, 009144h ; PTIMER_INTR_EN_0
    dd 009200h, 009214h ; PTIMER numerator, denominator
    dd 009400h, 009408h ; PTIMER time
    dd 100000h, 100004h ; PFB_BOOT_0
    dd 100080h, 100084h ; PFB_DEBUG_0
    dd 100200h, 100208h ; PFB_CONFIG_0, PFB_CONFIG_1
    dd 101000h, 101004h ; PSTRAPS
    dd 400080h, 400090h ; PGRAPH_DEBUG_0 .. 3
    dd 400100h, 400108h ; PGRAPH_INTR_0, INTR_1
    dd 400140h, 400148h ; PGRAPH_INTR_EN_0, EN_1
    dd 400180h, 4001a0h ; PGRAPH context switch and control
    dd 400550h, 400560h ; PGRAPH canvas
    dd 400630h, 400640h ; PGRAPH surface offsets 0-3
    dd 400650h, 400660h ; PGRAPH surface pitches 0-3
    dd 4006a0h, 4006b4h ; PGRAPH FIFO enable, SURF_CONFIG, STATUS
    dd 680500h, 680510h ; PRAMDAC NVPLL, MPLL, VPLL, PLL select
    dd 680600h, 680604h ; PRAMDAC general control
    dd 800010h, 800014h ; USER channel 0, subchannel 0 free count
N2_READ_WINDOW_COUNT    equ ($ - N2ReadWindows) / 8

; Writes accepted outside the USER area: exact offsets, each one rivafb
; writes on NV3 (riva_tbl.h word indexes x 4 plus the unit's base).
N2WriteOffsets label dword
    dd 000140h          ; PMC_INTR_EN_0                RivaTablePMC
    dd 000200h          ; PMC_ENABLE                   RivaTablePMC
    dd 009100h          ; PTIMER_INTR_0                RivaTablePTIMER
    dd 009140h          ; PTIMER_INTR_EN_0
    dd 009200h          ; PTIMER_NUMERATOR
    dd 009210h          ; PTIMER_DENOMINATOR
    dd 100200h          ; PFB_CONFIG_0                 LoadStateExt
    dd 002100h          ; PFIFO_INTR_0                 nv3TablePFIFO
    dd 002140h          ; PFIFO_INTR_EN_0
    dd 002210h          ; PFIFO_RAMHT
    dd 002214h          ; PFIFO_RAMFC
    dd 002218h          ; PFIFO_RAMRO
    dd 002400h          ; PFIFO runout status
    dd 002410h          ; PFIFO runout put
    dd 002420h          ; PFIFO runout get
    dd 002500h          ; PFIFO_CACHES
    dd 003200h          ; CACHE1_PUSH0
    dd 003204h          ; CACHE1_PUSH1
    dd 003210h          ; CACHE1_PUT
    dd 003240h          ; CACHE1_PULL0
    dd 003250h          ; CACHE1_PULL_STATE
    dd 003270h          ; CACHE1_GET
    ; CACHE1 context 0-7: word indexes 4A0h-4BCh step 4, so 10h apart.
    dd 003280h, 003290h, 0032a0h, 0032b0h
    dd 0032c0h, 0032d0h, 0032e0h, 0032f0h
    dd 400080h, 400084h, 400088h, 40008ch ; PGRAPH_DEBUG_0-3  nv3TablePGRAPH
    dd 400100h, 400104h ; PGRAPH_INTR_0, 1
    dd 400140h, 400144h ; PGRAPH_INTR_EN_0, 1
    dd 400180h          ; PGRAPH context switch
    dd 400190h          ; PGRAPH_CTX_CONTROL
    dd 400194h          ; PGRAPH context user
    dd 4001a0h, 4001a4h, 4001a8h, 4001ach ; PGRAPH context cache
    dd 4001b0h, 4001b4h, 4001b8h, 4001bch
    dd 400508h          ; PGRAPH 142h
    dd 400550h, 400554h, 400558h, 40055ch ; canvas
    dd 400628h          ; PGRAPH 18Ah
    dd 400630h, 400634h, 400638h, 40063ch ; surface offsets
    dd 400640h          ; PGRAPH 190h
    dd 400650h, 400654h, 400658h, 40065ch ; surface pitches
    dd 400680h          ; PGRAPH 1A0h
    dd 400688h          ; PGRAPH 1A2h
    dd 4006a0h          ; PGRAPH 1A8h
    dd 4006a4h          ; PGRAPH FIFO enable
    dd 4006a8h          ; PGRAPH SURF_CONFIG
    dd 401100h          ; PGRAPH 440h
    dd 401200h          ; PGRAPH 480h
N2_WRITE_OFFSET_COUNT   equ ($ - N2WriteOffsets) / 4

N2Result label dword
    dd N2_MAGIC                 ; 00 magic
    dd 0                        ; 01 status
    dd 0                        ; 02 id dword
    dd 0                        ; 03 BAR0
    dd 0                        ; 04 BAR1
    dd 0                        ; 05 PMC_BOOT_0
    dd 0                        ; 06 free count latched
    dd 0                        ; 07 reserved
    dd 0                        ; 08 ops executed
    dd 0                        ; 09 index of the refused op
    dd 0                        ; 10 block dwords used
    dd 0                        ; 11 configuration address
    dd 0                        ; 12 reserved
    dd 0                        ; 13 reserved
    dd 0                        ; 14 reserved
    dd 0                        ; 15 reserved
N2OpResults  dd N2_OP_MAX dup (0)
N2Block      dd N2_BLOCK_DWORDS dup (0)
N2Saved      dd (N2_SCRATCH_BYTES / 4) dup (0)
N2Mmio       dd 0
N2Ramin      dd 0
N2Scratch    dd 0
N2Config     dd 0
N2Empty      dd 0
VxD_LOCKED_DATA_ENDS

VxD_LOCKED_CODE_SEG

; EAX = PCI configuration address. Returns EAX = configuration dword.
; 0CF8h saved and restored with interrupts off.
BeginProc N2_Pci_Read
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
EndProc N2_Pci_Read

; Scans buses 0-15, device slots 0-31, function 0, for 12D2:0018.
; Returns CF clear and EAX = configuration address, or CF set.
BeginProc N2_Find
    push    ebx
    mov     ebx, 80000000h
N2_Find_Next:
    mov     eax, ebx
    call    N2_Pci_Read
    cmp     eax, N2_ID
    je      short N2_Find_Found
    add     ebx, 800h
    cmp     ebx, 80100000h
    jb      short N2_Find_Next
    pop     ebx
    stc
    ret
N2_Find_Found:
    mov     N2Result[8], eax
    mov     eax, ebx
    pop     ebx
    clc
    ret
EndProc N2_Find

; EAX = BAR as read. Returns CF clear and EAX = base if it is a 16 MiB-
; aligned, non-zero memory BAR.
BeginProc N2_Memory_Base
    test    eax, N2_BAR_IO
    jnz     short N2_Memory_Base_Bad
    and     eax, NOT 0fh
    jz      short N2_Memory_Base_Bad
    test    eax, N2_BASE_ALIGN
    jnz     short N2_Memory_Base_Bad
    clc
    ret
N2_Memory_Base_Bad:
    stc
    ret
EndProc N2_Memory_Base

; Finds and maps the card once. Returns CF clear when all three windows are
; mapped and the identity check passed.
BeginProc N2_Map
    pushad
    cmp     N2Scratch, 0
    jne     N2_Map_Ok

    call    N2_Find
    jc      N2_Map_Fail
    mov     N2Config, eax
    mov     N2Result[44], eax
    or      N2Result[4], N2_ST_FOUND

    mov     eax, N2Config
    or      eax, N2_CFG_BAR1
    call    N2_Pci_Read
    mov     N2Result[16], eax
    call    N2_Memory_Base
    jc      N2_Map_Bad_Bar
    mov     edi, eax

    mov     eax, N2Config
    or      eax, N2_CFG_BAR0
    call    N2_Pci_Read
    mov     N2Result[12], eax
    call    N2_Memory_Base
    jc      N2_Map_Bad_Bar

    VMMcall _MapPhysToLinear,<eax,N2_MMIO_BYTES,0>
    cmp     eax, 0ffffffffh
    je      N2_Map_Fail
    mov     N2Mmio, eax

    ; PMC_BOOT_0 names chip 3 (envytools pmc.rst; measured 00030122 on
    ; M-7V636C), or no window of video memory is mapped.
    mov     edx, [eax]
    mov     N2Result[20], edx
    and     edx, N2_BOOT_CHIP_MASK
    cmp     edx, N2_BOOT_CHIP_NV3
    jne     N2_Map_Fail
    or      N2Result[4], N2_ST_IDENTITY

    mov     eax, edi
    add     eax, N2_RAMIN_OFFSET
    VMMcall _MapPhysToLinear,<eax,N2_RAMIN_BYTES,0>
    cmp     eax, 0ffffffffh
    je      N2_Map_Fail
    mov     N2Ramin, eax

    mov     eax, edi
    add     eax, N2_SCRATCH_OFFSET
    VMMcall _MapPhysToLinear,<eax,N2_SCRATCH_BYTES,0>
    cmp     eax, 0ffffffffh
    je      N2_Map_Fail
    mov     N2Scratch, eax
    or      N2Result[4], N2_ST_MAPPED

N2_Map_Ok:
    popad
    clc
    ret
N2_Map_Bad_Bar:
    or      N2Result[4], N2_ST_BAD_BAR
N2_Map_Fail:
    popad
    stc
    ret
EndProc N2_Map

; EDX = offset. CF clear if readable.
BeginProc N2_Read_Allowed
    push    ecx
    push    esi
    test    edx, 3
    jnz     short N2_Read_Refused
    mov     esi, OFFSET32 N2ReadWindows
    mov     ecx, N2_READ_WINDOW_COUNT
N2_Read_Next:
    cmp     edx, [esi]
    jb      short N2_Read_Skip
    cmp     edx, [esi+4]
    jb      short N2_Read_Accepted
N2_Read_Skip:
    add     esi, 8
    dec     ecx
    jnz     short N2_Read_Next
N2_Read_Refused:
    pop     esi
    pop     ecx
    stc
    ret
N2_Read_Accepted:
    pop     esi
    pop     ecx
    clc
    ret
EndProc N2_Read_Allowed

; EDX = offset. CF clear if writable: an exact offset from N2WriteOffsets,
; or a channel 0 method in the USER area at 0 (bind), 300h-31Ch or
; 3FCh-404h of a subchannel.
BeginProc N2_Write_Allowed
    push    ecx
    push    esi
    test    edx, 3
    jnz     short N2_Write_Refused
    cmp     edx, N2_USER_START
    jb      short N2_Write_Table
    cmp     edx, N2_USER_LIMIT
    jae     short N2_Write_Refused
    mov     ecx, edx
    and     ecx, N2_SUBCHANNEL_MASK
    jz      short N2_Write_Accepted
    cmp     ecx, 300h
    jb      short N2_Write_Refused
    cmp     ecx, 31ch
    jbe     short N2_Write_Accepted
    cmp     ecx, 3fch
    jb      short N2_Write_Refused
    cmp     ecx, 404h
    jbe     short N2_Write_Accepted
    jmp     short N2_Write_Refused
N2_Write_Table:
    mov     esi, OFFSET32 N2WriteOffsets
    mov     ecx, N2_WRITE_OFFSET_COUNT
N2_Write_Next:
    cmp     edx, [esi]
    je      short N2_Write_Accepted
    add     esi, 4
    dec     ecx
    jnz     short N2_Write_Next
N2_Write_Refused:
    pop     esi
    pop     ecx
    stc
    ret
N2_Write_Accepted:
    pop     esi
    pop     ecx
    clc
    ret
EndProc N2_Write_Allowed

; EDX = offset, ECX = dwords (at least 1), EAX = window bytes. CF clear if
; the run lies inside the window and is aligned.
BeginProc N2_Window_Allowed
    push    ebx
    test    edx, 3
    jnz     short N2_Window_Refused
    test    ecx, ecx
    jz      short N2_Window_Refused
    cmp     edx, eax
    jae     short N2_Window_Refused
    mov     ebx, ecx
    shr     ebx, 30
    jnz     short N2_Window_Refused
    mov     ebx, ecx
    shl     ebx, 2
    add     ebx, edx
    cmp     ebx, eax
    ja      short N2_Window_Refused
    pop     ebx
    clc
    ret
N2_Window_Refused:
    pop     ebx
    stc
    ret
EndProc N2_Window_Allowed

BeginProc N2_Vram_Allowed
    push    eax
    mov     eax, N2_SCRATCH_BYTES
    call    N2_Window_Allowed
    pop     eax
    ret
EndProc N2_Vram_Allowed

BeginProc N2_Ramin_Allowed
    push    eax
    mov     eax, N2_RAMIN_BYTES
    call    N2_Window_Allowed
    pop     eax
    ret
EndProc N2_Ramin_Allowed

; ESI = op (four dwords). Returns EAX = the op's result, CF set if refused.
BeginProc N2_Execute
    push    ebx
    push    ecx
    push    edx
    push    edi
    mov     eax, [esi]
    mov     edx, [esi+4]

    cmp     eax, N2_OP_MMIO_READ
    jne     short N2_Exec_Not_Read
    call    N2_Read_Allowed
    jc      N2_Exec_Refused
    mov     ebx, N2Mmio
    mov     eax, [ebx+edx]
    jmp     N2_Exec_Done
N2_Exec_Not_Read:

    cmp     eax, N2_OP_MMIO_WRITE
    jne     short N2_Exec_Not_Write
    call    N2_Write_Allowed
    jc      N2_Exec_Refused
    mov     ebx, N2Mmio
    mov     eax, [esi+8]
    mov     [ebx+edx], eax
    xor     eax, eax
    jmp     N2_Exec_Done
N2_Exec_Not_Write:

    cmp     eax, N2_OP_VRAM_WRITE
    jne     short N2_Exec_Not_Vram_Write
    mov     ecx, 1
    call    N2_Vram_Allowed
    jc      N2_Exec_Refused
    mov     ebx, N2Scratch
    mov     eax, [esi+8]
    mov     [ebx+edx], eax
    xor     eax, eax
    jmp     N2_Exec_Done
N2_Exec_Not_Vram_Write:

    cmp     eax, N2_OP_VRAM_READ
    jne     short N2_Exec_Not_Vram_Read
    mov     ecx, 1
    call    N2_Vram_Allowed
    jc      N2_Exec_Refused
    mov     ebx, N2Scratch
    mov     eax, [ebx+edx]
    jmp     N2_Exec_Done
N2_Exec_Not_Vram_Read:

    cmp     eax, N2_OP_VRAM_FILL
    jne     short N2_Exec_Not_Fill
    mov     ecx, [esi+8]
    call    N2_Vram_Allowed
    jc      N2_Exec_Refused
    mov     edi, N2Scratch
    add     edi, edx
    mov     eax, [esi+12]
    cld
    rep     stosd
    xor     eax, eax
    jmp     N2_Exec_Done
N2_Exec_Not_Fill:

    cmp     eax, N2_OP_VRAM_PATTERN
    jne     short N2_Exec_Not_Pattern
    mov     ecx, [esi+8]
    call    N2_Vram_Allowed
    jc      N2_Exec_Refused
    mov     edi, N2Scratch
    add     edi, edx
    mov     eax, [esi+12]
N2_Exec_Pattern_Next:
    mov     [edi], eax
    add     eax, N2_PATTERN_STEP
    add     edi, 4
    dec     ecx
    jnz     short N2_Exec_Pattern_Next
    xor     eax, eax
    jmp     N2_Exec_Done
N2_Exec_Not_Pattern:

    cmp     eax, N2_OP_VRAM_BLOCK
    jne     short N2_Exec_Not_Block
    mov     ecx, [esi+8]
    call    N2_Vram_Allowed
    jc      N2_Exec_Refused
    mov     eax, N2Result[40]
    mov     ebx, eax
    add     ebx, ecx
    cmp     ebx, N2_BLOCK_DWORDS
    ja      N2_Exec_Refused
    mov     N2Result[40], ebx
    push    esi
    mov     esi, N2Scratch
    add     esi, edx
    lea     edi, N2Block[eax*4]
    cld
    rep     movsd
    pop     esi
    jmp     N2_Exec_Done
N2_Exec_Not_Block:

    cmp     eax, N2_OP_WAIT_MMIO
    jne     short N2_Exec_Not_Wait_Mmio
    call    N2_Read_Allowed
    jc      N2_Exec_Refused
    mov     ebx, N2Mmio
    add     ebx, edx
    xor     eax, eax
N2_Exec_Wait_Mmio_Next:
    mov     ecx, [ebx]
    and     ecx, [esi+8]
    cmp     ecx, [esi+12]
    je      N2_Exec_Done
    inc     eax
    cmp     eax, N2_WAIT_LIMIT
    jb      short N2_Exec_Wait_Mmio_Next
    or      N2Result[4], N2_ST_TIMEOUT
    mov     eax, N2_TIMEOUT
    jmp     N2_Exec_Done
N2_Exec_Not_Wait_Mmio:

    ; rivafb's nv3Busy: busy while the free count is below the latched
    ; empty count or PGRAPH STATUS bit 0 is set. Returns the reads taken.
    cmp     eax, N2_OP_WAIT_IDLE
    jne     short N2_Exec_Not_Wait_Idle
    mov     ebx, N2Mmio
    xor     eax, eax
N2_Exec_Wait_Idle_Next:
    mov     ecx, [ebx+N2_USER_FREE]
    and     ecx, N2_FREE_MASK
    cmp     ecx, N2Empty
    jb      short N2_Exec_Wait_Idle_Busy
    test    dword ptr [ebx+N2_PGRAPH_STATUS], 1
    jz      N2_Exec_Done
N2_Exec_Wait_Idle_Busy:
    inc     eax
    cmp     eax, N2_WAIT_LIMIT
    jb      short N2_Exec_Wait_Idle_Next
    or      N2Result[4], N2_ST_TIMEOUT
    mov     eax, N2_TIMEOUT
    jmp     N2_Exec_Done
N2_Exec_Not_Wait_Idle:

    cmp     eax, N2_OP_LATCH_FREE
    jne     short N2_Exec_Not_Latch
    mov     ebx, N2Mmio
    mov     eax, [ebx+N2_USER_FREE]
    and     eax, N2_FREE_MASK
    mov     N2Empty, eax
    mov     N2Result[24], eax
    jmp     N2_Exec_Done
N2_Exec_Not_Latch:

    cmp     eax, N2_OP_DELAY
    jne     short N2_Exec_Not_Delay
    mov     ecx, edx
    test    ecx, ecx
    jz      N2_Exec_Refused
    cmp     ecx, N2_DELAY_MAX
    ja      N2_Exec_Refused
    mov     ebx, N2Mmio
N2_Exec_Delay_Next:
    mov     eax, [ebx]
    dec     ecx
    jnz     short N2_Exec_Delay_Next
    jmp     N2_Exec_Done
N2_Exec_Not_Delay:

    cmp     eax, N2_OP_RAMIN_WRITE
    jne     short N2_Exec_Not_Ramin_Write
    mov     ecx, 1
    call    N2_Ramin_Allowed
    jc      N2_Exec_Refused
    mov     ebx, N2Ramin
    mov     eax, [esi+8]
    mov     [ebx+edx], eax
    xor     eax, eax
    jmp     N2_Exec_Done
N2_Exec_Not_Ramin_Write:

    cmp     eax, N2_OP_RAMIN_READ
    jne     short N2_Exec_Not_Ramin_Read
    mov     ecx, 1
    call    N2_Ramin_Allowed
    jc      N2_Exec_Refused
    mov     ebx, N2Ramin
    mov     eax, [ebx+edx]
    jmp     N2_Exec_Done
N2_Exec_Not_Ramin_Read:

    cmp     eax, N2_OP_RAMIN_FILL
    jne     short N2_Exec_Not_Ramin_Fill
    mov     ecx, [esi+8]
    call    N2_Ramin_Allowed
    jc      N2_Exec_Refused
    mov     edi, N2Ramin
    add     edi, edx
    mov     eax, [esi+12]
    cld
    rep     stosd
    xor     eax, eax
    jmp     N2_Exec_Done
N2_Exec_Not_Ramin_Fill:

    cmp     eax, N2_OP_SAVE
    jne     short N2_Exec_Not_Save
    push    esi
    mov     esi, N2Scratch
    mov     edi, OFFSET32 N2Saved
    mov     ecx, N2_SCRATCH_BYTES / 4
    cld
    rep     movsd
    pop     esi
    or      N2Result[4], N2_ST_SAVED
    xor     eax, eax
    jmp     short N2_Exec_Done
N2_Exec_Not_Save:

    cmp     eax, N2_OP_RESTORE
    jne     short N2_Exec_Refused
    ; Only what was saved in this run goes back.
    test    N2Result[4], N2_ST_SAVED
    jz      short N2_Exec_Refused
    push    esi
    mov     esi, OFFSET32 N2Saved
    mov     edi, N2Scratch
    mov     ecx, N2_SCRATCH_BYTES / 4
    cld
    rep     movsd
    pop     esi
    xor     eax, eax

N2_Exec_Done:
    pop     edi
    pop     edx
    pop     ecx
    pop     ebx
    clc
    ret
N2_Exec_Refused:
    pop     edi
    pop     edx
    pop     ecx
    pop     ebx
    stc
    ret
EndProc N2_Execute

; EBP = DIOC input buffer, ECX = its byte count.
BeginProc N2_Run
    pushad
    mov     eax, [ebp+N2_IN_COUNT]
    cmp     eax, N2_OP_MAX
    ja      short N2_Run_Refused
    sub     ecx, N2_IN_OPS
    shr     ecx, 4
    cmp     eax, ecx
    ja      short N2_Run_Refused
    call    N2_Map
    jc      short N2_Run_Done

    mov     ecx, [ebp+N2_IN_COUNT]
    lea     esi, [ebp+N2_IN_OPS]
    mov     edi, OFFSET32 N2OpResults
    xor     ebx, ebx
    jecxz   short N2_Run_Complete
N2_Run_Next:
    call    N2_Execute
    jc      short N2_Run_Refused_At
    mov     [edi], eax
    inc     ebx
    mov     N2Result[32], ebx
    add     esi, N2_OP_BYTES
    add     edi, 4
    dec     ecx
    jnz     short N2_Run_Next
N2_Run_Complete:
    or      N2Result[4], N2_ST_COMPLETE
    jmp     short N2_Run_Done
N2_Run_Refused_At:
    mov     N2Result[36], ebx
N2_Run_Refused:
    or      N2Result[4], N2_ST_REFUSED
N2_Run_Done:
    popad
    ret
EndProc N2_Run

BeginProc N2_Reset_Result
    pushad
    mov     edi, OFFSET32 N2Result + 4
    mov     ecx, N2_RESULT_DWORDS - 1
    xor     eax, eax
    cld
    rep     stosd
    popad
    ret
EndProc N2_Reset_Result

BeginProc N2_W32_DeviceIoControl
    cmp     ecx, DIOC_OPEN
    je      N2_Dioc_Ok
    cmp     ecx, DIOC_CLOSEHANDLE
    je      N2_Dioc_Ok
    cmp     ecx, N2_DIOC_RUN
    jne     N2_Dioc_Fail

    pushad
    mov     ebx, esi
    mov     ebp, [ebx.lpvInBuffer]
    test    ebp, ebp
    jz      N2_Dioc_Copy_Fail
    mov     ecx, [ebx.cbInBuffer]
    cmp     ecx, N2_IN_OPS
    jb      N2_Dioc_Copy_Fail
    mov     edi, [ebx.lpvOutBuffer]
    test    edi, edi
    jz      N2_Dioc_Copy_Fail
    cmp     [ebx.cbOutBuffer], N2_RESULT_DWORDS * 4
    jb      N2_Dioc_Copy_Fail

    call    N2_Reset_Result
    mov     ecx, [ebx.cbInBuffer]
    call    N2_Run

    mov     edi, [ebx.lpvOutBuffer]
    mov     esi, OFFSET32 N2Result
    mov     ecx, N2_RESULT_DWORDS
    cld
    rep     movsd
    mov     eax, [ebx.lpcbBytesReturned]
    test    eax, eax
    jz      short N2_Dioc_Copy_Done
    mov     dword ptr [eax], N2_RESULT_DWORDS * 4
N2_Dioc_Copy_Done:
    popad
N2_Dioc_Ok:
    xor     eax, eax
    ret

N2_Dioc_Copy_Fail:
    popad
N2_Dioc_Fail:
    mov     eax, 1
    ret
EndProc N2_W32_DeviceIoControl

BeginProc N2_Dynamic_Init
    clc
    ret
EndProc N2_Dynamic_Init

BeginProc N2_Dynamic_Exit
    clc
    ret
EndProc N2_Dynamic_Exit

Begin_Control_Dispatch N2
    Control_Dispatch Sys_Dynamic_Device_Init, N2_Dynamic_Init
    Control_Dispatch Sys_Dynamic_Device_Exit, N2_Dynamic_Exit
    Control_Dispatch W32_DeviceIoControl, N2_W32_DeviceIoControl
End_Control_Dispatch N2

VxD_LOCKED_CODE_ENDS

end
