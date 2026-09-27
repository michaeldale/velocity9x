; ATI Rage Mobility-M Phase 1 bounded scratch-register probe.
.386p
.xlist
include VMM.INC
include MINIVDD.INC
include VWIN32.INC
.list
Declare_Virtual_Device ATIEN, 1, 0, AtiE1_Control, \
                       Undefined_Device_ID, Undefined_Init_Order, , ,
ATIE1_MAGIC equ 31495441h
ATIE1_DIOC_RUN equ 1
ATIE1_RESULT_DWORDS equ 12
ATIE1_STAT_PCI_FOUND equ 00000001h
ATIE1_STAT_DECODE_ON equ 00000002h
ATIE1_STAT_BAR_MAPPED equ 00000004h
ATIE1_STAT_ID_MATCH equ 00000008h
ATIE1_STAT_PREFLIGHT equ 00000010h
ATIE1_STAT_WRITE_TRIED equ 00000020h
ATIE1_STAT_PATTERN_OK equ 00000040h
ATIE1_STAT_RESTORED equ 00000080h
ATIE1_SCRATCH_REG0 equ 0480h
ATIE1_CONFIG_CHIP_ID equ 04e0h
ATIE1_CONFIG_STAT0 equ 04e4h
ATIE1_GUI_STAT equ 0738h
ATIE1_PATTERN equ 55555555h
VxD_LOCKED_DATA_SEG
AtiE1Result label dword
 dd ATIE1_MAGIC
 dd 0,0,0,0,0,0,0,0,0,0,0
VxD_LOCKED_DATA_ENDS
VxD_LOCKED_CODE_SEG
BeginProc AtiE1_Pci_Read
 mov dx,0cf8h
 out dx,eax
 mov dx,0cfch
 in eax,dx
 ret
EndProc AtiE1_Pci_Read
BeginProc AtiE1_Run
 pushad
 mov AtiE1Result[4],0
 mov edi,OFFSET32 AtiE1Result+8
 mov ecx,ATIE1_RESULT_DWORDS-2
 xor eax,eax
 cld
 rep stosd
 mov ebx,80000000h
AtiE1_Pci_Next:
 mov eax,ebx
 call AtiE1_Pci_Read
 cmp eax,4c4d1002h
 je short AtiE1_Pci_Found
 add ebx,0800h
 cmp ebx,81000000h
 jb short AtiE1_Pci_Next
 jmp AtiE1_Done
AtiE1_Pci_Found:
 or AtiE1Result[4],ATIE1_STAT_PCI_FOUND
 mov AtiE1Result[8],ebx
 mov eax,ebx
 or eax,04h
 call AtiE1_Pci_Read
 mov AtiE1Result[12],eax
 test eax,3
 jz AtiE1_Done
 or AtiE1Result[4],ATIE1_STAT_DECODE_ON
 mov eax,ebx
 or eax,08h
 call AtiE1_Pci_Read
 mov AtiE1Result[16],eax
 cmp al,64h
 jne AtiE1_Done
 mov eax,ebx
 or eax,18h
 call AtiE1_Pci_Read
 and eax,0fffffff0h
 mov AtiE1Result[20],eax
 cmp eax,0f4100000h
 jne AtiE1_Done
 VMMcall _MapPhysToLinear,<eax,1000h,0>
 cmp eax,0ffffffffh
 je AtiE1_Done
 test eax,eax
 jz AtiE1_Done
 mov esi,eax
 or AtiE1Result[4],ATIE1_STAT_BAR_MAPPED
 mov eax,[esi+ATIE1_CONFIG_CHIP_ID]
 mov AtiE1Result[24],eax
 cmp eax,64004c4dh
 jne AtiE1_Done
 or AtiE1Result[4],ATIE1_STAT_ID_MATCH
 mov eax,[esi+ATIE1_CONFIG_STAT0]
 mov AtiE1Result[28],eax
 and eax,7
 cmp eax,6
 jne AtiE1_Done
 mov eax,[esi+ATIE1_GUI_STAT]
 mov AtiE1Result[32],eax
 test eax,1
 jnz AtiE1_Done
 or AtiE1Result[4],ATIE1_STAT_PREFLIGHT
 pushfd
 cli
 mov eax,[esi+ATIE1_SCRATCH_REG0]
 mov AtiE1Result[36],eax
 mov dword ptr [esi+ATIE1_SCRATCH_REG0],ATIE1_PATTERN
 or AtiE1Result[4],ATIE1_STAT_WRITE_TRIED
 mov eax,[esi+ATIE1_SCRATCH_REG0]
 mov AtiE1Result[40],eax
 cmp eax,ATIE1_PATTERN
 jne short AtiE1_Restore
 or AtiE1Result[4],ATIE1_STAT_PATTERN_OK
AtiE1_Restore:
 mov eax,AtiE1Result[36]
 mov [esi+ATIE1_SCRATCH_REG0],eax
 mov eax,[esi+ATIE1_SCRATCH_REG0]
 mov AtiE1Result[44],eax
 cmp eax,AtiE1Result[36]
 jne short AtiE1_Restore_Done
 or AtiE1Result[4],ATIE1_STAT_RESTORED
AtiE1_Restore_Done:
 popfd
AtiE1_Done:
 popad
 ret
EndProc AtiE1_Run
BeginProc AtiE1_W32_DeviceIoControl
 cmp ecx,DIOC_OPEN
 je short AtiE1_Dioc_Ok
 cmp ecx,DIOC_CLOSEHANDLE
 je short AtiE1_Dioc_Ok
 cmp ecx,ATIE1_DIOC_RUN
 jne short AtiE1_Dioc_Fail
 pushad
 mov ebp,esi
 call AtiE1_Run
 mov edi,[ebp.lpvOutBuffer]
 test edi,edi
 jz short AtiE1_Dioc_Copy_Fail
 cmp [ebp.cbOutBuffer],ATIE1_RESULT_DWORDS*4
 jb short AtiE1_Dioc_Copy_Fail
 mov esi,OFFSET32 AtiE1Result
 mov ecx,ATIE1_RESULT_DWORDS
 cld
 rep movsd
 mov eax,[ebp.lpcbBytesReturned]
 test eax,eax
 jz short AtiE1_Dioc_Copy_Done
 mov dword ptr [eax],ATIE1_RESULT_DWORDS*4
AtiE1_Dioc_Copy_Done:
 popad
AtiE1_Dioc_Ok:
 xor eax,eax
 ret
AtiE1_Dioc_Copy_Fail:
 popad
AtiE1_Dioc_Fail:
 mov eax,1
 ret
EndProc AtiE1_W32_DeviceIoControl
BeginProc AtiE1_Dynamic_Init
 clc
 ret
EndProc AtiE1_Dynamic_Init
BeginProc AtiE1_Dynamic_Exit
 clc
 ret
EndProc AtiE1_Dynamic_Exit
Begin_Control_Dispatch AtiE1
 Control_Dispatch Sys_Dynamic_Device_Init,AtiE1_Dynamic_Init
 Control_Dispatch Sys_Dynamic_Device_Exit,AtiE1_Dynamic_Exit
 Control_Dispatch W32_DeviceIoControl,AtiE1_W32_DeviceIoControl
End_Control_Dispatch AtiE1
VxD_LOCKED_CODE_ENDS
end
