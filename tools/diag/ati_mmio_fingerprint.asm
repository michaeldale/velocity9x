; ATI Rage Mobility-M Phase 0 read-only fingerprint VxD.
;
; The device is loaded dynamically by ATIMM.EXE.  It finds PCI 1002:4C4D with
; configuration mechanism 1, records BAR0/BAR1/BAR2 and command/status, maps
; only the 4 KiB dedicated MMIO BAR, cross-checks CONFIG_CHIP_ID, and captures
; two quiet snapshots.  The only MMIO writes select LCD indices 04h and 06h;
; the original LCD_INDEX dword is restored before interrupts are re-enabled.
; No engine, framebuffer, PCI configuration, or scratch register is written.

.386p

.xlist
include VMM.INC
include MINIVDD.INC
include VWIN32.INC
.list

Declare_Virtual_Device ATIMM, 1, 0, AtiMm_Control, \
                       Undefined_Device_ID, Undefined_Init_Order, , ,

ATIMM_MAGIC             equ 30495441h ; "ATI0"
ATIMM_DIOC_CAPTURE      equ 1
ATIMM_REG_COUNT         equ 29
ATIMM_RESULT_DWORDS     equ (11 + ATIMM_REG_COUNT * 3 + 8)

; Status bits returned to the Win32 publisher.
ATIMM_PCI_FOUND         equ 00000001h
ATIMM_BAR2_MAPPED       equ 00000002h
ATIMM_CHIP_MATCH        equ 00000004h
ATIMM_LCD_RESTORED      equ 00000008h
ATIMM_ASSIGNED_BAR      equ 00000010h
ATIMM_IN_APERTURE       equ 00000020h

; Dedicated MMIO BAR-relative offsets.  Block 0 begins at +0400h.
ATIMM_LCD_INDEX         equ 04a4h
ATIMM_LCD_DATA          equ 04a8h
ATIMM_CONFIG_CHIP_ID    equ 04e0h

VxD_LOCKED_DATA_SEG
AtiMmResult label dword
    dd ATIMM_MAGIC              ; 00 magic
    dd 0                        ; 01 status
    dd 0                        ; 02 PCI config address/BDF
    dd 0                        ; 03 command/status
    dd 0                        ; 04 revision/class
    dd 0                        ; 05 BAR0 raw
    dd 0                        ; 06 BAR1 raw
    dd 0                        ; 07 BAR2 raw
    dd 0                        ; 08 subsystem ids
    dd 0                        ; 09 interrupt/cache/latency/header
    dd 0                        ; 10 mapped BAR2 physical base
AtiMmOffsets dd 0414h, 041ch, 042ch, 04a0h, 04b0h, 04d0h, 04e0h, 04e4h
             dd 0500h, 0530h, 0548h, 054ch, 0550h, 06a8h, 06b4h, 06c8h
             dd 06cch, 06d0h, 06d4h, 06d8h, 06fch, 0708h, 0710h, 0730h
             dd 0738h, 0770h, 0774h, 0178h, 0304h
AtiMmFirst   dd ATIMM_REG_COUNT dup (0)
AtiMmSecond  dd ATIMM_REG_COUNT dup (0)
AtiMmLcdIndexFirst  dd 0
AtiMmLcdHorzFirst   dd 0
AtiMmLcdVertFirst   dd 0
AtiMmLcdIndexSecond dd 0
AtiMmLcdHorzSecond  dd 0
AtiMmLcdVertSecond  dd 0
AtiMmLcdIndexAfter  dd 0
AtiMmReserved       dd 0
AtiMmAssignedBar0   dd 0
AtiMmAssignedBar2   dd 0
VxD_LOCKED_DATA_ENDS

VxD_LOCKED_CODE_SEG

; EAX = PCI configuration address.  Returns EAX = configuration dword.
BeginProc AtiMm_Pci_Read
    mov     dx, 0cf8h
    out     dx, eax
    mov     dx, 0cfch
    in      eax, dx
    ret
EndProc AtiMm_Pci_Read

; ESI = mapped BAR2, EDI = destination snapshot.
BeginProc AtiMm_Read_Snapshot
    pushad
    mov     ebx, OFFSET32 AtiMmOffsets
    mov     ecx, ATIMM_REG_COUNT
AtiMm_Read_Snapshot_Next:
    mov     eax, [ebx]
    mov     eax, [esi+eax]
    mov     [edi], eax
    add     ebx, 4
    add     edi, 4
    dec     ecx
    jnz     short AtiMm_Read_Snapshot_Next
    popad
    ret
EndProc AtiMm_Read_Snapshot

; ESI = mapped BAR2. EDI points to LCD_INDEX/HORZ/VERT destination triplet.
; The selector update and restore are one non-preemptible sequence.
BeginProc AtiMm_Read_Lcd
    pushfd
    cli
    mov     eax, [esi+ATIMM_LCD_INDEX]
    mov     [edi], eax
    mov     ebx, eax
    and     eax, 0ffffffc0h
    or      eax, 04h
    mov     [esi+ATIMM_LCD_INDEX], eax
    mov     eax, [esi+ATIMM_LCD_DATA]
    mov     [edi+4], eax
    mov     eax, ebx
    and     eax, 0ffffffc0h
    or      eax, 06h
    mov     [esi+ATIMM_LCD_INDEX], eax
    mov     eax, [esi+ATIMM_LCD_DATA]
    mov     [edi+8], eax
    mov     [esi+ATIMM_LCD_INDEX], ebx
    mov     eax, [esi+ATIMM_LCD_INDEX]
    mov     AtiMmLcdIndexAfter, eax
    popfd
    ret
EndProc AtiMm_Read_Lcd

BeginProc AtiMm_Capture
    pushad

    ; Reset all result fields except the immutable offset table and magic.
    mov     AtiMmResult[4], 0
    mov     edi, OFFSET32 AtiMmResult + 8
    mov     ecx, 9
    xor     eax, eax
    cld
    rep     stosd
    mov     edi, OFFSET32 AtiMmFirst
    mov     ecx, ATIMM_REG_COUNT * 2 + 8
    rep     stosd

    ; Scan function zero of each PCI slot.  Display adapters are function zero;
    ; this bounds the scan at 8192 read-only configuration transactions.
    mov     ebx, 80000000h
AtiMm_Capture_Pci_Next:
    mov     eax, ebx
    call    AtiMm_Pci_Read
    cmp     eax, 4c4d1002h
    je      short AtiMm_Capture_Pci_Found
    add     ebx, 0800h
    cmp     ebx, 81000000h
    jb      short AtiMm_Capture_Pci_Next
    jmp     AtiMm_Capture_Done

AtiMm_Capture_Pci_Found:
    or      AtiMmResult[4], ATIMM_PCI_FOUND
    mov     AtiMmResult[8], ebx
    mov     eax, ebx
    or      eax, 04h
    call    AtiMm_Pci_Read
    mov     AtiMmResult[12], eax
    mov     eax, ebx
    or      eax, 08h
    call    AtiMm_Pci_Read
    mov     AtiMmResult[16], eax
    mov     eax, ebx
    or      eax, 10h
    call    AtiMm_Pci_Read
    mov     AtiMmResult[20], eax
    mov     eax, ebx
    or      eax, 14h
    call    AtiMm_Pci_Read
    mov     AtiMmResult[24], eax
    mov     eax, ebx
    or      eax, 18h
    call    AtiMm_Pci_Read
    mov     AtiMmResult[28], eax
    mov     eax, ebx
    or      eax, 2ch
    call    AtiMm_Pci_Read
    mov     AtiMmResult[32], eax
    mov     eax, ebx
    or      eax, 0ch
    call    AtiMm_Pci_Read
    mov     AtiMmResult[36], eax

    mov     eax, AtiMmAssignedBar2
    test    eax, eax
    jz      short AtiMm_Capture_Use_Raw_Bar2
    test    eax, 0fffh
    jnz     AtiMm_Capture_Done
    or      AtiMmResult[4], ATIMM_ASSIGNED_BAR
    jmp     short AtiMm_Capture_Map_Bar2
AtiMm_Capture_Use_Raw_Bar2:
    mov     eax, AtiMmResult[28]
    test    eax, 1
    jnz     AtiMm_Capture_Done
    and     eax, 0fffffff0h
    jz      AtiMm_Capture_Done
AtiMm_Capture_Map_Bar2:
    mov     AtiMmResult[40], eax
    VMMcall _MapPhysToLinear,<eax,1000h,0>
    cmp     eax, 0ffffffffh
    je      AtiMm_Capture_Done
    test    eax, eax
    jz      AtiMm_Capture_Done
    mov     esi, eax
    or      AtiMmResult[4], ATIMM_BAR2_MAPPED

    ; A successful mapping is not identity.  Refuse the rest unless the
    ; card's own register agrees with PCI configuration space.
    mov     eax, [esi+ATIMM_CONFIG_CHIP_ID]
    cmp     ax, 4c4dh
    je      short AtiMm_Capture_Identity_Ok

    ; On the first stock-driver run the live PCI BAR dwords and both reads from
    ; the dedicated aperture were zero even though Config Manager retained the
    ; assigned ranges.  Try the documented in-framebuffer register page,
    ; still read-only, before declining.  Map the containing 4 KiB page;
    ; ESI becomes the block-1 base, with block 0 at ESI+0400h just as above.
    mov     eax, AtiMmAssignedBar0
    test    eax, eax
    jz      AtiMm_Capture_Done
    add     eax, 007ff000h
    VMMcall _MapPhysToLinear,<eax,1000h,0>
    cmp     eax, 0ffffffffh
    je      AtiMm_Capture_Done
    test    eax, eax
    jz      AtiMm_Capture_Done
    lea     esi, [eax+0800h]
    mov     eax, AtiMmAssignedBar0
    add     eax, 007ff800h
    mov     AtiMmResult[40], eax
    mov     eax, [esi+ATIMM_CONFIG_CHIP_ID]
    cmp     ax, 4c4dh
    jne     AtiMm_Capture_Done
    or      AtiMmResult[4], ATIMM_IN_APERTURE

AtiMm_Capture_Identity_Ok:
    or      AtiMmResult[4], ATIMM_CHIP_MATCH

    mov     edi, OFFSET32 AtiMmFirst
    call    AtiMm_Read_Snapshot
    mov     edi, OFFSET32 AtiMmLcdIndexFirst
    call    AtiMm_Read_Lcd
    mov     edi, OFFSET32 AtiMmSecond
    call    AtiMm_Read_Snapshot
    mov     edi, OFFSET32 AtiMmLcdIndexSecond
    call    AtiMm_Read_Lcd
    mov     eax, AtiMmLcdIndexSecond
    cmp     AtiMmLcdIndexAfter, eax
    jne     short AtiMm_Capture_Done
    or      AtiMmResult[4], ATIMM_LCD_RESTORED

AtiMm_Capture_Done:
    popad
    ret
EndProc AtiMm_Capture

BeginProc AtiMm_W32_DeviceIoControl
    cmp     ecx, DIOC_OPEN
    je      short AtiMm_Dioc_Ok
    cmp     ecx, DIOC_CLOSEHANDLE
    je      short AtiMm_Dioc_Ok
    cmp     ecx, ATIMM_DIOC_CAPTURE
    jne     short AtiMm_Dioc_Fail

    pushad
    mov     ebp, esi
    mov     esi, [ebp.lpvInBuffer]
    test    esi, esi
    jz      short AtiMm_Dioc_Copy_Fail
    cmp     [ebp.cbInBuffer], 8
    jb      short AtiMm_Dioc_Copy_Fail
    mov     eax, [esi]
    mov     AtiMmAssignedBar0, eax
    mov     eax, [esi+4]
    mov     AtiMmAssignedBar2, eax
    call    AtiMm_Capture
    mov     edi, [ebp.lpvOutBuffer]
    test    edi, edi
    jz      short AtiMm_Dioc_Copy_Fail
    cmp     [ebp.cbOutBuffer], ATIMM_RESULT_DWORDS * 4
    jb      short AtiMm_Dioc_Copy_Fail
    mov     esi, OFFSET32 AtiMmResult
    mov     ecx, ATIMM_RESULT_DWORDS
    cld
    rep     movsd
    mov     eax, [ebp.lpcbBytesReturned]
    test    eax, eax
    jz      short AtiMm_Dioc_Copy_Done
    mov     dword ptr [eax], ATIMM_RESULT_DWORDS * 4
AtiMm_Dioc_Copy_Done:
    popad
AtiMm_Dioc_Ok:
    xor     eax, eax
    ret

AtiMm_Dioc_Copy_Fail:
    popad
AtiMm_Dioc_Fail:
    mov     eax, 1
    ret
EndProc AtiMm_W32_DeviceIoControl

BeginProc AtiMm_Dynamic_Init
    clc
    ret
EndProc AtiMm_Dynamic_Init

BeginProc AtiMm_Dynamic_Exit
    clc
    ret
EndProc AtiMm_Dynamic_Exit

Begin_Control_Dispatch AtiMm
    Control_Dispatch Sys_Dynamic_Device_Init, AtiMm_Dynamic_Init
    Control_Dispatch Sys_Dynamic_Device_Exit, AtiMm_Dynamic_Exit
    Control_Dispatch W32_DeviceIoControl, AtiMm_W32_DeviceIoControl
End_Control_Dispatch AtiMm

VxD_LOCKED_CODE_ENDS

end
