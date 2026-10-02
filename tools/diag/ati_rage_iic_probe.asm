; ATI Rage IIC read-only register probe VxD.
;
; Loaded dynamically by ATIIC.EXE. It finds PCI 1002:4757 with configuration
; mechanism 1, copies the 256-byte configuration header, reads CONFIG_CHIP_ID
; through each candidate register window, and when one names the card it
; reads the offsets ATIIC.EXE supplied twice. It also copies the shadowed
; video BIOS at C0000h.
;
; Nothing on the card is written: no MMIO store, no PCI configuration store,
; no index register. The ATIMM probe's LCD selector reads are deliberately
; absent - LCD_INDEX belongs to the LT/Mobility parts, and what that offset
; does on a Rage II is not known. The only port writes are the standard
; configuration-address writes to 0CF8h, each restored before interrupts are
; re-enabled.
;
; Which offsets are read is ATIIC.EXE's decision, not this VxD's; it only
; refuses offsets that are unaligned or outside the 2 KiB two-block window.

.386p

.xlist
include VMM.INC
include MINIVDD.INC
include VWIN32.INC
.list

Declare_Virtual_Device ATIIC, 1, 0, AtiIc_Control, \
                       Undefined_Device_ID, Undefined_Init_Order, , ,

ATIIC_MAGIC             equ 43494941h ; "AIIC"
ATIIC_DIOC_CAPTURE      equ 1
ATIIC_PCI_ID            equ 47571002h
ATIIC_CHIP_LOW          equ 4757h
ATIIC_CONFIG_DWORDS     equ 64
ATIIC_OFFSET_MAX        equ 512
ATIIC_WINDOW_BYTES      equ 0800h
ATIIC_ROM_PHYSICAL      equ 000c0000h
ATIIC_ROM_BYTES         equ 00010000h

; Status bits returned to the Win32 publisher.
ATIIC_PCI_FOUND         equ 00000001h
ATIIC_BAR2_MAPPED       equ 00000002h
ATIIC_BAR2_MATCH        equ 00000004h
ATIIC_APERTURE_MAPPED   equ 00000008h
ATIIC_APERTURE_MATCH    equ 00000010h
ATIIC_SNAPSHOT_TAKEN    equ 00000020h
ATIIC_ROM_COPIED        equ 00000040h
ATIIC_OFFSETS_REFUSED   equ 00000080h

; The register window as the Win32 side sees it: block 1 at +000h, block 0 at
; +400h, the layout of the Rage Pro's 4 KiB BAR2. Inside the 8 MiB
; little-endian aperture the same pair sits at +7FF800h (block 1) and +7FFC00h
; (block 0), the location the Mobility probe used as its fallback; the page
; mapped is the 4 KiB one containing both.
ATIIC_APERTURE_PAGE     equ 007ff000h
ATIIC_APERTURE_WINDOW   equ 00000800h
ATIIC_CONFIG_CHIP_ID    equ 04e0h

; Input from ATIIC.EXE.
ATIIC_IN_BAR0           equ 0
ATIIC_IN_BAR2           equ 4
ATIIC_IN_COUNT          equ 8
ATIIC_IN_OFFSETS        equ 12
ATIIC_IN_MIN_BYTES      equ 12

; Output layout, in dwords. Must match struct atiic_result in
; ati_rage_iic_probe_win32.c.
ATIIC_HEADER_DWORDS     equ 12
ATIIC_RESULT_DWORDS     equ (ATIIC_HEADER_DWORDS + ATIIC_CONFIG_DWORDS + \
                             ATIIC_OFFSET_MAX * 3 + ATIIC_ROM_BYTES / 4)

VxD_LOCKED_DATA_SEG
AtiIcResult label dword
    dd ATIIC_MAGIC              ; 00 magic
    dd 0                        ; 01 status
    dd 0                        ; 02 PCI config address/BDF
    dd 0                        ; 03 BAR2 candidate physical window base
    dd 0                        ; 04 CONFIG_CHIP_ID read through BAR2
    dd 0                        ; 05 aperture candidate physical window base
    dd 0                        ; 06 CONFIG_CHIP_ID read through aperture
    dd 0                        ; 07 physical base of the window snapshotted
    dd 0                        ; 08 offset count accepted
    dd 0                        ; 09 0CF8h value found and restored
    dd 0                        ; 10 reserved
    dd 0                        ; 11 reserved
AtiIcConfig  dd ATIIC_CONFIG_DWORDS dup (0)
AtiIcOffsets dd ATIIC_OFFSET_MAX dup (0)
AtiIcFirst   dd ATIIC_OFFSET_MAX dup (0)
AtiIcSecond  dd ATIIC_OFFSET_MAX dup (0)
AtiIcRom     dd (ATIIC_ROM_BYTES / 4) dup (0)
AtiIcAssignedBar0 dd 0
AtiIcAssignedBar2 dd 0
VxD_LOCKED_DATA_ENDS

VxD_LOCKED_CODE_SEG

; EAX = PCI configuration address. Returns EAX = configuration dword.
; 0CF8h is saved and restored with interrupts off, so a configuration cycle
; another driver had addressed is not left pointing somewhere else.
BeginProc AtiIc_Pci_Read
    push    ebx
    push    edx
    pushfd
    cli
    mov     ebx, eax
    mov     dx, 0cf8h
    in      eax, dx
    mov     AtiIcResult[36], eax
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
EndProc AtiIc_Pci_Read

; ESI = window base (block 1), EDI = destination, ECX = accepted count.
BeginProc AtiIc_Read_Snapshot
    pushad
    mov     ebx, OFFSET32 AtiIcOffsets
    jecxz   short AtiIc_Read_Snapshot_Done
AtiIc_Read_Snapshot_Next:
    mov     eax, [ebx]
    mov     eax, [esi+eax]
    mov     [edi], eax
    add     ebx, 4
    add     edi, 4
    dec     ecx
    jnz     short AtiIc_Read_Snapshot_Next
AtiIc_Read_Snapshot_Done:
    popad
    ret
EndProc AtiIc_Read_Snapshot

; Copy the 64 KiB shadow at C0000h. Shadow RAM, read-only here.
BeginProc AtiIc_Copy_Rom
    pushad
    VMMcall _MapPhysToLinear,<ATIIC_ROM_PHYSICAL,ATIIC_ROM_BYTES,0>
    cmp     eax, 0ffffffffh
    je      short AtiIc_Copy_Rom_Done
    test    eax, eax
    jz      short AtiIc_Copy_Rom_Done
    mov     esi, eax
    mov     edi, OFFSET32 AtiIcRom
    mov     ecx, ATIIC_ROM_BYTES / 4
    cld
    rep     movsd
    or      AtiIcResult[4], ATIIC_ROM_COPIED
AtiIc_Copy_Rom_Done:
    popad
    ret
EndProc AtiIc_Copy_Rom

; EBP = DIOC input buffer, ECX = its byte count. Copies the offset list,
; refusing the whole list if any entry is unaligned or outside the window.
BeginProc AtiIc_Accept_Offsets
    pushad
    mov     AtiIcResult[32], 0
    sub     ecx, ATIIC_IN_OFFSETS
    shr     ecx, 2
    mov     eax, [ebp+ATIIC_IN_COUNT]
    cmp     eax, ATIIC_OFFSET_MAX
    ja      short AtiIc_Accept_Offsets_Refused
    cmp     eax, ecx
    ja      short AtiIc_Accept_Offsets_Refused
    mov     ecx, eax
    lea     esi, [ebp+ATIIC_IN_OFFSETS]
    mov     edi, OFFSET32 AtiIcOffsets
    jecxz   short AtiIc_Accept_Offsets_Done
AtiIc_Accept_Offsets_Next:
    mov     edx, [esi]
    test    edx, 3
    jnz     short AtiIc_Accept_Offsets_Refused
    cmp     edx, ATIIC_WINDOW_BYTES
    jae     short AtiIc_Accept_Offsets_Refused
    mov     [edi], edx
    add     esi, 4
    add     edi, 4
    dec     ecx
    jnz     short AtiIc_Accept_Offsets_Next
AtiIc_Accept_Offsets_Done:
    mov     eax, [ebp+ATIIC_IN_COUNT]
    mov     AtiIcResult[32], eax
    popad
    ret
AtiIc_Accept_Offsets_Refused:
    or      AtiIcResult[4], ATIIC_OFFSETS_REFUSED
    popad
    ret
EndProc AtiIc_Accept_Offsets

BeginProc AtiIc_Capture
    pushad

    ; Scan function zero of every slot on every bus: the AGP card sits behind
    ; the host-to-AGP bridge on bus 1, not on bus 0.
    mov     ebx, 80000000h
AtiIc_Capture_Pci_Next:
    mov     eax, ebx
    call    AtiIc_Pci_Read
    cmp     eax, ATIIC_PCI_ID
    je      short AtiIc_Capture_Pci_Found
    add     ebx, 0800h
    cmp     ebx, 81000000h
    jb      short AtiIc_Capture_Pci_Next
    jmp     AtiIc_Capture_Rom

AtiIc_Capture_Pci_Found:
    or      AtiIcResult[4], ATIIC_PCI_FOUND
    mov     AtiIcResult[8], ebx
    mov     edi, OFFSET32 AtiIcConfig
    xor     ecx, ecx
AtiIc_Capture_Config_Next:
    mov     eax, ebx
    or      eax, ecx
    call    AtiIc_Pci_Read
    mov     [edi], eax
    add     edi, 4
    add     ecx, 4
    cmp     ecx, ATIIC_CONFIG_DWORDS * 4
    jb      short AtiIc_Capture_Config_Next

    ; Candidate 1: a dedicated register BAR, if Config Manager assigned a
    ; 4 KiB range. Whether the Rage IIC has one at all is one of the things
    ; this probe exists to measure.
    xor     esi, esi
    mov     eax, AtiIcAssignedBar2
    test    eax, eax
    jz      short AtiIc_Capture_Aperture
    test    eax, 0fffh
    jnz     short AtiIc_Capture_Aperture
    mov     AtiIcResult[12], eax
    VMMcall _MapPhysToLinear,<eax,1000h,0>
    cmp     eax, 0ffffffffh
    je      short AtiIc_Capture_Aperture
    test    eax, eax
    jz      short AtiIc_Capture_Aperture
    or      AtiIcResult[4], ATIIC_BAR2_MAPPED
    mov     edx, [eax+ATIIC_CONFIG_CHIP_ID]
    mov     AtiIcResult[16], edx
    cmp     dx, ATIIC_CHIP_LOW
    jne     short AtiIc_Capture_Aperture
    or      AtiIcResult[4], ATIIC_BAR2_MATCH
    mov     esi, eax
    mov     eax, AtiIcResult[12]
    mov     AtiIcResult[28], eax

    ; Candidate 2: the register pair at the top of the little-endian
    ; aperture. Read whether or not candidate 1 matched, so the report says
    ; whether both decode. The aperture has to be at least 8 MiB-aligned for
    ; +7FF000h to fall inside it.
AtiIc_Capture_Aperture:
    mov     eax, AtiIcAssignedBar0
    test    eax, eax
    jz      short AtiIc_Capture_Snapshot
    test    eax, 007fffffh
    jnz     short AtiIc_Capture_Snapshot
    add     eax, ATIIC_APERTURE_PAGE
    VMMcall _MapPhysToLinear,<eax,1000h,0>
    cmp     eax, 0ffffffffh
    je      short AtiIc_Capture_Snapshot
    test    eax, eax
    jz      short AtiIc_Capture_Snapshot
    or      AtiIcResult[4], ATIIC_APERTURE_MAPPED
    add     eax, ATIIC_APERTURE_WINDOW
    mov     edx, AtiIcAssignedBar0
    add     edx, ATIIC_APERTURE_PAGE + ATIIC_APERTURE_WINDOW
    mov     AtiIcResult[20], edx
    mov     edx, [eax+ATIIC_CONFIG_CHIP_ID]
    mov     AtiIcResult[24], edx
    cmp     dx, ATIIC_CHIP_LOW
    jne     short AtiIc_Capture_Snapshot
    or      AtiIcResult[4], ATIIC_APERTURE_MATCH
    test    esi, esi
    jnz     short AtiIc_Capture_Snapshot
    mov     esi, eax
    mov     eax, AtiIcResult[20]
    mov     AtiIcResult[28], eax

    ; A mapping is not identity: snapshot only through a window whose own
    ; CONFIG_CHIP_ID agrees with PCI configuration space.
AtiIc_Capture_Snapshot:
    test    esi, esi
    jz      short AtiIc_Capture_Rom
    mov     ecx, AtiIcResult[32]
    mov     edi, OFFSET32 AtiIcFirst
    call    AtiIc_Read_Snapshot
    mov     edi, OFFSET32 AtiIcSecond
    call    AtiIc_Read_Snapshot
    or      AtiIcResult[4], ATIIC_SNAPSHOT_TAKEN

AtiIc_Capture_Rom:
    call    AtiIc_Copy_Rom
    popad
    ret
EndProc AtiIc_Capture

BeginProc AtiIc_Reset_Result
    pushad
    mov     edi, OFFSET32 AtiIcResult + 4
    mov     ecx, ATIIC_RESULT_DWORDS - 1
    xor     eax, eax
    cld
    rep     stosd
    popad
    ret
EndProc AtiIc_Reset_Result

BeginProc AtiIc_W32_DeviceIoControl
    cmp     ecx, DIOC_OPEN
    je      short AtiIc_Dioc_Ok
    cmp     ecx, DIOC_CLOSEHANDLE
    je      short AtiIc_Dioc_Ok
    cmp     ecx, ATIIC_DIOC_CAPTURE
    jne     AtiIc_Dioc_Fail

    pushad
    mov     ebx, esi
    mov     ebp, [ebx.lpvInBuffer]
    test    ebp, ebp
    jz      short AtiIc_Dioc_Copy_Fail
    mov     ecx, [ebx.cbInBuffer]
    cmp     ecx, ATIIC_IN_MIN_BYTES
    jb      short AtiIc_Dioc_Copy_Fail
    mov     edi, [ebx.lpvOutBuffer]
    test    edi, edi
    jz      short AtiIc_Dioc_Copy_Fail
    cmp     [ebx.cbOutBuffer], ATIIC_RESULT_DWORDS * 4
    jb      short AtiIc_Dioc_Copy_Fail

    call    AtiIc_Reset_Result
    mov     eax, [ebp+ATIIC_IN_BAR0]
    mov     AtiIcAssignedBar0, eax
    mov     eax, [ebp+ATIIC_IN_BAR2]
    mov     AtiIcAssignedBar2, eax
    call    AtiIc_Accept_Offsets
    call    AtiIc_Capture

    mov     edi, [ebx.lpvOutBuffer]
    mov     esi, OFFSET32 AtiIcResult
    mov     ecx, ATIIC_RESULT_DWORDS
    cld
    rep     movsd
    mov     eax, [ebx.lpcbBytesReturned]
    test    eax, eax
    jz      short AtiIc_Dioc_Copy_Done
    mov     dword ptr [eax], ATIIC_RESULT_DWORDS * 4
AtiIc_Dioc_Copy_Done:
    popad
AtiIc_Dioc_Ok:
    xor     eax, eax
    ret

AtiIc_Dioc_Copy_Fail:
    popad
AtiIc_Dioc_Fail:
    mov     eax, 1
    ret
EndProc AtiIc_W32_DeviceIoControl

BeginProc AtiIc_Dynamic_Init
    clc
    ret
EndProc AtiIc_Dynamic_Init

BeginProc AtiIc_Dynamic_Exit
    clc
    ret
EndProc AtiIc_Dynamic_Exit

Begin_Control_Dispatch AtiIc
    Control_Dispatch Sys_Dynamic_Device_Init, AtiIc_Dynamic_Init
    Control_Dispatch Sys_Dynamic_Device_Exit, AtiIc_Dynamic_Exit
    Control_Dispatch W32_DeviceIoControl, AtiIc_W32_DeviceIoControl
End_Control_Dispatch AtiIc

VxD_LOCKED_CODE_ENDS

end
