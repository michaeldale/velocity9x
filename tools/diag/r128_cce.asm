; ATI Rage 128 CCE (Concurrent Command Engine) probe VxD.
;
; Decision 1 of docs\plans\ati-rage128.md: does the CCE run packets fed
; through its PIO FIFO ports, with no ring and no bus mastering? Loaded
; dynamically by R128CCE.EXE, which decides every access; this VxD only
; executes an op list against the card and refuses anything outside it. It
; is R1282D.VXD (tools\diag\r128_2d.asm) with a wider write list, for the
; CCE, and two ops more. It finds the Rage 128 with configuration mechanism
; 1 (an id in RCDevices, the list R1MM.VXD checks), maps BAR2 (16 KiB of
; registers) and the first 32 MiB of BAR0 (the framebuffer) once, checks
; that the BAR's copy of configuration space at 0F00h returns the same id,
; and runs:
;
;   MMIO write32      - PC_NGUI_CTLSTAT (0184h); the PM4 block 0700h-071Fh
;                       (buffer control, pointers, FPU setup); the microcode
;                       address and data ports 07D4h-07E3h; PM4_MICRO_CNTL
;                       (07FCh); the PIO FIFO ports 1000h and 1004h;
;                       PC_GUI_CTLSTAT (1748h), the GUI pixel cache; the 2D
;                       engine registers 1400h-16FCh; SCALE_3D_CNTL (1A00h);
;                       the 3D context registers 1C80h-1D47h. Aligned.
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
;                       R128WaitForIdle. At most RC_WAIT_LIMIT reads each.
;   PM4 wait idle     - PM4_STAT: at least b free CCE FIFO entries, PM4_BUSY
;                       and GUI_ACTIVE clear, then the pixel cache flushed,
;                       as the Linux DRM's r128_do_cce_idle; then the GUI
;                       pixel cache's busy bit (PC_GUI_CTLSTAT bit 31)
;                       clear, after the flush the stream ends with.
;   engine reset      - r128_accel.c's R128EngineReset, fixed: flush the
;                       pixel cache; save CLOCK_CNTL_INDEX; force the GUI
;                       and 3D clocks on through MCLK_CNTL (PLL 0Fh); pulse
;                       GEN_RESET_CNTL bit 0 with read-backs; restore
;                       MCLK_CNTL, CLOCK_CNTL_INDEX and GEN_RESET_CNTL. The
;                       PLL index and data and GEN_RESET_CNTL are written
;                       only inside this op.
;
; It never writes HOST_DATA, the CRTC, the PLLs other than the MCLK_CNTL
; dance above, or anything outside the engine. Offsets: xf86-video-r128
; r128_reg.h.

.386p

.xlist
include VMM.INC
include VWIN32.INC
.list

Declare_Virtual_Device R128CCE, 1, 0, RC_Control, \
                       Undefined_Device_ID, Undefined_Init_Order, , ,

RC_MAGIC                equ 45434352h ; "RCCE"
RC_DIOC_RUN             equ 1
RC_VENDOR_ATI           equ 1002h
RC_OP_MAX               equ 512
RC_BLOCK_DWORDS         equ 1024
RC_WAIT_LIMIT           equ 1000000
RC_TIMEOUT              equ 0ffffffffh

RC_MMIO_BYTES           equ 00004000h
RC_MMIO_ALIGN           equ 00003fffh
RC_LFB_BYTES            equ 02000000h
RC_LFB_ALIGN            equ 03ffffffh ; BAR0 is a 64 MiB window
RC_PCI_COPY             equ 0f00h

RC_GUI_STAT             equ 1740h
RC_GUI_FIFO_MASK        equ 00000fffh
RC_GUI_FIFO_SLOTS       equ 64
RC_GUI_ACTIVE           equ 80000000h
RC_PC_NGUI_CTLSTAT      equ 0184h
RC_PC_FLUSH_ALL         equ 000000ffh
RC_PC_BUSY              equ 80000000h
RC_PM4_STAT             equ 07b8h
RC_PC_GUI_CTLSTAT       equ 1748h
RC_PM4_FIFO_MASK        equ 00000fffh
RC_PM4_BUSY_BITS        equ 80010000h ; GUI_ACTIVE | PM4_BUSY
RC_CLOCK_CNTL_INDEX     equ 0008h
RC_CLOCK_CNTL_DATA      equ 000ch
RC_PLL_WR_EN            equ 00000080h
RC_PLL_MCLK_CNTL        equ 0fh
RC_MCLK_FORCE           equ 00030000h ; FORCE_GCP | FORCE_PIPE3D_CP
RC_GEN_RESET_CNTL       equ 00f0h
RC_SOFT_RESET_GUI       equ 00000001h

; Op codes. Must match r128_cce_win32.c.
RC_OP_MMIO_WRITE32      equ 1
RC_OP_MMIO_READ32       equ 2
RC_OP_LFB_WRITE32       equ 3
RC_OP_LFB_READ32        equ 4
RC_OP_LFB_FILL32        equ 5
RC_OP_LFB_GRADIENT      equ 6
RC_OP_LFB_READ_BLOCK    equ 7
RC_OP_WAIT_IDLE         equ 8
RC_OP_PM4_WAIT_IDLE     equ 9
RC_OP_ENGINE_RESET      equ 10
RC_OP_MMIO_POLL         equ 11
RC_OP_RING_ALLOC        equ 12
RC_OP_RING_WRITE32      equ 13
RC_OP_RING_READ32       equ 14

; /gart: one locked, physically contiguous block - the Rage 128's 32 KiB
; PCI GART table (PCI_GART_PAGE, RRG 3-207: one dword a page, 8192 of
; them) and a 64 KiB ring after it. Every table entry points into the ring,
; so no CCE address can reach memory that is not ours; it only reads
; (NOUPDATE). Entries hold the page's bus address, as Linux's
; drm_ati_pcigart writes them for a PCI GART.
RC_OP_GART_ALLOC        equ 15
RC_OP_BLOCK_READ32      equ 16
RC_OP_RING_PATTERN      equ 17
RC_OP_PHYS_READ32       equ 18
RC_OP_PCI_CFG_READ      equ 19
; WBINVD: write back and invalidate the CPU caches. The 440BX does not snoop
; the card's AGP reads, so a ring dword still in a write-back line is not
; what the card fetches; the card's writes (the read-pointer write-back) are
; snooped, which is why those land.
RC_OP_CACHE_FLUSH       equ 20

; RC_OP_RING_PATTERN: the whole ring as pairs - a type-0 header for
; GUI_SCRATCH_REG0 (15E0h >> 2) and 5A000000h | the dword index of that
; value - so the register says which pair the CCE executed.
RC_PATTERN_HEADER       equ 00000578h
RC_PATTERN_TAG          equ 5A000000h
; Type 0, count - 1 = 15 (bits 29:16), ONE_REG_WR (bit 15), 15E0h >> 2.
RC_PATTERN_LONG_HEADER  equ 000F8578h

; /vgart: 16 pages of the chipset's AGP aperture through the VMM's GART
; services (_GARTReserve, _GARTCommit; VMM.INC), as ATI's driver keeps its
; ring behind AGP_BASE. pDevObj is passed as 0 first.
RC_VGART_PAGES          equ 16

RC_RING_PAGES           equ 24
RC_GART_ENTRIES         equ 8192
RC_GART_BYTES           equ 8000h
RC_RING_AREA            equ 10000h
RC_RING_PAGE_MASK       equ 15

; Status bits.
RC_PCI_FOUND            equ 00000001h
RC_MAPPED               equ 00000002h
RC_RAN                  equ 00000004h
RC_REFUSED              equ 00000008h
RC_IDENTITY_MATCH       equ 00000010h

; One op: code, a, b, c, result - five dwords.
RC_OP_DWORDS            equ 5
RC_IN_MIN_BYTES         equ 4
RC_HEADER_DWORDS        equ 8
RC_RESULT_DWORDS        equ (RC_HEADER_DWORDS + RC_OP_MAX * RC_OP_DWORDS + \
                             RC_BLOCK_DWORDS)

VxD_LOCKED_DATA_SEG
RCDevices label word
    dw 5245h, 5246h, 5247h, 524bh, 524ch
    dw 5345h, 5346h, 5347h, 5348h, 534bh, 534ch, 534dh, 534eh
    dw 5041h, 5042h, 5043h, 5044h, 5045h, 5046h, 5047h, 5048h
    dw 5049h, 504ah, 504bh, 504ch, 504dh, 504eh, 504fh, 5050h
    dw 5051h, 5052h, 5053h, 5054h, 5055h, 5056h, 5057h, 5058h
    dw 5446h, 544ch, 5452h, 5453h, 5454h, 5455h
    dw 4c45h, 4c46h, 4d46h, 4d4ch
RC_DEVICE_COUNT         equ 47

RCResult label dword
    dd RC_MAGIC                 ; 00 magic
    dd 0                        ; 01 status
    dd 0                        ; 02 BAR0 physical (framebuffer)
    dd 0                        ; 03 BAR2 physical (registers)
    dd 0                        ; 04 ops executed
    dd 0                        ; 05 index of the refused op, or count
    dd 0                        ; 06 id dword from configuration space
    dd 0                        ; 07 id dword at 0F00h through BAR2
RCOps   dd (RC_OP_MAX * RC_OP_DWORDS) dup (0)
RCBlock dd RC_BLOCK_DWORDS dup (0)
RCLfbLinear  dd 0
RCMmioLinear dd 0
RCLfbPhys    dd 0
RCMmioPhys   dd 0
RCCount      dd 0
RCRingHandle dd 0
RCRingLinear dd 0
RCRingPhys   dd 0
; Where the ring's dwords are: RCRingLinear + 8000h for the locked block,
; the reserved aperture for /vgart.
RCRingData   dd 0
RCGartLinear dd 0
RCGartDev    dd 0
RCGartCommitted dd 0
RCGartFlags  dd 0
VxD_LOCKED_DATA_ENDS

VxD_LOCKED_CODE_SEG

; EAX = PCI configuration address. Returns EAX = configuration dword, with
; 0CF8h restored and interrupts off around the cycle.
BeginProc RC_Pci_Read
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
EndProc RC_Pci_Read

; EAX = configuration id dword. Returns CF clear for a Rage 128.
BeginProc RC_Device_Known
    push    ecx
    push    esi
    cmp     ax, RC_VENDOR_ATI
    jne     short RC_Device_Unknown
    shr     eax, 16
    mov     esi, OFFSET32 RCDevices
    mov     ecx, RC_DEVICE_COUNT
RC_Device_Next:
    cmp     ax, [esi]
    je      short RC_Device_Found
    add     esi, 2
    dec     ecx
    jnz     short RC_Device_Next
RC_Device_Unknown:
    pop     esi
    pop     ecx
    stc
    ret
RC_Device_Found:
    pop     esi
    pop     ecx
    clc
    ret
EndProc RC_Device_Known

; Find the card on buses 0-7 and map both BARs once. Sets RC_MAPPED on
; success. A BAR that moved since the first mapping is refused rather than
; mapped twice.
BeginProc RC_Map
    pushad
    mov     ebx, 80000000h
RC_Map_Next:
    mov     eax, ebx
    call    RC_Pci_Read
    mov     RCResult[24], eax
    call    RC_Device_Known
    jnc     short RC_Map_Found
    add     ebx, 0800h
    cmp     ebx, 80080000h
    jb      short RC_Map_Next
    jmp     RC_Map_Done

RC_Map_Found:
    or      RCResult[4], RC_PCI_FOUND
    lea     eax, [ebx+10h]
    call    RC_Pci_Read
    test    eax, 1
    jnz     RC_Map_Done
    and     eax, 0fffffff0h
    mov     esi, eax
    lea     eax, [ebx+18h]
    call    RC_Pci_Read
    test    eax, 1
    jnz     RC_Map_Done
    and     eax, 0fffffff0h
    mov     edi, eax
    mov     RCResult[8], esi
    mov     RCResult[12], edi

    test    esi, esi
    jz      RC_Map_Done
    test    esi, RC_LFB_ALIGN
    jnz     RC_Map_Done
    test    edi, edi
    jz      RC_Map_Done
    test    edi, RC_MMIO_ALIGN
    jnz     RC_Map_Done

    cmp     RCLfbLinear, 0
    je      short RC_Map_Fresh
    cmp     esi, RCLfbPhys
    jne     short RC_Map_Done
    cmp     edi, RCMmioPhys
    jne     short RC_Map_Done
    jmp     short RC_Map_Identity

RC_Map_Fresh:
    VMMcall _MapPhysToLinear,<edi,RC_MMIO_BYTES,0>
    cmp     eax, 0ffffffffh
    je      short RC_Map_Done
    mov     RCMmioLinear, eax
    mov     RCMmioPhys, edi
    VMMcall _MapPhysToLinear,<esi,RC_LFB_BYTES,0>
    cmp     eax, 0ffffffffh
    je      short RC_Map_Unmapped
    mov     RCLfbLinear, eax
    mov     RCLfbPhys, esi

    ; The register window must answer with the configuration id, or it is
    ; not this card's and nothing runs against it.
RC_Map_Identity:
    mov     eax, RCMmioLinear
    mov     eax, [eax+RC_PCI_COPY]
    mov     RCResult[28], eax
    cmp     eax, RCResult[24]
    jne     short RC_Map_Done
    or      RCResult[4], RC_IDENTITY_MATCH or RC_MAPPED
    jmp     short RC_Map_Done
RC_Map_Unmapped:
    mov     RCMmioLinear, 0
RC_Map_Done:
    popad
    ret
EndProc RC_Map

; EBX = offset, ECX = dword count. Returns CF set unless the whole run is
; dword aligned and inside the framebuffer window.
BeginProc RC_Check_Lfb_Run
    push    eax
    test    ebx, 3
    jnz     short RC_Check_Lfb_Bad
    cmp     ebx, RC_LFB_BYTES
    jae     short RC_Check_Lfb_Bad
    cmp     ecx, RC_LFB_BYTES / 4
    ja      short RC_Check_Lfb_Bad
    mov     eax, ecx
    shl     eax, 2
    add     eax, ebx
    cmp     eax, RC_LFB_BYTES
    ja      short RC_Check_Lfb_Bad
    pop     eax
    clc
    ret
RC_Check_Lfb_Bad:
    pop     eax
    stc
    ret
EndProc RC_Check_Lfb_Run

; EBX = register offset. CF clear if a write is allowed.
BeginProc RC_Write_Allowed
    test    ebx, 3
    jnz     RC_Write_Refused
    cmp     ebx, RC_PC_NGUI_CTLSTAT
    je     RC_Write_Ok
    ; /loc: MM_INDEX as a canary. A ring of zeros is type-0 writes of 0 to
    ; register 0, so a seeded MM_INDEX reading 0 after the kick means the
    ; CCE fetched zeros. Restored after.
    cmp     ebx, 0000h
    je     RC_Write_Ok
    ; /gart and /agp: BUS_CNTL (bus mastering), AGP_BASE, PCI_GART_PAGE.
    cmp     ebx, 0030h
    je     RC_Write_Ok
    cmp     ebx, 0170h
    je     RC_Write_Ok
    cmp     ebx, 0174h
    je     RC_Write_Ok
    ; /atis: AGP_CNTL_B and PM4_VC_DEBUG_CONFIG, as ATI's start-up sets.
    cmp     ebx, 0b44h
    je     RC_Write_Ok
    cmp     ebx, 07a4h
    je     RC_Write_Ok
    cmp     ebx, 017ch
    je     RC_Write_Ok
    cmp     ebx, 1a00h
    je     RC_Write_Ok
    cmp     ebx, RC_PC_GUI_CTLSTAT
    je     RC_Write_Ok
    cmp     ebx, 07fch
    je     RC_Write_Ok
    cmp     ebx, 1000h
    je     RC_Write_Ok
    cmp     ebx, 1004h
    je     RC_Write_Ok
    cmp     ebx, 0700h
    jb     RC_Write_Refused
    cmp     ebx, 0720h
    jb     RC_Write_Ok
    cmp     ebx, 07d4h
    jb     RC_Write_Refused
    cmp     ebx, 07e4h
    jb     RC_Write_Ok
    cmp     ebx, 1400h
    jb     RC_Write_Refused
    cmp     ebx, 1700h
    jb     RC_Write_Ok
    cmp     ebx, 1c80h
    jb     RC_Write_Refused
    cmp     ebx, 1d48h
    jae     RC_Write_Refused
RC_Write_Ok:
    clc
    ret
RC_Write_Refused:
    stc
    ret
EndProc RC_Write_Allowed

; EBX = register offset. CF clear if a read is allowed.
BeginProc RC_Read_Allowed
    test    ebx, 3
    jnz     short RC_Read_Refused
    ; MM_INDEX, for the /loc canary; MM_DATA (4) stays refused.
    test    ebx, ebx
    jz      short RC_Read_Ok
    cmp     ebx, 000ch
    je      short RC_Read_Refused
    cmp     ebx, 00b4h
    je      short RC_Read_Refused
    cmp     ebx, 0008h
    jb      short RC_Read_Refused
    cmp     ebx, 0f60h
    jb      short RC_Read_Ok
    cmp     ebx, 1400h
    jb      short RC_Read_Refused
    cmp     ebx, 1d48h
    jae     short RC_Read_Refused
    cmp     ebx, 17c0h
    jb      short RC_Read_Ok
    cmp     ebx, 17e4h
    jb      short RC_Read_Refused
RC_Read_Ok:
    clc
    ret
RC_Read_Refused:
    stc
    ret
EndProc RC_Read_Allowed

; ESI = op (code, a, b, c, result). Returns CF set if the op is refused.
BeginProc RC_Execute_One
    pushad
    mov     eax, [esi]
    mov     ebx, [esi+4]
    mov     ecx, [esi+8]
    mov     edx, [esi+12]

    cmp     eax, RC_OP_MMIO_WRITE32
    je      RC_Do_Mmio_Write32
    cmp     eax, RC_OP_MMIO_READ32
    je      RC_Do_Mmio_Read32
    cmp     eax, RC_OP_LFB_WRITE32
    je      RC_Do_Lfb_Write32
    cmp     eax, RC_OP_LFB_READ32
    je      RC_Do_Lfb_Read32
    cmp     eax, RC_OP_LFB_FILL32
    je      RC_Do_Lfb_Fill32
    cmp     eax, RC_OP_LFB_GRADIENT
    je      RC_Do_Lfb_Gradient
    cmp     eax, RC_OP_LFB_READ_BLOCK
    je      RC_Do_Lfb_Read_Block
    cmp     eax, RC_OP_WAIT_IDLE
    je      RC_Do_Wait_Idle
    cmp     eax, RC_OP_PM4_WAIT_IDLE
    je      RC_Do_Pm4_Wait_Idle
    cmp     eax, RC_OP_ENGINE_RESET
    je      RC_Do_Engine_Reset
    cmp     eax, RC_OP_MMIO_POLL
    je      RC_Do_Mmio_Poll
    cmp     eax, RC_OP_RING_ALLOC
    je      RC_Do_Ring_Alloc
    cmp     eax, RC_OP_RING_WRITE32
    je      RC_Do_Ring_Write32
    cmp     eax, RC_OP_RING_READ32
    je      RC_Do_Ring_Read32
    cmp     eax, RC_OP_GART_ALLOC
    je      RC_Do_Gart_Alloc
    cmp     eax, RC_OP_BLOCK_READ32
    je      RC_Do_Block_Read32
    cmp     eax, RC_OP_RING_PATTERN
    je      RC_Do_Ring_Pattern
    cmp     eax, RC_OP_PHYS_READ32
    je      RC_Do_Phys_Read32
    cmp     eax, RC_OP_PCI_CFG_READ
    je      RC_Do_Pci_Cfg_Read
    cmp     eax, RC_OP_CACHE_FLUSH
    je      RC_Do_Cache_Flush
    jmp     RC_Do_Refuse

RC_Do_Cache_Flush:
    db      0Fh, 09h                ; wbinvd: the module assembles as .386p

    jmp     RC_Do_Done

; a = a configuration mechanism 1 address (80000000h | bus << 16 | device
; << 11 | function << 8 | register), dword-aligned. Read only: the host
; bridge's AGP capability, beside the card's. Result: the dword.
RC_Do_Pci_Cfg_Read:
    test    ebx, 3
    jnz     RC_Do_Refuse
    test    ebx, 80000000h
    jz      RC_Do_Refuse
    pushfd
    cli
    mov     dx, 0cf8h
    in      eax, dx
    mov     ecx, eax
    mov     eax, ebx
    out     dx, eax
    mov     dx, 0cfch
    in      eax, dx
    mov     [esi+16], eax
    mov     dx, 0cf8h
    mov     eax, ecx
    out     dx, eax
    popfd
    jmp     RC_Do_Done

; /dump under ATI's driver: a = a dword-aligned physical address (ATI's
; ring through the AGP aperture, its read-pointer write-back in RAM).
; Mapped a page at a time and not unmapped: a handful of reads per run.
; Result: the dword.
RC_Do_Phys_Read32:
    test    ebx, 3
    jnz     RC_Do_Refuse
    mov     eax, ebx
    and     eax, 0fffff000h
    VMMcall _MapPhysToLinear,<eax,1000h,0>
    cmp     eax, 0ffffffffh
    je      RC_Do_Refuse
    mov     ecx, [esi+4]
    and     ecx, 0fffh
    mov     eax, [eax+ecx]
    mov     [esi+16], eax
    jmp     RC_Do_Done

; a = byte offset in the locked block (the write-back target). Result:
; the dword.
RC_Do_Block_Read32:
    cmp     RCRingLinear, 0
    je      RC_Do_Refuse
    test    ebx, 3
    jnz     RC_Do_Refuse
    cmp     ebx, RC_GART_BYTES
    jae     RC_Do_Refuse
    mov     edi, RCRingLinear
    mov     eax, [edi+ebx]
    mov     [esi+16], eax
    jmp     RC_Do_Done

; Fill the ring with the locating pattern. a = 1 for the long form: every
; dword 5A000000h | its index, and every 17th a type-0 header for sixteen
; writes to GUI_SCRATCH_REG0 alone (ONE_REG_WR). A CCE that reads one of
; those headers writes a tagged value, or waits for the rest of the packet;
; it cannot go idle with the register untouched. Result: dwords written.
RC_Do_Ring_Pattern:
    cmp     RCRingData, 0
    je      RC_Do_Refuse
    mov     edi, RCRingData
    xor     ecx, ecx
    cmp     ebx, 1
    je      short RC_Do_Ring_Pattern_Long
RC_Do_Ring_Pattern_Next:
    mov     dword ptr [edi+ecx*4], RC_PATTERN_HEADER
    lea     eax, [ecx+1]
    or      eax, RC_PATTERN_TAG
    mov     [edi+ecx*4+4], eax
    add     ecx, 2
    cmp     ecx, RC_RING_AREA / 4
    jb      short RC_Do_Ring_Pattern_Next
    mov     [esi+16], ecx
    jmp     RC_Do_Done
RC_Do_Ring_Pattern_Long:
    xor     edx, edx                ; dwords since the last header
RC_Do_Ring_Pattern_Long_Next:
    mov     eax, ecx
    or      eax, RC_PATTERN_TAG
    test    edx, edx
    jnz     short RC_Do_Ring_Pattern_Long_Store
    mov     eax, RC_PATTERN_LONG_HEADER
RC_Do_Ring_Pattern_Long_Store:
    mov     [edi+ecx*4], eax
    inc     edx
    cmp     edx, 17
    jb      short RC_Do_Ring_Pattern_Long_Count
    xor     edx, edx
RC_Do_Ring_Pattern_Long_Count:
    inc     ecx
    cmp     ecx, RC_RING_AREA / 4
    jb      short RC_Do_Ring_Pattern_Long_Next
    mov     [esi+16], ecx
    jmp     RC_Do_Done

; The table and ring block, once per load. a = entry form: 0 the page's bus
; address (Linux), 1 its page number (RRG 3-207, PhysAddr[31:12] <-
; PhysPageNo). Result: the table's physical address (the ring follows at
; +8000h), or 0 if _PageAllocate refused.
RC_Do_Ring_Alloc:
    cmp     RCRingLinear, 0
    jne     short RC_Do_Ring_Alloc_Have
    mov     ebx, OFFSET32 RCRingPhys
    VMMcall _PageAllocate,<RC_RING_PAGES,PG_SYS,0,0,0,0FFFFFh,ebx,PAGEFIXED+PAGEUSEALIGN+PAGECONTIG+PAGEZEROINIT>
    test    eax, eax
    jz      short RC_Do_Ring_Alloc_Failed
    mov     RCRingHandle, eax
    mov     RCRingLinear, edx
    lea     eax, [edx+RC_GART_BYTES]
    mov     RCRingData, eax
    mov     edi, edx
    mov     ebx, [esi+4]
    xor     ecx, ecx
RC_Do_Ring_Alloc_Entry:
    mov     eax, ecx
    and     eax, RC_RING_PAGE_MASK
    shl     eax, 12
    add     eax, RCRingPhys
    add     eax, RC_GART_BYTES
    test    ebx, ebx
    jz      short RC_Do_Ring_Alloc_Store
    shr     eax, 12
RC_Do_Ring_Alloc_Store:
    mov     [edi+ecx*4], eax
    inc     ecx
    cmp     ecx, RC_GART_ENTRIES
    jb      short RC_Do_Ring_Alloc_Entry
RC_Do_Ring_Alloc_Have:
    mov     eax, RCRingPhys
    mov     [esi+16], eax
    jmp     RC_Do_Done
RC_Do_Ring_Alloc_Failed:
    mov     dword ptr [esi+16], 0
    jmp     RC_Do_Done

; a = byte offset in the ring, b = value.
RC_Do_Ring_Write32:
    cmp     RCRingData, 0
    je      RC_Do_Refuse
    test    ebx, 3
    jnz     RC_Do_Refuse
    cmp     ebx, RC_RING_AREA
    jae     RC_Do_Refuse
    mov     edi, RCRingData
    mov     [edi+ebx], ecx
    jmp     RC_Do_Done

; a = byte offset in the ring. Result: the dword.
RC_Do_Ring_Read32:
    cmp     RCRingData, 0
    je      RC_Do_Refuse
    test    ebx, 3
    jnz     RC_Do_Refuse
    cmp     ebx, RC_RING_AREA
    jae     RC_Do_Refuse
    mov     edi, RCRingData
    mov     eax, [edi+ebx]
    mov     [esi+16], eax
    jmp     RC_Do_Done

; /vgart: reserve and commit RC_VGART_PAGES of the AGP aperture; the ring
; ops then write through the reserve's linear address. a = the GART flags,
; 0 for PG_UNCACHED (ATI's mini-VDD passes PG_WRITECOMBINED). Result: the
; pages' device (bus) address, or 0 if either service refused.
RC_Do_Gart_Alloc:
    cmp     RCGartLinear, 0
    jne     short RC_Do_Gart_Alloc_Have
    test    ebx, ebx
    jnz     short RC_Do_Gart_Alloc_Flags
    mov     ebx, PG_UNCACHED
RC_Do_Gart_Alloc_Flags:
    mov     RCGartFlags, ebx
    mov     ebx, OFFSET32 RCGartDev
    VMMcall _GARTReserve,<0,RC_VGART_PAGES,0,ebx,RCGartFlags>
    test    eax, eax
    jz      short RC_Do_Gart_Alloc_Failed
    mov     RCGartLinear, eax
    mov     ebx, OFFSET32 RCGartDev
    VMMcall _GARTCommit,<RCGartLinear,0,RC_VGART_PAGES,ebx,RCGartFlags>
    test    eax, eax
    jz      short RC_Do_Gart_Alloc_Failed
    mov     RCGartCommitted, 1
    mov     eax, RCGartLinear
    mov     RCRingData, eax
RC_Do_Gart_Alloc_Have:
    mov     eax, RCGartDev
    mov     [esi+16], eax
    jmp     RC_Do_Done
RC_Do_Gart_Alloc_Failed:
    mov     dword ptr [esi+16], 0
    jmp     RC_Do_Done

; a = register offset, b = mask, c = value. Polls until (reg & mask) ==
; value, at most RC_WAIT_LIMIT reads: the ring test's wait for
; PM4_BUFFER_DL_RPTR to reach the write pointer. Result: reads taken, or
; RC_TIMEOUT; the last value read is not kept.
RC_Do_Mmio_Poll:
    call    RC_Read_Allowed
    jc      RC_Do_Refuse
    mov     edi, RCMmioLinear
    xor     ebp, ebp
RC_Do_Mmio_Poll_Next:
    inc     ebp
    mov     eax, [edi+ebx]
    and     eax, ecx
    cmp     eax, edx
    je      short RC_Do_Mmio_Poll_Done
    cmp     ebp, RC_WAIT_LIMIT
    jb      short RC_Do_Mmio_Poll_Next
    mov     ebp, RC_TIMEOUT
RC_Do_Mmio_Poll_Done:
    mov     [esi+16], ebp
    jmp     RC_Do_Done

; b = free CCE FIFO entries required. Result: reads taken, or RC_TIMEOUT.
RC_Do_Pm4_Wait_Idle:
    mov     edi, RCMmioLinear
    xor     edx, edx
RC_Do_Pm4_Wait_Next:
    inc     edx
    mov     eax, [edi+RC_PM4_STAT]
    cmp     eax, 0ffffffffh
    je      RC_Do_Pm4_Wait_Timeout
    test    eax, RC_PM4_BUSY_BITS
    jnz     short RC_Do_Pm4_Wait_Again
    and     eax, RC_PM4_FIFO_MASK
    cmp     eax, ecx
    jae     short RC_Do_Pm4_Wait_Flush
RC_Do_Pm4_Wait_Again:
    cmp     edx, RC_WAIT_LIMIT
    jb      short RC_Do_Pm4_Wait_Next
    jmp     short RC_Do_Pm4_Wait_Timeout
RC_Do_Pm4_Wait_Flush:
    mov     eax, [edi+RC_PC_NGUI_CTLSTAT]
    or      eax, RC_PC_FLUSH_ALL
    mov     [edi+RC_PC_NGUI_CTLSTAT], eax
    xor     ecx, ecx
RC_Do_Pm4_Wait_Cache:
    inc     ecx
    mov     eax, [edi+RC_PC_NGUI_CTLSTAT]
    test    eax, RC_PC_BUSY
    jz      short RC_Do_Pm4_Wait_Gui_Cache
    cmp     ecx, RC_WAIT_LIMIT
    jb      short RC_Do_Pm4_Wait_Cache
    jmp     short RC_Do_Pm4_Wait_Timeout
    ; And the GUI pixel cache, which the stream's own flush packet emptied:
    ; its busy bit (PC_GUI_CTLSTAT bit 31) says when that finished.
RC_Do_Pm4_Wait_Gui_Cache:
    inc     ecx
    mov     eax, [edi+RC_PC_GUI_CTLSTAT]
    test    eax, RC_PC_BUSY
    jz      short RC_Do_Pm4_Wait_Done
    cmp     ecx, RC_WAIT_LIMIT
    jb      short RC_Do_Pm4_Wait_Gui_Cache
RC_Do_Pm4_Wait_Timeout:
    mov     edx, RC_TIMEOUT
    jmp     short RC_Do_Pm4_Wait_Store
RC_Do_Pm4_Wait_Done:
    add     edx, ecx
RC_Do_Pm4_Wait_Store:
    mov     [esi+16], edx
    jmp     RC_Do_Done

; R128EngineReset. The PLL index is written a byte at a time, as X's
; OUTPLL does: bits 9:8 of the dword select the pixel clock divider the
; CRTC runs on, and must not change. Result: GEN_RESET_CNTL as found.
RC_Do_Engine_Reset:
    mov     edi, RCMmioLinear
    mov     eax, [edi+RC_PC_NGUI_CTLSTAT]
    or      eax, RC_PC_FLUSH_ALL
    mov     [edi+RC_PC_NGUI_CTLSTAT], eax
    xor     ecx, ecx
RC_Do_Engine_Reset_Cache:
    inc     ecx
    mov     eax, [edi+RC_PC_NGUI_CTLSTAT]
    test    eax, RC_PC_BUSY
    jz      short RC_Do_Engine_Reset_Clocks
    cmp     ecx, RC_WAIT_LIMIT
    jb      short RC_Do_Engine_Reset_Cache
RC_Do_Engine_Reset_Clocks:
    movzx   ebp, byte ptr [edi+RC_CLOCK_CNTL_INDEX]
    mov     byte ptr [edi+RC_CLOCK_CNTL_INDEX], RC_PLL_MCLK_CNTL
    mov     edx, [edi+RC_CLOCK_CNTL_DATA]
    mov     byte ptr [edi+RC_CLOCK_CNTL_INDEX], RC_PLL_MCLK_CNTL or RC_PLL_WR_EN
    mov     eax, edx
    or      eax, RC_MCLK_FORCE
    mov     [edi+RC_CLOCK_CNTL_DATA], eax
    mov     ebx, [edi+RC_GEN_RESET_CNTL]
    mov     [esi+16], ebx
    mov     eax, ebx
    or      eax, RC_SOFT_RESET_GUI
    mov     [edi+RC_GEN_RESET_CNTL], eax
    mov     eax, [edi+RC_GEN_RESET_CNTL]
    mov     eax, ebx
    and     eax, NOT RC_SOFT_RESET_GUI
    mov     [edi+RC_GEN_RESET_CNTL], eax
    mov     eax, [edi+RC_GEN_RESET_CNTL]
    mov     byte ptr [edi+RC_CLOCK_CNTL_INDEX], RC_PLL_MCLK_CNTL or RC_PLL_WR_EN
    mov     [edi+RC_CLOCK_CNTL_DATA], edx
    mov     eax, ebp
    mov     byte ptr [edi+RC_CLOCK_CNTL_INDEX], al
    mov     [edi+RC_GEN_RESET_CNTL], ebx
    jmp     RC_Do_Done

; a = register offset, b = value.
RC_Do_Mmio_Write32:
    call    RC_Write_Allowed
    jc      RC_Do_Refuse
    mov     edi, RCMmioLinear
    mov     [edi+ebx], ecx
    jmp     RC_Do_Done

; a = register offset.
RC_Do_Mmio_Read32:
    call    RC_Read_Allowed
    jc      RC_Do_Refuse
    mov     edi, RCMmioLinear
    mov     eax, [edi+ebx]
    mov     [esi+16], eax
    jmp     RC_Do_Done

; a = offset, b = value.
RC_Do_Lfb_Write32:
    push    ecx
    mov     ecx, 1
    call    RC_Check_Lfb_Run
    pop     ecx
    jc      RC_Do_Refuse
    mov     edi, RCLfbLinear
    mov     [edi+ebx], ecx
    jmp     RC_Do_Done

; a = offset.
RC_Do_Lfb_Read32:
    mov     ecx, 1
    call    RC_Check_Lfb_Run
    jc      RC_Do_Refuse
    mov     edi, RCLfbLinear
    mov     eax, [edi+ebx]
    mov     [esi+16], eax
    jmp     RC_Do_Done

; a = offset, b = value, c = dword count.
RC_Do_Lfb_Fill32:
    push    ecx
    mov     ecx, edx
    call    RC_Check_Lfb_Run
    pop     eax
    jc      RC_Do_Refuse
    mov     edi, RCLfbLinear
    add     edi, ebx
    mov     ecx, edx
    cld
    rep     stosd
    jmp     RC_Do_Done

; a = offset, c = dword count. Dword i holds 2i (low half) and 2i+1 (high
; half), counted from the start of the run.
RC_Do_Lfb_Gradient:
    mov     ecx, edx
    call    RC_Check_Lfb_Run
    jc      RC_Do_Refuse
    mov     edi, RCLfbLinear
    add     edi, ebx
    xor     eax, eax
RC_Do_Lfb_Gradient_Next:
    jecxz   short RC_Do_Lfb_Gradient_Done
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
    jmp     short RC_Do_Lfb_Gradient_Next
RC_Do_Lfb_Gradient_Done:
    jmp     RC_Do_Done

; a = offset, c = dword count (at most the block). Copies into the block.
RC_Do_Lfb_Read_Block:
    cmp     edx, RC_BLOCK_DWORDS
    ja      RC_Do_Refuse
    mov     ecx, edx
    call    RC_Check_Lfb_Run
    jc      RC_Do_Refuse
    push    esi
    mov     esi, RCLfbLinear
    add     esi, ebx
    mov     edi, OFFSET32 RCBlock
    mov     ecx, edx
    cld
    rep     movsd
    pop     esi
    jmp     RC_Do_Done

; Result: reads taken in all, or RC_TIMEOUT.
RC_Do_Wait_Idle:
    mov     edi, RCMmioLinear
    xor     edx, edx
RC_Do_Wait_Idle_Gui:
    inc     edx
    mov     eax, [edi+RC_GUI_STAT]
    cmp     eax, 0ffffffffh
    je      short RC_Do_Wait_Idle_Timeout
    test    eax, RC_GUI_ACTIVE
    jnz     short RC_Do_Wait_Idle_Gui_Again
    and     eax, RC_GUI_FIFO_MASK
    cmp     eax, RC_GUI_FIFO_SLOTS
    jae     short RC_Do_Wait_Idle_Flush
RC_Do_Wait_Idle_Gui_Again:
    cmp     edx, RC_WAIT_LIMIT
    jb      short RC_Do_Wait_Idle_Gui
    jmp     short RC_Do_Wait_Idle_Timeout
RC_Do_Wait_Idle_Flush:
    mov     eax, [edi+RC_PC_NGUI_CTLSTAT]
    or      eax, RC_PC_FLUSH_ALL
    mov     [edi+RC_PC_NGUI_CTLSTAT], eax
    xor     ecx, ecx
RC_Do_Wait_Idle_Cache:
    inc     ecx
    mov     eax, [edi+RC_PC_NGUI_CTLSTAT]
    test    eax, RC_PC_BUSY
    jz      short RC_Do_Wait_Idle_Done
    cmp     ecx, RC_WAIT_LIMIT
    jb      short RC_Do_Wait_Idle_Cache
RC_Do_Wait_Idle_Timeout:
    mov     edx, RC_TIMEOUT
    jmp     short RC_Do_Wait_Idle_Store
RC_Do_Wait_Idle_Done:
    add     edx, ecx
RC_Do_Wait_Idle_Store:
    mov     [esi+16], edx
    jmp     short RC_Do_Done

RC_Do_Refuse:
    popad
    stc
    ret
RC_Do_Done:
    popad
    clc
    ret
EndProc RC_Execute_One

BeginProc RC_Run
    pushad
    call    RC_Map
    test    RCResult[4], RC_MAPPED
    jz      short RC_Run_Done
    mov     ecx, RCCount
    mov     RCResult[20], ecx
    mov     esi, OFFSET32 RCOps
    xor     ebx, ebx
    jecxz   short RC_Run_All
RC_Run_Next:
    call    RC_Execute_One
    jc      short RC_Run_Refused
    add     esi, RC_OP_DWORDS * 4
    inc     ebx
    cmp     ebx, ecx
    jb      short RC_Run_Next
RC_Run_All:
    or      RCResult[4], RC_RAN
    jmp     short RC_Run_Count
RC_Run_Refused:
    or      RCResult[4], RC_REFUSED
    mov     RCResult[20], ebx
RC_Run_Count:
    mov     RCResult[16], ebx
RC_Run_Done:
    popad
    ret
EndProc RC_Run

BeginProc RC_W32_DeviceIoControl
    cmp     ecx, DIOC_OPEN
    je      RC_Dioc_Ok
    cmp     ecx, DIOC_CLOSEHANDLE
    je      RC_Dioc_Ok
    cmp     ecx, RC_DIOC_RUN
    jne     RC_Dioc_Fail

    pushad
    mov     ebx, esi
    mov     ebp, [ebx.lpvInBuffer]
    test    ebp, ebp
    jz      RC_Dioc_Copy_Fail
    mov     ecx, [ebx.cbInBuffer]
    cmp     ecx, RC_IN_MIN_BYTES
    jb      RC_Dioc_Copy_Fail
    mov     edi, [ebx.lpvOutBuffer]
    test    edi, edi
    jz      RC_Dioc_Copy_Fail
    cmp     [ebx.cbOutBuffer], RC_RESULT_DWORDS * 4
    jb      RC_Dioc_Copy_Fail

    ; The count must fit both the op table and the bytes supplied.
    mov     eax, [ebp]
    cmp     eax, RC_OP_MAX
    ja      RC_Dioc_Copy_Fail
    mov     edx, eax
    imul    edx, RC_OP_DWORDS * 4
    add     edx, 4
    cmp     ecx, edx
    jb      RC_Dioc_Copy_Fail
    mov     RCCount, eax

    mov     RCResult[4], 0
    mov     RCResult[16], 0
    mov     RCResult[20], 0
    lea     esi, [ebp+4]
    mov     edi, OFFSET32 RCOps
    mov     ecx, eax
    imul    ecx, RC_OP_DWORDS
    cld
    rep     movsd

    call    RC_Run

    mov     edi, [ebx.lpvOutBuffer]
    mov     esi, OFFSET32 RCResult
    mov     ecx, RC_RESULT_DWORDS
    cld
    rep     movsd
    mov     eax, [ebx.lpcbBytesReturned]
    test    eax, eax
    jz      short RC_Dioc_Copy_Done
    mov     dword ptr [eax], RC_RESULT_DWORDS * 4
RC_Dioc_Copy_Done:
    popad
RC_Dioc_Ok:
    xor     eax, eax
    ret

RC_Dioc_Copy_Fail:
    popad
RC_Dioc_Fail:
    mov     eax, 1
    ret
EndProc RC_W32_DeviceIoControl

BeginProc RC_Dynamic_Init
    clc
    ret
EndProc RC_Dynamic_Init

; The program has stopped the CCE and put BUS_CNTL and PCI_GART_PAGE back
; before it closes the device, so nothing reads the block when it goes.
BeginProc RC_Dynamic_Exit
    cmp     RCRingHandle, 0
    je      short RC_Dynamic_Exit_Gart
    VMMcall _PageFree,<RCRingHandle,0>
    mov     RCRingHandle, 0
    mov     RCRingLinear, 0
RC_Dynamic_Exit_Gart:
    cmp     RCGartLinear, 0
    je      short RC_Dynamic_Exit_Done
    cmp     RCGartCommitted, 0
    je      short RC_Dynamic_Exit_Free
    VMMcall _GARTUnCommit,<RCGartLinear,0,RC_VGART_PAGES>
RC_Dynamic_Exit_Free:
    VMMcall _GARTFree,<RCGartLinear>
    mov     RCGartLinear, 0
RC_Dynamic_Exit_Done:
    mov     RCRingData, 0
    clc
    ret
EndProc RC_Dynamic_Exit

Begin_Control_Dispatch RC
    Control_Dispatch Sys_Dynamic_Device_Init, RC_Dynamic_Init
    Control_Dispatch Sys_Dynamic_Device_Exit, RC_Dynamic_Exit
    Control_Dispatch W32_DeviceIoControl, RC_W32_DeviceIoControl
End_Control_Dispatch RC

VxD_LOCKED_CODE_ENDS

end
