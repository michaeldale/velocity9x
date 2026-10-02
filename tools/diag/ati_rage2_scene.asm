; ATI Rage IIC scene mapper VxD.
;
; Loaded dynamically by ATIRX.EXE. It finds PCI 1002:4757, maps the 4 KiB
; register window at the Config Manager-assigned BAR2 and, only when
; CONFIG_CHIP_ID read through it names 4757h, maps the framebuffer aperture
; at BAR0. It returns both linear addresses and writes nothing to the card:
; every engine write the scenes make is made by ATIRX.EXE from ring 3,
; through these mappings, as V9XHAL.DLL makes its own.
;
; Each window is mapped once per load and reused, so repeated runs cannot
; accumulate mappings; a different BAR on a later call is refused.

.386p

.xlist
include VMM.INC
include MINIVDD.INC
include VWIN32.INC
.list

Declare_Virtual_Device ATIRX, 1, 0, AtiRx_Control, \
                       Undefined_Device_ID, Undefined_Init_Order, , ,

ATIRX_MAGIC             equ 58524941h ; "AIRX"
ATIRX_DIOC_MAP          equ 1
ATIRX_PCI_ID            equ 47571002h
ATIRX_CHIP_LOW          equ 4757h
ATIRX_MMIO_BYTES        equ 00001000h
ATIRX_FB_BYTES          equ 00400000h
ATIRX_CONFIG_CHIP_ID    equ 04e0h

ATIRX_PCI_FOUND         equ 00000001h
ATIRX_MMIO_MAPPED       equ 00000002h
ATIRX_CHIP_MATCH        equ 00000004h
ATIRX_FB_MAPPED         equ 00000008h
ATIRX_BAR_REFUSED       equ 00000010h

; Input: assigned BAR0, assigned BAR2.
ATIRX_IN_BAR0           equ 0
ATIRX_IN_BAR2           equ 4
ATIRX_IN_BYTES          equ 8

ATIRX_RESULT_DWORDS     equ 8

VxD_LOCKED_DATA_SEG
AtiRxResult label dword
    dd ATIRX_MAGIC              ; 00 magic
    dd 0                        ; 01 status
    dd 0                        ; 02 PCI config address
    dd 0                        ; 03 CONFIG_CHIP_ID
    dd 0                        ; 04 register window linear
    dd 0                        ; 05 framebuffer linear
    dd 0                        ; 06 framebuffer bytes mapped
    dd 0                        ; 07 reserved
AtiRxMmioPhys   dd 0
AtiRxMmioLinear dd 0
AtiRxFbPhys     dd 0
AtiRxFbLinear   dd 0
VxD_LOCKED_DATA_ENDS

VxD_LOCKED_CODE_SEG

; EAX = configuration address. Returns EAX = dword. 0CF8h restored.
BeginProc AtiRx_Pci_Read
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
EndProc AtiRx_Pci_Read

; EBP = input buffer. Fills AtiRxResult.
BeginProc AtiRx_Map
    pushad
    mov     AtiRxResult[4], 0
    mov     AtiRxResult[8], 0
    mov     AtiRxResult[12], 0
    mov     AtiRxResult[16], 0
    mov     AtiRxResult[20], 0
    mov     AtiRxResult[24], 0

    mov     ebx, 80000000h
AtiRx_Map_Pci_Next:
    mov     eax, ebx
    call    AtiRx_Pci_Read
    cmp     eax, ATIRX_PCI_ID
    je      short AtiRx_Map_Pci_Found
    add     ebx, 0800h
    cmp     ebx, 81000000h
    jb      short AtiRx_Map_Pci_Next
    jmp     AtiRx_Map_Done
AtiRx_Map_Pci_Found:
    or      AtiRxResult[4], ATIRX_PCI_FOUND
    mov     AtiRxResult[8], ebx

    ; The register window: 4 KiB aligned, mapped once.
    mov     eax, [ebp+ATIRX_IN_BAR2]
    test    eax, eax
    jz      AtiRx_Map_Done
    test    eax, ATIRX_MMIO_BYTES - 1
    jnz     AtiRx_Map_Refused
    cmp     AtiRxMmioLinear, 0
    je      short AtiRx_Map_Mmio_New
    cmp     eax, AtiRxMmioPhys
    jne     AtiRx_Map_Refused
    jmp     short AtiRx_Map_Mmio_Check
AtiRx_Map_Mmio_New:
    mov     AtiRxMmioPhys, eax
    VMMcall _MapPhysToLinear,<eax,ATIRX_MMIO_BYTES,0>
    cmp     eax, 0ffffffffh
    je      AtiRx_Map_Mmio_Failed
    test    eax, eax
    jz      AtiRx_Map_Mmio_Failed
    mov     AtiRxMmioLinear, eax
AtiRx_Map_Mmio_Check:
    or      AtiRxResult[4], ATIRX_MMIO_MAPPED
    mov     edx, AtiRxMmioLinear
    mov     eax, [edx+ATIRX_CONFIG_CHIP_ID]
    mov     AtiRxResult[12], eax
    cmp     ax, ATIRX_CHIP_LOW
    jne     AtiRx_Map_Done
    or      AtiRxResult[4], ATIRX_CHIP_MATCH
    mov     AtiRxResult[16], edx

    ; The aperture, 16 MiB aligned as the card decodes it, mapped once.
    mov     eax, [ebp+ATIRX_IN_BAR0]
    test    eax, eax
    jz      short AtiRx_Map_Done
    test    eax, 00ffffffh
    jnz     short AtiRx_Map_Refused
    cmp     AtiRxFbLinear, 0
    je      short AtiRx_Map_Fb_New
    cmp     eax, AtiRxFbPhys
    jne     short AtiRx_Map_Refused
    jmp     short AtiRx_Map_Fb_Ok
AtiRx_Map_Fb_New:
    mov     AtiRxFbPhys, eax
    VMMcall _MapPhysToLinear,<eax,ATIRX_FB_BYTES,0>
    cmp     eax, 0ffffffffh
    je      short AtiRx_Map_Fb_Failed
    test    eax, eax
    jz      short AtiRx_Map_Fb_Failed
    mov     AtiRxFbLinear, eax
AtiRx_Map_Fb_Ok:
    or      AtiRxResult[4], ATIRX_FB_MAPPED
    mov     eax, AtiRxFbLinear
    mov     AtiRxResult[20], eax
    mov     AtiRxResult[24], ATIRX_FB_BYTES
    jmp     short AtiRx_Map_Done

AtiRx_Map_Fb_Failed:
    mov     AtiRxFbPhys, 0
    jmp     short AtiRx_Map_Done
AtiRx_Map_Mmio_Failed:
    mov     AtiRxMmioPhys, 0
    jmp     short AtiRx_Map_Done
AtiRx_Map_Refused:
    or      AtiRxResult[4], ATIRX_BAR_REFUSED
AtiRx_Map_Done:
    popad
    ret
EndProc AtiRx_Map

BeginProc AtiRx_W32_DeviceIoControl
    cmp     ecx, DIOC_OPEN
    je      short AtiRx_Dioc_Ok
    cmp     ecx, DIOC_CLOSEHANDLE
    je      short AtiRx_Dioc_Ok
    cmp     ecx, ATIRX_DIOC_MAP
    jne     short AtiRx_Dioc_Fail

    pushad
    mov     ebx, esi
    mov     ebp, [ebx.lpvInBuffer]
    test    ebp, ebp
    jz      short AtiRx_Dioc_Copy_Fail
    cmp     [ebx.cbInBuffer], ATIRX_IN_BYTES
    jb      short AtiRx_Dioc_Copy_Fail
    mov     edi, [ebx.lpvOutBuffer]
    test    edi, edi
    jz      short AtiRx_Dioc_Copy_Fail
    cmp     [ebx.cbOutBuffer], ATIRX_RESULT_DWORDS * 4
    jb      short AtiRx_Dioc_Copy_Fail

    call    AtiRx_Map

    mov     edi, [ebx.lpvOutBuffer]
    mov     esi, OFFSET32 AtiRxResult
    mov     ecx, ATIRX_RESULT_DWORDS
    cld
    rep     movsd
    mov     eax, [ebx.lpcbBytesReturned]
    test    eax, eax
    jz      short AtiRx_Dioc_Copy_Done
    mov     dword ptr [eax], ATIRX_RESULT_DWORDS * 4
AtiRx_Dioc_Copy_Done:
    popad
AtiRx_Dioc_Ok:
    xor     eax, eax
    ret

AtiRx_Dioc_Copy_Fail:
    popad
AtiRx_Dioc_Fail:
    mov     eax, 1
    ret
EndProc AtiRx_W32_DeviceIoControl

BeginProc AtiRx_Dynamic_Init
    clc
    ret
EndProc AtiRx_Dynamic_Init

BeginProc AtiRx_Dynamic_Exit
    clc
    ret
EndProc AtiRx_Dynamic_Exit

Begin_Control_Dispatch AtiRx
    Control_Dispatch Sys_Dynamic_Device_Init, AtiRx_Dynamic_Init
    Control_Dispatch Sys_Dynamic_Device_Exit, AtiRx_Dynamic_Exit
    Control_Dispatch W32_DeviceIoControl, AtiRx_W32_DeviceIoControl
End_Control_Dispatch AtiRx

VxD_LOCKED_CODE_ENDS

end
