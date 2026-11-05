; ATI Radeon X1200 IGP (RS690) 3D engine draw probe VxD.
;
; Phase 5 of docs\plans\ati-rs690-x1200.md, first step. Loaded dynamically
; by V9XRS3D.EXE, which decides every access; this VxD only executes an op
; list against the chip and refuses anything outside it. The 2D write probe
; (rs690_2d.asm) is the model, with the write windows moved to the 3D
; engine and two waits added. It finds 1002:791F with configuration
; mechanism 1, maps BAR2 (64 KiB of registers) and the first 32 MiB of
; BAR0 (the frame buffer) once, refusing either BAR if it is 64-bit with a
; non-zero high half, checks that CONFIG_MEMSIZE answers through the
; mapping (the RS690 keeps no configuration copy at 0F00h:
; docs\decisions\2026-11-04-rs690-register-probe.md), and runs:
;
;   MMIO write32      - the 2D engine registers 1400h-16FCh (for a fill
;                       after the 3D tests), DST_PIPE_CONFIG (170Ch),
;                       WAIT_UNTIL (1720h), the vertex port SE_PORT_DATA0
;                       (2000h), the VAP 2080h-22FFh and the 3D state
;                       4000h-4FFFh. Aligned.
;   MMIO read32       - CONFIG_MEMSIZE (00F8h), HDP_FB_LOCATION (0134h),
;                       CP_CSQ_CNTL (0740h), RBBM_STATUS (0E40h), the 2D
;                       block 1400h-17BFh less WAIT_UNTIL (1720h), D1's
;                       CRTC and graphics blocks 6000h-614Fh,
;                       VAP_CNTL_STATUS (2140h), GB_TILE_CONFIG (4018h),
;                       GB_PIPE_SELECT (402Ch), RB3D_DSTCACHE_CTLSTAT
;                       (4E4Ch) and ZB_ZCACHE_CTLSTAT (4F18h). Aligned.
;   LFB write/read    - BAR0, dword-aligned offsets below 32 MiB.
;   LFB fill          - a dword run of one value.
;   LFB gradient      - a dword run where dword i holds 2i and 2i+1 in its
;                       two halves.
;   LFB read block    - up to 1024 dwords into the result's block area.
;   wait idle         - RBBM_STATUS: 64 free FIFO entries and the engine
;                       not active, then the 2D destination cache flushed
;                       and freed (DSTCACHE_CTLSTAT bits 0 and 2 set, bit
;                       31 polled clear), as xf86-video-ati 6.14.6's
;                       RADEONWaitForIdleMMIO and the R300 branch of
;                       RADEONEngineFlush. At most RS_WAIT_LIMIT reads each.
;   wait FIFO         - RBBM_STATUS shows at least a free FIFO entries.
;                       Every run of writes waits for its length first, as
;                       xf86-video-ati's BEGIN_ACCEL does; a write into a
;                       full FIFO is what this avoids.
;
; The 3D engine's idle is the same wait idle: the builders end a draw with
; an RB3D cache flush and WAIT_UNTIL 3D idle-clean in the FIFO
; (RadeonFinishComposite), so an empty FIFO and an inactive engine mean
; the draw has landed. No 3D cache busy bit is polled; neither source
; names one.
;
; A wait that runs out of reads stops the op list there, so nothing more
; is written to an engine that has stopped taking writes.
;
; It never writes the CP's registers, the host data ports, the memory
; controller's index, or the display controller. Offsets: xf86-video-ati
; 6.14.6 radeon_reg.h, Mesa 22.3 r300_reg.h, Linux rs690d.h.

.386p

.xlist
include VMM.INC
include VWIN32.INC
.list

Declare_Virtual_Device RS3D, 1, 0, RS_Control, \
                       Undefined_Device_ID, Undefined_Init_Order, , ,

RS_MAGIC                equ 44335352h ; "RS3D"
RS_DIOC_RUN             equ 1
RS_VENDOR_ATI           equ 1002h
RS_DEVICE_RS690         equ 791fh
RS_OP_MAX               equ 512
RS_BLOCK_DWORDS         equ 1024
RS_WAIT_LIMIT           equ 1000000
RS_TIMEOUT              equ 0ffffffffh

RS_MMIO_BYTES           equ 00010000h
RS_MMIO_ALIGN           equ 0000ffffh
RS_LFB_BYTES            equ 02000000h
RS_LFB_ALIGN            equ 03ffffffh ; CONFIG_APER_SIZE reads 64 MiB
RS_BAR_TYPE_MASK        equ 00000006h
RS_BAR_TYPE_64          equ 00000004h
RS_CONFIG_MEMSIZE       equ 00f8h
RS_MEMSIZE_MAX          equ 20000000h

RS_RBBM_STATUS          equ 0e40h
RS_RBBM_FIFO_MASK       equ 0000007fh
RS_RBBM_FIFO_SLOTS      equ 64
RS_RBBM_ACTIVE          equ 80000000h
RS_DSTCACHE_CTLSTAT     equ 1714h
RS_DC_FLUSH_ALL         equ 00000005h
RS_DC_BUSY              equ 80000000h

; Op codes. Must match rs690_3d_win32.c.
RS_OP_MMIO_WRITE32      equ 1
RS_OP_MMIO_READ32       equ 2
RS_OP_LFB_WRITE32       equ 3
RS_OP_LFB_READ32        equ 4
RS_OP_LFB_FILL32        equ 5
RS_OP_LFB_GRADIENT      equ 6
RS_OP_LFB_READ_BLOCK    equ 7
RS_OP_WAIT_IDLE         equ 8
RS_OP_WAIT_FIFO         equ 9
RS_OP_PHYS_READ_BLOCK   equ 10
RS_PHYS_WINDOW_BASE     equ 05c00000h
RS_PHYS_WINDOW_BYTES    equ 00800000h

; Status bits.
RS_PCI_FOUND            equ 00000001h
RS_MAPPED               equ 00000002h
RS_RAN                  equ 00000004h
RS_REFUSED              equ 00000008h
RS_IDENTITY_MATCH       equ 00000010h

; One op: code, a, b, c, result - five dwords.
RS_OP_DWORDS            equ 5
RS_IN_MIN_BYTES         equ 4
RS_HEADER_DWORDS        equ 8
RS_RESULT_DWORDS        equ (RS_HEADER_DWORDS + RS_OP_MAX * RS_OP_DWORDS + \
                             RS_BLOCK_DWORDS)

VxD_LOCKED_DATA_SEG
; Readable windows: start, limit (exclusive).
RSReadWindows label dword
    dd 0078h, 0080h     ; MC_INDEX, MC_DATA (MC registers, read only)
    dd 00f8h, 00fch     ; CONFIG_MEMSIZE
    dd 0134h, 0138h     ; HDP_FB_LOCATION
    dd 0740h, 0744h     ; CP_CSQ_CNTL
    dd 0e40h, 0e44h     ; RBBM_STATUS
    dd 1400h, 1720h     ; the 2D block below WAIT_UNTIL
    dd 1724h, 17c0h     ; DSTCACHE_*, the rest below HOST_DATA0
    dd 6000h, 6150h     ; D1CRTC_*, D1GRPH_*
    dd 2140h, 2144h     ; VAP_CNTL_STATUS
    dd 3428h, 342ch     ; RB2D_DSTCACHE_MODE
    dd 4018h, 401ch     ; GB_TILE_CONFIG
    dd 402ch, 4030h     ; GB_PIPE_SELECT
    dd 4e4ch, 4e50h     ; RB3D_DSTCACHE_CTLSTAT
    dd 4f18h, 4f1ch     ; ZB_ZCACHE_CTLSTAT
RS_READ_WINDOW_COUNT    equ 14

; Writable windows: start, limit (exclusive). MC_INDEX takes an index
; only: a value with MC_IND_WR_EN (bit 9) is refused, so the memory
; controller's registers can be read through MC_DATA and never written
; (Linux rs690_mc_rreg's sequence).
RS_MC_INDEX             equ 0078h
RS_MC_IND_WR_EN         equ 0200h
RSWriteWindows label dword
    dd 0078h, 007ch     ; MC_INDEX
    dd 1400h, 1700h     ; the 2D engine
    dd 170ch, 1710h     ; DST_PIPE_CONFIG
    dd 1714h, 1718h     ; DSTCACHE_CTLSTAT (eng_rs690.c's 2D flush)
    dd 1720h, 1724h     ; WAIT_UNTIL
    dd 2000h, 2004h     ; SE_PORT_DATA0
    dd 2080h, 2300h     ; VAP
    dd 3428h, 342ch     ; RB2D_DSTCACHE_MODE (Linux r420_pipes_init)
    dd 4000h, 5000h     ; GB, GA, SU, RS, SC, US, FG, RB3D, ZB
RS_WRITE_WINDOW_COUNT   equ 9

RSResult label dword
    dd RS_MAGIC                 ; 00 magic
    dd 0                        ; 01 status
    dd 0                        ; 02 BAR0 physical (frame buffer)
    dd 0                        ; 03 BAR2 physical (registers)
    dd 0                        ; 04 ops executed
    dd 0                        ; 05 index of the refused op, or count
    dd 0                        ; 06 id dword from configuration space
    dd 0                        ; 07 CONFIG_MEMSIZE through BAR2
RSOps   dd (RS_OP_MAX * RS_OP_DWORDS) dup (0)
RSBlock dd RS_BLOCK_DWORDS dup (0)
RSLfbLinear  dd 0
RSMmioLinear dd 0
RSPhysLinear dd 0
RSLfbPhys    dd 0
RSMmioPhys   dd 0
RSCount      dd 0
VxD_LOCKED_DATA_ENDS

VxD_LOCKED_CODE_SEG
; EAX = PCI configuration address. Returns EAX = configuration dword, with
; 0CF8h restored and interrupts off around the cycle.
BeginProc RS_Pci_Read
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
EndProc RS_Pci_Read

; EAX = configuration id dword. Returns CF clear for 1002:791F.
BeginProc RS_Device_Known
    cmp     eax, (RS_DEVICE_RS690 shl 16) or RS_VENDOR_ATI
    jne     short RS_Device_Unknown
    clc
    ret
RS_Device_Unknown:
    stc
    ret
EndProc RS_Device_Known

; EAX = the BAR's configuration address. Returns CF clear and EAX = the
; base for a memory BAR below 4 GiB: I/O refused, and a 64-bit BAR whose
; high half (the next dword) is not zero refused, since mapping its low
; half would map some other range.
BeginProc RS_Bar_Base
    push    ebx
    mov     ebx, eax
    call    RS_Pci_Read
    test    eax, 1
    jnz     short RS_Bar_Bad
    push    eax
    and     eax, RS_BAR_TYPE_MASK
    cmp     eax, RS_BAR_TYPE_64
    pop     eax
    jne     short RS_Bar_Low
    push    eax
    lea     eax, [ebx+4]
    call    RS_Pci_Read
    test    eax, eax
    pop     eax
    jnz     short RS_Bar_Bad
RS_Bar_Low:
    and     eax, 0fffffff0h
    pop     ebx
    clc
    ret
RS_Bar_Bad:
    pop     ebx
    stc
    ret
EndProc RS_Bar_Base

; Find the chip on buses 0-7 and map both BARs once. Sets RS_MAPPED on
; success. A BAR that moved since the first mapping is refused rather than
; mapped twice.
BeginProc RS_Map
    pushad
    mov     ebx, 80000000h
RS_Map_Next:
    mov     eax, ebx
    call    RS_Pci_Read
    mov     RSResult[24], eax
    call    RS_Device_Known
    jnc     short RS_Map_Found
    add     ebx, 0800h
    cmp     ebx, 80080000h
    jb      short RS_Map_Next
    jmp     RS_Map_Done

RS_Map_Found:
    or      RSResult[4], RS_PCI_FOUND
    lea     eax, [ebx+10h]
    call    RS_Bar_Base
    jc      RS_Map_Done
    mov     esi, eax
    lea     eax, [ebx+18h]
    call    RS_Bar_Base
    jc      RS_Map_Done
    mov     edi, eax
    mov     RSResult[8], esi
    mov     RSResult[12], edi

    test    esi, esi
    jz      RS_Map_Done
    test    esi, RS_LFB_ALIGN
    jnz     RS_Map_Done
    test    edi, edi
    jz      RS_Map_Done
    test    edi, RS_MMIO_ALIGN
    jnz     RS_Map_Done

    cmp     RSLfbLinear, 0
    je      short RS_Map_Fresh
    cmp     esi, RSLfbPhys
    jne     short RS_Map_Done
    cmp     edi, RSMmioPhys
    jne     short RS_Map_Done
    jmp     short RS_Map_Identity

RS_Map_Fresh:
    VMMcall _MapPhysToLinear,<edi,RS_MMIO_BYTES,0>
    cmp     eax, 0ffffffffh
    je      short RS_Map_Done
    mov     RSMmioLinear, eax
    mov     RSMmioPhys, edi
    VMMcall _MapPhysToLinear,<esi,RS_LFB_BYTES,0>
    cmp     eax, 0ffffffffh
    je      short RS_Map_Unmapped
    mov     RSLfbLinear, eax
    mov     RSLfbPhys, esi

    ; The register window must answer with a plausible memory size, or it
    ; is not this chip's and nothing runs against it. A window that decodes
    ; nothing reads FFFFFFFFh.
RS_Map_Identity:
    mov     eax, RSMmioLinear
    mov     eax, [eax+RS_CONFIG_MEMSIZE]
    mov     RSResult[28], eax
    test    eax, eax
    jz      short RS_Map_Done
    cmp     eax, RS_MEMSIZE_MAX
    ja      short RS_Map_Done
    or      RSResult[4], RS_IDENTITY_MATCH or RS_MAPPED
    jmp     short RS_Map_Done
RS_Map_Unmapped:
    mov     RSMmioLinear, 0
RS_Map_Done:
    popad
    ret
EndProc RS_Map

; EBX = offset, ECX = dword count. Returns CF set unless the whole run is
; dword aligned and inside the frame buffer window.
BeginProc RS_Check_Lfb_Run
    push    eax
    test    ebx, 3
    jnz     short RS_Check_Lfb_Bad
    cmp     ebx, RS_LFB_BYTES
    jae     short RS_Check_Lfb_Bad
    cmp     ecx, RS_LFB_BYTES / 4
    ja      short RS_Check_Lfb_Bad
    mov     eax, ecx
    shl     eax, 2
    add     eax, ebx
    cmp     eax, RS_LFB_BYTES
    ja      short RS_Check_Lfb_Bad
    pop     eax
    clc
    ret
RS_Check_Lfb_Bad:
    pop     eax
    stc
    ret
EndProc RS_Check_Lfb_Run

; EBX = register offset. CF clear if it lies in a writable window.
BeginProc RS_Write_Allowed
    push    ecx
    push    esi
    test    ebx, 3
    jnz     short RS_Write_Refused
    mov     esi, OFFSET32 RSWriteWindows
    mov     ecx, RS_WRITE_WINDOW_COUNT
RS_Write_Next:
    cmp     ebx, [esi]
    jb      short RS_Write_Skip
    cmp     ebx, [esi+4]
    jb      short RS_Write_Ok
RS_Write_Skip:
    add     esi, 8
    dec     ecx
    jnz     short RS_Write_Next
RS_Write_Refused:
    pop     esi
    pop     ecx
    stc
    ret
RS_Write_Ok:
    pop     esi
    pop     ecx
    clc
    ret
EndProc RS_Write_Allowed

; EBX = register offset. CF clear if it lies in a readable window.
BeginProc RS_Read_Allowed
    push    ecx
    push    esi
    test    ebx, 3
    jnz     short RS_Read_Refused
    mov     esi, OFFSET32 RSReadWindows
    mov     ecx, RS_READ_WINDOW_COUNT
RS_Read_Next:
    cmp     ebx, [esi]
    jb      short RS_Read_Skip
    cmp     ebx, [esi+4]
    jb      short RS_Read_Ok
RS_Read_Skip:
    add     esi, 8
    dec     ecx
    jnz     short RS_Read_Next
RS_Read_Refused:
    pop     esi
    pop     ecx
    stc
    ret
RS_Read_Ok:
    pop     esi
    pop     ecx
    clc
    ret
EndProc RS_Read_Allowed

; ESI = op (code, a, b, c, result). Returns CF set if the op is refused.
BeginProc RS_Execute_One
    pushad
    mov     eax, [esi]
    mov     ebx, [esi+4]
    mov     ecx, [esi+8]
    mov     edx, [esi+12]

    cmp     eax, RS_OP_MMIO_WRITE32
    je      RS_Do_Mmio_Write32
    cmp     eax, RS_OP_MMIO_READ32
    je      RS_Do_Mmio_Read32
    cmp     eax, RS_OP_LFB_WRITE32
    je      RS_Do_Lfb_Write32
    cmp     eax, RS_OP_LFB_READ32
    je      RS_Do_Lfb_Read32
    cmp     eax, RS_OP_LFB_FILL32
    je      RS_Do_Lfb_Fill32
    cmp     eax, RS_OP_LFB_GRADIENT
    je      RS_Do_Lfb_Gradient
    cmp     eax, RS_OP_LFB_READ_BLOCK
    je      RS_Do_Lfb_Read_Block
    cmp     eax, RS_OP_WAIT_IDLE
    je      RS_Do_Wait_Idle
    cmp     eax, RS_OP_WAIT_FIFO
    je      RS_Do_Wait_Fifo
    cmp     eax, RS_OP_PHYS_READ_BLOCK
    je      RS_Do_Phys_Read_Block
    jmp     RS_Do_Refuse

; a = byte offset into the physical window, c = dword count (at most the
; block). Read only: the window is the system RAM the T15 hunt found the
; second Direct3D process's pixels in
; (docs\issues\2026-11-05-rs690-t15-heap-words-zeroed-once.md). Mapped once,
; on first use; nothing in it is ever written.
RS_Do_Phys_Read_Block:
    cmp     edx, RS_BLOCK_DWORDS
    ja      RS_Do_Refuse
    test    ebx, 3
    jnz     RS_Do_Refuse
    cmp     ebx, RS_PHYS_WINDOW_BYTES
    jae     RS_Do_Refuse
    mov     eax, RS_PHYS_WINDOW_BYTES
    sub     eax, ebx
    shr     eax, 2
    cmp     edx, eax
    ja      RS_Do_Refuse
    cmp     RSPhysLinear, 0
    jne     short RS_Do_Phys_Mapped
    push    ebx
    push    edx
    VMMcall _MapPhysToLinear,<RS_PHYS_WINDOW_BASE,RS_PHYS_WINDOW_BYTES,0>
    pop     edx
    pop     ebx
    cmp     eax, 0ffffffffh
    je      RS_Do_Refuse
    mov     RSPhysLinear, eax
RS_Do_Phys_Mapped:
    push    esi
    mov     esi, RSPhysLinear
    add     esi, ebx
    mov     edi, OFFSET32 RSBlock
    mov     ecx, edx
    cld
    rep     movsd
    pop     esi
    jmp     RS_Do_Done

; a = free entries wanted, 1 to 64. Result: reads taken, or RS_TIMEOUT,
; which refuses the op so that nothing after it is written.
RS_Do_Wait_Fifo:
    test    ebx, ebx
    jz      RS_Do_Refuse
    cmp     ebx, RS_RBBM_FIFO_SLOTS
    ja      RS_Do_Refuse
    mov     edi, RSMmioLinear
    xor     edx, edx
RS_Do_Wait_Fifo_Next:
    inc     edx
    mov     eax, [edi+RS_RBBM_STATUS]
    cmp     eax, 0ffffffffh
    je      short RS_Do_Wait_Fifo_Timeout
    and     eax, RS_RBBM_FIFO_MASK
    cmp     eax, ebx
    jae     short RS_Do_Wait_Fifo_Done
    cmp     edx, RS_WAIT_LIMIT
    jb      short RS_Do_Wait_Fifo_Next
RS_Do_Wait_Fifo_Timeout:
    mov     dword ptr [esi+16], RS_TIMEOUT
    jmp     RS_Do_Refuse
RS_Do_Wait_Fifo_Done:
    mov     [esi+16], edx
    jmp     RS_Do_Done

; a = register offset, b = value.
RS_Do_Mmio_Write32:
    call    RS_Write_Allowed
    jc      RS_Do_Refuse
    cmp     ebx, RS_MC_INDEX
    jne     short RS_Do_Mmio_Write32_Go
    test    ecx, RS_MC_IND_WR_EN
    jnz     RS_Do_Refuse
RS_Do_Mmio_Write32_Go:
    mov     edi, RSMmioLinear
    mov     [edi+ebx], ecx
    jmp     RS_Do_Done

; a = register offset.
RS_Do_Mmio_Read32:
    call    RS_Read_Allowed
    jc      RS_Do_Refuse
    mov     edi, RSMmioLinear
    mov     eax, [edi+ebx]
    mov     [esi+16], eax
    jmp     RS_Do_Done

; a = offset, b = value.
RS_Do_Lfb_Write32:
    push    ecx
    mov     ecx, 1
    call    RS_Check_Lfb_Run
    pop     ecx
    jc      RS_Do_Refuse
    mov     edi, RSLfbLinear
    mov     [edi+ebx], ecx
    jmp     RS_Do_Done

; a = offset.
RS_Do_Lfb_Read32:
    mov     ecx, 1
    call    RS_Check_Lfb_Run
    jc      RS_Do_Refuse
    mov     edi, RSLfbLinear
    mov     eax, [edi+ebx]
    mov     [esi+16], eax
    jmp     RS_Do_Done

; a = offset, b = value, c = dword count.
RS_Do_Lfb_Fill32:
    push    ecx
    mov     ecx, edx
    call    RS_Check_Lfb_Run
    pop     eax
    jc      RS_Do_Refuse
    mov     edi, RSLfbLinear
    add     edi, ebx
    mov     ecx, edx
    cld
    rep     stosd
    jmp     RS_Do_Done

; a = offset, c = dword count. Dword i holds 2i (low half) and 2i+1 (high
; half), counted from the start of the run.
RS_Do_Lfb_Gradient:
    mov     ecx, edx
    call    RS_Check_Lfb_Run
    jc      RS_Do_Refuse
    mov     edi, RSLfbLinear
    add     edi, ebx
    xor     eax, eax
RS_Do_Lfb_Gradient_Next:
    jecxz   short RS_Do_Lfb_Gradient_Done
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
    jmp     short RS_Do_Lfb_Gradient_Next
RS_Do_Lfb_Gradient_Done:
    jmp     RS_Do_Done

; a = offset, c = dword count (at most the block). Copies into the block.
RS_Do_Lfb_Read_Block:
    cmp     edx, RS_BLOCK_DWORDS
    ja      RS_Do_Refuse
    mov     ecx, edx
    call    RS_Check_Lfb_Run
    jc      RS_Do_Refuse
    push    esi
    mov     esi, RSLfbLinear
    add     esi, ebx
    mov     edi, OFFSET32 RSBlock
    mov     ecx, edx
    cld
    rep     movsd
    pop     esi
    jmp     RS_Do_Done

; Result: reads taken in all, or RS_TIMEOUT.
RS_Do_Wait_Idle:
    mov     edi, RSMmioLinear
    xor     edx, edx
RS_Do_Wait_Idle_Gui:
    inc     edx
    mov     eax, [edi+RS_RBBM_STATUS]
    cmp     eax, 0ffffffffh
    je      short RS_Do_Wait_Idle_Timeout
    test    eax, RS_RBBM_ACTIVE
    jnz     short RS_Do_Wait_Idle_Gui_Again
    and     eax, RS_RBBM_FIFO_MASK
    cmp     eax, RS_RBBM_FIFO_SLOTS
    jae     short RS_Do_Wait_Idle_Flush
RS_Do_Wait_Idle_Gui_Again:
    cmp     edx, RS_WAIT_LIMIT
    jb      short RS_Do_Wait_Idle_Gui
    jmp     short RS_Do_Wait_Idle_Timeout
RS_Do_Wait_Idle_Flush:
    mov     eax, [edi+RS_DSTCACHE_CTLSTAT]
    or      eax, RS_DC_FLUSH_ALL
    mov     [edi+RS_DSTCACHE_CTLSTAT], eax
    xor     ecx, ecx
RS_Do_Wait_Idle_Cache:
    inc     ecx
    mov     eax, [edi+RS_DSTCACHE_CTLSTAT]
    test    eax, RS_DC_BUSY
    jz      short RS_Do_Wait_Idle_Done
    cmp     ecx, RS_WAIT_LIMIT
    jb      short RS_Do_Wait_Idle_Cache
; A timeout refuses the op, unlike the 2D probe's: after a 3D draw it means
; the engine did not finish, and nothing more should be written to it.
RS_Do_Wait_Idle_Timeout:
    mov     dword ptr [esi+16], RS_TIMEOUT
    jmp     short RS_Do_Refuse
RS_Do_Wait_Idle_Done:
    add     edx, ecx
    mov     [esi+16], edx
    jmp     short RS_Do_Done

RS_Do_Refuse:
    popad
    stc
    ret
RS_Do_Done:
    popad
    clc
    ret
EndProc RS_Execute_One

BeginProc RS_Run
    pushad
    call    RS_Map
    test    RSResult[4], RS_MAPPED
    jz      short RS_Run_Done
    mov     ecx, RSCount
    mov     RSResult[20], ecx
    mov     esi, OFFSET32 RSOps
    xor     ebx, ebx
    jecxz   short RS_Run_All
RS_Run_Next:
    call    RS_Execute_One
    jc      short RS_Run_Refused
    add     esi, RS_OP_DWORDS * 4
    inc     ebx
    cmp     ebx, ecx
    jb      short RS_Run_Next
RS_Run_All:
    or      RSResult[4], RS_RAN
    jmp     short RS_Run_Count
RS_Run_Refused:
    or      RSResult[4], RS_REFUSED
    mov     RSResult[20], ebx
RS_Run_Count:
    mov     RSResult[16], ebx
RS_Run_Done:
    popad
    ret
EndProc RS_Run

BeginProc RS_W32_DeviceIoControl
    cmp     ecx, DIOC_OPEN
    je      RS_Dioc_Ok
    cmp     ecx, DIOC_CLOSEHANDLE
    je      RS_Dioc_Ok
    cmp     ecx, RS_DIOC_RUN
    jne     RS_Dioc_Fail

    pushad
    mov     ebx, esi
    mov     ebp, [ebx.lpvInBuffer]
    test    ebp, ebp
    jz      RS_Dioc_Copy_Fail
    mov     ecx, [ebx.cbInBuffer]
    cmp     ecx, RS_IN_MIN_BYTES
    jb      RS_Dioc_Copy_Fail
    mov     edi, [ebx.lpvOutBuffer]
    test    edi, edi
    jz      RS_Dioc_Copy_Fail
    cmp     [ebx.cbOutBuffer], RS_RESULT_DWORDS * 4
    jb      RS_Dioc_Copy_Fail

    ; The count must fit both the op table and the bytes supplied.
    mov     eax, [ebp]
    cmp     eax, RS_OP_MAX
    ja      RS_Dioc_Copy_Fail
    mov     edx, eax
    imul    edx, RS_OP_DWORDS * 4
    add     edx, 4
    cmp     ecx, edx
    jb      RS_Dioc_Copy_Fail
    mov     RSCount, eax

    mov     RSResult[4], 0
    mov     RSResult[16], 0
    mov     RSResult[20], 0
    lea     esi, [ebp+4]
    mov     edi, OFFSET32 RSOps
    mov     ecx, eax
    imul    ecx, RS_OP_DWORDS
    cld
    rep     movsd

    call    RS_Run

    mov     edi, [ebx.lpvOutBuffer]
    mov     esi, OFFSET32 RSResult
    mov     ecx, RS_RESULT_DWORDS
    cld
    rep     movsd
    mov     eax, [ebx.lpcbBytesReturned]
    test    eax, eax
    jz      short RS_Dioc_Copy_Done
    mov     dword ptr [eax], RS_RESULT_DWORDS * 4
RS_Dioc_Copy_Done:
    popad
RS_Dioc_Ok:
    xor     eax, eax
    ret

RS_Dioc_Copy_Fail:
    popad
RS_Dioc_Fail:
    mov     eax, 1
    ret
EndProc RS_W32_DeviceIoControl

BeginProc RS_Dynamic_Init
    clc
    ret
EndProc RS_Dynamic_Init

BeginProc RS_Dynamic_Exit
    clc
    ret
EndProc RS_Dynamic_Exit

Begin_Control_Dispatch RS
    Control_Dispatch Sys_Dynamic_Device_Init, RS_Dynamic_Init
    Control_Dispatch Sys_Dynamic_Device_Exit, RS_Dynamic_Exit
    Control_Dispatch W32_DeviceIoControl, RS_W32_DeviceIoControl
End_Control_Dispatch RS

VxD_LOCKED_CODE_ENDS

end
