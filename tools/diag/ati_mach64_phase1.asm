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
ATIE1_BUS_CNTL equ 04a0h
ATIE1_GEN_TEST_CNTL equ 04d0h
ATIE1_GUI_STAT equ 0738h
ATIE1_PATTERN equ 55555555h
ATIE2_MAGIC equ 32495441h
ATIE2_DIOC_FILL equ 2
ATIE3_DIOC_COPY equ 3
ATIE4_DIOC_TRIANGLE equ 4
ATIE5_DIOC_GOURAUD equ 5
ATIE6_DIOC_ZTEST equ 6
ATIE7_DIOC_ZWRITE equ 7
ATIE8_DIOC_ZCLEAR equ 8
ATIE9_DIOC_TEXTURE equ 9
ATIE10_DIOC_TEXTURE_STATE equ 10
ATIE11_DIOC_PERSPECTIVE equ 11
ATIE12_DIOC_WRAP equ 12
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
ATIE4_MAGIC equ 34495441h
ATIE5_MAGIC equ 35495441h
ATIE6_MAGIC equ 36495441h
ATIE7_MAGIC equ 37495441h
ATIE8_MAGIC equ 38495441h
ATIE9_MAGIC equ 39495441h
ATIE10_MAGIC equ 3a495441h
ATIE11_MAGIC equ 3b495441h
ATIE12_MAGIC equ 3c495441h
ATIE4_RESULT_DWORDS equ 1004
ATIE4_TARGET_OFFSET equ 00200100h
ATIE4_TARGET_PAGE equ 00200000h
ATIE4_TARGET_COLOR equ 0ffff00ffh
ATIE4_EXPECTED_565 equ 0f81fh
ATIE4_SENTINEL equ 0a55ah
ATIE6_DEPTH_PAGE equ 00202000h
ATIE6_DEPTH_OFFSET equ 00202100h
ATIE6_DEPTH_GUARD equ 05aa5h
ATIE6_DEPTH_STORED equ 08000h
ATIE6_DEPTH_INCOMING equ 040000000h
ATIE9_TEXTURE_PAGE equ 00204000h
ATIE9_TEXTURE_OFFSET equ ATIE9_TEXTURE_PAGE
ATIE9_TEXTURE_GUARD equ 05aa5h
ATIE4_STATE_COUNT equ 17
ATIE4_SETUP_COUNT equ 19
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
AtiE4Result label dword
 dd ATIE4_MAGIC
 dd 1003 dup (0)
AtiE4StateOffsets dd 06d4h,06d8h,0708h,0730h,06a8h,06b4h
                   dd 0500h,0548h,054ch,0550h,05fch,06c4h
                   dd 06c8h,06d0h,0304h,0770h,0774h,0778h,05cch
AtiE4StateValues dd 00070007h,00000505h,0,3,003f0000h,001b0000h
                  dd 02040020h,02040020h,0,0,000100c1h,0
                  dd 0ffffffffh,40040444h,00000018h,0,0,0,0
AtiE4SetupOffsets dd 0240h,0244h,0248h,0250h,0254h,0258h
                   dd 0260h,0264h,0268h,0270h,0274h,0278h
                   dd 0280h,0284h,0288h,0290h,0294h,0298h,029ch
AtiE4SetupValues dd 0,0,03f800000h,07fff8000h,ATIE4_TARGET_COLOR,00200018h
                  dd 0,0,03f800000h,07fff8000h,ATIE4_TARGET_COLOR,00a00018h
                  dd 0,0,03f800000h,07fff8000h,ATIE4_TARGET_COLOR,00200058h
                  dd 03b000000h
AtiE4StateSaved dd 19 dup (0)
AtiE4SceneMode dd 0
AtiE4StateCount dd ATIE4_STATE_COUNT
AtiE4SetupCount dd ATIE4_SETUP_COUNT
AtiE4MmioLinear dd 0
AtiE4FbLinear dd 0
AtiE6DepthLinear dd 0
AtiE9TextureLinear dd 0
AtiE4BusSaved dd 0
AtiE4TestSaved dd 0
AtiE8DstYXSaved dd 0
AtiE4Backup db 4096 dup (0)
AtiE6DepthBackup db 4096 dup (0)
AtiE9TextureBackup db 4096 dup (0)
AtiE9TextureData dd 0f800f800h,0f800f800h,007e007e0h,007e007e0h
                 dd 0f800f800h,0f800f800h,007e007e0h,007e007e0h
                 dd 0f800f800h,0f800f800h,007e007e0h,007e007e0h
                 dd 0f800f800h,0f800f800h,007e007e0h,007e007e0h
                 dd 001f001fh,001f001fh,0ffffffffh,0ffffffffh
                 dd 001f001fh,001f001fh,0ffffffffh,0ffffffffh
                 dd 001f001fh,001f001fh,0ffffffffh,0ffffffffh
                 dd 001f001fh,001f001fh,0ffffffffh,0ffffffffh
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
 mov ecx,6
 call AtiE2_WaitFifo
 jc AtiE2_Restore_State
 mov dword ptr [esi+ATIE2_DST_OFF_PITCH],02040000h
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
 ; Mobility-M leaves physical word zero untouched when a fill starts at
 ; logical (0,0). Address that same word through an aligned base 16 bytes
 ; earlier and x=8, then drain before verification.
 mov ecx,3
 call AtiE2_WaitFifo
 jc AtiE2_Restore_State
 mov dword ptr [esi+ATIE2_DST_OFF_PITCH],0203fffeh
 mov dword ptr [esi+ATIE2_DST_Y_X],00080000h
 mov dword ptr [esi+ATIE2_DST_HEIGHT_WIDTH],00010001h
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

; One guarded, off-screen, opaque RGB565 triangle.  Every persistent state
; register is saved and restored; ONE_OVER_AREA is the only draw trigger.
BeginProc AtiE4_Run
 pushad
 ; Reset every scene-dependent value because the dynamic VxD serves all modes.
 mov dword ptr AtiE4Result[0],ATIE4_MAGIC
 mov dword ptr AtiE4StateValues[28],02040020h
 mov dword ptr AtiE4StateValues[32],0
 mov dword ptr AtiE4StateValues[40],000100c1h
 mov dword ptr AtiE4StateValues[56],00000018h
 mov dword ptr AtiE4StateValues[60],0
 mov dword ptr AtiE4StateValues[64],0
 mov dword ptr AtiE4StateValues[68],0
 mov dword ptr AtiE4StateValues[72],0
 mov dword ptr AtiE4StateCount,ATIE4_STATE_COUNT
 mov dword ptr AtiE4SetupCount,ATIE4_SETUP_COUNT
 mov dword ptr AtiE4SetupValues[0],0
 mov dword ptr AtiE4SetupValues[4],0
 mov dword ptr AtiE4SetupValues[8],03f800000h
 mov dword ptr AtiE4SetupValues[24],0
 mov dword ptr AtiE4SetupValues[28],0
 mov dword ptr AtiE4SetupValues[32],03f800000h
 mov dword ptr AtiE4SetupValues[48],0
 mov dword ptr AtiE4SetupValues[52],0
 mov dword ptr AtiE4SetupValues[56],03f800000h
 mov dword ptr AtiE4SetupValues[12],07fff8000h
 mov dword ptr AtiE4SetupValues[36],07fff8000h
 mov dword ptr AtiE4SetupValues[60],07fff8000h
 mov dword ptr AtiE4SetupValues[16],ATIE4_TARGET_COLOR
 mov dword ptr AtiE4SetupValues[40],ATIE4_TARGET_COLOR
 mov dword ptr AtiE4SetupValues[64],ATIE4_TARGET_COLOR
 cmp AtiE4SceneMode,1
 je short AtiE4_Select_Gouraud
 cmp AtiE4SceneMode,2
 je AtiE4_Select_ZTest
 cmp AtiE4SceneMode,3
 je AtiE4_Select_ZWrite
 cmp AtiE4SceneMode,4
 je AtiE4_Select_ZClear
 cmp AtiE4SceneMode,5
 je AtiE4_Select_Texture
 cmp AtiE4SceneMode,6
 je AtiE4_Select_Texture_State
 cmp AtiE4SceneMode,7
 je AtiE4_Select_Perspective
 cmp AtiE4SceneMode,8
 je AtiE4_Select_Wrap
 jmp AtiE4_Selected_Scene
AtiE4_Select_Gouraud:
 mov dword ptr AtiE4Result[0],ATIE5_MAGIC
 mov dword ptr AtiE4StateValues[56],0
 mov dword ptr AtiE4SetupValues[16],0ffff0000h
 mov dword ptr AtiE4SetupValues[40],0ff00ff00h
 mov dword ptr AtiE4SetupValues[64],0ff0000ffh
 jmp AtiE4_Selected_Scene
AtiE4_Select_ZTest:
 mov dword ptr AtiE4Result[0],ATIE6_MAGIC
 mov dword ptr AtiE4StateValues[28],02040420h
 mov dword ptr AtiE4StateValues[32],00000011h
 mov dword ptr AtiE4SetupValues[12],ATIE6_DEPTH_INCOMING
 mov dword ptr AtiE4SetupValues[36],ATIE6_DEPTH_INCOMING
 mov dword ptr AtiE4SetupValues[60],ATIE6_DEPTH_INCOMING
 jmp AtiE4_Selected_Scene
AtiE4_Select_ZWrite:
 mov dword ptr AtiE4Result[0],ATIE7_MAGIC
 mov dword ptr AtiE4StateValues[28],02040420h
 mov dword ptr AtiE4StateValues[32],00000111h
 mov dword ptr AtiE4SetupValues[12],ATIE6_DEPTH_INCOMING
 mov dword ptr AtiE4SetupValues[36],ATIE6_DEPTH_INCOMING
 mov dword ptr AtiE4SetupValues[60],ATIE6_DEPTH_INCOMING
 jmp AtiE4_Selected_Scene
AtiE4_Select_ZClear:
 mov dword ptr AtiE4Result[0],ATIE8_MAGIC
 mov dword ptr AtiE4StateValues[28],02040420h
 mov dword ptr AtiE4StateValues[32],00000111h
 mov dword ptr AtiE4SetupValues[12],ATIE6_DEPTH_INCOMING
 mov dword ptr AtiE4SetupValues[36],ATIE6_DEPTH_INCOMING
 mov dword ptr AtiE4SetupValues[60],ATIE6_DEPTH_INCOMING
 jmp AtiE4_Selected_Scene
AtiE4_Select_Texture:
 mov dword ptr AtiE4Result[0],ATIE9_MAGIC
 jmp short AtiE4_Select_Texture_Common
AtiE4_Select_Texture_State:
 mov dword ptr AtiE4Result[0],ATIE10_MAGIC
 mov dword ptr AtiE4SetupCount,0
 jmp short AtiE4_Select_Texture_Common
AtiE4_Select_Perspective:
 mov dword ptr AtiE4Result[0],ATIE11_MAGIC
 mov dword ptr AtiE4SetupValues[32],03e800000h
 mov dword ptr AtiE4SetupValues[56],03e800000h
 jmp short AtiE4_Select_Texture_Common
AtiE4_Select_Wrap:
 mov dword ptr AtiE4Result[0],ATIE12_MAGIC
AtiE4_Select_Texture_Common:
 mov dword ptr AtiE4StateCount,19
 mov dword ptr AtiE4StateValues[40],00010081h
 mov dword ptr AtiE4StateValues[56],0
 mov dword ptr AtiE4StateValues[60],00000333h
 mov dword ptr AtiE4StateValues[64],40860000h
 mov dword ptr AtiE4StateValues[68],0
 mov dword ptr AtiE4StateValues[72],ATIE9_TEXTURE_OFFSET
 mov dword ptr AtiE4SetupValues[0],0be800000h
 mov dword ptr AtiE4SetupValues[4],0be800000h
 mov dword ptr AtiE4SetupValues[24],03fa00000h
 mov dword ptr AtiE4SetupValues[28],0be800000h
 mov dword ptr AtiE4SetupValues[48],0be800000h
 mov dword ptr AtiE4SetupValues[52],03fa00000h
 cmp AtiE4SceneMode,8
 jne short AtiE4_Selected_Scene
 mov dword ptr AtiE4StateValues[64],40800000h
AtiE4_Selected_Scene:
 mov AtiE4Result[4],0
 mov edi,OFFSET32 AtiE4Result+8
 mov ecx,ATIE4_RESULT_DWORDS-2
 xor eax,eax
 cld
 rep stosd
 mov AtiE4MmioLinear,0
 mov AtiE4FbLinear,0
 mov AtiE6DepthLinear,0
 mov AtiE9TextureLinear,0
 mov dword ptr AtiE4Result[48],ATIE4_TARGET_OFFSET
 mov dword ptr AtiE4Result[52],02040020h
 mov eax,AtiE4SetupValues[16]
 mov AtiE4Result[56],eax
 mov dword ptr AtiE4Result[60],03b000000h
 mov dword ptr AtiE4Result[84],0ffffffffh
 mov dword ptr AtiE4Result[88],0ffffffffh
 mov eax,AtiE4StateCount
 mov AtiE4Result[104],eax
 mov eax,AtiE4SetupCount
 mov AtiE4Result[108],eax

 ; Publish the intended transcript before the first engine write.
 mov esi,OFFSET32 AtiE4StateOffsets
 mov edi,OFFSET32 AtiE4Result+128
 mov ecx,AtiE4StateCount
 rep movsd
 mov esi,OFFSET32 AtiE4SetupOffsets
 mov ecx,AtiE4SetupCount
 rep movsd
 mov esi,OFFSET32 AtiE4StateValues
 mov edi,OFFSET32 AtiE4Result+280
 mov ecx,AtiE4StateCount
 rep movsd
 mov esi,OFFSET32 AtiE4SetupValues
 mov ecx,AtiE4SetupCount
 rep movsd

 mov ebx,80000000h
AtiE4_Pci_Next:
 mov eax,ebx
 call AtiE1_Pci_Read
 cmp eax,4c4d1002h
 je short AtiE4_Pci_Found
 add ebx,0800h
 cmp ebx,81000000h
 jb short AtiE4_Pci_Next
 jmp AtiE4_Done
AtiE4_Pci_Found:
 or AtiE4Result[4],1
 mov eax,ebx
 or eax,04h
 call AtiE1_Pci_Read
 mov AtiE4Result[8],eax
 test eax,3
 jz AtiE4_Done
 or AtiE4Result[4],2
 mov eax,ebx
 or eax,08h
 call AtiE1_Pci_Read
 mov AtiE4Result[12],eax
 cmp al,64h
 jne AtiE4_Done
 mov eax,ebx
 or eax,10h
 call AtiE1_Pci_Read
 and eax,0fffffff0h
 mov AtiE4Result[16],eax
 cmp eax,0f5000000h
 jne AtiE4_Done
 mov eax,ebx
 or eax,18h
 call AtiE1_Pci_Read
 and eax,0fffffff0h
 mov AtiE4Result[20],eax
 cmp eax,0f4100000h
 jne AtiE4_Done
 VMMcall _MapPhysToLinear,<eax,1000h,0>
 cmp eax,0ffffffffh
 je AtiE4_Done
 test eax,eax
 jz AtiE4_Done
 mov AtiE4MmioLinear,eax
 mov esi,eax
 or AtiE4Result[4],4
 mov eax,[esi+ATIE1_CONFIG_CHIP_ID]
 mov AtiE4Result[24],eax
 cmp eax,64004c4dh
 jne AtiE4_Done
 or AtiE4Result[4],8
 mov eax,[esi+ATIE1_CONFIG_STAT0]
 mov AtiE4Result[28],eax
 and eax,7
 cmp eax,6
 jne AtiE4_Done
 mov eax,[esi+ATIE1_GUI_STAT]
 mov AtiE4Result[32],eax
 mov AtiE4Result[100],eax
 test eax,1
 jnz AtiE4_Done
 or AtiE4Result[4],10h

 mov eax,AtiE4Result[16]
 add eax,ATIE4_TARGET_PAGE
 VMMcall _MapPhysToLinear,<eax,1000h,0>
 cmp eax,0ffffffffh
 je AtiE4_Done
 test eax,eax
 jz AtiE4_Done
 mov AtiE4FbLinear,eax
 or AtiE4Result[4],20h
 cmp AtiE4SceneMode,2
 jb short AtiE4_Depth_Map_Done
 cmp AtiE4SceneMode,5
 jae short AtiE4_Texture_Map
 mov eax,AtiE4Result[16]
 add eax,ATIE6_DEPTH_PAGE
 VMMcall _MapPhysToLinear,<eax,1000h,0>
 cmp eax,0ffffffffh
 je AtiE4_Done
 test eax,eax
 jz AtiE4_Done
 mov AtiE6DepthLinear,eax
 jmp short AtiE4_Depth_Map_Done
AtiE4_Texture_Map:
 mov eax,AtiE4Result[16]
 add eax,ATIE9_TEXTURE_PAGE
 VMMcall _MapPhysToLinear,<eax,1000h,0>
 cmp eax,0ffffffffh
 je AtiE4_Done
 test eax,eax
 jz AtiE4_Done
 mov AtiE9TextureLinear,eax
AtiE4_Depth_Map_Done:
 mov esi,AtiE4MmioLinear
 call AtiE2_WaitIdle
 jc AtiE4_Done

 mov ebx,OFFSET32 AtiE4StateOffsets
 mov edi,OFFSET32 AtiE4StateSaved
 mov ecx,AtiE4StateCount
AtiE4_Save_State:
 mov edx,[ebx]
 mov eax,[esi+edx]
 mov [edi],eax
 add ebx,4
 add edi,4
 dec ecx
 jnz short AtiE4_Save_State
 mov eax,[esi+ATIE2_MEM_BUF_CNTL]
 mov AtiE4Result[40],eax
 mov eax,[esi+ATIE1_BUS_CNTL]
 mov AtiE4BusSaved,eax
 mov eax,[esi+ATIE1_GEN_TEST_CNTL]
 mov AtiE4TestSaved,eax
 cmp AtiE4SceneMode,4
 jne short AtiE4_Save_Extra_Done
 mov eax,[esi+ATIE2_DST_Y_X]
 mov AtiE8DstYXSaved,eax
AtiE4_Save_Extra_Done:
 or AtiE4Result[4],80h

 mov esi,AtiE4FbLinear
 mov edi,OFFSET32 AtiE4Backup
 mov ecx,1024
 cld
 rep movsd
 mov edi,AtiE4FbLinear
 mov ax,ATIE4_SENTINEL
 mov ecx,2048
 rep stosw
 mov ax,[edi-2]
 or AtiE4Result[4],40h

 ; The Z test owns a different 4K page.  Preserve it, surround the 64x28
 ; Z16 surface with guards, and seed every stored depth to 0x8000.
 cmp AtiE4SceneMode,2
 jb short AtiE4_Depth_Init_Done
 cmp AtiE4SceneMode,4
 ja short AtiE4_Depth_Init_Done
 mov esi,AtiE6DepthLinear
 mov edi,OFFSET32 AtiE6DepthBackup
 mov ecx,1024
 cld
 rep movsd
 mov edi,AtiE6DepthLinear
 mov ax,ATIE6_DEPTH_GUARD
 mov ecx,2048
 rep stosw
 mov edi,AtiE6DepthLinear
 add edi,100h
 mov ax,ATIE6_DEPTH_STORED
 mov ecx,1792
 rep stosw
AtiE4_Depth_Init_Done:

 ; A local-VRAM 8x8 RGB565 texture, with the rest of its page guarded.
 cmp AtiE4SceneMode,5
 jb short AtiE4_Texture_Init_Done
 mov esi,AtiE9TextureLinear
 mov edi,OFFSET32 AtiE9TextureBackup
 mov ecx,1024
 cld
 rep movsd
 mov edi,AtiE9TextureLinear
 mov ax,ATIE9_TEXTURE_GUARD
 mov ecx,2048
 rep stosw
 mov esi,OFFSET32 AtiE9TextureData
 mov edi,AtiE9TextureLinear
 mov ecx,32
 rep movsd
AtiE4_Texture_Init_Done:

 ; Emit all complete state in one reserved, status-read-free batch.
 mov esi,AtiE4MmioLinear
 mov ecx,AtiE4StateCount
 call AtiE2_WaitFifo
 jnc short AtiE4_State_Fifo_Ok
 mov dword ptr AtiE4Result[116],1
 jmp AtiE4_Reset_Then_Restore
AtiE4_State_Fifo_Ok:
 mov ebx,OFFSET32 AtiE4StateOffsets
 mov edi,OFFSET32 AtiE4StateValues
 mov ecx,AtiE4StateCount
AtiE4_Emit_State:
 mov edx,[ebx]
 mov eax,[edi]
 mov [esi+edx],eax
 add ebx,4
 add edi,4
 dec ecx
 jnz short AtiE4_Emit_State
 or AtiE4Result[4],100h

 cmp AtiE4SceneMode,6
 je AtiE4_After_Setup

 ; Three exact setup batches: vertices 1, 2, then vertex 3 plus trigger.
 mov ebx,OFFSET32 AtiE4SetupOffsets
 mov edi,OFFSET32 AtiE4SetupValues
 mov ebp,2
AtiE4_Emit_Vertex_Batch:
 mov ecx,6
 call AtiE2_WaitFifo
 jnc short AtiE4_Vertex_Fifo_Ok
 mov eax,3
 sub eax,ebp
 mov AtiE4Result[116],eax
 jmp AtiE4_Reset_Then_Restore
AtiE4_Vertex_Fifo_Ok:
 mov ecx,6
AtiE4_Emit_Vertex:
 mov edx,[ebx]
 mov eax,[edi]
 mov [esi+edx],eax
 add ebx,4
 add edi,4
 dec ecx
 jnz short AtiE4_Emit_Vertex
 dec ebp
 jnz short AtiE4_Emit_Vertex_Batch
 mov ecx,7
 call AtiE2_WaitFifo
 jnc short AtiE4_Final_Fifo_Ok
 mov dword ptr AtiE4Result[116],4
 jmp AtiE4_Reset_Then_Restore
AtiE4_Final_Fifo_Ok:
 mov ecx,7
AtiE4_Emit_Final:
 mov edx,[ebx]
 mov eax,[edi]
 mov [esi+edx],eax
 add ebx,4
 add edi,4
 dec ecx
 jnz short AtiE4_Emit_Final
 or AtiE4Result[4],200h
AtiE4_After_Setup:
 call AtiE2_WaitIdle
 jnc short AtiE4_Idle_Ok
 mov dword ptr AtiE4Result[116],5
 jmp AtiE4_Reset_Then_Restore
AtiE4_Idle_Ok:
 mov eax,[esi+ATIE1_GUI_STAT]
 mov AtiE4Result[36],eax
 or AtiE4Result[4],400h
 mov eax,[esi+ATIE2_MEM_BUF_CNTL]
 or eax,00800000h
 mov [esi+ATIE2_MEM_BUF_CNTL],eax
 mov eax,[esi+ATIE2_MEM_BUF_CNTL]
 mov AtiE4Result[44],eax
 or AtiE4Result[4],800h

 cmp AtiE4SceneMode,6
 jne short AtiE4_Inspect_Render
 or AtiE4Result[4],3000h
 jmp AtiE4_Guards

 ; Interior samples are deliberately far from all three edges.
AtiE4_Inspect_Render:
 mov esi,AtiE4FbLinear
 add esi,100h
 cmp AtiE4SceneMode,5
 jae AtiE9_Texture_Interior
 cmp AtiE4SceneMode,1
 je AtiE4_Gouraud_Interior
 mov ax,[esi+0518h]
 cmp ax,ATIE4_EXPECTED_565
 je short AtiE4_Interior_2
 inc dword ptr AtiE4Result[64]
 movzx eax,ax
 mov AtiE4Result[120],eax
 mov dword ptr AtiE4Result[124],ATIE4_EXPECTED_565
AtiE4_Interior_2:
 mov ax,[esi+0520h]
 cmp ax,ATIE4_EXPECTED_565
 je short AtiE4_Interior_3
 inc dword ptr AtiE4Result[64]
AtiE4_Interior_3:
 mov ax,[esi+0718h]
 cmp ax,ATIE4_EXPECTED_565
 je short AtiE4_Interior_Done
 inc dword ptr AtiE4Result[64]
AtiE4_Interior_Done:
 cmp dword ptr AtiE4Result[64],0
 jne AtiE4_Exterior
 or AtiE4Result[4],1000h
 jmp AtiE4_Exterior

AtiE9_Texture_Interior:
 cmp AtiE4SceneMode,8
 je AtiE12_Wrap_Interior
 mov ax,[esi+0414h] ; (10,8), S/T below zero clamp to top-left red
 cmp ax,0f800h
 je short AtiE9_Texture_Interior_2
 inc dword ptr AtiE4Result[64]
 movzx eax,ax
 mov AtiE4Result[120],eax
 mov dword ptr AtiE4Result[124],0000f800h
AtiE9_Texture_Interior_2:
 cmp AtiE4SceneMode,7
 jne short AtiE9_Texture_Affine_Probe_2
 mov ax,[esi+043ch] ; (30,8), perspective-correct red
 cmp ax,0f800h
 jmp short AtiE9_Texture_Probe_2_Result
AtiE9_Texture_Affine_Probe_2:
 mov ax,[esi+0444h] ; (34,8), top-right green
 cmp ax,007e0h
AtiE9_Texture_Probe_2_Result:
 je short AtiE9_Texture_Interior_3
 inc dword ptr AtiE4Result[64]
AtiE9_Texture_Interior_3:
 cmp AtiE4SceneMode,7
 jne short AtiE9_Texture_Affine_Probe_3
 mov ax,[esi+0814h] ; (10,16), perspective-correct red
 cmp ax,0f800h
 jmp short AtiE9_Texture_Probe_3_Result
AtiE9_Texture_Affine_Probe_3:
 mov ax,[esi+0914h] ; (10,18), bottom-left blue
 cmp ax,001fh
AtiE9_Texture_Probe_3_Result:
 je short AtiE9_Texture_Interior_Done
 inc dword ptr AtiE4Result[64]
AtiE9_Texture_Interior_Done:
 cmp dword ptr AtiE4Result[64],0
 jne AtiE4_Exterior
 or AtiE4Result[4],1000h
 jmp AtiE4_Exterior

AtiE12_Wrap_Interior:
 mov ax,[esi+0414h] ; (10,8), negative S/T repeat into bottom-right white
 cmp ax,0ffffh
 je short AtiE12_Wrap_Interior_2
 inc dword ptr AtiE4Result[64]
 movzx eax,ax
 mov AtiE4Result[120],eax
 mov dword ptr AtiE4Result[124],0000ffffh
AtiE12_Wrap_Interior_2:
 mov ax,[esi+0444h] ; (34,8), negative T repeats into bottom-right white
 cmp ax,0ffffh
 je short AtiE12_Wrap_Interior_3
 inc dword ptr AtiE4Result[64]
AtiE12_Wrap_Interior_3:
 mov ax,[esi+0914h] ; (10,18), negative S repeats into bottom-right white
 cmp ax,0ffffh
 je short AtiE12_Wrap_Interior_Done
 inc dword ptr AtiE4Result[64]
AtiE12_Wrap_Interior_Done:
 cmp dword ptr AtiE4Result[64],0
 jne AtiE4_Exterior
 or AtiE4Result[4],1000h
 jmp AtiE4_Exterior

 ; Four samples prove red, green and blue dominance plus a mixed interior.
 ; Component thresholds tolerate the setup engine's subpixel rounding while
 ; still rejecting flat colour, a missing channel or an untouched sentinel.
AtiE4_Gouraud_Interior:
 movzx ebx,word ptr [esi+0414h] ; (10,8), red-dominant
 mov eax,ebx
 and eax,31
 mov ecx,ebx
 shr ecx,5
 and ecx,63
 mov edx,ebx
 shr edx,11
 cmp edx,16
 jb short AtiE4_Gouraud_Red_Fail
 cmp edx,ecx
 jbe short AtiE4_Gouraud_Red_Fail
 cmp edx,eax
 ja short AtiE4_Gouraud_Green
AtiE4_Gouraud_Red_Fail:
 inc dword ptr AtiE4Result[64]
 mov AtiE4Result[120],ebx
 mov dword ptr AtiE4Result[124],00100000h

AtiE4_Gouraud_Green:
 movzx ebx,word ptr [esi+0444h] ; (34,8), green-dominant
 mov eax,ebx
 and eax,31
 mov ecx,ebx
 shr ecx,5
 and ecx,63
 mov edx,ebx
 shr edx,11
 cmp ecx,32
 jb short AtiE4_Gouraud_Green_Fail
 cmp ecx,edx
 jbe short AtiE4_Gouraud_Green_Fail
 cmp ecx,eax
 ja short AtiE4_Gouraud_Blue
AtiE4_Gouraud_Green_Fail:
 inc dword ptr AtiE4Result[64]

AtiE4_Gouraud_Blue:
 movzx ebx,word ptr [esi+0914h] ; (10,18), blue-dominant
 mov eax,ebx
 and eax,31
 mov ecx,ebx
 shr ecx,5
 and ecx,63
 mov edx,ebx
 shr edx,11
 cmp eax,16
 jb short AtiE4_Gouraud_Blue_Fail
 cmp eax,edx
 jbe short AtiE4_Gouraud_Blue_Fail
 cmp eax,ecx
 ja short AtiE4_Gouraud_Mixed
AtiE4_Gouraud_Blue_Fail:
 inc dword ptr AtiE4Result[64]

AtiE4_Gouraud_Mixed:
 movzx ebx,word ptr [esi+0620h] ; (16,12), mixed interior
 mov eax,ebx
 and eax,31
 mov ecx,ebx
 shr ecx,5
 and ecx,63
 mov edx,ebx
 shr edx,11
 cmp eax,4
 jb short AtiE4_Gouraud_Mixed_Fail
 cmp ecx,4
 jb short AtiE4_Gouraud_Mixed_Fail
 cmp edx,4
 jae short AtiE4_Gouraud_Interior_Done
AtiE4_Gouraud_Mixed_Fail:
 inc dword ptr AtiE4Result[64]
AtiE4_Gouraud_Interior_Done:
 cmp dword ptr AtiE4Result[64],0
 jne short AtiE4_Exterior
 or AtiE4Result[4],1000h

AtiE4_Exterior:
 mov ax,[esi+0560h]
 cmp ax,ATIE4_SENTINEL
 je short AtiE4_Exterior_2
 inc dword ptr AtiE4Result[68]
AtiE4_Exterior_2:
 mov ax,[esi+0960h]
 cmp ax,ATIE4_SENTINEL
 je short AtiE4_Exterior_3
 inc dword ptr AtiE4Result[68]
AtiE4_Exterior_3:
 mov ax,[esi+0208h]
 cmp ax,ATIE4_SENTINEL
 je short AtiE4_Exterior_Done
 inc dword ptr AtiE4Result[68]
AtiE4_Exterior_Done:
 cmp dword ptr AtiE4Result[68],0
 jne short AtiE4_Guards
 or AtiE4Result[4],2000h

 ; The first and last 256 bytes surround the target surface physically.
AtiE4_Guards:
 cmp AtiE4SceneMode,5
 jae AtiE9_Texture_Guards
 cmp AtiE4SceneMode,2
 jb AtiE4_Color_Guards
 cmp AtiE4SceneMode,3
 jae AtiE7_Depth_Target
 mov esi,AtiE6DepthLinear
 add esi,100h
 mov ecx,1792
AtiE6_Depth_Target:
 cmp word ptr [esi],ATIE6_DEPTH_STORED
 je short AtiE6_Depth_Target_Next
 inc dword ptr AtiE4Result[72]
AtiE6_Depth_Target_Next:
 add esi,2
 dec ecx
 jnz short AtiE6_Depth_Target
 jmp AtiE6_Depth_Guards

AtiE9_Texture_Guards:
 mov esi,AtiE9TextureLinear
 add esi,80h
 mov ecx,1984
AtiE9_Texture_Guard_After:
 cmp word ptr [esi],ATIE9_TEXTURE_GUARD
 je short AtiE9_Texture_Guard_After_Next
 inc dword ptr AtiE4Result[72]
AtiE9_Texture_Guard_After_Next:
 add esi,2
 dec ecx
 jnz short AtiE9_Texture_Guard_After
 mov esi,AtiE9TextureLinear
 mov edi,OFFSET32 AtiE9TextureData
 mov ecx,32
AtiE9_Texture_Data_Check:
 mov eax,[esi]
 cmp eax,[edi]
 je short AtiE9_Texture_Data_Next
 inc dword ptr AtiE4Result[72]
AtiE9_Texture_Data_Next:
 add esi,4
 add edi,4
 dec ecx
 jnz short AtiE9_Texture_Data_Check
 jmp AtiE4_Color_Guards

 ; With writes enabled, every rasterized color pixel must have the submitted
 ; Z value and every untouched color pixel must retain the seeded depth.
AtiE7_Depth_Target:
 mov esi,AtiE4FbLinear
 add esi,100h
 mov edi,AtiE6DepthLinear
 add edi,100h
 mov ecx,1792
AtiE7_Depth_Target_Loop:
 mov ax,[esi]
 cmp ax,ATIE4_SENTINEL
 jne short AtiE7_Depth_Changed
 movzx eax,word ptr [edi]
 cmp ax,ATIE6_DEPTH_STORED
 je short AtiE7_Depth_Target_Next
 inc dword ptr AtiE4Result[72]
 cmp dword ptr AtiE4Result[124],0
 jne short AtiE7_Depth_Target_Next
 mov AtiE4Result[120],eax
 mov dword ptr AtiE4Result[124],ATIE6_DEPTH_STORED
 jmp short AtiE7_Depth_Target_Next
AtiE7_Depth_Changed:
 movzx eax,word ptr [edi]
 cmp ax,04000h
 je short AtiE7_Depth_Target_Next
 inc dword ptr AtiE4Result[72]
 cmp dword ptr AtiE4Result[124],0
 jne short AtiE7_Depth_Target_Next
 mov AtiE4Result[120],eax
 mov dword ptr AtiE4Result[124],04000h
AtiE7_Depth_Target_Next:
 add esi,2
 add edi,2
 dec ecx
 jnz short AtiE7_Depth_Target_Loop

AtiE6_Depth_Guards:
 mov esi,AtiE6DepthLinear
 mov ecx,128
AtiE6_Depth_Guard_Before:
 cmp word ptr [esi],ATIE6_DEPTH_GUARD
 je short AtiE6_Depth_Guard_Before_Next
 inc dword ptr AtiE4Result[72]
AtiE6_Depth_Guard_Before_Next:
 add esi,2
 dec ecx
 jnz short AtiE6_Depth_Guard_Before
 mov esi,AtiE6DepthLinear
 add esi,0f00h
 mov ecx,128
AtiE6_Depth_Guard_After:
 cmp word ptr [esi],ATIE6_DEPTH_GUARD
 je short AtiE6_Depth_Guard_After_Next
 inc dword ptr AtiE4Result[72]
AtiE6_Depth_Guard_After_Next:
 add esi,2
 dec ecx
 jnz short AtiE6_Depth_Guard_After

AtiE4_Color_Guards:
 mov esi,AtiE4FbLinear
 mov ecx,128
AtiE4_Guard_Before:
 cmp word ptr [esi],ATIE4_SENTINEL
 je short AtiE4_Guard_Before_Next
 inc dword ptr AtiE4Result[72]
AtiE4_Guard_Before_Next:
 add esi,2
 dec ecx
 jnz short AtiE4_Guard_Before
 mov esi,AtiE4FbLinear
 add esi,0f00h
 mov ecx,128
AtiE4_Guard_After:
 cmp word ptr [esi],ATIE4_SENTINEL
 je short AtiE4_Guard_After_Next
 inc dword ptr AtiE4Result[72]
AtiE4_Guard_After_Next:
 add esi,2
 dec ecx
 jnz short AtiE4_Guard_After
 cmp dword ptr AtiE4Result[72],0
 jne short AtiE4_Count_Changed
 or AtiE4Result[4],4000h

 ; Record changed-pixel count and its bounding box without assuming edges.
AtiE4_Count_Changed:
 mov esi,AtiE4FbLinear
 add esi,100h
 xor edx,edx
AtiE4_Changed_Row:
 xor ecx,ecx
AtiE4_Changed_Col:
 cmp word ptr [esi],ATIE4_SENTINEL
 je short AtiE4_Changed_Next
 inc dword ptr AtiE4Result[80]
 cmp ecx,AtiE4Result[84]
 jae short AtiE4_Min_X_Done
 mov AtiE4Result[84],ecx
AtiE4_Min_X_Done:
 cmp edx,AtiE4Result[88]
 jae short AtiE4_Min_Y_Done
 mov AtiE4Result[88],edx
AtiE4_Min_Y_Done:
 cmp ecx,AtiE4Result[92]
 jbe short AtiE4_Max_X_Done
 mov AtiE4Result[92],ecx
AtiE4_Max_X_Done:
 cmp edx,AtiE4Result[96]
 jbe short AtiE4_Changed_Next
 mov AtiE4Result[96],edx
AtiE4_Changed_Next:
 add esi,2
 inc ecx
 cmp ecx,64
 jb short AtiE4_Changed_Col
 inc edx
 cmp edx,28
 jb short AtiE4_Changed_Row

 ; Preserve the 64x28 target image in the returned result before restoration.
 mov esi,AtiE4FbLinear
 add esi,100h
 mov edi,OFFSET32 AtiE4Result+432
 mov ecx,896
 cld
 rep movsd
 cmp AtiE4SceneMode,4
 jne AtiE4_Restore_State

 ; Order the proven 2D fill behind the completed 3D Z write, clear the exact
 ; same Z16 surface to FFFF, repair the Mobility origin word, then observe it.
 ; Disabling Z before the clear forces its dirty cache to retire before the
 ; 2D engine overwrites the same allocation.
 mov esi,AtiE4MmioLinear
 mov ecx,1
 call AtiE2_WaitFifo
 jnc short AtiE8_Disable_Z_Fifo_Ok
 mov dword ptr AtiE4Result[116],8
 jmp AtiE4_Reset_Then_Restore
AtiE8_Disable_Z_Fifo_Ok:
 mov dword ptr [esi+054ch],0
 call AtiE2_WaitIdle
 jnc short AtiE8_Disable_Z_Idle_Ok
 mov dword ptr AtiE4Result[116],9
 jmp AtiE4_Reset_Then_Restore
AtiE8_Disable_Z_Idle_Ok:
 mov ecx,12
 call AtiE2_WaitFifo
 jnc short AtiE8_Clear_Fifo_Ok
 mov dword ptr AtiE4Result[116],10
 jmp AtiE4_Reset_Then_Restore
AtiE8_Clear_Fifo_Ok:
 mov dword ptr [esi+ATIE2_DST_OFF_PITCH],02040420h
 mov dword ptr [esi+ATIE2_DST_CNTL],00000003h
 mov dword ptr [esi+ATIE2_SC_LEFT_RIGHT],003f0000h
 mov dword ptr [esi+ATIE2_SC_TOP_BOTTOM],001b0000h
 mov dword ptr [esi+ATIE2_DP_FRGD_CLR],0000ffffh
 mov dword ptr [esi+ATIE2_DP_WRITE_MASK],0ffffffffh
 mov dword ptr [esi+ATIE2_DP_PIX_WIDTH],00040004h
 mov dword ptr [esi+ATIE2_DP_MIX],00070003h
 mov dword ptr [esi+ATIE2_DP_SRC],00000100h
 mov dword ptr [esi+ATIE2_CLR_CMP_CNTL],0
 mov dword ptr [esi+ATIE2_DST_Y_X],0
 mov dword ptr [esi+ATIE2_DST_HEIGHT_WIDTH],0040001ch
 call AtiE2_WaitIdle
 jnc short AtiE8_Clear_Idle_Ok
 mov dword ptr AtiE4Result[116],11
 jmp AtiE4_Reset_Then_Restore
AtiE8_Clear_Idle_Ok:
 mov ecx,3
 call AtiE2_WaitFifo
 jnc short AtiE8_Repair_Fifo_Ok
 mov dword ptr AtiE4Result[116],12
 jmp AtiE4_Reset_Then_Restore
AtiE8_Repair_Fifo_Ok:
 mov dword ptr [esi+ATIE2_DST_OFF_PITCH],0204041eh
 mov dword ptr [esi+ATIE2_DST_Y_X],00080000h
 mov dword ptr [esi+ATIE2_DST_HEIGHT_WIDTH],00010001h
 call AtiE2_WaitIdle
 jnc short AtiE8_Repair_Idle_Ok
 mov dword ptr AtiE4Result[116],13
 jmp AtiE4_Reset_Then_Restore
AtiE8_Repair_Idle_Ok:
 mov eax,[esi+ATIE2_MEM_BUF_CNTL]
 or eax,00800000h
 mov [esi+ATIE2_MEM_BUF_CNTL],eax
 mov esi,AtiE6DepthLinear
 add esi,100h
 mov ecx,1792
AtiE8_Clear_Verify:
 cmp word ptr [esi],0ffffh
 je short AtiE8_Clear_Verify_Next
 inc dword ptr AtiE4Result[72]
 cmp dword ptr AtiE4Result[124],0
 jne short AtiE8_Clear_Verify_Next
 movzx eax,word ptr [esi]
 mov AtiE4Result[120],eax
 mov dword ptr AtiE4Result[124],0ffffh
AtiE8_Clear_Verify_Next:
 add esi,2
 dec ecx
 jnz short AtiE8_Clear_Verify
 mov esi,AtiE6DepthLinear
 mov ecx,128
AtiE8_Clear_Guard_Before:
 cmp word ptr [esi],ATIE6_DEPTH_GUARD
 je short AtiE8_Clear_Guard_Before_Next
 inc dword ptr AtiE4Result[72]
AtiE8_Clear_Guard_Before_Next:
 add esi,2
 dec ecx
 jnz short AtiE8_Clear_Guard_Before
 mov esi,AtiE6DepthLinear
 add esi,0f00h
 mov ecx,128
AtiE8_Clear_Guard_After:
 cmp word ptr [esi],ATIE6_DEPTH_GUARD
 je short AtiE8_Clear_Guard_After_Next
 inc dword ptr AtiE4Result[72]
AtiE8_Clear_Guard_After_Next:
 add esi,2
 dec ecx
 jnz short AtiE8_Clear_Guard_After
 ; For this diagnostic the BMP is a literal post-clear Z16 dump.  White is
 ; cleared depth; any retained 0x8000/0x4000 region is directly visible.
 mov esi,AtiE6DepthLinear
 add esi,100h
 mov edi,OFFSET32 AtiE4Result+432
 mov ecx,896
 cld
 rep movsd
 jmp AtiE4_Restore_State

AtiE4_Reset_Then_Restore:
 cmp dword ptr AtiE4Result[112],0
 jne AtiE4_Restore_Vram
 mov esi,AtiE4MmioLinear
 mov eax,AtiE4BusSaved
 and eax,0ffbfffffh
 or eax,00800004h
 mov [esi+ATIE1_BUS_CNTL],eax
 mov eax,AtiE4TestSaved
 and eax,0fffffeffh
 mov [esi+ATIE1_GEN_TEST_CNTL],eax
 or eax,00000100h
 mov [esi+ATIE1_GEN_TEST_CNTL],eax
 inc dword ptr AtiE4Result[112]

AtiE4_Restore_State:
 mov esi,AtiE4MmioLinear
 test esi,esi
 jz AtiE4_Restore_Vram
 mov ecx,AtiE4StateCount
 cmp AtiE4SceneMode,4
 jne short AtiE4_Restore_Fifo_Count_Ready
 inc ecx
AtiE4_Restore_Fifo_Count_Ready:
 call AtiE2_WaitFifo
 jnc short AtiE4_Restore_Fifo_Ok
 mov dword ptr AtiE4Result[116],6
 cmp dword ptr AtiE4Result[112],0
 je AtiE4_Reset_Then_Restore
 jmp AtiE4_Restore_Vram
AtiE4_Restore_Fifo_Ok:
 mov ebx,OFFSET32 AtiE4StateOffsets
 mov edi,OFFSET32 AtiE4StateSaved
 mov ecx,AtiE4StateCount
AtiE4_Restore_State_Loop:
 mov edx,[ebx]
 mov eax,[edi]
 mov [esi+edx],eax
 add ebx,4
 add edi,4
 dec ecx
 jnz short AtiE4_Restore_State_Loop
 cmp AtiE4SceneMode,4
 jne short AtiE4_Restore_Extra_Done
 mov eax,AtiE8DstYXSaved
 mov [esi+ATIE2_DST_Y_X],eax
AtiE4_Restore_Extra_Done:
 mov eax,AtiE4Result[40]
 mov [esi+ATIE2_MEM_BUF_CNTL],eax
 cmp dword ptr AtiE4Result[112],0
 je short AtiE4_Restore_No_Reset_State
 mov eax,AtiE4BusSaved
 mov [esi+ATIE1_BUS_CNTL],eax
 mov eax,AtiE4TestSaved
 mov [esi+ATIE1_GEN_TEST_CNTL],eax
AtiE4_Restore_No_Reset_State:
 call AtiE2_WaitIdle
 jnc short AtiE4_Restore_Idle_Ok
 mov dword ptr AtiE4Result[116],7
 jmp short AtiE4_Restore_Vram
AtiE4_Restore_Idle_Ok:
 mov ebx,OFFSET32 AtiE4StateOffsets
 mov edi,OFFSET32 AtiE4StateSaved
 mov ecx,AtiE4StateCount
 xor eax,eax
AtiE4_Verify_State_Restore:
 mov edx,[ebx]
 mov ebp,[esi+edx]
 cmp ebp,[edi]
 je short AtiE4_Verify_State_Next
 inc eax
AtiE4_Verify_State_Next:
 add ebx,4
 add edi,4
 dec ecx
 jnz short AtiE4_Verify_State_Restore
 mov edx,[esi+ATIE2_MEM_BUF_CNTL]
 cmp edx,AtiE4Result[40]
 je short AtiE4_Verify_State_Done
 inc eax
AtiE4_Verify_State_Done:
 cmp AtiE4SceneMode,4
 jne short AtiE4_Verify_Extra_Done
 mov edx,[esi+ATIE2_DST_Y_X]
 cmp edx,AtiE8DstYXSaved
 je short AtiE4_Verify_Extra_Done
 inc eax
AtiE4_Verify_Extra_Done:
 mov AtiE4Result[76],eax
 test eax,eax
 jnz short AtiE4_Restore_Vram
 or AtiE4Result[4],8000h

AtiE4_Restore_Vram:
 mov edi,AtiE4FbLinear
 test edi,edi
 jz short AtiE4_Restore_Depth
 mov esi,OFFSET32 AtiE4Backup
 mov ecx,1024
 cld
 rep movsd
 mov esi,OFFSET32 AtiE4Backup
 mov edi,AtiE4FbLinear
 mov ecx,1024
 mov eax,AtiE4Result[76]
AtiE4_Check_Restore:
 mov edx,[esi]
 cmp edx,[edi]
 je short AtiE4_Check_Restore_Next
 inc eax
AtiE4_Check_Restore_Next:
 add esi,4
 add edi,4
 dec ecx
 jnz short AtiE4_Check_Restore
 mov AtiE4Result[76],eax

AtiE4_Restore_Depth:
 mov edi,AtiE6DepthLinear
 test edi,edi
 jz short AtiE4_Restore_Texture
 mov esi,OFFSET32 AtiE6DepthBackup
 mov ecx,1024
 cld
 rep movsd
 mov esi,OFFSET32 AtiE6DepthBackup
 mov edi,AtiE6DepthLinear
 mov ecx,1024
 mov eax,AtiE4Result[76]
AtiE6_Check_Restore:
 mov edx,[esi]
 cmp edx,[edi]
 je short AtiE6_Check_Restore_Next
 inc eax
AtiE6_Check_Restore_Next:
 add esi,4
 add edi,4
 dec ecx
 jnz short AtiE6_Check_Restore
 mov AtiE4Result[76],eax

AtiE4_Restore_Texture:
 mov edi,AtiE9TextureLinear
 test edi,edi
 jz short AtiE4_Restore_All_Done
 mov esi,OFFSET32 AtiE9TextureBackup
 mov ecx,1024
 cld
 rep movsd
 mov esi,OFFSET32 AtiE9TextureBackup
 mov edi,AtiE9TextureLinear
 mov ecx,1024
 mov eax,AtiE4Result[76]
AtiE9_Check_Restore:
 mov edx,[esi]
 cmp edx,[edi]
 je short AtiE9_Check_Restore_Next
 inc eax
AtiE9_Check_Restore_Next:
 add esi,4
 add edi,4
 dec ecx
 jnz short AtiE9_Check_Restore
 mov AtiE4Result[76],eax

AtiE4_Restore_All_Done:
 cmp dword ptr AtiE4Result[76],0
 jne short AtiE4_Done
 or AtiE4Result[4],10000h
AtiE4_Done:
 popad
 ret
EndProc AtiE4_Run

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
 cmp ecx,ATIE4_DIOC_TRIANGLE
 je AtiE1_Dioc_Run4
 cmp ecx,ATIE5_DIOC_GOURAUD
 je AtiE1_Dioc_Run5
 cmp ecx,ATIE6_DIOC_ZTEST
 je AtiE1_Dioc_Run6
 cmp ecx,ATIE7_DIOC_ZWRITE
 je AtiE1_Dioc_Run7
 cmp ecx,ATIE8_DIOC_ZCLEAR
 je AtiE1_Dioc_Run8
 cmp ecx,ATIE9_DIOC_TEXTURE
 je AtiE1_Dioc_Run9
 cmp ecx,ATIE10_DIOC_TEXTURE_STATE
 je AtiE1_Dioc_Run10
 cmp ecx,ATIE11_DIOC_PERSPECTIVE
 je AtiE1_Dioc_Run11
 cmp ecx,ATIE12_DIOC_WRAP
 je AtiE1_Dioc_Run12
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
 jmp AtiE1_Dioc_Ok
AtiE1_Dioc_Run2:
 pushad
 mov ebp,esi
 call AtiE2_Run
 mov edi,[ebp.lpvOutBuffer]
 test edi,edi
 jz AtiE1_Dioc_Copy_Fail
 cmp [ebp.cbOutBuffer],ATIE2_RESULT_DWORDS*4
 jb AtiE1_Dioc_Copy_Fail
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
 jmp AtiE1_Dioc_Ok
AtiE1_Dioc_Run3:
 pushad
 mov ebp,esi
 call AtiE3_Run
 mov edi,[ebp.lpvOutBuffer]
 test edi,edi
 jz AtiE1_Dioc_Copy_Fail
 cmp [ebp.cbOutBuffer],ATIE3_RESULT_DWORDS*4
 jb AtiE1_Dioc_Copy_Fail
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
 jmp AtiE1_Dioc_Ok
AtiE1_Dioc_Run4:
 pushad
 mov ebp,esi
 mov AtiE4SceneMode,0
 call AtiE4_Run
 mov edi,[ebp.lpvOutBuffer]
 test edi,edi
 jz AtiE1_Dioc_Copy_Fail
 cmp [ebp.cbOutBuffer],ATIE4_RESULT_DWORDS*4
 jb AtiE1_Dioc_Copy_Fail
 mov esi,OFFSET32 AtiE4Result
 mov ecx,ATIE4_RESULT_DWORDS
 cld
 rep movsd
 mov eax,[ebp.lpcbBytesReturned]
 test eax,eax
 jz short AtiE4_Dioc_Copy_Done
 mov dword ptr [eax],ATIE4_RESULT_DWORDS*4
AtiE4_Dioc_Copy_Done:
 popad
 jmp AtiE1_Dioc_Ok
AtiE1_Dioc_Run5:
 pushad
 mov ebp,esi
 mov AtiE4SceneMode,1
 call AtiE4_Run
 mov edi,[ebp.lpvOutBuffer]
 test edi,edi
 jz AtiE1_Dioc_Copy_Fail
 cmp [ebp.cbOutBuffer],ATIE4_RESULT_DWORDS*4
 jb AtiE1_Dioc_Copy_Fail
 mov esi,OFFSET32 AtiE4Result
 mov ecx,ATIE4_RESULT_DWORDS
 cld
 rep movsd
 mov eax,[ebp.lpcbBytesReturned]
 test eax,eax
 jz short AtiE5_Dioc_Copy_Done
 mov dword ptr [eax],ATIE4_RESULT_DWORDS*4
AtiE5_Dioc_Copy_Done:
 popad
 jmp AtiE1_Dioc_Ok
AtiE1_Dioc_Run6:
 pushad
 mov ebp,esi
 mov AtiE4SceneMode,2
 call AtiE4_Run
 mov edi,[ebp.lpvOutBuffer]
 test edi,edi
 jz AtiE1_Dioc_Copy_Fail
 cmp [ebp.cbOutBuffer],ATIE4_RESULT_DWORDS*4
 jb AtiE1_Dioc_Copy_Fail
 mov esi,OFFSET32 AtiE4Result
 mov ecx,ATIE4_RESULT_DWORDS
 cld
 rep movsd
 mov eax,[ebp.lpcbBytesReturned]
 test eax,eax
 jz short AtiE6_Dioc_Copy_Done
 mov dword ptr [eax],ATIE4_RESULT_DWORDS*4
AtiE6_Dioc_Copy_Done:
 popad
 jmp AtiE1_Dioc_Ok
AtiE1_Dioc_Run7:
 pushad
 mov ebp,esi
 mov AtiE4SceneMode,3
 call AtiE4_Run
 mov edi,[ebp.lpvOutBuffer]
 test edi,edi
 jz AtiE1_Dioc_Copy_Fail
 cmp [ebp.cbOutBuffer],ATIE4_RESULT_DWORDS*4
 jb AtiE1_Dioc_Copy_Fail
 mov esi,OFFSET32 AtiE4Result
 mov ecx,ATIE4_RESULT_DWORDS
 cld
 rep movsd
 mov eax,[ebp.lpcbBytesReturned]
 test eax,eax
 jz short AtiE7_Dioc_Copy_Done
 mov dword ptr [eax],ATIE4_RESULT_DWORDS*4
AtiE7_Dioc_Copy_Done:
 popad
 jmp AtiE1_Dioc_Ok
AtiE1_Dioc_Run8:
 pushad
 mov ebp,esi
 mov AtiE4SceneMode,4
 call AtiE4_Run
 mov edi,[ebp.lpvOutBuffer]
 test edi,edi
 jz AtiE1_Dioc_Copy_Fail
 cmp [ebp.cbOutBuffer],ATIE4_RESULT_DWORDS*4
 jb AtiE1_Dioc_Copy_Fail
 mov esi,OFFSET32 AtiE4Result
 mov ecx,ATIE4_RESULT_DWORDS
 cld
 rep movsd
 mov eax,[ebp.lpcbBytesReturned]
 test eax,eax
 jz short AtiE8_Dioc_Copy_Done
 mov dword ptr [eax],ATIE4_RESULT_DWORDS*4
AtiE8_Dioc_Copy_Done:
 popad
 jmp AtiE1_Dioc_Ok
AtiE1_Dioc_Run9:
 pushad
 mov ebp,esi
 mov AtiE4SceneMode,5
 call AtiE4_Run
 mov edi,[ebp.lpvOutBuffer]
 test edi,edi
 jz AtiE1_Dioc_Copy_Fail
 cmp [ebp.cbOutBuffer],ATIE4_RESULT_DWORDS*4
 jb AtiE1_Dioc_Copy_Fail
 mov esi,OFFSET32 AtiE4Result
 mov ecx,ATIE4_RESULT_DWORDS
 cld
 rep movsd
 mov eax,[ebp.lpcbBytesReturned]
 test eax,eax
 jz short AtiE9_Dioc_Copy_Done
 mov dword ptr [eax],ATIE4_RESULT_DWORDS*4
AtiE9_Dioc_Copy_Done:
 popad
 jmp AtiE1_Dioc_Ok
AtiE1_Dioc_Run10:
 pushad
 mov ebp,esi
 mov AtiE4SceneMode,6
 call AtiE4_Run
 mov edi,[ebp.lpvOutBuffer]
 test edi,edi
 jz AtiE1_Dioc_Copy_Fail
 cmp [ebp.cbOutBuffer],ATIE4_RESULT_DWORDS*4
 jb AtiE1_Dioc_Copy_Fail
 mov esi,OFFSET32 AtiE4Result
 mov ecx,ATIE4_RESULT_DWORDS
 cld
 rep movsd
 mov eax,[ebp.lpcbBytesReturned]
 test eax,eax
 jz short AtiE10_Dioc_Copy_Done
 mov dword ptr [eax],ATIE4_RESULT_DWORDS*4
AtiE10_Dioc_Copy_Done:
 popad
 jmp AtiE1_Dioc_Ok
AtiE1_Dioc_Run11:
 pushad
 mov ebp,esi
 mov AtiE4SceneMode,7
 call AtiE4_Run
 mov edi,[ebp.lpvOutBuffer]
 test edi,edi
 jz AtiE1_Dioc_Copy_Fail
 cmp [ebp.cbOutBuffer],ATIE4_RESULT_DWORDS*4
 jb AtiE1_Dioc_Copy_Fail
 mov esi,OFFSET32 AtiE4Result
 mov ecx,ATIE4_RESULT_DWORDS
 cld
 rep movsd
 mov eax,[ebp.lpcbBytesReturned]
 test eax,eax
 jz short AtiE11_Dioc_Copy_Done
 mov dword ptr [eax],ATIE4_RESULT_DWORDS*4
AtiE11_Dioc_Copy_Done:
 popad
 jmp AtiE1_Dioc_Ok
AtiE1_Dioc_Run12:
 pushad
 mov ebp,esi
 mov AtiE4SceneMode,8
 call AtiE4_Run
 mov edi,[ebp.lpvOutBuffer]
 test edi,edi
 jz AtiE1_Dioc_Copy_Fail
 cmp [ebp.cbOutBuffer],ATIE4_RESULT_DWORDS*4
 jb AtiE1_Dioc_Copy_Fail
 mov esi,OFFSET32 AtiE4Result
 mov ecx,ATIE4_RESULT_DWORDS
 cld
 rep movsd
 mov eax,[ebp.lpcbBytesReturned]
 test eax,eax
 jz short AtiE12_Dioc_Copy_Done
 mov dword ptr [eax],ATIE4_RESULT_DWORDS*4
AtiE12_Dioc_Copy_Done:
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
