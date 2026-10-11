; 3dfx VSA-100 read-only register probe VxD.
;
; Loaded dynamically by VSA100.EXE. It finds PCI 121A:0009 with configuration
; mechanism 1, copies function 0's 256-byte configuration header, counts the
; other functions of the same device that answer with vendor 121A (one per
; extra chip on a Voodoo5), reads the sequencer (SR00-SR3F) and CRTC
; (CR00-CR3F) registers through the standard VGA ports, reads the BAR0
; offsets VSA100.EXE supplied twice, and copies the shadowed video BIOS at
; C0000h.
;
; Writes, all restored or data-neutral:
; - the sequencer and CRTC index ports, each read before its loop and
;   restored after it, with interrupts off throughout;
; - 0CF8h, saved and restored around every configuration read;
; - the lfbMemoryConfig selector (BAR0 + 0Ch), only when VSA100.EXE passes
;   the tile flag. A write with bit 30 set selects which of tile control or
;   tile compare a read returns and changes neither (Napalm spec bit table;
;   held on the Mini-Me under VelocityNT, 2026-10-11). The value a plain read
;   returned first is matched against the two and the matching selector is
;   written back, so the next reader sees what it saw before.
;
; Offsets are accepted only inside three BAR0 windows: the init and video
; registers (0-FFh), the command FIFO registers (80000h-800FFh) and the 2D
; registers (100000h-1000FFh). The 2D launch area (100080h and up) is
; write-only data and is refused. VSA100.EXE asks for the FIFO and 2D windows
; only under /engine.
;
; Register meanings: docs\specifications\vsa100-registers.md.

.386p

.xlist
include VMM.INC
include MINIVDD.INC
include VWIN32.INC
.list

Declare_Virtual_Device VSA100, 1, 0, Vsa_Control, \
                       Undefined_Device_ID, Undefined_Init_Order, , ,

VSA_MAGIC               equ 30315356h ; "VS10"
VSA_DIOC_CAPTURE        equ 1
VSA_PCI_ID              equ 0009121ah
VSA_VENDOR              equ 121ah
VSA_CONFIG_DWORDS       equ 64
VSA_VGA_INDEXES         equ 40h
VSA_OFFSET_MAX          equ 256
VSA_FUNCTIONS           equ 8
VSA_ROM_PHYSICAL        equ 000c0000h
VSA_ROM_BYTES           equ 00010000h

; BAR0 windows, each mapped as one 4 KiB page from its base.
VSA_WINDOW_MAP_BYTES    equ 1000h
VSA_IO_BASE             equ 000000h
VSA_IO_LIMIT            equ 000100h
VSA_FIFO_BASE           equ 080000h
VSA_FIFO_LIMIT          equ 080100h
VSA_2D_BASE             equ 100000h
VSA_2D_LIMIT            equ 100080h

; lfbMemoryConfig and its selector values (Napalm spec; see the header).
VSA_LFB_MEMORY_CONFIG   equ 0ch
VSA_SELECT_CONTROL      equ 40000000h
VSA_SELECT_COMPARE      equ 60000000h

; Status bits returned to the Win32 publisher.
VSA_PCI_FOUND           equ 00000001h
VSA_VGA_READ            equ 00000002h
VSA_IO_MAPPED           equ 00000004h
VSA_SNAPSHOT_TAKEN      equ 00000008h
VSA_ROM_COPIED          equ 00000010h
VSA_OFFSETS_REFUSED     equ 00000020h
VSA_BAR0_INVALID        equ 00000040h
VSA_FIFO_MAPPED         equ 00000080h
VSA_2D_MAPPED           equ 00000100h
VSA_TILE_READ           equ 00000200h
VSA_TILE_AMBIGUOUS      equ 00000400h

; Input from VSA100.EXE.
VSA_IN_BAR0             equ 0
VSA_IN_FLAGS            equ 4
VSA_IN_COUNT            equ 8
VSA_IN_OFFSETS          equ 12
VSA_IN_MIN_BYTES        equ 12
VSA_FLAG_TILE           equ 00000001h
VSA_FLAG_ENGINE         equ 00000002h

; Output layout, in dwords. Must match struct vsa_result in
; vsa100_probe_win32.c.
VSA_HEADER_DWORDS       equ 16
VSA_RESULT_DWORDS       equ (VSA_HEADER_DWORDS + VSA_CONFIG_DWORDS + \
                             VSA_FUNCTIONS + \
                             (VSA_VGA_INDEXES * 2) / 4 + \
                             VSA_OFFSET_MAX * 3 + VSA_ROM_BYTES / 4)

VxD_LOCKED_DATA_SEG
VsaResult label dword
    dd VSA_MAGIC                ; 00 magic
    dd 0                        ; 01 status
    dd 0                        ; 02 PCI config address/BDF of function 0
    dd 0                        ; 03 BAR0 physical base used
    dd 0                        ; 04 offset count accepted
    dd 0                        ; 05 0CF8h value found and restored
    dd 0                        ; 06 misc output register (3CCh)
    dd 0                        ; 07 sequencer index found and restored
    dd 0                        ; 08 CRTC index port used
    dd 0                        ; 09 CRTC index found and restored
    dd 0                        ; 10 functions answering with vendor 121A
    dd 0                        ; 11 lfbMemoryConfig, plain read first
    dd 0                        ; 12 tile control
    dd 0                        ; 13 tile compare
    dd 0                        ; 14 selector written back
    dd 0                        ; 15 reserved
VsaConfig    dd VSA_CONFIG_DWORDS dup (0)
VsaFunctions dd VSA_FUNCTIONS dup (0)
VsaSr        db VSA_VGA_INDEXES dup (0)
VsaCr        db VSA_VGA_INDEXES dup (0)
VsaOffsets   dd VSA_OFFSET_MAX dup (0)
VsaFirst     dd VSA_OFFSET_MAX dup (0)
VsaSecond    dd VSA_OFFSET_MAX dup (0)
VsaRom       dd (VSA_ROM_BYTES / 4) dup (0)
VsaBar0      dd 0
VsaFlags     dd 0
VsaIoLinear  dd 0
VsaFifoLinear dd 0
Vsa2dLinear  dd 0
VxD_LOCKED_DATA_ENDS

VxD_LOCKED_CODE_SEG

; EAX = PCI configuration address. Returns EAX = configuration dword.
; 0CF8h is saved and restored with interrupts off, so a configuration cycle
; another driver had addressed is not left pointing somewhere else.
BeginProc Vsa_Pci_Read
    push    ebx
    push    edx
    pushfd
    cli
    mov     ebx, eax
    mov     dx, 0cf8h
    in      eax, dx
    mov     VsaResult[20], eax
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
EndProc Vsa_Pci_Read

; DX = index port, EDI = destination. Reads indexes 00h-3Fh through DX/DX+1
; and restores the index found. Caller has interrupts off.
; Returns AL = the index that was found and restored.
BeginProc Vsa_Read_Indexed
    push    ebx
    push    ecx
    in      al, dx
    mov     bl, al
    xor     ecx, ecx
Vsa_Read_Indexed_Next:
    mov     al, cl
    out     dx, al
    inc     dx
    in      al, dx
    dec     dx
    mov     [edi+ecx], al
    inc     ecx
    cmp     ecx, VSA_VGA_INDEXES
    jb      short Vsa_Read_Indexed_Next
    mov     al, bl
    out     dx, al
    pop     ecx
    pop     ebx
    ret
EndProc Vsa_Read_Indexed

; Sequencer SR00-SR3F and CRTC CR00-CR3F, each index restored.
BeginProc Vsa_Read_Vga
    pushad
    pushfd
    cli

    mov     dx, 03cch
    in      al, dx
    movzx   eax, al
    mov     VsaResult[24], eax

    mov     dx, 03c4h
    mov     edi, OFFSET32 VsaSr
    call    Vsa_Read_Indexed
    movzx   eax, al
    mov     VsaResult[28], eax

    ; Misc output D0 selects the colour (3D4h) or mono (3B4h) CRTC address.
    mov     dx, 03d4h
    test    byte ptr VsaResult[24], 1
    jnz     short Vsa_Read_Vga_Crtc
    mov     dx, 03b4h
Vsa_Read_Vga_Crtc:
    movzx   eax, dx
    mov     VsaResult[32], eax
    mov     edi, OFFSET32 VsaCr
    call    Vsa_Read_Indexed
    movzx   eax, al
    mov     VsaResult[36], eax

    or      VsaResult[4], VSA_VGA_READ
    popfd
    popad
    ret
EndProc Vsa_Read_Vga

; EAX = BAR0 offset. Returns EAX = linear address of that offset, or 0 when
; its window is not mapped.
BeginProc Vsa_Linear_Of
    cmp     eax, VSA_IO_LIMIT
    jae     short Vsa_Linear_Of_Fifo
    add     eax, VsaIoLinear
    cmp     VsaIoLinear, 0
    jne     short Vsa_Linear_Of_Done
    xor     eax, eax
    ret
Vsa_Linear_Of_Fifo:
    cmp     eax, VSA_FIFO_LIMIT
    jae     short Vsa_Linear_Of_2d
    sub     eax, VSA_FIFO_BASE
    add     eax, VsaFifoLinear
    cmp     VsaFifoLinear, 0
    jne     short Vsa_Linear_Of_Done
    xor     eax, eax
    ret
Vsa_Linear_Of_2d:
    sub     eax, VSA_2D_BASE
    add     eax, Vsa2dLinear
    cmp     Vsa2dLinear, 0
    jne     short Vsa_Linear_Of_Done
    xor     eax, eax
Vsa_Linear_Of_Done:
    ret
EndProc Vsa_Linear_Of

; EDI = destination, ECX = accepted count. An offset whose window is not
; mapped reads as zero.
BeginProc Vsa_Read_Snapshot
    pushad
    mov     ebx, OFFSET32 VsaOffsets
    jecxz   short Vsa_Read_Snapshot_Done
Vsa_Read_Snapshot_Next:
    mov     eax, [ebx]
    call    Vsa_Linear_Of
    test    eax, eax
    jz      short Vsa_Read_Snapshot_Store
    mov     eax, [eax]
Vsa_Read_Snapshot_Store:
    mov     [edi], eax
    add     ebx, 4
    add     edi, 4
    dec     ecx
    jnz     short Vsa_Read_Snapshot_Next
Vsa_Read_Snapshot_Done:
    popad
    ret
EndProc Vsa_Read_Snapshot

; The lfbMemoryConfig selector protocol, interrupts off: a plain read, then
; control and compare through their selectors, then the selector whose value
; matched the plain read written back. If neither or both matched, control
; is selected and the ambiguity is reported.
BeginProc Vsa_Read_Tile
    pushad
    pushfd
    cli
    mov     esi, VsaIoLinear
    mov     eax, [esi+VSA_LFB_MEMORY_CONFIG]
    mov     VsaResult[44], eax
    mov     dword ptr [esi+VSA_LFB_MEMORY_CONFIG], VSA_SELECT_CONTROL
    mov     ebx, [esi+VSA_LFB_MEMORY_CONFIG]
    mov     VsaResult[48], ebx
    mov     dword ptr [esi+VSA_LFB_MEMORY_CONFIG], VSA_SELECT_COMPARE
    mov     ecx, [esi+VSA_LFB_MEMORY_CONFIG]
    mov     VsaResult[52], ecx

    mov     edx, VSA_SELECT_CONTROL
    cmp     eax, ebx
    jne     short Vsa_Read_Tile_Try_Compare
    cmp     eax, ecx
    je      short Vsa_Read_Tile_Ambiguous
    jmp     short Vsa_Read_Tile_Restore
Vsa_Read_Tile_Try_Compare:
    mov     edx, VSA_SELECT_COMPARE
    cmp     eax, ecx
    je      short Vsa_Read_Tile_Restore
    mov     edx, VSA_SELECT_CONTROL
Vsa_Read_Tile_Ambiguous:
    or      VsaResult[4], VSA_TILE_AMBIGUOUS
Vsa_Read_Tile_Restore:
    mov     [esi+VSA_LFB_MEMORY_CONFIG], edx
    mov     VsaResult[56], edx
    or      VsaResult[4], VSA_TILE_READ
    popfd
    popad
    ret
EndProc Vsa_Read_Tile

; Copy the 64 KiB shadow at C0000h. Shadow RAM, read-only here.
BeginProc Vsa_Copy_Rom
    pushad
    VMMcall _MapPhysToLinear,<VSA_ROM_PHYSICAL,VSA_ROM_BYTES,0>
    cmp     eax, 0ffffffffh
    je      short Vsa_Copy_Rom_Done
    test    eax, eax
    jz      short Vsa_Copy_Rom_Done
    mov     esi, eax
    mov     edi, OFFSET32 VsaRom
    mov     ecx, VSA_ROM_BYTES / 4
    cld
    rep     movsd
    or      VsaResult[4], VSA_ROM_COPIED
Vsa_Copy_Rom_Done:
    popad
    ret
EndProc Vsa_Copy_Rom

; EBP = DIOC input buffer, ECX = its byte count. Copies the offset list,
; refusing the whole list if any entry is unaligned or outside the windows.
BeginProc Vsa_Accept_Offsets
    pushad
    mov     VsaResult[16], 0
    sub     ecx, VSA_IN_OFFSETS
    shr     ecx, 2
    mov     eax, [ebp+VSA_IN_COUNT]
    cmp     eax, VSA_OFFSET_MAX
    ja      short Vsa_Accept_Offsets_Refused
    cmp     eax, ecx
    ja      short Vsa_Accept_Offsets_Refused
    mov     ecx, eax
    lea     esi, [ebp+VSA_IN_OFFSETS]
    mov     edi, OFFSET32 VsaOffsets
    jecxz   short Vsa_Accept_Offsets_Done
Vsa_Accept_Offsets_Next:
    mov     edx, [esi]
    test    edx, 3
    jnz     short Vsa_Accept_Offsets_Refused
    cmp     edx, VSA_IO_LIMIT
    jb      short Vsa_Accept_Offsets_Store
    cmp     edx, VSA_FIFO_BASE
    jb      short Vsa_Accept_Offsets_Refused
    cmp     edx, VSA_FIFO_LIMIT
    jb      short Vsa_Accept_Offsets_Store
    cmp     edx, VSA_2D_BASE
    jb      short Vsa_Accept_Offsets_Refused
    cmp     edx, VSA_2D_LIMIT
    jae     short Vsa_Accept_Offsets_Refused
Vsa_Accept_Offsets_Store:
    mov     [edi], edx
    add     esi, 4
    add     edi, 4
    dec     ecx
    jnz     short Vsa_Accept_Offsets_Next
Vsa_Accept_Offsets_Done:
    mov     eax, [ebp+VSA_IN_COUNT]
    mov     VsaResult[16], eax
    popad
    ret
Vsa_Accept_Offsets_Refused:
    or      VsaResult[4], VSA_OFFSETS_REFUSED
    popad
    ret
EndProc Vsa_Accept_Offsets

; EAX = physical address. Returns EAX = linear address or 0.
BeginProc Vsa_Map_Window
    VMMcall _MapPhysToLinear,<eax,VSA_WINDOW_MAP_BYTES,0>
    cmp     eax, 0ffffffffh
    jne     short Vsa_Map_Window_Done
    xor     eax, eax
Vsa_Map_Window_Done:
    ret
EndProc Vsa_Map_Window

BeginProc Vsa_Capture
    pushad

    ; Scan function zero of every slot on every bus: an AGP card sits behind
    ; the host-to-AGP bridge on bus 1, not on bus 0.
    mov     ebx, 80000000h
Vsa_Capture_Pci_Next:
    mov     eax, ebx
    call    Vsa_Pci_Read
    cmp     eax, VSA_PCI_ID
    je      short Vsa_Capture_Pci_Found
    add     ebx, 0800h
    cmp     ebx, 81000000h
    jb      short Vsa_Capture_Pci_Next
    jmp     Vsa_Capture_Rom

Vsa_Capture_Pci_Found:
    or      VsaResult[4], VSA_PCI_FOUND
    mov     VsaResult[8], ebx
    mov     edi, OFFSET32 VsaConfig
    xor     ecx, ecx
Vsa_Capture_Config_Next:
    mov     eax, ebx
    or      eax, ecx
    call    Vsa_Pci_Read
    mov     [edi], eax
    add     edi, 4
    add     ecx, 4
    cmp     ecx, VSA_CONFIG_DWORDS * 4
    jb      short Vsa_Capture_Config_Next

    ; The ID dword of functions 0-7. Only the count of those answering with
    ; vendor 121A is interpreted; no other function register is read.
    mov     edi, OFFSET32 VsaFunctions
    xor     ecx, ecx
    xor     edx, edx
Vsa_Capture_Function_Next:
    mov     eax, ecx
    shl     eax, 8
    or      eax, ebx
    call    Vsa_Pci_Read
    mov     [edi+ecx*4], eax
    cmp     ax, VSA_VENDOR
    jne     short Vsa_Capture_Function_Skip
    inc     edx
Vsa_Capture_Function_Skip:
    inc     ecx
    cmp     ecx, VSA_FUNCTIONS
    jb      short Vsa_Capture_Function_Next
    mov     VsaResult[40], edx

    call    Vsa_Read_Vga

    ; BAR0 as Config Manager allocated it, matched to the live BAR by
    ; VSA100.EXE: 32 MiB or larger, so at least 32 MiB-aligned.
    mov     eax, VsaBar0
    test    eax, eax
    jz      Vsa_Capture_Bad_Bar
    test    eax, 01ffffffh
    jnz     Vsa_Capture_Bad_Bar
    mov     VsaResult[12], eax

    add     eax, VSA_IO_BASE
    call    Vsa_Map_Window
    mov     VsaIoLinear, eax
    test    eax, eax
    jz      Vsa_Capture_Rom
    or      VsaResult[4], VSA_IO_MAPPED

    test    VsaFlags, VSA_FLAG_ENGINE
    jz      short Vsa_Capture_Windows_Done
    mov     eax, VsaBar0
    add     eax, VSA_FIFO_BASE
    call    Vsa_Map_Window
    mov     VsaFifoLinear, eax
    test    eax, eax
    jz      short Vsa_Capture_Map_2d
    or      VsaResult[4], VSA_FIFO_MAPPED
Vsa_Capture_Map_2d:
    mov     eax, VsaBar0
    add     eax, VSA_2D_BASE
    call    Vsa_Map_Window
    mov     Vsa2dLinear, eax
    test    eax, eax
    jz      short Vsa_Capture_Windows_Done
    or      VsaResult[4], VSA_2D_MAPPED
Vsa_Capture_Windows_Done:

    mov     ecx, VsaResult[16]
    mov     edi, OFFSET32 VsaFirst
    call    Vsa_Read_Snapshot
    mov     edi, OFFSET32 VsaSecond
    call    Vsa_Read_Snapshot
    or      VsaResult[4], VSA_SNAPSHOT_TAKEN

    test    VsaFlags, VSA_FLAG_TILE
    jz      short Vsa_Capture_Rom
    call    Vsa_Read_Tile
    jmp     short Vsa_Capture_Rom

Vsa_Capture_Bad_Bar:
    or      VsaResult[4], VSA_BAR0_INVALID

Vsa_Capture_Rom:
    call    Vsa_Copy_Rom
    popad
    ret
EndProc Vsa_Capture

BeginProc Vsa_Reset_Result
    pushad
    mov     edi, OFFSET32 VsaResult + 4
    mov     ecx, VSA_RESULT_DWORDS - 1
    xor     eax, eax
    cld
    rep     stosd
    mov     VsaIoLinear, eax
    mov     VsaFifoLinear, eax
    mov     Vsa2dLinear, eax
    popad
    ret
EndProc Vsa_Reset_Result

BeginProc Vsa_W32_DeviceIoControl
    cmp     ecx, DIOC_OPEN
    je      short Vsa_Dioc_Ok
    cmp     ecx, DIOC_CLOSEHANDLE
    je      short Vsa_Dioc_Ok
    cmp     ecx, VSA_DIOC_CAPTURE
    jne     Vsa_Dioc_Fail

    pushad
    mov     ebx, esi
    mov     ebp, [ebx.lpvInBuffer]
    test    ebp, ebp
    jz      short Vsa_Dioc_Copy_Fail
    mov     ecx, [ebx.cbInBuffer]
    cmp     ecx, VSA_IN_MIN_BYTES
    jb      short Vsa_Dioc_Copy_Fail
    mov     edi, [ebx.lpvOutBuffer]
    test    edi, edi
    jz      short Vsa_Dioc_Copy_Fail
    cmp     [ebx.cbOutBuffer], VSA_RESULT_DWORDS * 4
    jb      short Vsa_Dioc_Copy_Fail

    call    Vsa_Reset_Result
    mov     eax, [ebp+VSA_IN_BAR0]
    mov     VsaBar0, eax
    mov     eax, [ebp+VSA_IN_FLAGS]
    mov     VsaFlags, eax
    call    Vsa_Accept_Offsets
    call    Vsa_Capture

    mov     edi, [ebx.lpvOutBuffer]
    mov     esi, OFFSET32 VsaResult
    mov     ecx, VSA_RESULT_DWORDS
    cld
    rep     movsd
    mov     eax, [ebx.lpcbBytesReturned]
    test    eax, eax
    jz      short Vsa_Dioc_Copy_Done
    mov     dword ptr [eax], VSA_RESULT_DWORDS * 4
Vsa_Dioc_Copy_Done:
    popad
Vsa_Dioc_Ok:
    xor     eax, eax
    ret

Vsa_Dioc_Copy_Fail:
    popad
Vsa_Dioc_Fail:
    mov     eax, 1
    ret
EndProc Vsa_W32_DeviceIoControl

BeginProc Vsa_Dynamic_Init
    clc
    ret
EndProc Vsa_Dynamic_Init

BeginProc Vsa_Dynamic_Exit
    clc
    ret
EndProc Vsa_Dynamic_Exit

Begin_Control_Dispatch Vsa
    Control_Dispatch Sys_Dynamic_Device_Init, Vsa_Dynamic_Init
    Control_Dispatch Sys_Dynamic_Device_Exit, Vsa_Dynamic_Exit
    Control_Dispatch W32_DeviceIoControl, Vsa_W32_DeviceIoControl
End_Control_Dispatch Vsa

VxD_LOCKED_CODE_ENDS

end
