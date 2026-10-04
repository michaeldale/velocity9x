; SiS 6326 read-only register probe VxD.
;
; Loaded dynamically by SIS6326.EXE. It finds PCI 1039:6326 with
; configuration mechanism 1, copies the 256-byte configuration header, reads
; the sequencer (SR00-SR3F) and CRTC (CR00-CR3F, CR80) registers through the
; standard VGA ports, locates the MMIO window the stock driver selected in
; SRB D[6:5], reads the offsets SIS6326.EXE supplied twice through it, and
; copies the shadowed video BIOS at C0000h.
;
; Nothing on the card is written except index registers: no MMIO store, no
; PCI configuration store, no data-port store. Reading an indexed VGA register
; needs its index written first, so the sequencer and CRTC index ports are
; written; each is read before the loop and restored after it, with
; interrupts off throughout so nothing else sees the changed index. SR5, the
; extension lock, is read and reported but never written: if the stock driver
; left the extensions locked the SR06+ values are what a locked chip returns,
; and the report says so.
;
; The 3D register block (8800h and up) is read only when SR39 D2, "Enable 3D
; Accelerator", is set, or when SIS6326.EXE passes the force flag. Whether a
; read of a disabled block is harmless is not documented.
;
; Register meanings: docs\specifications\sis6326-registers.md.

.386p

.xlist
include VMM.INC
include MINIVDD.INC
include VWIN32.INC
.list

Declare_Virtual_Device SIS6326, 1, 0, Sis6326_Control, \
                       Undefined_Device_ID, Undefined_Init_Order, , ,

SIS_MAGIC               equ 36325349h ; "SI26"
SIS_DIOC_CAPTURE        equ 1
SIS_PCI_ID              equ 63261039h
SIS_CONFIG_DWORDS       equ 64
SIS_VGA_INDEXES         equ 40h
SIS_OFFSET_MAX          equ 512
SIS_WINDOW_BYTES        equ 10000h
SIS_3D_FIRST_OFFSET     equ 8800h
SIS_ROM_PHYSICAL        equ 000c0000h
SIS_ROM_BYTES           equ 00010000h

; SRB D[6:5], the MMIO window select (datasheet 7.7.8).
SIS_SRB                 equ 0bh
SIS_SRB_MMIO_MASK       equ 60h
SIS_SRB_MMIO_A0000      equ 20h
SIS_SRB_MMIO_B0000      equ 40h
SIS_SRB_MMIO_BAR1       equ 60h
SIS_MMIO_A0000          equ 000a0000h
SIS_MMIO_B0000          equ 000b0000h

; SR39 D2, the 3D accelerator enable (datasheet 7.7.60).
SIS_SR39                equ 39h
SIS_SR39_3D_ENABLE      equ 04h

SIS_CR80                equ 80h

; Status bits returned to the Win32 publisher.
SIS_PCI_FOUND           equ 00000001h
SIS_VGA_READ            equ 00000002h
SIS_MMIO_MAPPED         equ 00000004h
SIS_SNAPSHOT_TAKEN      equ 00000008h
SIS_ROM_COPIED          equ 00000010h
SIS_OFFSETS_REFUSED     equ 00000020h
SIS_3D_SKIPPED          equ 00000040h
SIS_MMIO_DISABLED       equ 00000080h
SIS_MMIO_BAR_INVALID    equ 00000100h

; Input from SIS6326.EXE.
SIS_IN_MMIO_BAR         equ 0
SIS_IN_FLAGS            equ 4
SIS_IN_COUNT            equ 8
SIS_IN_OFFSETS          equ 12
SIS_IN_MIN_BYTES        equ 12
SIS_FLAG_FORCE_3D       equ 00000001h

; Output layout, in dwords. Must match struct sis_result in
; sis6326_probe_win32.c.
SIS_HEADER_DWORDS       equ 12
SIS_RESULT_DWORDS       equ (SIS_HEADER_DWORDS + SIS_CONFIG_DWORDS + \
                             (SIS_VGA_INDEXES * 2) / 4 + \
                             SIS_OFFSET_MAX * 3 + SIS_ROM_BYTES / 4)

VxD_LOCKED_DATA_SEG
SisResult label dword
    dd SIS_MAGIC                ; 00 magic
    dd 0                        ; 01 status
    dd 0                        ; 02 PCI config address/BDF
    dd 0                        ; 03 physical base of the MMIO window read
    dd 0                        ; 04 offset count accepted
    dd 0                        ; 05 0CF8h value found and restored
    dd 0                        ; 06 misc output register (3CCh)
    dd 0                        ; 07 sequencer index found and restored
    dd 0                        ; 08 CRTC index port used
    dd 0                        ; 09 CRTC index found and restored
    dd 0                        ; 10 CR80 (video register password)
    dd 0                        ; 11 reserved
SisConfig  dd SIS_CONFIG_DWORDS dup (0)
SisSr      db SIS_VGA_INDEXES dup (0)
SisCr      db SIS_VGA_INDEXES dup (0)
SisOffsets dd SIS_OFFSET_MAX dup (0)
SisFirst   dd SIS_OFFSET_MAX dup (0)
SisSecond  dd SIS_OFFSET_MAX dup (0)
SisRom     dd (SIS_ROM_BYTES / 4) dup (0)
SisAssignedMmio dd 0
SisFlags   dd 0
VxD_LOCKED_DATA_ENDS

VxD_LOCKED_CODE_SEG

; EAX = PCI configuration address. Returns EAX = configuration dword.
; 0CF8h is saved and restored with interrupts off, so a configuration cycle
; another driver had addressed is not left pointing somewhere else.
BeginProc Sis_Pci_Read
    push    ebx
    push    edx
    pushfd
    cli
    mov     ebx, eax
    mov     dx, 0cf8h
    in      eax, dx
    mov     SisResult[20], eax
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
EndProc Sis_Pci_Read

; DX = index port, EDI = destination. Reads indexes 00h-3Fh through DX/DX+1
; and restores the index found. Caller has interrupts off.
; Returns AL = the index that was found and restored.
BeginProc Sis_Read_Indexed
    push    ebx
    push    ecx
    in      al, dx
    mov     bl, al
    xor     ecx, ecx
Sis_Read_Indexed_Next:
    mov     al, cl
    out     dx, al
    inc     dx
    in      al, dx
    dec     dx
    mov     [edi+ecx], al
    inc     ecx
    cmp     ecx, SIS_VGA_INDEXES
    jb      short Sis_Read_Indexed_Next
    mov     al, bl
    out     dx, al
    pop     ecx
    pop     ebx
    ret
EndProc Sis_Read_Indexed

; Sequencer SR00-SR3F, CRTC CR00-CR3F and CR80, with each index restored.
BeginProc Sis_Read_Vga
    pushad
    pushfd
    cli

    mov     dx, 03cch
    in      al, dx
    movzx   eax, al
    mov     SisResult[24], eax

    mov     dx, 03c4h
    mov     edi, OFFSET32 SisSr
    call    Sis_Read_Indexed
    movzx   eax, al
    mov     SisResult[28], eax

    ; Misc output D0 selects the colour (3D4h) or mono (3B4h) CRTC address.
    mov     dx, 03d4h
    test    byte ptr SisResult[24], 1
    jnz     short Sis_Read_Vga_Crtc
    mov     dx, 03b4h
Sis_Read_Vga_Crtc:
    movzx   eax, dx
    mov     SisResult[32], eax
    mov     edi, OFFSET32 SisCr
    call    Sis_Read_Indexed
    movzx   eax, al
    mov     SisResult[36], eax

    ; CR80 alone from the video accelerator range: it is the password
    ; register, and its value says whether CR81-CRBD were readable at all.
    mov     bl, al
    mov     al, SIS_CR80
    out     dx, al
    inc     dx
    in      al, dx
    dec     dx
    movzx   eax, al
    mov     SisResult[40], eax
    mov     al, bl
    out     dx, al

    or      SisResult[4], SIS_VGA_READ
    popfd
    popad
    ret
EndProc Sis_Read_Vga

; ESI = MMIO window base, EDI = destination, ECX = accepted count.
; Offsets in the 3D block are recorded as zero when SIS_3D_SKIPPED is set.
BeginProc Sis_Read_Snapshot
    pushad
    mov     ebx, OFFSET32 SisOffsets
    jecxz   short Sis_Read_Snapshot_Done
Sis_Read_Snapshot_Next:
    mov     eax, [ebx]
    cmp     eax, SIS_3D_FIRST_OFFSET
    jb      short Sis_Read_Snapshot_Read
    test    SisResult[4], SIS_3D_SKIPPED
    jz      short Sis_Read_Snapshot_Read
    xor     eax, eax
    jmp     short Sis_Read_Snapshot_Store
Sis_Read_Snapshot_Read:
    mov     eax, [esi+eax]
Sis_Read_Snapshot_Store:
    mov     [edi], eax
    add     ebx, 4
    add     edi, 4
    dec     ecx
    jnz     short Sis_Read_Snapshot_Next
Sis_Read_Snapshot_Done:
    popad
    ret
EndProc Sis_Read_Snapshot

; Copy the 64 KiB shadow at C0000h. Shadow RAM, read-only here.
BeginProc Sis_Copy_Rom
    pushad
    VMMcall _MapPhysToLinear,<SIS_ROM_PHYSICAL,SIS_ROM_BYTES,0>
    cmp     eax, 0ffffffffh
    je      short Sis_Copy_Rom_Done
    test    eax, eax
    jz      short Sis_Copy_Rom_Done
    mov     esi, eax
    mov     edi, OFFSET32 SisRom
    mov     ecx, SIS_ROM_BYTES / 4
    cld
    rep     movsd
    or      SisResult[4], SIS_ROM_COPIED
Sis_Copy_Rom_Done:
    popad
    ret
EndProc Sis_Copy_Rom

; EBP = DIOC input buffer, ECX = its byte count. Copies the offset list,
; refusing the whole list if any entry is unaligned or outside the window.
BeginProc Sis_Accept_Offsets
    pushad
    mov     SisResult[16], 0
    sub     ecx, SIS_IN_OFFSETS
    shr     ecx, 2
    mov     eax, [ebp+SIS_IN_COUNT]
    cmp     eax, SIS_OFFSET_MAX
    ja      short Sis_Accept_Offsets_Refused
    cmp     eax, ecx
    ja      short Sis_Accept_Offsets_Refused
    mov     ecx, eax
    lea     esi, [ebp+SIS_IN_OFFSETS]
    mov     edi, OFFSET32 SisOffsets
    jecxz   short Sis_Accept_Offsets_Done
Sis_Accept_Offsets_Next:
    mov     edx, [esi]
    test    edx, 3
    jnz     short Sis_Accept_Offsets_Refused
    cmp     edx, SIS_WINDOW_BYTES
    jae     short Sis_Accept_Offsets_Refused
    mov     [edi], edx
    add     esi, 4
    add     edi, 4
    dec     ecx
    jnz     short Sis_Accept_Offsets_Next
Sis_Accept_Offsets_Done:
    mov     eax, [ebp+SIS_IN_COUNT]
    mov     SisResult[16], eax
    popad
    ret
Sis_Accept_Offsets_Refused:
    or      SisResult[4], SIS_OFFSETS_REFUSED
    popad
    ret
EndProc Sis_Accept_Offsets

BeginProc Sis_Capture
    pushad

    ; Scan function zero of every slot on every bus: an AGP card sits behind
    ; the host-to-AGP bridge on bus 1, not on bus 0.
    mov     ebx, 80000000h
Sis_Capture_Pci_Next:
    mov     eax, ebx
    call    Sis_Pci_Read
    cmp     eax, SIS_PCI_ID
    je      short Sis_Capture_Pci_Found
    add     ebx, 0800h
    cmp     ebx, 81000000h
    jb      short Sis_Capture_Pci_Next
    jmp     Sis_Capture_Rom

Sis_Capture_Pci_Found:
    or      SisResult[4], SIS_PCI_FOUND
    mov     SisResult[8], ebx
    mov     edi, OFFSET32 SisConfig
    xor     ecx, ecx
Sis_Capture_Config_Next:
    mov     eax, ebx
    or      eax, ecx
    call    Sis_Pci_Read
    mov     [edi], eax
    add     edi, 4
    add     ecx, 4
    cmp     ecx, SIS_CONFIG_DWORDS * 4
    jb      short Sis_Capture_Config_Next

    call    Sis_Read_Vga

    test    byte ptr SisSr[SIS_SR39], SIS_SR39_3D_ENABLE
    jnz     short Sis_Capture_Window
    test    SisFlags, SIS_FLAG_FORCE_3D
    jnz     short Sis_Capture_Window
    or      SisResult[4], SIS_3D_SKIPPED

    ; The window the stock driver selected. Only 11 (PCI BAR1) is the
    ; documented one for a linear-framebuffer driver; A0000h and B0000h are
    ; followed too, because what this driver chose is the thing measured.
Sis_Capture_Window:
    movzx   eax, byte ptr SisSr[SIS_SRB]
    and     al, SIS_SRB_MMIO_MASK
    jz      short Sis_Capture_No_Window
    mov     edx, SIS_MMIO_A0000
    cmp     al, SIS_SRB_MMIO_A0000
    je      short Sis_Capture_Map
    mov     edx, SIS_MMIO_B0000
    cmp     al, SIS_SRB_MMIO_B0000
    je      short Sis_Capture_Map

    ; BAR1, as Config Manager allocated it: 64 KiB, so 64 KiB-aligned.
    mov     edx, SisAssignedMmio
    test    edx, edx
    jz      short Sis_Capture_Bad_Bar
    test    edx, SIS_WINDOW_BYTES - 1
    jnz     short Sis_Capture_Bad_Bar

Sis_Capture_Map:
    mov     SisResult[12], edx
    VMMcall _MapPhysToLinear,<edx,SIS_WINDOW_BYTES,0>
    cmp     eax, 0ffffffffh
    je      short Sis_Capture_Rom
    test    eax, eax
    jz      short Sis_Capture_Rom
    or      SisResult[4], SIS_MMIO_MAPPED
    mov     esi, eax
    mov     ecx, SisResult[16]
    mov     edi, OFFSET32 SisFirst
    call    Sis_Read_Snapshot
    mov     edi, OFFSET32 SisSecond
    call    Sis_Read_Snapshot
    or      SisResult[4], SIS_SNAPSHOT_TAKEN
    jmp     short Sis_Capture_Rom

Sis_Capture_Bad_Bar:
    or      SisResult[4], SIS_MMIO_BAR_INVALID
    jmp     short Sis_Capture_Rom

Sis_Capture_No_Window:
    or      SisResult[4], SIS_MMIO_DISABLED

Sis_Capture_Rom:
    call    Sis_Copy_Rom
    popad
    ret
EndProc Sis_Capture

BeginProc Sis_Reset_Result
    pushad
    mov     edi, OFFSET32 SisResult + 4
    mov     ecx, SIS_RESULT_DWORDS - 1
    xor     eax, eax
    cld
    rep     stosd
    popad
    ret
EndProc Sis_Reset_Result

BeginProc Sis_W32_DeviceIoControl
    cmp     ecx, DIOC_OPEN
    je      short Sis_Dioc_Ok
    cmp     ecx, DIOC_CLOSEHANDLE
    je      short Sis_Dioc_Ok
    cmp     ecx, SIS_DIOC_CAPTURE
    jne     Sis_Dioc_Fail

    pushad
    mov     ebx, esi
    mov     ebp, [ebx.lpvInBuffer]
    test    ebp, ebp
    jz      short Sis_Dioc_Copy_Fail
    mov     ecx, [ebx.cbInBuffer]
    cmp     ecx, SIS_IN_MIN_BYTES
    jb      short Sis_Dioc_Copy_Fail
    mov     edi, [ebx.lpvOutBuffer]
    test    edi, edi
    jz      short Sis_Dioc_Copy_Fail
    cmp     [ebx.cbOutBuffer], SIS_RESULT_DWORDS * 4
    jb      short Sis_Dioc_Copy_Fail

    call    Sis_Reset_Result
    mov     eax, [ebp+SIS_IN_MMIO_BAR]
    mov     SisAssignedMmio, eax
    mov     eax, [ebp+SIS_IN_FLAGS]
    mov     SisFlags, eax
    call    Sis_Accept_Offsets
    call    Sis_Capture

    mov     edi, [ebx.lpvOutBuffer]
    mov     esi, OFFSET32 SisResult
    mov     ecx, SIS_RESULT_DWORDS
    cld
    rep     movsd
    mov     eax, [ebx.lpcbBytesReturned]
    test    eax, eax
    jz      short Sis_Dioc_Copy_Done
    mov     dword ptr [eax], SIS_RESULT_DWORDS * 4
Sis_Dioc_Copy_Done:
    popad
Sis_Dioc_Ok:
    xor     eax, eax
    ret

Sis_Dioc_Copy_Fail:
    popad
Sis_Dioc_Fail:
    mov     eax, 1
    ret
EndProc Sis_W32_DeviceIoControl

BeginProc Sis_Dynamic_Init
    clc
    ret
EndProc Sis_Dynamic_Init

BeginProc Sis_Dynamic_Exit
    clc
    ret
EndProc Sis_Dynamic_Exit

Begin_Control_Dispatch Sis6326
    Control_Dispatch Sys_Dynamic_Device_Init, Sis_Dynamic_Init
    Control_Dispatch Sys_Dynamic_Device_Exit, Sis_Dynamic_Exit
    Control_Dispatch W32_DeviceIoControl, Sis_W32_DeviceIoControl
End_Control_Dispatch Sis6326

VxD_LOCKED_CODE_ENDS

end
