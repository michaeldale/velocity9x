; NVIDIA GT218 (GeForce 210) PFIFO channel and PGRAPH 2D bring-up probe VxD.
;
; Step 3 of docs\plans\nvidia-gt218-pgraph-2d.md. Loaded dynamically by
; V9XN5CH.EXE, which decides every access; this VxD only executes an op
; list against the card and refuses anything outside it. EGCP.VXD
; (evergreen_cp.asm) is the model.
;
; It finds PCI 10DE:0A65 with configuration mechanism 1, requires BAR0 a
; 32-bit memory BAR aligned to 16 MiB and BAR1 a 64-bit memory BAR below
; 4 GiB aligned to 256 MiB, maps all of BAR0 and a 4 MiB window of BAR1 at
; + 128 MiB, and refuses to run unless PMC_BOOT_0 names chipset A8h. BAR1
; decodes video memory linearly with no BAR VM on this card
; (docs\decisions\2026-11-11-gt218-bar-windows.md), and the display driver
; maps only BAR1's first 128 MiB, so the window is memory nothing else
; uses. Runs 1 and 2 put it at + 192 MiB, never measured, and lost the card
; at their first access through it.
;
;   MMIO read32    - the status and state registers in N5ReadWindows.
;   MMIO write32   - the registers nouveau's nv50_fifo_init, channel bind
;                    and runlist commit, nv50_gr_init and the context
;                    program upload write (fifo\nv50.c, fifo\g84.c,
;                    gr\nv50.c, gr\ctxnv50.c), the VM TLB flush and the
;                    channel 1 IB PUT; nothing else. PMC_ENABLE (200h) may
;                    change only its PFIFO and PGRAPH bits.
;   VRAM data      - a run of the request's data area into the window.
;   VRAM ops       - read, fill, read block, inside the window.
;   wait           - an MMIO dword under a mask equal to a value, or a
;                    window dword equal to a value, bounded.
;   delay          - a count of PTIMER reads.
;
; Interrupts: PMC_INTR_EN (140h) is not writable here and read 0 on boot
; 82, so whatever the engines raise stays in their status registers for the
; EXE to poll; nothing reaches a Windows that has no handler for the card.
; 400328h, the context program's data port, is writable and never
; readable: its index auto-increments.

.386p

.xlist
include VMM.INC
include VWIN32.INC
.list

Declare_Virtual_Device N5CH, 1, 0, NC_Control, \
                       Undefined_Device_ID, Undefined_Init_Order, , ,

NC_MAGIC                equ 4843354eh ; "N5CH"
NC_DIOC_RUN             equ 1
NC_ID                   equ 0a6510deh ; device 0A65h, vendor 10DEh
NC_OP_MAX               equ 512
NC_BLOCK_DWORDS         equ 4096
NC_DATA_DWORDS          equ 30000h    ; covers nv50_chan.h's region
NC_TIMEOUT              equ 0ffffffffh

NC_CFG_BAR0             equ 10h
NC_CFG_BAR1             equ 14h
NC_CFG_BAR1_HIGH        equ 18h
NC_BAR_IO               equ 00000001h
NC_BAR_TYPE_MASK        equ 00000006h
NC_BAR_TYPE_32          equ 00000000h
NC_BAR_TYPE_64          equ 00000004h
NC_MMIO_BYTES           equ 01000000h
NC_MMIO_ALIGN           equ 00ffffffh
NC_BAR1_ALIGN           equ 0fffffffh
NC_WINDOW_OFFSET        equ 08000000h
NC_WINDOW_BYTES         equ 00400000h
NC_BOOT_CHIPSET_MASK    equ 1ff00000h
NC_BOOT_CHIPSET_GT218   equ 0a800000h
NC_PMC_ENABLE           equ 200h
NC_PMC_ENABLE_ALLOWED   equ 00201100h ; PGRAPH 00201000h | PFIFO 100h
NC_PTIMER_TIME          equ 9400h
NC_CTXPROG_DATA         equ 400328h

; Ops.
NC_OP_MMIO_READ         equ 1   ; a offset
NC_OP_MMIO_WRITE        equ 2   ; a offset, b value
NC_OP_VRAM_DATA         equ 3   ; a window offset, b first data dword, c dwords
NC_OP_VRAM_READ         equ 4   ; a window offset
NC_OP_VRAM_FILL         equ 5   ; a window offset, b dwords, c value
NC_OP_VRAM_BLOCK        equ 6   ; a window offset, b dwords -> block area
NC_OP_WAIT_MMIO         equ 7   ; a offset, b mask, c value; limit fixed
NC_OP_WAIT_VRAM         equ 8   ; a window offset, b value, c limit
NC_OP_DELAY             equ 9   ; a PTIMER reads
NC_OP_FLUSH_PAGE        equ 10  ; give 100C08h a host page if it has none
NC_FLUSH_PAGE_REG       equ 100c08h
NC_PAGE_MAX_PHYS        equ 0fffffh ; pages: below 4 GiB
NC_DELAY_MAX            equ 2000000
NC_WAIT_MMIO_LIMIT      equ 4000000
NC_OP_BYTES             equ 16

; Status bits.
NC_ST_FOUND             equ 00000001h
NC_ST_MAPPED            equ 00000002h
NC_ST_IDENTITY          equ 00000004h
NC_ST_REFUSED           equ 00000008h
NC_ST_COMPLETE          equ 00000010h
NC_ST_BAD_BAR           equ 00000020h
NC_ST_TIMEOUT           equ 00000040h

; Input: count, then NC_OP_MAX ops, then a data count and the data area.
NC_IN_COUNT             equ 0
NC_IN_OPS               equ 4
NC_IN_DATA_COUNT        equ (NC_IN_OPS + NC_OP_MAX * NC_OP_BYTES)
NC_IN_DATA              equ (NC_IN_DATA_COUNT + 4)
NC_IN_BYTES             equ (NC_IN_DATA + NC_DATA_DWORDS * 4)

; Output layout, in dwords. Must match struct v9x_n5ch_result in
; nv50_channel_probe_win32.c.
NC_HEADER_DWORDS        equ 16
NC_RESULT_DWORDS        equ (NC_HEADER_DWORDS + NC_OP_MAX + NC_BLOCK_DWORDS)

VxD_LOCKED_DATA_SEG

; Reads accepted: start, limit (exclusive).
NCReadWindows label dword
    dd 000000h, 000008h ; PMC_BOOT_0, PMC_BOOT_1
    dd 000100h, 000104h ; PMC_INTR_0
    dd 000140h, 000144h ; PMC_INTR_EN_0
    dd 000200h, 000204h ; PMC_ENABLE
    dd 001540h, 001544h ; PBUS units
    dd 001700h, 001710h ; PRAMIN window, BAR VM
    dd 002044h, 002048h ; PFIFO 2044
    dd 002100h, 002104h ; PFIFO_INTR
    dd 002140h, 002144h ; PFIFO_INTR_EN
    dd 002500h, 002504h ; PFIFO caches
    dd 00250ch, 002510h ; PFIFO 250C
    dd 002520h, 002524h ; PFIFO engine select
    dd 002600h, 002800h ; PFIFO channel table
    dd 003200h, 003204h ; cache1 push
    dd 003250h, 003254h ; cache1 pull
    dd 0032ech, 003300h ; runlist count, address, 32FC
    dd 009400h, 009408h ; PTIMER time
    dd 100c80h, 100c84h ; VM flush
    dd 100c00h, 100ca0h ; VM: fault page (100C08h), flush, fault status
    dd 400100h, 400110h ; PGRAPH_INTR, trap status
    dd 400138h, 400140h ; PGRAPH interrupt enables
    dd 400324h, 400328h ; context program index
    dd 40032ch, 400334h ; context instance
    dd 400380h, 40038ch ; unit status (gr\g84.c tlb flush)
    dd 400500h, 400504h ; PGRAPH fifo access
    dd 400700h, 400710h ; PGRAPH status
    dd 400804h, 400818h ; trap: status, address, data, class
    dd 40008ch, 400090h ; context switch control
    dd 402ca8h, 402cach ; zcull
    dd 402cc0h, 402cc4h ; zcull
    dd 700000h, 800000h ; PRAMIN, the GPU's own view of video memory
    ; No USERD (C02000h) and no CACHE1 sweep (3200h-32FCh) reads: on this
    ; card reads there time out (FFFFFFFF, tens of ms) and the card leaves
    ; configuration space soon after (runs 3, 6 and 13 of
    ; docs\decisions\2026-11-11-gt218-channel-probe-run1-hang.md).
NC_READ_WINDOW_COUNT    equ 31

; Writes accepted: start, limit (exclusive).
NCWriteWindows label dword
    dd 000200h, 000204h ; PMC_ENABLE (value checked)
    dd 002044h, 002048h ; nv50_fifo_init
    dd 002100h, 002104h
    dd 002140h, 002144h
    dd 002500h, 002504h
    dd 00250ch, 002510h
    dd 002600h, 002800h ; channel table (cleared, then channel 1)
    dd 003200h, 003204h
    dd 003250h, 003254h
    dd 0032ech, 0032f0h ; runlist count
    dd 0032f4h, 0032f8h ; runlist address
    dd 100c80h, 100c84h ; VM flush
    dd 40008ch, 400090h ; nv50_gr_init
    dd 400804h, 400808h
    dd 406800h, 406804h
    dd 400c04h, 400c08h
    dd 401800h, 401804h
    dd 405018h, 40501ch
    dd 402000h, 402004h
    dd 408600h, 408604h ; per-TP trap enables, TP 0 (1540h = F3010001h)
    dd 408708h, 40870ch
    dd 40831ch, 408320h
    dd 400100h, 400104h
    dd 400108h, 40010ch
    dd 400138h, 40013ch
    dd 40013ch, 400140h
    dd 400500h, 400504h
    dd 400324h, 400328h ; context program index
    dd 400328h, 40032ch ; context program data
    dd 400824h, 400834h
    dd 40032ch, 400334h
    dd 402c20h, 402ca0h ; zcull, 8 x 4 registers
    dd 402ca8h, 402cach
    dd 402cc0h, 402cc4h
    dd 0c0208ch, 0c02090h ; channel 1 IB PUT
    dd 001700h, 001704h ; PRAMIN window base (nv50_instmem), restored after
NC_WRITE_WINDOW_COUNT   equ 36

NCResult label dword
    dd NC_MAGIC                 ; 00 magic
    dd 0                        ; 01 status
    dd 0                        ; 02 id
    dd 0                        ; 03 BAR0
    dd 0                        ; 04 BAR1
    dd 0                        ; 05 BAR1 high
    dd 0                        ; 06 PMC_BOOT_0
    dd 0                        ; 07 ops done
    dd 0                        ; 08 op refused, if any
    dd 0                        ; 09 configuration address
    dd 0                        ; 10 block dwords used
    dd 0                        ; 11 reserved
    dd 0                        ; 12 reserved
    dd 0                        ; 13 reserved
    dd 0                        ; 14 reserved
    dd 0                        ; 15 reserved
NCOpResults  dd NC_OP_MAX dup (0)
NCBlock      dd NC_BLOCK_DWORDS dup (0)
NCConfig     dd 0
NCMmio       dd 0
NCWindow     dd 0
NCData       dd 0
NCDataCount  dd 0
NCFlushPhys  dd 0
VxD_LOCKED_DATA_ENDS

VxD_LOCKED_CODE_SEG

; EAX = PCI configuration address. Returns EAX = configuration dword.
; 0CF8h saved and restored with interrupts off.
BeginProc NC_Pci_Read
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
EndProc NC_Pci_Read

; Scans buses 0-15, device slots 0-31, function 0, for 10DE:0A65.
; Returns CF clear and EAX = configuration address, or CF set.
BeginProc NC_Find
    push    ebx
    mov     ebx, 80000000h
NC_Find_Next:
    mov     eax, ebx
    call    NC_Pci_Read
    cmp     eax, NC_ID
    je      short NC_Find_Found
    add     ebx, 800h
    cmp     ebx, 80100000h
    jb      short NC_Find_Next
    pop     ebx
    stc
    ret
NC_Find_Found:
    mov     NCResult[8], eax
    mov     eax, ebx
    pop     ebx
    clc
    ret
EndProc NC_Find

; Finds and maps the card once. Returns CF clear when both windows are
; mapped and the identity check passed.
BeginProc NC_Map
    pushad
    cmp     NCWindow, 0
    jne     NC_Map_Ok

    call    NC_Find
    jc      NC_Map_Fail
    mov     NCConfig, eax
    mov     NCResult[36], eax
    or      NCResult[4], NC_ST_FOUND

    ; BAR0: 32-bit memory, 16 MiB aligned.
    mov     eax, NCConfig
    or      eax, NC_CFG_BAR0
    call    NC_Pci_Read
    mov     NCResult[12], eax
    mov     esi, eax
    test    esi, NC_BAR_IO
    jnz     NC_Map_Bad_Bar
    mov     edx, esi
    and     edx, NC_BAR_TYPE_MASK
    cmp     edx, NC_BAR_TYPE_32
    jne     NC_Map_Bad_Bar
    and     esi, NOT 0fh
    jz      NC_Map_Bad_Bar
    test    esi, NC_MMIO_ALIGN
    jnz     NC_Map_Bad_Bar

    ; BAR1: 64-bit memory with the high half zero, 256 MiB aligned.
    mov     eax, NCConfig
    or      eax, NC_CFG_BAR1
    call    NC_Pci_Read
    mov     NCResult[16], eax
    mov     edi, eax
    mov     eax, NCConfig
    or      eax, NC_CFG_BAR1_HIGH
    call    NC_Pci_Read
    mov     NCResult[20], eax
    test    eax, eax
    jnz     NC_Map_Bad_Bar
    test    edi, NC_BAR_IO
    jnz     NC_Map_Bad_Bar
    mov     edx, edi
    and     edx, NC_BAR_TYPE_MASK
    cmp     edx, NC_BAR_TYPE_64
    jne     NC_Map_Bad_Bar
    and     edi, NOT 0fh
    jz      NC_Map_Bad_Bar
    test    edi, NC_BAR1_ALIGN
    jnz     NC_Map_Bad_Bar

    VMMcall _MapPhysToLinear,<esi,NC_MMIO_BYTES,0>
    cmp     eax, 0ffffffffh
    je      NC_Map_Fail
    mov     NCMmio, eax

    ; The identity check before the window is mapped.
    mov     edx, [eax]
    mov     NCResult[24], edx
    and     edx, NC_BOOT_CHIPSET_MASK
    cmp     edx, NC_BOOT_CHIPSET_GT218
    jne     NC_Map_Fail
    or      NCResult[4], NC_ST_IDENTITY

    add     edi, NC_WINDOW_OFFSET
    VMMcall _MapPhysToLinear,<edi,NC_WINDOW_BYTES,0>
    cmp     eax, 0ffffffffh
    je      NC_Map_Fail
    mov     NCWindow, eax
    or      NCResult[4], NC_ST_MAPPED

NC_Map_Ok:
    popad
    clc
    ret
NC_Map_Bad_Bar:
    or      NCResult[4], NC_ST_BAD_BAR
NC_Map_Fail:
    popad
    stc
    ret
EndProc NC_Map

; EDX = offset, ESI = window table, ECX = its count. CF clear if inside.
BeginProc NC_In_Windows
    push    ecx
    push    esi
    test    edx, 3
    jnz     short NC_Windows_Refused
NC_Windows_Next:
    cmp     edx, [esi]
    jb      short NC_Windows_Skip
    cmp     edx, [esi+4]
    jb      short NC_Windows_Accepted
NC_Windows_Skip:
    add     esi, 8
    dec     ecx
    jnz     short NC_Windows_Next
NC_Windows_Refused:
    pop     esi
    pop     ecx
    stc
    ret
NC_Windows_Accepted:
    pop     esi
    pop     ecx
    clc
    ret
EndProc NC_In_Windows

BeginProc NC_Read_Allowed
    push    ecx
    push    esi
    mov     esi, OFFSET32 NCReadWindows
    mov     ecx, NC_READ_WINDOW_COUNT
    call    NC_In_Windows
    pop     esi
    pop     ecx
    ret
EndProc NC_Read_Allowed

BeginProc NC_Write_Allowed
    push    ecx
    push    esi
    mov     esi, OFFSET32 NCWriteWindows
    mov     ecx, NC_WRITE_WINDOW_COUNT
    call    NC_In_Windows
    pop     esi
    pop     ecx
    ret
EndProc NC_Write_Allowed

; EDX = window offset, ECX = dwords (at least 1). CF clear if the run lies
; inside the window and is aligned.
BeginProc NC_Vram_Allowed
    push    eax
    test    edx, 3
    jnz     short NC_Vram_Refused
    test    ecx, ecx
    jz      short NC_Vram_Refused
    cmp     ecx, NC_WINDOW_BYTES / 4
    ja      short NC_Vram_Refused
    cmp     edx, NC_WINDOW_BYTES
    jae     short NC_Vram_Refused
    mov     eax, ecx
    shl     eax, 2
    add     eax, edx
    cmp     eax, NC_WINDOW_BYTES
    ja      short NC_Vram_Refused
    pop     eax
    clc
    ret
NC_Vram_Refused:
    pop     eax
    stc
    ret
EndProc NC_Vram_Allowed

; ESI = op (four dwords). Returns EAX = the op's result, CF set if refused.
BeginProc NC_Execute
    push    ebx
    push    ecx
    push    edx
    push    edi
    mov     eax, [esi]
    mov     edx, [esi+4]

    cmp     eax, NC_OP_MMIO_READ
    jne     short NC_Exec_Not_Read
    call    NC_Read_Allowed
    jc      NC_Exec_Refused
    mov     ebx, NCMmio
    mov     eax, [ebx+edx]
    jmp     NC_Exec_Done
NC_Exec_Not_Read:

    cmp     eax, NC_OP_MMIO_WRITE
    jne     short NC_Exec_Not_Write
    call    NC_Write_Allowed
    jc      NC_Exec_Refused
    mov     ebx, NCMmio
    mov     eax, [esi+8]
    ; PMC_ENABLE: only the PFIFO and PGRAPH bits may change, so nothing
    ; else on the card (the display, the bus) is ever switched off here.
    cmp     edx, NC_PMC_ENABLE
    jne     short NC_Exec_Write_Value_Ok
    mov     ecx, [ebx+NC_PMC_ENABLE]
    xor     ecx, eax
    test    ecx, NOT NC_PMC_ENABLE_ALLOWED
    jnz     NC_Exec_Refused
NC_Exec_Write_Value_Ok:
    mov     [ebx+edx], eax
    ; Post the write before the next op.
    mov     eax, [ebx]
    xor     eax, eax
    jmp     NC_Exec_Done
NC_Exec_Not_Write:

    cmp     eax, NC_OP_VRAM_DATA
    jne     short NC_Exec_Not_Data
    mov     ecx, [esi+12]
    call    NC_Vram_Allowed
    jc      NC_Exec_Refused
    mov     ebx, [esi+8]
    cmp     ebx, NCDataCount
    jae     NC_Exec_Refused
    mov     eax, NCDataCount
    sub     eax, ebx
    cmp     ecx, eax
    ja      NC_Exec_Refused
    push    esi
    mov     esi, NCData
    lea     esi, [esi+ebx*4]
    mov     edi, NCWindow
    add     edi, edx
    cld
    rep     movsd
    pop     esi
    xor     eax, eax
    jmp     NC_Exec_Done
NC_Exec_Not_Data:

    cmp     eax, NC_OP_VRAM_READ
    jne     short NC_Exec_Not_Vram_Read
    mov     ecx, 1
    call    NC_Vram_Allowed
    jc      NC_Exec_Refused
    mov     ebx, NCWindow
    mov     eax, [ebx+edx]
    jmp     NC_Exec_Done
NC_Exec_Not_Vram_Read:

    cmp     eax, NC_OP_VRAM_FILL
    jne     short NC_Exec_Not_Fill
    mov     ecx, [esi+8]
    call    NC_Vram_Allowed
    jc      NC_Exec_Refused
    mov     edi, NCWindow
    add     edi, edx
    mov     eax, [esi+12]
    cld
    rep     stosd
    xor     eax, eax
    jmp     NC_Exec_Done
NC_Exec_Not_Fill:

    cmp     eax, NC_OP_VRAM_BLOCK
    jne     short NC_Exec_Not_Block
    mov     ecx, [esi+8]
    call    NC_Vram_Allowed
    jc      NC_Exec_Refused
    mov     eax, NCResult[40]
    mov     ebx, eax
    add     ebx, ecx
    cmp     ebx, NC_BLOCK_DWORDS
    ja      NC_Exec_Refused
    mov     NCResult[40], ebx
    push    esi
    mov     esi, NCWindow
    add     esi, edx
    lea     edi, NCBlock[eax*4]
    cld
    rep     movsd
    pop     esi
    jmp     NC_Exec_Done
NC_Exec_Not_Block:

    cmp     eax, NC_OP_WAIT_MMIO
    jne     short NC_Exec_Not_Wait_Mmio
    call    NC_Read_Allowed
    jc      NC_Exec_Refused
    mov     ebx, NCMmio
    add     ebx, edx
    xor     eax, eax
NC_Exec_Wait_Mmio_Next:
    mov     ecx, [ebx]
    and     ecx, [esi+8]
    cmp     ecx, [esi+12]
    je      NC_Exec_Done
    inc     eax
    cmp     eax, NC_WAIT_MMIO_LIMIT
    jb      short NC_Exec_Wait_Mmio_Next
    or      NCResult[4], NC_ST_TIMEOUT
    mov     eax, NC_TIMEOUT
    jmp     NC_Exec_Done
NC_Exec_Not_Wait_Mmio:

    cmp     eax, NC_OP_WAIT_VRAM
    jne     short NC_Exec_Not_Wait_Vram
    mov     ecx, 1
    call    NC_Vram_Allowed
    jc      NC_Exec_Refused
    mov     ebx, NCWindow
    add     ebx, edx
    mov     edx, [esi+8]
    mov     ecx, [esi+12]
    xor     eax, eax
NC_Exec_Wait_Vram_Next:
    cmp     [ebx], edx
    je      NC_Exec_Done
    inc     eax
    cmp     eax, ecx
    jb      short NC_Exec_Wait_Vram_Next
    or      NCResult[4], NC_ST_TIMEOUT
    mov     eax, NC_TIMEOUT
    jmp     NC_Exec_Done
NC_Exec_Not_Wait_Vram:

    ; The sysmem flush page (fb\nv50.c nv50_fb_sysmem_flush_page_init):
    ; the VBIOS leaves 100C08h at 0, host physical address 0. When it is
    ; 0, one fixed, zeroed, contiguous page below 4 GiB is allocated and
    ; its address >> 8 written there. The page is never freed: the GPU may
    ; keep using it after this VxD unloads. Result: 100C08h afterwards.
    cmp     eax, NC_OP_FLUSH_PAGE
    jne     short NC_Exec_Not_Flush_Page
    mov     ebx, NCMmio
    mov     eax, [ebx+NC_FLUSH_PAGE_REG]
    test    eax, eax
    jnz     NC_Exec_Done
    push    esi
    VMMcall _PageAllocate,<1,PG_SYS,0,0,0,NC_PAGE_MAX_PHYS,<OFFSET32 NCFlushPhys>,PAGEZEROINIT+PAGEUSEALIGN+PAGECONTIG+PAGEFIXED>
    pop     esi
    test    eax, eax
    jz      NC_Exec_Refused
    mov     eax, NCFlushPhys
    test    eax, 0fffh
    jnz     NC_Exec_Refused
    shr     eax, 8
    mov     ebx, NCMmio
    mov     [ebx+NC_FLUSH_PAGE_REG], eax
    mov     eax, [ebx+NC_FLUSH_PAGE_REG]
    jmp     NC_Exec_Done
NC_Exec_Not_Flush_Page:

    cmp     eax, NC_OP_DELAY
    jne     short NC_Exec_Refused
    mov     ecx, edx
    test    ecx, ecx
    jz      short NC_Exec_Refused
    cmp     ecx, NC_DELAY_MAX
    ja      short NC_Exec_Refused
    mov     ebx, NCMmio
NC_Exec_Delay_Next:
    mov     eax, [ebx+NC_PTIMER_TIME]
    dec     ecx
    jnz     short NC_Exec_Delay_Next

NC_Exec_Done:
    pop     edi
    pop     edx
    pop     ecx
    pop     ebx
    clc
    ret
NC_Exec_Refused:
    pop     edi
    pop     edx
    pop     ecx
    pop     ebx
    stc
    ret
EndProc NC_Execute

; EBP = DIOC input buffer, ECX = its byte count.
BeginProc NC_Run
    pushad
    cmp     ecx, NC_IN_BYTES
    jb      short NC_Run_Refused
    mov     eax, [ebp+NC_IN_COUNT]
    cmp     eax, NC_OP_MAX
    ja      short NC_Run_Refused
    mov     eax, [ebp+NC_IN_DATA_COUNT]
    cmp     eax, NC_DATA_DWORDS
    ja      short NC_Run_Refused
    mov     NCDataCount, eax
    lea     eax, [ebp+NC_IN_DATA]
    mov     NCData, eax
    call    NC_Map
    jc      short NC_Run_Done

    mov     ecx, [ebp+NC_IN_COUNT]
    lea     esi, [ebp+NC_IN_OPS]
    mov     edi, OFFSET32 NCOpResults
    xor     ebx, ebx
    jecxz   short NC_Run_Complete
NC_Run_Next:
    call    NC_Execute
    jc      short NC_Run_Refused_At
    mov     [edi], eax
    inc     ebx
    mov     NCResult[28], ebx
    add     esi, NC_OP_BYTES
    add     edi, 4
    dec     ecx
    jnz     short NC_Run_Next
NC_Run_Complete:
    or      NCResult[4], NC_ST_COMPLETE
    jmp     short NC_Run_Done
NC_Run_Refused_At:
    mov     NCResult[32], ebx
NC_Run_Refused:
    or      NCResult[4], NC_ST_REFUSED
NC_Run_Done:
    popad
    ret
EndProc NC_Run

BeginProc NC_Reset_Result
    pushad
    mov     edi, OFFSET32 NCResult + 4
    mov     ecx, NC_RESULT_DWORDS - 1
    xor     eax, eax
    cld
    rep     stosd
    popad
    ret
EndProc NC_Reset_Result

BeginProc NC_W32_DeviceIoControl
    cmp     ecx, DIOC_OPEN
    je      NC_Dioc_Ok
    cmp     ecx, DIOC_CLOSEHANDLE
    je      NC_Dioc_Ok
    cmp     ecx, NC_DIOC_RUN
    jne     NC_Dioc_Fail

    pushad
    mov     ebx, esi
    mov     ebp, [ebx.lpvInBuffer]
    test    ebp, ebp
    jz      short NC_Dioc_Copy_Fail
    mov     ecx, [ebx.cbInBuffer]
    cmp     ecx, NC_IN_OPS
    jb      short NC_Dioc_Copy_Fail
    mov     edi, [ebx.lpvOutBuffer]
    test    edi, edi
    jz      short NC_Dioc_Copy_Fail
    cmp     [ebx.cbOutBuffer], NC_RESULT_DWORDS * 4
    jb      short NC_Dioc_Copy_Fail

    call    NC_Reset_Result
    mov     ecx, [ebx.cbInBuffer]
    call    NC_Run

    mov     edi, [ebx.lpvOutBuffer]
    mov     esi, OFFSET32 NCResult
    mov     ecx, NC_RESULT_DWORDS
    cld
    rep     movsd
    mov     eax, [ebx.lpcbBytesReturned]
    test    eax, eax
    jz      short NC_Dioc_Copy_Done
    mov     dword ptr [eax], NC_RESULT_DWORDS * 4
NC_Dioc_Copy_Done:
    popad
NC_Dioc_Ok:
    xor     eax, eax
    ret

NC_Dioc_Copy_Fail:
    popad
NC_Dioc_Fail:
    mov     eax, 1
    ret
EndProc NC_W32_DeviceIoControl

BeginProc NC_Dynamic_Init
    clc
    ret
EndProc NC_Dynamic_Init

BeginProc NC_Dynamic_Exit
    clc
    ret
EndProc NC_Dynamic_Exit

Begin_Control_Dispatch NC
    Control_Dispatch Sys_Dynamic_Device_Init, NC_Dynamic_Init
    Control_Dispatch Sys_Dynamic_Device_Exit, NC_Dynamic_Exit
    Control_Dispatch W32_DeviceIoControl, NC_W32_DeviceIoControl
End_Control_Dispatch NC

VxD_LOCKED_CODE_ENDS

end
