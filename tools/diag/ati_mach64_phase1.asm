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
ATIE2_MAGIC equ 32495441h
ATIE2_DIOC_FILL equ 2
ATIE3_DIOC_COPY equ 3
ATIE2_RESULT_DWORDS equ 23
ATIE2_TARGET_OFFSET equ 00200000h
ATIE2_TARGET_PITCH equ 128
ATIE2_TARGET_COLOR equ 0000f81fh
ATIE2_SENTINEL equ 0a55ah
ATIE2_PASS_STATUS equ 00001fffh
ATIE2_DST_OFF_PITCH equ 0500h
ATIE2_DST_Y_X equ 050ch
ATIE2_DST_HEIGHT_WIDTH equ 0518h
ATIE2_DST_CNTL equ 0530h
ATIE2_SC_LEFT_RIGHT equ 06a8h
ATIE2_SC_TOP_BOTTOM equ 06b4h
ATIE2_DP_FRGD_CLR equ 06c4h
ATIE2_DP_WRITE_MASK equ 06c8h
ATIE2_DP_PIX_WIDTH equ 06d0h
ATIE2_DP_MIX equ 06d4h
ATIE2_DP_SRC equ 06d8h
ATIE2_CLR_CMP_CNTL equ 0708h
ATIE2_MEM_BUF_CNTL equ 042ch
ATIE3_MAGIC equ 33495441h
ATIE3_RESULT_DWORDS equ 21
ATIE3_SRC_OFF_PITCH equ 0580h
ATIE3_SRC_Y_X equ 058ch
ATIE3_SRC_WIDTH1 equ 0590h
ATIE3_CRTC_OFF_PITCH equ 0414h
VxD_LOCKED_DATA_SEG
AtiE1Result label dword
 dd ATIE1_MAGIC
 dd 0,0,0,0,0,0,0,0,0,0,0
AtiE2Result label dword
 dd ATIE2_MAGIC
 dd 22 dup (0)
AtiE2StateOffsets dd 0500h,0530h,06a8h,06b4h,06c4h
                  dd 06c8h,06d0h,06d4h,06d8h,0708h
AtiE2StateSaved dd 10 dup (0)
AtiE2MmioLinear dd 0
AtiE2FbLinear dd 0
AtiE2Backup db 4096 dup (0)
AtiE3Result label dword
 dd ATIE3_MAGIC
 dd 20 dup (0)
AtiE3StateOffsets dd 06c8h,06d0h,0580h,0500h,06d8h
                  dd 06d4h,0708h,0530h,06a8h,06b4h
AtiE3StateSaved dd 10 dup (0)
AtiE3Cases dd 20,16,16,12,3
             dd 16,16,20,12,2
             dd 20,12,16,16,1
             dd 16,12,20,16,0
AtiE3SourceX dd 0
AtiE3SourceY dd 0
AtiE3DestinationX dd 0
AtiE3DestinationY dd 0
AtiE3Direction dd 0
AtiE3Expected db 4096 dup (0)
AtiE3FrontLinear dd 0
AtiE3FrontOffPitch dd 0
AtiE3FrontPitchPixels dd 0
AtiE3FrontBackup db 4096 dup (0)
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

; ESI = MMIO base, ECX = exact number of slots. Carry set on timeout.
BeginProc AtiE2_WaitFifo
 push eax
 push ebx
 push edx
 mov ebx,00100000h
AtiE2_Fifo_Loop:
 mov eax,[esi+ATIE1_GUI_STAT]
 mov AtiE2Result[72],eax
 mov edx,eax
 shr edx,16
 and edx,03ffh
 cmp edx,ecx
 jae short AtiE2_Fifo_Ok
 dec ebx
 jnz short AtiE2_Fifo_Loop
 stc
 jmp short AtiE2_Fifo_Done
AtiE2_Fifo_Ok:
 clc
AtiE2_Fifo_Done:
 pop edx
 pop ebx
 pop eax
 ret
EndProc AtiE2_WaitFifo

; ESI = MMIO base. Carry set on timeout.
BeginProc AtiE2_WaitIdle
 push eax
 push ecx
 mov ecx,00200000h
AtiE2_Idle_Loop:
 mov eax,[esi+ATIE1_GUI_STAT]
 test eax,1
 jz short AtiE2_Idle_Ok
 dec ecx
 jnz short AtiE2_Idle_Loop
 stc
 jmp short AtiE2_Idle_Done
AtiE2_Idle_Ok:
 mov AtiE2Result[36],eax
 clc
AtiE2_Idle_Done:
 pop ecx
 pop eax
 ret
EndProc AtiE2_WaitIdle

BeginProc AtiE2_Run
 pushad
 mov AtiE2Result[4],0
 mov edi,OFFSET32 AtiE2Result+8
 mov ecx,ATIE2_RESULT_DWORDS-2
 xor eax,eax
 cld
 rep stosd
 mov AtiE2MmioLinear,0
 mov AtiE2FbLinear,0
 mov dword ptr AtiE2Result[48],ATIE2_TARGET_OFFSET
 mov dword ptr AtiE2Result[52],02040000h
 mov dword ptr AtiE2Result[56],ATIE2_TARGET_COLOR
 mov dword ptr AtiE2Result[80],0ffffffffh

 mov ebx,80000000h
AtiE2_Pci_Next:
 mov eax,ebx
 call AtiE1_Pci_Read
 cmp eax,4c4d1002h
 je short AtiE2_Pci_Found
 add ebx,0800h
 cmp ebx,81000000h
 jb short AtiE2_Pci_Next
 jmp AtiE2_Done
AtiE2_Pci_Found:
 or AtiE2Result[4],1
 mov eax,ebx
 or eax,04h
 call AtiE1_Pci_Read
 mov AtiE2Result[8],eax
 test eax,3
 jz AtiE2_Done
 or AtiE2Result[4],2
 mov eax,ebx
 or eax,08h
 call AtiE1_Pci_Read
 mov AtiE2Result[12],eax
 cmp al,64h
 jne AtiE2_Done
 mov eax,ebx
 or eax,10h
 call AtiE1_Pci_Read
 and eax,0fffffff0h
 mov AtiE2Result[16],eax
 cmp eax,0f5000000h
 jne AtiE2_Done
 mov eax,ebx
 or eax,18h
 call AtiE1_Pci_Read
 and eax,0fffffff0h
 mov AtiE2Result[20],eax
 cmp eax,0f4100000h
 jne AtiE2_Done
 VMMcall _MapPhysToLinear,<eax,1000h,0>
 cmp eax,0ffffffffh
 je AtiE2_Done
 test eax,eax
 jz AtiE2_Done
 mov AtiE2MmioLinear,eax
 mov esi,eax
 or AtiE2Result[4],4
 mov eax,[esi+ATIE1_CONFIG_CHIP_ID]
 mov AtiE2Result[24],eax
 cmp eax,64004c4dh
 jne AtiE2_Done
 or AtiE2Result[4],8
 mov eax,[esi+ATIE1_CONFIG_STAT0]
 mov AtiE2Result[28],eax
 and eax,7
 cmp eax,6
 jne AtiE2_Done
 mov eax,[esi+ATIE1_GUI_STAT]
 mov AtiE2Result[32],eax
 test eax,1
 jnz AtiE2_Done
 or AtiE2Result[4],10h

 mov eax,AtiE2Result[16]
 add eax,ATIE2_TARGET_OFFSET
 VMMcall _MapPhysToLinear,<eax,1000h,0>
 cmp eax,0ffffffffh
 je AtiE2_Done
 test eax,eax
 jz AtiE2_Done
 mov AtiE2FbLinear,eax
 or AtiE2Result[4],20h

 mov esi,AtiE2MmioLinear
 call AtiE2_WaitIdle
 jc AtiE2_Done
 mov ebx,OFFSET32 AtiE2StateOffsets
 mov edi,OFFSET32 AtiE2StateSaved
 mov ecx,10
AtiE2_Save_State:
 mov edx,[ebx]
 mov eax,[esi+edx]
 mov [edi],eax
 add ebx,4
 add edi,4
 dec ecx
 jnz short AtiE2_Save_State
 mov eax,[esi+ATIE2_MEM_BUF_CNTL]
 mov AtiE2Result[40],eax

 mov esi,AtiE2FbLinear
 mov edi,OFFSET32 AtiE2Backup
 mov ecx,1024
 cld
 rep movsd
 mov edi,AtiE2FbLinear
 mov ax,ATIE2_SENTINEL
 mov ecx,2048
 rep stosw
 ; Complete posted host writes before the GUI engine can target this page.
 mov ax,[edi-2]
 or AtiE2Result[4],40h

 mov esi,AtiE2MmioLinear
 mov ecx,12
 call AtiE2_WaitFifo
 jc AtiE2_Restore_Vram
 mov dword ptr [esi+ATIE2_DST_OFF_PITCH],02040000h
 mov dword ptr [esi+ATIE2_DST_CNTL],00000003h
 mov dword ptr [esi+ATIE2_SC_LEFT_RIGHT],003f0000h
 mov dword ptr [esi+ATIE2_SC_TOP_BOTTOM],001f0000h
 mov dword ptr [esi+ATIE2_DP_FRGD_CLR],ATIE2_TARGET_COLOR
 mov dword ptr [esi+ATIE2_DP_WRITE_MASK],0ffffffffh
 mov dword ptr [esi+ATIE2_DP_PIX_WIDTH],00040004h
 mov dword ptr [esi+ATIE2_DP_MIX],00070003h
 mov dword ptr [esi+ATIE2_DP_SRC],00000100h
 mov dword ptr [esi+ATIE2_CLR_CMP_CNTL],0
 mov dword ptr [esi+ATIE2_DST_Y_X],00080008h
 mov dword ptr [esi+ATIE2_DST_HEIGHT_WIDTH],00100010h
 mov dword ptr AtiE2Result[76],1
 or AtiE2Result[4],80h
 ; 999 more fills, alternating green and magenta. Each iteration reserves the
 ; exact two slots before the colour and trigger writes.
 mov edi,999
AtiE2_Fill_Stress:
 mov ecx,2
 call AtiE2_WaitFifo
 jc AtiE2_Restore_State
 test edi,1
 jz short AtiE2_Fill_Magenta
 mov eax,000007e0h
 jmp short AtiE2_Fill_Color
AtiE2_Fill_Magenta:
 mov eax,ATIE2_TARGET_COLOR
AtiE2_Fill_Color:
 mov [esi+ATIE2_DP_FRGD_CLR],eax
 mov AtiE2Result[56],eax
 mov dword ptr [esi+ATIE2_DST_HEIGHT_WIDTH],00100010h
 inc dword ptr AtiE2Result[76]
 dec edi
 jnz short AtiE2_Fill_Stress
 call AtiE2_WaitIdle
 jc AtiE2_Restore_State
 or AtiE2Result[4],100h
 mov eax,[esi+ATIE2_MEM_BUF_CNTL]
 or eax,00800000h
 mov [esi+ATIE2_MEM_BUF_CNTL],eax
 mov eax,[esi+ATIE2_MEM_BUF_CNTL]
 mov AtiE2Result[44],eax

 ; Verify 64x32 pixels: the 16x16 rectangle at (8,8) is the fill.
 mov ebx,AtiE2FbLinear
 xor edx,edx
 mov edi,32
AtiE2_Verify_Row:
 xor ecx,ecx
AtiE2_Verify_Col:
 mov ax,[ebx]
 cmp edx,8
 jb short AtiE2_Expect_Guard
 cmp edx,24
 jae short AtiE2_Expect_Guard
 cmp ecx,8
 jb short AtiE2_Expect_Guard
 cmp ecx,24
 jae short AtiE2_Expect_Guard
 cmp ax,word ptr AtiE2Result[56]
 je short AtiE2_Verify_Next
 inc dword ptr AtiE2Result[60]
 jmp short AtiE2_Verify_Next
AtiE2_Expect_Guard:
 cmp ax,ATIE2_SENTINEL
 je short AtiE2_Verify_Next
 inc dword ptr AtiE2Result[64]
AtiE2_Verify_Next:
 add ebx,2
 inc ecx
 cmp ecx,64
 jb short AtiE2_Verify_Col
 inc edx
 dec edi
 jnz short AtiE2_Verify_Row
 cmp dword ptr AtiE2Result[60],0
 jne short AtiE2_Check_Guards
 or AtiE2Result[4],200h
AtiE2_Check_Guards:
 cmp dword ptr AtiE2Result[64],0
 jne AtiE2_Restore_State
 or AtiE2Result[4],400h

 ; Clear a correctly sized 32x16 surface twice while retaining guards in the
 ; unused half of every pitch row and in the lower half of the mapped page.
 ; The first pass is an RGB565 colour clear to zero; the second is Z16 FFFF.
 mov ebp,2
AtiE2_Clear_Next:
 mov edi,AtiE2FbLinear
 mov ax,ATIE2_SENTINEL
 mov ecx,2048
 cld
 rep stosw
 mov ax,[edi-2]
 mov esi,AtiE2MmioLinear
 mov ecx,5
 call AtiE2_WaitFifo
 jc AtiE2_Restore_State
 mov dword ptr [esi+ATIE2_SC_LEFT_RIGHT],001f0000h
 mov dword ptr [esi+ATIE2_SC_TOP_BOTTOM],000f0000h
 cmp ebp,2
 jne short AtiE2_Clear_Depth
 xor eax,eax
 jmp short AtiE2_Clear_Color_Ready
AtiE2_Clear_Depth:
 mov eax,0000ffffh
AtiE2_Clear_Color_Ready:
 mov AtiE2Result[56],eax
 mov [esi+ATIE2_DP_FRGD_CLR],eax
 mov dword ptr [esi+ATIE2_DST_Y_X],0
 mov dword ptr [esi+ATIE2_DST_HEIGHT_WIDTH],00200010h
 inc dword ptr AtiE2Result[76]
 call AtiE2_WaitIdle
 jc AtiE2_Restore_State
 mov eax,[esi+ATIE2_MEM_BUF_CNTL]
 or eax,00800000h
 mov [esi+ATIE2_MEM_BUF_CNTL],eax

 mov ebx,AtiE2FbLinear
 xor edx,edx
 mov edi,32
AtiE2_Clear_Verify_Row:
 xor ecx,ecx
AtiE2_Clear_Verify_Col:
 mov ax,[ebx]
 cmp edx,16
 jae short AtiE2_Clear_Expect_Guard
 cmp ecx,32
 jae short AtiE2_Clear_Expect_Guard
 cmp ax,word ptr AtiE2Result[56]
 je short AtiE2_Clear_Verify_Next
 inc dword ptr AtiE2Result[60]
 cmp dword ptr AtiE2Result[80],0ffffffffh
 jne short AtiE2_Clear_Verify_Next
 push eax
 mov eax,edx
 shl eax,6
 add eax,ecx
 mov AtiE2Result[80],eax
 pop eax
 and eax,0ffffh
 mov AtiE2Result[84],eax
 mov eax,AtiE2Result[56]
 mov AtiE2Result[88],eax
 jmp short AtiE2_Clear_Verify_Next
AtiE2_Clear_Expect_Guard:
 cmp ax,ATIE2_SENTINEL
 je short AtiE2_Clear_Verify_Next
 inc dword ptr AtiE2Result[64]
AtiE2_Clear_Verify_Next:
 add ebx,2
 inc ecx
 cmp ecx,64
 jb short AtiE2_Clear_Verify_Col
 inc edx
 dec edi
 jnz short AtiE2_Clear_Verify_Row
 cmp dword ptr AtiE2Result[60],0
 jne short AtiE2_Restore_State
 cmp dword ptr AtiE2Result[64],0
 jne short AtiE2_Restore_State
 dec ebp
 jnz AtiE2_Clear_Next

AtiE2_Restore_State:
 mov esi,AtiE2MmioLinear
 mov ecx,10
 call AtiE2_WaitFifo
 jc short AtiE2_Restore_Vram
 mov ebx,OFFSET32 AtiE2StateOffsets
 mov edi,OFFSET32 AtiE2StateSaved
 mov ecx,10
AtiE2_Restore_State_Loop:
 mov edx,[ebx]
 mov eax,[edi]
 mov [esi+edx],eax
 add ebx,4
 add edi,4
 dec ecx
 jnz short AtiE2_Restore_State_Loop
 mov eax,AtiE2Result[40]
 mov [esi+ATIE2_MEM_BUF_CNTL],eax
 or AtiE2Result[4],1000h

AtiE2_Restore_Vram:
 mov esi,OFFSET32 AtiE2Backup
 mov edi,AtiE2FbLinear
 test edi,edi
 jz short AtiE2_Done
 mov ecx,1024
 cld
 rep movsd
 mov esi,OFFSET32 AtiE2Backup
 mov edi,AtiE2FbLinear
 mov ecx,1024
 xor eax,eax
AtiE2_Check_Restore:
 mov edx,[esi]
 cmp edx,[edi]
 je short AtiE2_Check_Restore_Next
 inc eax
AtiE2_Check_Restore_Next:
 add esi,4
 add edi,4
 dec ecx
 jnz short AtiE2_Check_Restore
 mov AtiE2Result[68],eax
 test eax,eax
 jnz short AtiE2_Done
 or AtiE2Result[4],800h
AtiE2_Done:
 popad
 ret
EndProc AtiE2_Run

; Seed the mapped scratch page and the CPU reference with a unique word per
; pixel. Both buffers are exactly 64x32 RGB565 pixels at a 128-byte pitch.
BeginProc AtiE3_Seed
 pushad
 mov esi,AtiE2FbLinear
 mov edi,OFFSET32 AtiE3Expected
 mov ecx,2048
 mov ax,1000h
 cld
AtiE3_Seed_Loop:
 mov [esi],ax
 mov [edi],ax
 add esi,2
 add edi,2
 inc ax
 dec ecx
 jnz short AtiE3_Seed_Loop
 mov ax,[esi-2]
 popad
 ret
EndProc AtiE3_Seed

; Apply the current 16x8 case to the CPU reference with memmove ordering.
BeginProc AtiE3_ReferenceCopy
 pushad
 mov esi,OFFSET32 AtiE3Expected
 mov edi,esi
 mov eax,AtiE3SourceY
 shl eax,7
 add esi,eax
 mov eax,AtiE3SourceX
 shl eax,1
 add esi,eax
 mov eax,AtiE3DestinationY
 shl eax,7
 add edi,eax
 mov eax,AtiE3DestinationX
 shl eax,1
 add edi,eax
 mov edx,AtiE3Direction
 test edx,2
 jnz short AtiE3_Reference_Y_Ready
 add esi,7*128
 add edi,7*128
AtiE3_Reference_Y_Ready:
 test edx,1
 jnz short AtiE3_Reference_X_Ready
 add esi,30
 add edi,30
 std
AtiE3_Reference_X_Ready:
 mov ebx,8
AtiE3_Reference_Row:
 mov ecx,16
 rep movsw
 test edx,1
 jz short AtiE3_Reference_X_Negative
 test edx,2
 jz short AtiE3_Reference_X_Pos_Y_Neg
 add esi,96
 add edi,96
 jmp short AtiE3_Reference_Next_Row
AtiE3_Reference_X_Pos_Y_Neg:
 sub esi,160
 sub edi,160
 jmp short AtiE3_Reference_Next_Row
AtiE3_Reference_X_Negative:
 test edx,2
 jz short AtiE3_Reference_X_Neg_Y_Neg
 add esi,160
 add edi,160
 jmp short AtiE3_Reference_Next_Row
AtiE3_Reference_X_Neg_Y_Neg:
 sub esi,96
 sub edi,96
AtiE3_Reference_Next_Row:
 dec ebx
 jnz short AtiE3_Reference_Row
 cld
 popad
 ret
EndProc AtiE3_ReferenceCopy

BeginProc AtiE3_Run
 pushad
 mov AtiE3Result[4],0
 mov edi,OFFSET32 AtiE3Result+8
 mov ecx,ATIE3_RESULT_DWORDS-2
 xor eax,eax
 cld
 rep stosd
 mov AtiE2MmioLinear,0
 mov AtiE2FbLinear,0
 mov AtiE3FrontLinear,0
 mov dword ptr AtiE3Result[72],16
 mov dword ptr AtiE3Result[76],2

 mov ebx,80000000h
AtiE3_Pci_Next:
 mov eax,ebx
 call AtiE1_Pci_Read
 cmp eax,4c4d1002h
 je short AtiE3_Pci_Found
 add ebx,0800h
 cmp ebx,81000000h
 jb short AtiE3_Pci_Next
 jmp AtiE3_Done
AtiE3_Pci_Found:
 or AtiE3Result[4],1
 mov eax,ebx
 or eax,04h
 call AtiE1_Pci_Read
 mov AtiE3Result[8],eax
 test eax,3
 jz AtiE3_Done
 or AtiE3Result[4],2
 mov eax,ebx
 or eax,08h
 call AtiE1_Pci_Read
 mov AtiE3Result[12],eax
 cmp al,64h
 jne AtiE3_Done
 mov eax,ebx
 or eax,10h
 call AtiE1_Pci_Read
 and eax,0fffffff0h
 mov AtiE3Result[16],eax
 cmp eax,0f5000000h
 jne AtiE3_Done
 mov eax,ebx
 or eax,18h
 call AtiE1_Pci_Read
 and eax,0fffffff0h
 mov AtiE3Result[20],eax
 cmp eax,0f4100000h
 jne AtiE3_Done
 VMMcall _MapPhysToLinear,<eax,1000h,0>
 cmp eax,0ffffffffh
 je AtiE3_Done
 test eax,eax
 jz AtiE3_Done
 mov AtiE2MmioLinear,eax
 mov esi,eax
 or AtiE3Result[4],4
 mov eax,[esi+ATIE1_CONFIG_CHIP_ID]
 mov AtiE3Result[24],eax
 cmp eax,64004c4dh
 jne AtiE3_Done
 or AtiE3Result[4],8
 mov eax,[esi+ATIE1_CONFIG_STAT0]
 mov AtiE3Result[28],eax
 and eax,7
 cmp eax,6
 jne AtiE3_Done
 mov eax,[esi+ATIE1_GUI_STAT]
 mov AtiE3Result[32],eax
 test eax,1
 jnz AtiE3_Done
 or AtiE3Result[4],10h

 mov eax,AtiE3Result[16]
 add eax,ATIE2_TARGET_OFFSET
 VMMcall _MapPhysToLinear,<eax,1000h,0>
 cmp eax,0ffffffffh
 je AtiE3_Done
 test eax,eax
 jz AtiE3_Done
 mov AtiE2FbLinear,eax
 or AtiE3Result[4],20h
 mov esi,AtiE2MmioLinear
 call AtiE2_WaitIdle
 jc AtiE3_Done

 mov ebx,OFFSET32 AtiE3StateOffsets
 mov edi,OFFSET32 AtiE3StateSaved
 mov ecx,10
AtiE3_Save_State:
 mov edx,[ebx]
 mov eax,[esi+edx]
 mov [edi],eax
 add ebx,4
 add edi,4
 dec ecx
 jnz short AtiE3_Save_State
 mov eax,[esi+ATIE2_MEM_BUF_CNTL]
 mov AtiE3Result[36],eax
 mov esi,AtiE2FbLinear
 mov edi,OFFSET32 AtiE2Backup
 mov ecx,1024
 cld
 rep movsd
 or AtiE3Result[4],40h

 mov ebp,OFFSET32 AtiE3Cases
 xor ebx,ebx
AtiE3_Case_Loop:
 mov eax,[ebp]
 mov AtiE3SourceX,eax
 mov eax,[ebp+4]
 mov AtiE3SourceY,eax
 mov eax,[ebp+8]
 mov AtiE3DestinationX,eax
 mov eax,[ebp+12]
 mov AtiE3DestinationY,eax
 mov eax,[ebp+16]
 mov AtiE3Direction,eax
 call AtiE3_Seed
 call AtiE3_ReferenceCopy

 mov esi,AtiE2MmioLinear
 mov ecx,14
 call AtiE2_WaitFifo
 jc AtiE3_Restore_State
 mov dword ptr [esi+ATIE2_DP_WRITE_MASK],0ffffffffh
 mov dword ptr [esi+ATIE2_DP_PIX_WIDTH],00040404h
 mov dword ptr [esi+ATIE3_SRC_OFF_PITCH],02040000h
 mov dword ptr [esi+ATIE2_DST_OFF_PITCH],02040000h
 mov dword ptr [esi+ATIE2_DP_SRC],00000300h
 mov dword ptr [esi+ATIE2_DP_MIX],00070000h
 mov dword ptr [esi+ATIE2_CLR_CMP_CNTL],0
 mov eax,AtiE3Direction
 mov [esi+ATIE2_DST_CNTL],eax
 mov dword ptr [esi+ATIE2_SC_LEFT_RIGHT],003f0000h
 mov dword ptr [esi+ATIE2_SC_TOP_BOTTOM],001f0000h
 mov eax,AtiE3SourceX
 mov edx,AtiE3DestinationX
 test dword ptr AtiE3Direction,1
 jnz short AtiE3_X_Coords_Ready
 add eax,15
 add edx,15
AtiE3_X_Coords_Ready:
 shl eax,16
 shl edx,16
 mov ecx,AtiE3SourceY
 mov edi,AtiE3DestinationY
 test dword ptr AtiE3Direction,2
 jnz short AtiE3_Y_Coords_Ready
 add ecx,7
 add edi,7
AtiE3_Y_Coords_Ready:
 or eax,ecx
 or edx,edi
 mov [esi+ATIE3_SRC_Y_X],eax
 mov dword ptr [esi+ATIE3_SRC_WIDTH1],16
 mov [esi+ATIE2_DST_Y_X],edx
 mov dword ptr [esi+ATIE2_DST_HEIGHT_WIDTH],00100008h
 inc dword ptr AtiE3Result[60]
 call AtiE2_WaitIdle
 jc AtiE3_Restore_State
 mov eax,[esi+ATIE2_MEM_BUF_CNTL]
 or eax,00800000h
 mov [esi+ATIE2_MEM_BUF_CNTL],eax

 mov esi,AtiE2FbLinear
 mov edi,OFFSET32 AtiE3Expected
 mov ecx,1024
 xor eax,eax
 cld
AtiE3_Compare_Loop:
 mov edx,[esi]
 cmp edx,[edi]
 je short AtiE3_Compare_Next
 inc eax
AtiE3_Compare_Next:
 add esi,4
 add edi,4
 dec ecx
 jnz short AtiE3_Compare_Loop
 add AtiE3Result[40+ebx*4],eax
 test eax,eax
 jnz AtiE3_Restore_State
 mov eax,100h
 mov ecx,ebx
 shl eax,cl
 or AtiE3Result[4],eax
 add ebp,20
 inc ebx
 cmp ebx,4
 jb AtiE3_Case_Loop
 cmp dword ptr AtiE3Result[60],1000
 jae short AtiE3_Presentation
 mov ebp,OFFSET32 AtiE3Cases
 xor ebx,ebx
 jmp AtiE3_Case_Loop

 ; Present a 16x2 off-screen RGB565 rectangle into the first two scan lines.
 ; A 4 KiB mapping covers at least two complete supported scan lines, allowing
 ; every mapped destination guard word and exact restoration to be checked.
AtiE3_Presentation:
 call AtiE3_Seed
 mov eax,AtiE3Result[16]
 VMMcall _MapPhysToLinear,<eax,1000h,0>
 cmp eax,0ffffffffh
 je AtiE3_Restore_State
 test eax,eax
 jz AtiE3_Restore_State
 mov AtiE3FrontLinear,eax
 mov esi,eax
 mov edi,OFFSET32 AtiE3FrontBackup
 mov ecx,1024
 cld
 rep movsd

 mov esi,AtiE2MmioLinear
 mov eax,[esi+ATIE3_CRTC_OFF_PITCH]
 mov AtiE3FrontOffPitch,eax
 mov edx,eax
 shr edx,22
 shl edx,3
 cmp edx,16
 jb AtiE3_Restore_Front
 cmp edx,1024
 ja AtiE3_Restore_Front
 mov AtiE3FrontPitchPixels,edx
 shl edx,1
 mov AtiE3Result[80],edx
 mov ecx,14
 call AtiE2_WaitFifo
 jc AtiE3_Restore_Front
 mov dword ptr [esi+ATIE2_DP_WRITE_MASK],0ffffffffh
 mov dword ptr [esi+ATIE2_DP_PIX_WIDTH],00040404h
 mov dword ptr [esi+ATIE3_SRC_OFF_PITCH],02040000h
 mov eax,AtiE3FrontOffPitch
 mov [esi+ATIE2_DST_OFF_PITCH],eax
 mov dword ptr [esi+ATIE2_DP_SRC],00000300h
 mov dword ptr [esi+ATIE2_DP_MIX],00070000h
 mov dword ptr [esi+ATIE2_CLR_CMP_CNTL],0
 mov dword ptr [esi+ATIE2_DST_CNTL],3
 mov dword ptr [esi+ATIE2_SC_LEFT_RIGHT],000f0000h
 mov dword ptr [esi+ATIE2_SC_TOP_BOTTOM],00010000h
 mov dword ptr [esi+ATIE3_SRC_Y_X],0
 mov dword ptr [esi+ATIE3_SRC_WIDTH1],16
 mov dword ptr [esi+ATIE2_DST_Y_X],0
 mov dword ptr [esi+ATIE2_DST_HEIGHT_WIDTH],00100002h
 inc dword ptr AtiE3Result[60]
 call AtiE2_WaitIdle
 jc AtiE3_Restore_Front
 mov eax,[esi+ATIE2_MEM_BUF_CNTL]
 or eax,00800000h
 mov [esi+ATIE2_MEM_BUF_CNTL],eax

 mov esi,AtiE3FrontLinear
 mov edi,OFFSET32 AtiE3FrontBackup
 mov ebp,OFFSET32 AtiE3Expected
 xor ebx,ebx
 xor edx,edx
 mov ecx,2048
AtiE3_Presentation_Compare:
 cmp ebx,16
 jb short AtiE3_Presentation_Source
 cmp ebx,AtiE3FrontPitchPixels
 jb short AtiE3_Presentation_Guard
 mov eax,AtiE3FrontPitchPixels
 add eax,16
 cmp ebx,eax
 jb short AtiE3_Presentation_Source_Row1
AtiE3_Presentation_Guard:
 mov ax,[esi]
 cmp ax,[edi]
 je short AtiE3_Presentation_Compare_Next
 inc edx
 jmp short AtiE3_Presentation_Compare_Next
AtiE3_Presentation_Source:
 mov ax,[esi]
 cmp ax,[ebp+ebx*2]
 je short AtiE3_Presentation_Compare_Next
 inc edx
 jmp short AtiE3_Presentation_Compare_Next
AtiE3_Presentation_Source_Row1:
 push edi
 mov edi,AtiE3FrontPitchPixels
 sub edi,64
 shl edi,1
 neg edi
 add edi,ebp
 mov ax,[esi]
 cmp ax,[edi+ebx*2]
 pop edi
 je short AtiE3_Presentation_Compare_Next
 inc edx
AtiE3_Presentation_Compare_Next:
 add esi,2
 add edi,2
 inc ebx
 dec ecx
 jnz AtiE3_Presentation_Compare
 mov AtiE3Result[64],edx
 test edx,edx
 jnz short AtiE3_Restore_Front
 or AtiE3Result[4],4000h

AtiE3_Restore_Front:
 mov esi,OFFSET32 AtiE3FrontBackup
 mov edi,AtiE3FrontLinear
 test edi,edi
 jz short AtiE3_Restore_State
 mov ecx,1024
 cld
 rep movsd
 mov esi,OFFSET32 AtiE3FrontBackup
 mov edi,AtiE3FrontLinear
 mov ecx,1024
 xor eax,eax
AtiE3_Check_Front_Restore:
 mov edx,[esi]
 cmp edx,[edi]
 je short AtiE3_Check_Front_Restore_Next
 inc eax
AtiE3_Check_Front_Restore_Next:
 add esi,4
 add edi,4
 dec ecx
 jnz short AtiE3_Check_Front_Restore
 mov AtiE3Result[68],eax
 test eax,eax
 jnz short AtiE3_Restore_State
 or AtiE3Result[4],8000h

AtiE3_Restore_State:
 mov esi,AtiE2MmioLinear
 mov ecx,10
 call AtiE2_WaitFifo
 jc short AtiE3_Restore_Vram
 mov ebx,OFFSET32 AtiE3StateOffsets
 mov edi,OFFSET32 AtiE3StateSaved
 mov ecx,10
AtiE3_Restore_State_Loop:
 mov edx,[ebx]
 mov eax,[edi]
 mov [esi+edx],eax
 add ebx,4
 add edi,4
 dec ecx
 jnz short AtiE3_Restore_State_Loop
 mov eax,AtiE3Result[36]
 mov [esi+ATIE2_MEM_BUF_CNTL],eax
 or AtiE3Result[4],1000h
AtiE3_Restore_Vram:
 mov esi,OFFSET32 AtiE2Backup
 mov edi,AtiE2FbLinear
 test edi,edi
 jz short AtiE3_Done
 mov ecx,1024
 cld
 rep movsd
 mov esi,OFFSET32 AtiE2Backup
 mov edi,AtiE2FbLinear
 mov ecx,1024
 xor eax,eax
AtiE3_Check_Restore:
 mov edx,[esi]
 cmp edx,[edi]
 je short AtiE3_Check_Restore_Next
 inc eax
AtiE3_Check_Restore_Next:
 add esi,4
 add edi,4
 dec ecx
 jnz short AtiE3_Check_Restore
 mov AtiE3Result[56],eax
 test eax,eax
 jnz short AtiE3_Done
 or AtiE3Result[4],2000h
AtiE3_Done:
 popad
 ret
EndProc AtiE3_Run

BeginProc AtiE1_W32_DeviceIoControl
 cmp ecx,DIOC_OPEN
 je AtiE1_Dioc_Ok
 cmp ecx,DIOC_CLOSEHANDLE
 je AtiE1_Dioc_Ok
 cmp ecx,ATIE1_DIOC_RUN
 je short AtiE1_Dioc_Run1
 cmp ecx,ATIE2_DIOC_FILL
 je AtiE1_Dioc_Run2
 cmp ecx,ATIE3_DIOC_COPY
 je AtiE1_Dioc_Run3
 jmp AtiE1_Dioc_Fail
AtiE1_Dioc_Run1:
 pushad
 mov ebp,esi
 call AtiE1_Run
 mov edi,[ebp.lpvOutBuffer]
 test edi,edi
 jz AtiE1_Dioc_Copy_Fail
 cmp [ebp.cbOutBuffer],ATIE1_RESULT_DWORDS*4
 jb AtiE1_Dioc_Copy_Fail
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
 jmp short AtiE1_Dioc_Ok
AtiE1_Dioc_Run2:
 pushad
 mov ebp,esi
 call AtiE2_Run
 mov edi,[ebp.lpvOutBuffer]
 test edi,edi
 jz short AtiE1_Dioc_Copy_Fail
 cmp [ebp.cbOutBuffer],ATIE2_RESULT_DWORDS*4
 jb short AtiE1_Dioc_Copy_Fail
 mov esi,OFFSET32 AtiE2Result
 mov ecx,ATIE2_RESULT_DWORDS
 cld
 rep movsd
 mov eax,[ebp.lpcbBytesReturned]
 test eax,eax
 jz short AtiE2_Dioc_Copy_Done
 mov dword ptr [eax],ATIE2_RESULT_DWORDS*4
AtiE2_Dioc_Copy_Done:
 popad
 jmp short AtiE1_Dioc_Ok
AtiE1_Dioc_Run3:
 pushad
 mov ebp,esi
 call AtiE3_Run
 mov edi,[ebp.lpvOutBuffer]
 test edi,edi
 jz short AtiE1_Dioc_Copy_Fail
 cmp [ebp.cbOutBuffer],ATIE3_RESULT_DWORDS*4
 jb short AtiE1_Dioc_Copy_Fail
 mov esi,OFFSET32 AtiE3Result
 mov ecx,ATIE3_RESULT_DWORDS
 cld
 rep movsd
 mov eax,[ebp.lpcbBytesReturned]
 test eax,eax
 jz short AtiE3_Dioc_Copy_Done
 mov dword ptr [eax],ATIE3_RESULT_DWORDS*4
AtiE3_Dioc_Copy_Done:
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
