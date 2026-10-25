; Page-table copy VxD, for V9XD32's T15.
;
; T15 found one bit cleared in its pattern at the same page offset (5C0h)
; in a different linear page each run (A8U4I5, 2026-10-25). The linear
; address says nothing about the writer; the physical frame does. Ring 3
; cannot read the page tables on Windows 98, so this copies the caller's
; page-table entries for a range with _CopyPageTable, in the caller's
; context (DeviceIoControl runs there).
;
; Writes: none to any device, and none to memory but the caller's output
; buffer. _CopyPageTable only reads the tables.

.386p

.xlist
include VMM.INC
include VWIN32.INC
.list

Declare_Virtual_Device PFRM, 1, 0, PF_Control, \
                       Undefined_Device_ID, Undefined_Init_Order, , ,

PF_DIOC_COPY            equ 1
; Input: the first linear page number and a page count.
PF_IN_PAGE              equ 0
PF_IN_COUNT             equ 4
PF_IN_BYTES             equ 8
; 32 MiB of 4 KiB pages: T15's heap.
PF_COUNT_MAX            equ 8192

VxD_LOCKED_CODE_SEG

BeginProc PF_W32_DeviceIoControl
    cmp     ecx, DIOC_OPEN
    je      PF_Dioc_Ok
    cmp     ecx, DIOC_CLOSEHANDLE
    je      PF_Dioc_Ok
    cmp     ecx, PF_DIOC_COPY
    jne     PF_Dioc_Fail

    pushad
    mov     ebx, esi
    mov     ebp, [ebx.lpvInBuffer]
    test    ebp, ebp
    jz      short PF_Dioc_Copy_Fail
    cmp     [ebx.cbInBuffer], PF_IN_BYTES
    jb      short PF_Dioc_Copy_Fail
    mov     ecx, [ebp+PF_IN_COUNT]
    test    ecx, ecx
    jz      short PF_Dioc_Copy_Fail
    cmp     ecx, PF_COUNT_MAX
    ja      short PF_Dioc_Copy_Fail
    mov     edi, [ebx.lpvOutBuffer]
    test    edi, edi
    jz      short PF_Dioc_Copy_Fail
    mov     eax, ecx
    shl     eax, 2
    cmp     [ebx.cbOutBuffer], eax
    jb      short PF_Dioc_Copy_Fail

    mov     edx, [ebp+PF_IN_PAGE]
    push    ecx
    VMMcall _CopyPageTable,<edx,ecx,edi,0>
    pop     ecx
    test    eax, eax
    jz      short PF_Dioc_Copy_Fail
    mov     eax, [ebx.lpcbBytesReturned]
    test    eax, eax
    jz      short PF_Dioc_Copy_Done
    shl     ecx, 2
    mov     [eax], ecx
PF_Dioc_Copy_Done:
    popad
PF_Dioc_Ok:
    xor     eax, eax
    ret

PF_Dioc_Copy_Fail:
    popad
PF_Dioc_Fail:
    mov     eax, 1
    ret
EndProc PF_W32_DeviceIoControl

BeginProc PF_Dynamic_Init
    clc
    ret
EndProc PF_Dynamic_Init

BeginProc PF_Dynamic_Exit
    clc
    ret
EndProc PF_Dynamic_Exit

Begin_Control_Dispatch PF
    Control_Dispatch Sys_Dynamic_Device_Init, PF_Dynamic_Init
    Control_Dispatch Sys_Dynamic_Device_Exit, PF_Dynamic_Exit
    Control_Dispatch W32_DeviceIoControl, PF_W32_DeviceIoControl
End_Control_Dispatch PF

VxD_LOCKED_CODE_ENDS

end
