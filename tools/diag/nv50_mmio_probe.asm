; NVIDIA GT218 (GeForce 210) register probe VxD.
;
; Phase 2 of docs\plans\nvidia-gt218.md. Maps BAR0, the 16 MiB register
; window, and reads a caller's list of offsets, each twice. The RIVA 128
; probe (nv3_mmio_probe.asm) is the model, without its BAR1 windows and
; alias test.
;
; Writes: nothing, ever. 0CF8h is saved and restored around each
; configuration read. Mapping a physical range with _MapPhysToLinear
; changes no device state.
;
; The caller names the device by its configuration address; the VxD reads
; the id and BAR0 itself and refuses anything that is not PCI 10DE:0A65
; with a 16 MiB-aligned memory BAR0. Once BAR0 is mapped, PMC_BOOT_0
; (BAR0 + 0) must name chipset A8h in bits 28:20, nouveau's chipset field
; and its number for the GT218, or nothing else is read.
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
; PFIFO, PGRAPH and the copy engine are in the list but V9XN5MM.EXE asks
; for each only when PMC_ENABLE shows it on, as the NV3 probe does. PDISP
; has no enable or reset bit in nouveau's GT215 map (subdev\mc\gt215.c),
; and drives the desktop being probed, so it is read unconditionally.
; The PRAMIN window (700000h-7FFFFFh) is accepted whole: it is video memory
; through whatever base PBUS 1700h already holds, and this VxD never moves
; it.

.386p

.xlist
include VMM.INC
include VWIN32.INC
.list

Declare_Virtual_Device N5MM, 1, 0, N5_Control, \
                       Undefined_Device_ID, Undefined_Init_Order, , ,

N5_MAGIC                equ 4d4d354eh ; "N5MM"
N5_DIOC_CAPTURE         equ 1
N5_ID                   equ 0a6510deh ; device 0A65h, vendor 10DEh
N5_CFG_REVISION         equ 08h
N5_CFG_BAR0             equ 10h
N5_MMIO_BYTES           equ 01000000h
N5_BASE_ALIGN           equ 00ffffffh
N5_BAR_IO               equ 00000001h
N5_BOOT_CHIPSET_MASK    equ 1ff00000h
N5_BOOT_CHIPSET_GT218   equ 0a800000h
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
    dd 009400h, 009408h ; PTIMER time
    dd 100200h, 100210h ; PFB config 0, 1, 208, memory size
    dd 100714h, 100718h ; PFB memory type
    dd 101000h, 101004h ; PSTRAPS
    dd 104040h, 104044h ; copy engine falcon status
    dd 104100h, 104110h ; copy engine falcon CPUCTL, BOOTVEC, HWCFG, 10C
    dd 400100h, 400104h ; PGRAPH_INTR
    dd 400108h, 40010ch ; PGRAPH trap status
    dd 40032ch, 400330h ; PGRAPH context instance
    dd 610020h, 610034h ; PDISP interrupts, supervisor
    dd 610184h, 610188h ; PDISP capabilities
    dd 610200h, 610204h ; PDISP core channel control
    dd 610a00h, 610c00h ; PDISP core state mirrors, head 0 and outputs
    dd 610f40h, 611140h ; PDISP core state mirrors, head 1
    dd 700000h, 800000h ; PRAMIN window
N5_WINDOW_COUNT         equ 30

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
    dd 0                        ; 09 reserved
    dd 0                        ; 10 class and revision at 08h
    dd 0                        ; 11 reserved
    dd 0                        ; 12 reserved
    dd 0                        ; 13 reserved
    dd 0                        ; 14 reserved
    dd 0                        ; 15 reserved
N5Offsets    dd N5_OFFSET_MAX dup (0)
N5First      dd N5_OFFSET_MAX dup (0)
N5Second     dd N5_OFFSET_MAX dup (0)
N5Config     dd 0
N5Mmio       dd 0
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

; EDI = destination. Reads every accepted offset through BAR0.
BeginProc N5_Read_Snapshot
    pushad
    mov     ecx, N5Result[16]
    mov     ebx, OFFSET32 N5Offsets
    mov     esi, N5Mmio
    jecxz   short N5_Read_Snapshot_Done
N5_Read_Snapshot_Next:
    mov     eax, [ebx]
    mov     eax, [esi+eax]
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

    ; PMC_BOOT_0: chipset A8h. A window that decodes anything else is
    ; never sampled.
    mov     edx, [eax]
    mov     N5Result[20], edx
    and     edx, N5_BOOT_CHIPSET_MASK
    cmp     edx, N5_BOOT_CHIPSET_GT218
    jne     N5_Capture_Done
    or      N5Result[4], N5_ST_IDENTITY_MATCH

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
    popad
    ret
EndProc N5_Reset_Result

BeginProc N5_W32_DeviceIoControl
    cmp     ecx, DIOC_OPEN
    je      N5_Dioc_Ok
    cmp     ecx, DIOC_CLOSEHANDLE
    je      N5_Dioc_Ok
    cmp     ecx, N5_DIOC_CAPTURE
    jne     N5_Dioc_Fail

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
    call    N5_Accept_Offsets
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
