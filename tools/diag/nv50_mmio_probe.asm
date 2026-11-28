; NVIDIA GT218 (GeForce 210) register probe VxD.
;
; Phase 2 of docs\plans\nvidia-gt218.md. Maps BAR0, the 16 MiB register
; window, and reads a caller's list of offsets, each twice. The RIVA 128
; probe (nv3_mmio_probe.asm) is the model, without its BAR1 windows and
; alias test.
;
; Writes: nothing, except in N5_DIOC_FBWRITE and N5_DIOC_HWSQ below.
; 0CF8h is saved and restored around each
; configuration read. Mapping a physical range with _MapPhysToLinear
; changes no device state.
;
; The caller names the device by its configuration address; the VxD reads
; the id and BAR0 itself and refuses anything that is not PCI 10DE:0A65
; (GT218) or 10DE:0640 (G96, GeForce 9500 GT, docs\plans\
; nvidia-g96-geforce9500gt.md Phase 2) with a 16 MiB-aligned memory BAR0.
; Once BAR0 is mapped, PMC_BOOT_0 (BAR0 + 0) must name chipset A8h or 96h
; in bits 28:20, nouveau's chipset field, or nothing else is read. The
; register list is the GT218's; on the G96 every unit in it exists
; (nouveau nv96_chipset) except the GT215 copy engine, which
; V9XN5MM.EXE does not ask for there.
;
; Offsets are accepted only inside N5Windows below. Offsets come from Linux
; nouveau (nvkm: subdev\mc, subdev\fb\ramnv50.c, subdev\bar\nv50.c,
; subdev\instmem\nv50.c, engine\fifo\nv50.c, engine\gr\nv50.c,
; engine\ce\gt215.c, falcon\v1.c, engine\disp\nv50.c), all unmeasured.
; Left out on purpose:
;   601000h-601FFFh  PRMCIO, the VGA CRTC ports, indexed, and input status
;                    1, whose read resets the attribute flip-flop;
;   681000h-681FFFh  PRMDIO, the palette ports, whose reads advance;
;   the falcon data ports (base + 180h-1CFh), whose reads auto-increment;
;   every engine's method and channel areas.
; The clock windows (4000h-403Fh, 4700h, 4800h, C040h, C050h, E18Ch,
; E81Ch-E84Fh) are plain PLL and source-select registers that nouveau's
; nv50_clk_read only reads, and the GPIO windows (E104h-E10Bh,
; E280h-E287h) its gpio sense reads; V9XN5MM.EXE asks for them on the
; G96 only. 300000h-31FFFFh is the PROM, for /prom.
; PFIFO, PGRAPH and the copy engine are in the list but V9XN5MM.EXE asks
; for each only when PMC_ENABLE shows it on, as the NV3 probe does. PDISP
; has no enable or reset bit in nouveau's GT215 map (subdev\mc\gt215.c),
; and drives the desktop being probed, so it is read unconditionally.
; The PRAMIN window (700000h-7FFFFFh) is accepted whole: it is video memory
; through whatever base PBUS 1700h already holds, and this VxD never moves
; it.
;
; N5_DIOC_FB reads video memory instead, through four 2 MiB windows: the
; VBE framebuffer (BAR3 + 16 MiB, CF000000h on M-BXMFA8), BAR3 + 0, BAR1 +
; 16 MiB and BAR1 + 0. An offset with bit 31 set names one: bits 25:24 the
; window (N5_FB_*), bits 20:0 the byte in it. The first probe found no BAR
; VM (PBUS 1704h-170Ch zero), so what BAR1 decodes is unknown; the
; publisher asks for BAR1 last. Reads only.
;
; N5_DIOC_HWSQ writes registers: it runs a caller's script on the PBUS
; sequencer (docs\plans\nvidia-g96-reclock.md), on the G96 only. The
; script is built by V9XN5MM.EXE /reclock from nouveau's nv50_clk_calc.
;
; N5_DIOC_FBWRITE writes only video memory
; through BAR1, never a register: is BAR1 linear with no BAR VM
; (docs\decisions\2026-11-11-gt218-bar-windows.md, "Next")? One 4 KiB page
; is mapped at BAR1 + 0 (the desktop's first row) and at each of
; N5WriteTargets, all past the VBE's 14 MiB. The first 16 dwords of every
; page are saved; each target gets 0A5000000h | window << 16 | byte offset;
; every page is read back; the targets are restored from last to first,
; then BAR1 + 0 is rewritten with its own saved words, which undoes any
; alias onto the desktop; every page is read again. Output, window w dword
; i at index w * 16 + i: N5Offsets the saved words, N5First the reads with
; the patterns in place, N5Second the reads after the restore. BAR1 + 0
; itself is never given a pattern.

.386p

.xlist
include VMM.INC
include VWIN32.INC
.list

Declare_Virtual_Device N5MM, 1, 0, N5_Control, \
                       Undefined_Device_ID, Undefined_Init_Order, , ,

N5_MAGIC                equ 4d4d354eh ; "N5MM"
N5_DIOC_CAPTURE         equ 1
N5_DIOC_FB              equ 2
N5_DIOC_FBWRITE         equ 3
N5_DIOC_HWSQ            equ 4
N5_HWSQ_WORDS_MAX       equ 128         ; g94_bus hwsq_size
N5_HWSQ_SPINS           equ 4000000     ; ~2 s of 1308h reads, nouveau's wait
N5_HWSQ_READBACK        equ 8
N5_ST_HWSQ_DONE         equ 00000800h
N5_ST_HWSQ_TIMEOUT      equ 00001000h
N5_WR_WINDOWS           equ 6         ; BAR1 + 0, then the five targets
N5_WR_DWORDS            equ 16
N5_WR_PAGE              equ 1000h
N5_WR_PATTERN           equ 0a5000000h
N5_ST_WRITE_DONE        equ 00000400h
N5_ID                   equ 0a6510deh ; device 0A65h, vendor 10DEh
N5_ID_G96               equ 064010deh ; GeForce 9500 GT, device 0640h
N5_CFG_REVISION         equ 08h
N5_CFG_BAR0             equ 10h
N5_CFG_BAR1             equ 14h
N5_CFG_BAR3             equ 1ch
N5_FB_FLAG              equ 80000000h
N5_FB_WINDOW_SHIFT      equ 24
N5_FB_WINDOW_MASK       equ 3
N5_FB_OFFSET_MASK       equ 001fffffh
N5_FB_RESERVED_MASK     equ 7ce00000h ; bits 30:26 and 23:21 must be zero
N5_FB_WINDOW_BYTES      equ 00200000h
N5_FB_HIGH              equ 01000000h ; the + 16 MiB windows
N5_FB_LFB               equ 0         ; BAR3 + 16 MiB
N5_FB_BAR3_LOW          equ 1         ; BAR3 + 0
N5_FB_BAR1_HIGH         equ 2         ; BAR1 + 16 MiB
N5_FB_BAR1_LOW          equ 3         ; BAR1 + 0
N5_BAR_PREFETCH_64      equ 0000000ch ; memory, 64-bit, prefetchable
N5_MMIO_BYTES           equ 01000000h
N5_BASE_ALIGN           equ 00ffffffh
N5_BAR_IO               equ 00000001h
N5_BOOT_CHIPSET_MASK    equ 1ff00000h
N5_BOOT_CHIPSET_GT218   equ 0a800000h
N5_BOOT_CHIPSET_G96     equ 09600000h
N5_OFFSET_MAX           equ 256

; Status bits returned to the publisher.
N5_ST_GT218             equ 00000001h
N5_ST_BASE_INVALID      equ 00000002h
N5_ST_MMIO_MAPPED       equ 00000004h
N5_ST_IDENTITY_MATCH    equ 00000008h
N5_ST_OFFSETS_REFUSED   equ 00000010h
N5_ST_SNAPSHOT_TAKEN    equ 00000020h
N5_ST_NOT_GT218         equ 00000080h
N5_ST_ADDRESS_INVALID   equ 00000100h
N5_ST_FB_MAPPED         equ 00000200h

; Input from V9XN5MM.EXE.
N5_IN_CONFIG            equ 0
N5_IN_COUNT             equ 4
N5_IN_OFFSETS           equ 8
N5_IN_MIN_BYTES         equ 8

; Output layout, in dwords. Must match struct v9x_n5mm_result in
; nv50_mmio_probe_win32.c.
N5_HEADER_DWORDS        equ 16
N5_RESULT_DWORDS        equ (N5_HEADER_DWORDS + N5_OFFSET_MAX * 3)

VxD_LOCKED_DATA_SEG
; Accepted BAR0 windows: start, limit (exclusive).
N5Windows label dword
    dd 000000h, 000008h ; PMC_BOOT_0, PMC_BOOT_1
    dd 000100h, 000104h ; PMC_INTR_0
    dd 000140h, 000144h ; PMC_INTR_EN_0
    dd 000200h, 000204h ; PMC_ENABLE
    dd 001540h, 001544h ; PBUS units: memory partitions 23:16
    dd 001700h, 001710h ; PRAMIN window base, BAR1 VM, BAR1 and BAR3 objects
    dd 001850h, 001854h ; PBUS ROM access
    dd 002100h, 002104h ; PFIFO_INTR
    dd 002140h, 002144h ; PFIFO_INTR_EN
    dd 002500h, 002504h ; PFIFO caches
    dd 00250ch, 002510h ; PFIFO 250C
    dd 002600h, 002620h ; PFIFO channel table, channels 0-7
    dd 003200h, 003204h ; PFIFO cache1 push
    dd 003250h, 003254h ; PFIFO cache1 pull
    dd 0032ech, 003300h ; PFIFO playlist and runlist state
    dd 004000h, 004040h ; G96 PLLs: mem 4008h, shader 4020h, core 4028h,
                        ; vdec 4030h, control and coefficient pairs
                        ; (nouveau subdev\clk\nv50.c read_pll)
    dd 004700h, 004704h ; clock dividers, G84/G86/G98 (nv50.c read_div)
    dd 004800h, 004804h ; clock dividers, G92/G94/G96 (nv50.c read_div)
    dd 009400h, 009408h ; PTIMER time
    dd 00c040h, 00c044h ; clock source select (nv50.c, "mast")
    dd 00c050h, 00c054h ; PLL reference select, G94-G98 (nv50.c read_pll_src)
    dd 00e18ch, 00e190h ; PLL reference select, G80/GT200 (nv50.c)
    dd 00e81ch, 00e850h ; reference PLLs 0 and 1, G94-G98 (nv50.c)
    dd 00e104h, 00e10ch ; GPIO lines 0-15, 4 bits each (nouveau
                        ; subdev\gpio\nv50.c nv50_gpio_location)
    dd 00e280h, 00e288h ; GPIO lines 16-31
    dd 100200h, 100210h ; PFB config 0, 1, 208, memory size
    dd 100714h, 100718h ; PFB memory type
    dd 101000h, 101004h ; PSTRAPS
    dd 104040h, 104044h ; copy engine falcon status
    dd 104100h, 104110h ; copy engine falcon CPUCTL, BOOTVEC, HWCFG, 10C
    dd 300000h, 320000h ; PROM, the ROM chip's first 128 KiB while PBUS
                        ; 1850h bit 0 is clear (nouveau bios\shadowrom.c)
    dd 400100h, 400104h ; PGRAPH_INTR
    dd 400108h, 40010ch ; PGRAPH trap status
    dd 40032ch, 400330h ; PGRAPH context instance
    dd 610020h, 610034h ; PDISP interrupts, supervisor
    dd 610184h, 610188h ; PDISP capabilities
    dd 610200h, 610204h ; PDISP core channel control
    dd 610a00h, 610c00h ; PDISP core state mirrors, head 0 and outputs
    dd 610f40h, 611140h ; PDISP core state mirrors, head 1
    dd 6194e8h, 6194ech ; PDISP VGA ownership (nouveau nv50_disp_init)
    dd 619e00h, 61a000h ; PDISPLAY.VGA block (envytools g80 vga-stack,
                        ; vga-mutex; workspace base guessed at 619F04h)
    dd 700000h, 800000h ; PRAMIN window
N5_WINDOW_COUNT         equ 42

N5Result label dword
    dd N5_MAGIC                 ; 00 magic
    dd 0                        ; 01 status
    dd 0                        ; 02 id dword at configuration 00h
    dd 0                        ; 03 BAR0 as read
    dd 0                        ; 04 offsets accepted
    dd 0                        ; 05 PMC_BOOT_0 through the BAR
    dd 0                        ; 06 0CF8h found and restored
    dd 0                        ; 07 first offset refused, if any
    dd 0                        ; 08 configuration address used
    dd 0                        ; 09 BAR1 as read (N5_DIOC_FB)
    dd 0                        ; 10 class and revision at 08h
    dd 0                        ; 11 BAR3 as read (N5_DIOC_FB)
    dd 0                        ; 12 reserved
    dd 0                        ; 13 reserved
    dd 0                        ; 14 reserved
    dd 0                        ; 15 reserved
N5Offsets    dd N5_OFFSET_MAX dup (0)
N5First      dd N5_OFFSET_MAX dup (0)
N5Second     dd N5_OFFSET_MAX dup (0)
N5Config     dd 0
N5Mmio       dd 0
N5Request    dd 0
; Linear bases of the four framebuffer windows, by N5_FB_* index.
N5FbLinear   dd 4 dup (0)
; N5_DIOC_FBWRITE: BAR1 offsets of its pages, window 0 first, and their
; linear bases. The last target is the last page of BAR1's 256 MiB.
N5WriteTargets label dword
    dd 00000000h, 01000000h, 02000000h, 04000000h, 08000000h, 0fffff000h
N5WriteLinear dd N5_WR_WINDOWS dup (0)
; N5_DIOC_HWSQ: registers read twice after the script, into N5First and
; N5Second: the sequencer's status, the shader and core PLLs, the clock
; source select, PFIFO's block word and the sequencer's enable.
N5HwsqReadback label dword
    dd 001308h, 004020h, 004024h, 004028h, 00402ch, 00c040h, 002504h, 001098h
VxD_LOCKED_DATA_ENDS

VxD_LOCKED_CODE_SEG

; EAX = PCI configuration address. Returns EAX = configuration dword.
; 0CF8h saved and restored with interrupts off.
BeginProc N5_Pci_Read
    push    ebx
    push    edx
    pushfd
    cli
    mov     ebx, eax
    mov     dx, 0cf8h
    in      eax, dx
    mov     N5Result[24], eax
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
EndProc N5_Pci_Read

; EDX = offset. Returns CF clear if it lies in an accepted window.
BeginProc N5_Offset_Allowed
    push    ecx
    push    esi
    test    edx, 3
    jnz     short N5_Offset_Refused
    test    edx, N5_FB_FLAG
    jnz     short N5_Offset_Fb
    mov     esi, OFFSET32 N5Windows
    mov     ecx, N5_WINDOW_COUNT
N5_Offset_Next:
    cmp     edx, [esi]
    jb      short N5_Offset_Skip
    cmp     edx, [esi+4]
    jb      short N5_Offset_Accepted
N5_Offset_Skip:
    add     esi, 8
    dec     ecx
    jnz     short N5_Offset_Next
N5_Offset_Refused:
    pop     esi
    pop     ecx
    stc
    ret

; A framebuffer-window offset: only in an N5_DIOC_FB request, and with no
; bit set outside the window index and the byte offset.
N5_Offset_Fb:
    cmp     N5Request, N5_DIOC_FB
    jne     short N5_Offset_Refused
    test    edx, N5_FB_RESERVED_MASK
    jnz     short N5_Offset_Refused
N5_Offset_Accepted:
    pop     esi
    pop     ecx
    clc
    ret
EndProc N5_Offset_Allowed

; EBP = DIOC input buffer, ECX = its byte count. Copies the offset list,
; refusing the whole list if any entry is unaligned or outside the windows.
BeginProc N5_Accept_Offsets
    pushad
    mov     N5Result[16], 0
    sub     ecx, N5_IN_OFFSETS
    shr     ecx, 2
    mov     eax, [ebp+N5_IN_COUNT]
    cmp     eax, N5_OFFSET_MAX
    ja      short N5_Accept_Refused
    cmp     eax, ecx
    ja      short N5_Accept_Refused
    mov     ecx, eax
    lea     esi, [ebp+N5_IN_OFFSETS]
    mov     edi, OFFSET32 N5Offsets
    jecxz   short N5_Accept_Done
N5_Accept_Next:
    mov     edx, [esi]
    call    N5_Offset_Allowed
    jc      short N5_Accept_Refused_At
    mov     [edi], edx
    add     esi, 4
    add     edi, 4
    dec     ecx
    jnz     short N5_Accept_Next
N5_Accept_Done:
    mov     eax, [ebp+N5_IN_COUNT]
    mov     N5Result[16], eax
    popad
    ret
N5_Accept_Refused_At:
    mov     N5Result[28], edx
N5_Accept_Refused:
    or      N5Result[4], N5_ST_OFFSETS_REFUSED
    popad
    ret
EndProc N5_Accept_Offsets

; EDI = destination. Reads every accepted offset: a BAR0 offset through
; the register window, a framebuffer-window offset through its window. A
; window that did not map reads as zero.
BeginProc N5_Read_Snapshot
    pushad
    mov     ecx, N5Result[16]
    mov     ebx, OFFSET32 N5Offsets
    jecxz   short N5_Read_Snapshot_Done
N5_Read_Snapshot_Next:
    mov     eax, [ebx]
    test    eax, N5_FB_FLAG
    jnz     short N5_Read_Fb
    mov     esi, N5Mmio
    jmp     short N5_Read_Word
N5_Read_Fb:
    mov     edx, eax
    shr     edx, N5_FB_WINDOW_SHIFT
    and     edx, N5_FB_WINDOW_MASK
    mov     esi, N5FbLinear[edx*4]
    and     eax, N5_FB_OFFSET_MASK
    test    esi, esi
    jnz     short N5_Read_Word
    xor     eax, eax
    jmp     short N5_Read_Store
N5_Read_Word:
    mov     eax, [esi+eax]
N5_Read_Store:
    mov     [edi], eax
    add     ebx, 4
    add     edi, 4
    dec     ecx
    jnz     short N5_Read_Snapshot_Next
N5_Read_Snapshot_Done:
    popad
    ret
EndProc N5_Read_Snapshot

; EAX = BAR as read. Returns CF clear and EAX = base if it is a 16 MiB-
; aligned, non-zero memory BAR.
BeginProc N5_Memory_Base
    test    eax, N5_BAR_IO
    jnz     short N5_Memory_Base_Bad
    and     eax, NOT 0fh
    jz      short N5_Memory_Base_Bad
    test    eax, N5_BASE_ALIGN
    jnz     short N5_Memory_Base_Bad
    clc
    ret
N5_Memory_Base_Bad:
    stc
    ret
EndProc N5_Memory_Base

; EAX = configuration offset of a 64-bit BAR. Returns CF clear and EAX =
; base if it is a 64-bit prefetchable memory BAR below 4 GiB, 16 MiB-
; aligned, as both of the card's video memory BARs read on boot 82.
BeginProc N5_Bar64_Base
    push    ebx
    mov     ebx, eax
    mov     eax, N5Config
    or      eax, ebx
    add     eax, 4
    call    N5_Pci_Read
    test    eax, eax
    jnz     short N5_Bar64_Bad
    mov     eax, N5Config
    or      eax, ebx
    call    N5_Pci_Read
    mov     ebx, eax
    and     ebx, 0fh
    cmp     ebx, N5_BAR_PREFETCH_64
    jne     short N5_Bar64_Bad
    call    N5_Memory_Base
    pop     ebx
    ret
N5_Bar64_Bad:
    pop     ebx
    stc
    ret
EndProc N5_Bar64_Base

; Map the four framebuffer windows. Any that fails stays zero and reads
; as zero; the status says whether all four mapped.
BeginProc N5_Map_Fb
    pushad
    mov     eax, N5_CFG_BAR3
    call    N5_Bar64_Base
    jc      short N5_Map_Fb_Bar1
    mov     N5Result[44], eax
    mov     ebx, eax
    add     eax, N5_FB_HIGH
    VMMcall _MapPhysToLinear,<eax,N5_FB_WINDOW_BYTES,0>
    cmp     eax, 0ffffffffh
    je      short N5_Map_Fb_Bar3_Low
    mov     N5FbLinear[N5_FB_LFB*4], eax
N5_Map_Fb_Bar3_Low:
    VMMcall _MapPhysToLinear,<ebx,N5_FB_WINDOW_BYTES,0>
    cmp     eax, 0ffffffffh
    je      short N5_Map_Fb_Bar1
    mov     N5FbLinear[N5_FB_BAR3_LOW*4], eax

N5_Map_Fb_Bar1:
    mov     eax, N5_CFG_BAR1
    call    N5_Bar64_Base
    jc      short N5_Map_Fb_Check
    mov     N5Result[36], eax
    mov     ebx, eax
    add     eax, N5_FB_HIGH
    VMMcall _MapPhysToLinear,<eax,N5_FB_WINDOW_BYTES,0>
    cmp     eax, 0ffffffffh
    je      short N5_Map_Fb_Bar1_Low
    mov     N5FbLinear[N5_FB_BAR1_HIGH*4], eax
N5_Map_Fb_Bar1_Low:
    VMMcall _MapPhysToLinear,<ebx,N5_FB_WINDOW_BYTES,0>
    cmp     eax, 0ffffffffh
    je      short N5_Map_Fb_Check
    mov     N5FbLinear[N5_FB_BAR1_LOW*4], eax

N5_Map_Fb_Check:
    xor     ecx, ecx
N5_Map_Fb_Next:
    cmp     N5FbLinear[ecx*4], 0
    je      short N5_Map_Fb_Done
    inc     ecx
    cmp     ecx, 4
    jb      short N5_Map_Fb_Next
    or      N5Result[4], N5_ST_FB_MAPPED
N5_Map_Fb_Done:
    popad
    ret
EndProc N5_Map_Fb

; EDI = destination (N5First or N5Second). Reads N5_WR_DWORDS from each
; mapped write-test page into it, window-major.
BeginProc N5_Write_Read_All
    pushad
    cld
    xor     ebx, ebx
N5_Wra_Window:
    mov     esi, N5WriteLinear[ebx*4]
    xor     ecx, ecx
N5_Wra_Dword:
    mov     eax, [esi+ecx*4]
    stosd
    inc     ecx
    cmp     ecx, N5_WR_DWORDS
    jb      short N5_Wra_Dword
    inc     ebx
    cmp     ebx, N5_WR_WINDOWS
    jb      short N5_Wra_Window
    popad
    ret
EndProc N5_Write_Read_All

; The write test; see N5_DIOC_FBWRITE in the header. Every page must map,
; or nothing is written.
BeginProc N5_Write_Test
    pushad
    mov     eax, N5_CFG_BAR1
    call    N5_Bar64_Base
    jc      N5_Write_Test_Done
    mov     N5Result[36], eax
    mov     ebp, eax
    xor     ebx, ebx
N5_Wt_Map:
    mov     eax, ebp
    add     eax, N5WriteTargets[ebx*4]
    VMMcall _MapPhysToLinear,<eax,N5_WR_PAGE,0>
    cmp     eax, 0ffffffffh
    je      N5_Write_Test_Done
    mov     N5WriteLinear[ebx*4], eax
    inc     ebx
    cmp     ebx, N5_WR_WINDOWS
    jb      short N5_Wt_Map

    ; Save every page's first dwords into N5Offsets.
    mov     edi, OFFSET32 N5Offsets
    call    N5_Write_Read_All

    ; Patterns into the targets, windows 1 onwards.
    mov     ebx, 1
N5_Wt_Pattern_Window:
    mov     esi, N5WriteLinear[ebx*4]
    xor     ecx, ecx
N5_Wt_Pattern_Dword:
    mov     eax, ebx
    shl     eax, 16
    or      eax, N5_WR_PATTERN
    lea     edx, [ecx*4]
    or      eax, edx
    mov     [esi+ecx*4], eax
    inc     ecx
    cmp     ecx, N5_WR_DWORDS
    jb      short N5_Wt_Pattern_Dword
    inc     ebx
    cmp     ebx, N5_WR_WINDOWS
    jb      short N5_Wt_Pattern_Window

    mov     edi, OFFSET32 N5First
    call    N5_Write_Read_All

    ; Restore the targets from last to first, then BAR1 + 0.
    mov     ebx, N5_WR_WINDOWS - 1
N5_Wt_Restore_Window:
    mov     esi, N5WriteLinear[ebx*4]
    mov     edx, ebx
    shl     edx, 6                  ; N5_WR_DWORDS * 4 bytes per window
    xor     ecx, ecx
N5_Wt_Restore_Dword:
    mov     eax, N5Offsets[edx+ecx*4]
    mov     [esi+ecx*4], eax
    inc     ecx
    cmp     ecx, N5_WR_DWORDS
    jb      short N5_Wt_Restore_Dword
    dec     ebx
    jns     short N5_Wt_Restore_Window

    mov     edi, OFFSET32 N5Second
    call    N5_Write_Read_All
    mov     N5Result[16], N5_WR_WINDOWS * N5_WR_DWORDS
    or      N5Result[4], N5_ST_WRITE_DONE
N5_Write_Test_Done:
    popad
    ret
EndProc N5_Write_Test

; EBP = DIOC input buffer, ECX = its byte count. N5_DIOC_HWSQ: the input
; list is script words, not offsets; copies up to N5_HWSQ_WORDS_MAX.
BeginProc N5_Accept_Script
    pushad
    mov     N5Result[16], 0
    sub     ecx, N5_IN_OFFSETS
    shr     ecx, 2
    mov     eax, [ebp+N5_IN_COUNT]
    test    eax, eax
    jz      short N5_Script_Refused
    cmp     eax, N5_HWSQ_WORDS_MAX
    ja      short N5_Script_Refused
    cmp     eax, ecx
    ja      short N5_Script_Refused
    mov     ecx, eax
    lea     esi, [ebp+N5_IN_OFFSETS]
    mov     edi, OFFSET32 N5Offsets
    cld
    rep     movsd
    mov     N5Result[16], eax
    popad
    ret
N5_Script_Refused:
    or      N5Result[4], N5_ST_OFFSETS_REFUSED
    popad
    ret
EndProc N5_Accept_Script

; Runs the script in N5Offsets on the PBUS sequencer, as nouveau's
; g94_bus_hwsq_exec does (subdev\bus\g94.c): 1098h bit 3 off, 1304h and
; 1318h zero, the words at 80000h, 1098h bits 4:3 on, 130Ch = 1, then
; wait up to ~2 s for 1308h bit 8 to clear. Then the readback list is put
; in N5Offsets and read twice. Header dword 12 = spins left.
BeginProc N5_Hwsq_Run
    pushad
    mov     esi, N5Mmio
    and     dword ptr [esi+1098h], NOT 00000008h
    mov     dword ptr [esi+1304h], 0
    mov     dword ptr [esi+1318h], 0
    mov     ecx, N5Result[16]
    xor     ebx, ebx
N5_Hwsq_Copy:
    mov     eax, N5Offsets[ebx*4]
    mov     [esi+80000h+ebx*4], eax
    inc     ebx
    dec     ecx
    jnz     short N5_Hwsq_Copy
    or      dword ptr [esi+1098h], 00000018h
    mov     dword ptr [esi+130ch], 1
    mov     ecx, N5_HWSQ_SPINS
N5_Hwsq_Poll:
    test    dword ptr [esi+1308h], 00000100h
    jz      short N5_Hwsq_Finished
    dec     ecx
    jnz     short N5_Hwsq_Poll
    or      N5Result[4], N5_ST_HWSQ_TIMEOUT
    jmp     short N5_Hwsq_Read_Back
N5_Hwsq_Finished:
    or      N5Result[4], N5_ST_HWSQ_DONE
N5_Hwsq_Read_Back:
    mov     N5Result[48], ecx
    mov     esi, OFFSET32 N5HwsqReadback
    mov     edi, OFFSET32 N5Offsets
    mov     ecx, N5_HWSQ_READBACK
    cld
    rep     movsd
    mov     N5Result[16], N5_HWSQ_READBACK
    mov     edi, OFFSET32 N5First
    call    N5_Read_Snapshot
    mov     edi, OFFSET32 N5Second
    call    N5_Read_Snapshot
    popad
    ret
EndProc N5_Hwsq_Run

BeginProc N5_Capture
    pushad

    ; A configuration address: enable bit set, register field zero, and
    ; nothing in the reserved bits 30:24.
    mov     eax, N5Config
    mov     N5Result[32], eax
    test    eax, 80000000h
    jz      N5_Capture_Bad_Address
    test    eax, 7f0000ffh
    jnz     N5_Capture_Bad_Address

    call    N5_Pci_Read
    mov     N5Result[8], eax
    cmp     eax, N5_ID
    je      short N5_Capture_Check_Base
    cmp     eax, N5_ID_G96
    je      short N5_Capture_Check_Base
    or      N5Result[4], N5_ST_NOT_GT218
    jmp     N5_Capture_Done

N5_Capture_Check_Base:
    or      N5Result[4], N5_ST_GT218
    mov     eax, N5Config
    or      eax, N5_CFG_REVISION
    call    N5_Pci_Read
    mov     N5Result[40], eax
    mov     eax, N5Config
    or      eax, N5_CFG_BAR0
    call    N5_Pci_Read
    mov     N5Result[12], eax
    call    N5_Memory_Base
    jc      N5_Capture_Bad_Base

    VMMcall _MapPhysToLinear,<eax,N5_MMIO_BYTES,0>
    cmp     eax, 0ffffffffh
    je      N5_Capture_Done
    mov     N5Mmio, eax
    or      N5Result[4], N5_ST_MMIO_MAPPED

    ; PMC_BOOT_0: chipset A8h, or 96h for the G96. A window that decodes
    ; anything else is never sampled. The id is not paired with the
    ; chipset: either id with either chipset is a Tesla behind BAR0.
    mov     edx, [eax]
    mov     N5Result[20], edx
    and     edx, N5_BOOT_CHIPSET_MASK
    cmp     edx, N5_BOOT_CHIPSET_GT218
    je      short N5_Capture_Identity
    cmp     edx, N5_BOOT_CHIPSET_G96
    jne     N5_Capture_Done
N5_Capture_Identity:
    or      N5Result[4], N5_ST_IDENTITY_MATCH

    ; The sequencer runs on the G96 only: the script is built for
    ; g84_clk's registers and g94_bus's sequencer.
    cmp     N5Request, N5_DIOC_HWSQ
    jne     short N5_Capture_Not_Hwsq
    mov     edx, N5Result[20]
    and     edx, N5_BOOT_CHIPSET_MASK
    cmp     edx, N5_BOOT_CHIPSET_G96
    jne     N5_Capture_Done
    call    N5_Hwsq_Run
    or      N5Result[4], N5_ST_SNAPSHOT_TAKEN
    jmp     N5_Capture_Done
N5_Capture_Not_Hwsq:
    cmp     N5Request, N5_DIOC_FBWRITE
    jne     short N5_Capture_Not_Write
    call    N5_Write_Test
    or      N5Result[4], N5_ST_SNAPSHOT_TAKEN
    jmp     short N5_Capture_Done
N5_Capture_Not_Write:
    cmp     N5Request, N5_DIOC_FB
    jne     short N5_Capture_Read
    call    N5_Map_Fb

N5_Capture_Read:
    mov     edi, OFFSET32 N5First
    call    N5_Read_Snapshot
    mov     edi, OFFSET32 N5Second
    call    N5_Read_Snapshot
    or      N5Result[4], N5_ST_SNAPSHOT_TAKEN
    jmp     short N5_Capture_Done

N5_Capture_Bad_Address:
    or      N5Result[4], N5_ST_ADDRESS_INVALID
    jmp     short N5_Capture_Done
N5_Capture_Bad_Base:
    or      N5Result[4], N5_ST_BASE_INVALID
N5_Capture_Done:
    popad
    ret
EndProc N5_Capture

BeginProc N5_Reset_Result
    pushad
    mov     edi, OFFSET32 N5Result + 4
    mov     ecx, N5_RESULT_DWORDS - 1
    xor     eax, eax
    cld
    rep     stosd
    mov     N5Mmio, eax
    mov     edi, OFFSET32 N5FbLinear
    mov     ecx, 4
    rep     stosd
    mov     edi, OFFSET32 N5WriteLinear
    mov     ecx, N5_WR_WINDOWS
    rep     stosd
    popad
    ret
EndProc N5_Reset_Result

BeginProc N5_W32_DeviceIoControl
    cmp     ecx, DIOC_OPEN
    je      N5_Dioc_Ok
    cmp     ecx, DIOC_CLOSEHANDLE
    je      N5_Dioc_Ok
    cmp     ecx, N5_DIOC_CAPTURE
    je      short N5_Dioc_Known
    cmp     ecx, N5_DIOC_FB
    je      short N5_Dioc_Known
    cmp     ecx, N5_DIOC_FBWRITE
    je      short N5_Dioc_Known
    cmp     ecx, N5_DIOC_HWSQ
    jne     N5_Dioc_Fail
N5_Dioc_Known:
    mov     N5Request, ecx

    pushad
    mov     ebx, esi
    mov     ebp, [ebx.lpvInBuffer]
    test    ebp, ebp
    jz     N5_Dioc_Copy_Fail
    mov     ecx, [ebx.cbInBuffer]
    cmp     ecx, N5_IN_MIN_BYTES
    jb     N5_Dioc_Copy_Fail
    mov     edi, [ebx.lpvOutBuffer]
    test    edi, edi
    jz     N5_Dioc_Copy_Fail
    cmp     [ebx.cbOutBuffer], N5_RESULT_DWORDS * 4
    jb     N5_Dioc_Copy_Fail

    call    N5_Reset_Result
    mov     eax, [ebp+N5_IN_CONFIG]
    mov     N5Config, eax
    cmp     N5Request, N5_DIOC_HWSQ
    jne     short N5_Dioc_Offsets
    call    N5_Accept_Script
    jmp     short N5_Dioc_Accepted
N5_Dioc_Offsets:
    call    N5_Accept_Offsets
N5_Dioc_Accepted:
    test    N5Result[4], N5_ST_OFFSETS_REFUSED
    jnz     short N5_Dioc_Copy_Out
    call    N5_Capture

N5_Dioc_Copy_Out:
    mov     edi, [ebx.lpvOutBuffer]
    mov     esi, OFFSET32 N5Result
    mov     ecx, N5_RESULT_DWORDS
    cld
    rep     movsd
    mov     eax, [ebx.lpcbBytesReturned]
    test    eax, eax
    jz      short N5_Dioc_Copy_Done
    mov     dword ptr [eax], N5_RESULT_DWORDS * 4
N5_Dioc_Copy_Done:
    popad
N5_Dioc_Ok:
    xor     eax, eax
    ret

N5_Dioc_Copy_Fail:
    popad
N5_Dioc_Fail:
    mov     eax, 1
    ret
EndProc N5_W32_DeviceIoControl

BeginProc N5_Dynamic_Init
    clc
    ret
EndProc N5_Dynamic_Init

BeginProc N5_Dynamic_Exit
    clc
    ret
EndProc N5_Dynamic_Exit

Begin_Control_Dispatch N5
    Control_Dispatch Sys_Dynamic_Device_Init, N5_Dynamic_Init
    Control_Dispatch Sys_Dynamic_Device_Exit, N5_Dynamic_Exit
    Control_Dispatch W32_DeviceIoControl, N5_W32_DeviceIoControl
End_Control_Dispatch N5

VxD_LOCKED_CODE_ENDS

end
