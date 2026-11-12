; ATI Radeon HD 2600 XT (RV630, 1002:9588) read-only register probe VxD.
;
; Phase 2 of docs\plans\ati-rv630-hd2600xt.md. Maps the 64 KiB register
; BAR (BAR2: Linux radeon_device.c maps BAR2 for every chip before
; Bonaire) and reads a caller's list of offsets, each twice. The RS690
; probe (rs690_mmio_probe.asm) is the model; the display block is the same
; AVIVO one, the rest of the register file is R600's.
;
; Writes: none, to the device or to memory. 0CF8h is saved and restored
; around each configuration read. Mapping a physical range with
; _MapPhysToLinear changes no device state.
;
; The caller names the device by its configuration address; the VxD reads
; the id and BAR2 itself and refuses anything that is not 1002:9588 with a
; 64 KiB-aligned memory BAR2 below 4 GiB. A 64-bit BAR (type bits 2:1 =
; 10b) must read 0 in its high half at 1Ch, or nothing is mapped.
;
; Identity check after mapping: CONFIG_MEMSIZE (5428h on R600, in bytes;
; Linux r600d.h, r600.c r600_mc_init) must be non-zero and at most 1 GiB.
; A window that decodes nothing reads FFFFFFFFh and fails it.
;
; Offsets are accepted only inside RVWindows below. Left out on purpose,
; because a read has a side effect or reads through an index:
;   0000h-0007h  MM_INDEX / MM_DATA;
;   0038h-003Fh  PCIE_PORT_INDEX / PCIE_PORT_DATA (r600d.h);
;   C150h-C163h  CP_PFP_UCODE_ADDR/DATA, CP_ME_RAM_RADDR/WADDR/DATA: the
;                microcode ports, whose access moves the address;
;   648Ch-6497h  DC_LUT_SEQ_COLOR, DC_LUT_PWL_DATA, DC_LUT_30_COLOR: the
;                palette data ports, whose access advances the LUT index.
; Offsets are Linux radeon's r600d.h and r500_reg.h
; (build\external\evergreen\linux-radeon\, build\external\rs690\
; linux-radeon\), not checked against an AMD register guide.

.386p

.xlist
include VMM.INC
include VWIN32.INC
.list

Declare_Virtual_Device RVMM, 1, 0, RV_Control, \
                       Undefined_Device_ID, Undefined_Init_Order, , ,

RV_MAGIC                equ 4d4d5652h ; "RVMM"
RV_DIOC_CAPTURE         equ 1
RV_VENDOR_ATI           equ 1002h
RV_DEVICE_RV630         equ 9588h
RV_CFG_BAR2             equ 18h
RV_CFG_BAR2_HIGH        equ 1ch
RV_MMIO_BYTES           equ 00010000h
RV_BASE_ALIGN           equ 0000ffffh
RV_BAR_IO               equ 00000001h
RV_BAR_TYPE_MASK        equ 00000006h
RV_BAR_TYPE_64          equ 00000004h
RV_CONFIG_MEMSIZE       equ 5428h
RV_MEMSIZE_MAX          equ 40000000h
RV_OFFSET_MAX           equ 256

; Status bits returned to the publisher.
RV_ST_ATI               equ 00000001h
RV_ST_BASE_INVALID      equ 00000002h
RV_ST_MMIO_MAPPED       equ 00000004h
RV_ST_IDENTITY_MATCH    equ 00000008h
RV_ST_OFFSETS_REFUSED   equ 00000010h
RV_ST_SNAPSHOT_TAKEN    equ 00000020h
RV_ST_BAR_64            equ 00000040h
RV_ST_NOT_ACCEPTED      equ 00000080h
RV_ST_ADDRESS_INVALID   equ 00000100h
RV_ST_BAR_ABOVE_4G      equ 00000200h

; Input from V9XRVMM.EXE.
RV_IN_CONFIG            equ 0
RV_IN_COUNT             equ 4
RV_IN_OFFSETS           equ 8
RV_IN_MIN_BYTES         equ 8

; Output layout, in dwords. Must match struct v9x_rvmm_result in
; rv630_mmio_probe_win32.c.
RV_HEADER_DWORDS        equ 16
RV_RESULT_DWORDS        equ (RV_HEADER_DWORDS + RV_OFFSET_MAX * 3)

VxD_LOCKED_DATA_SEG

; Accepted windows: start, limit (exclusive). The second display
; controller's blocks sit 0800h above the first's (r500_reg.h).
RVWindows label dword
    dd 0300h, 0340h     ; VGA_RENDER_CONTROL, VGA memory base, D1/D2 VGA control
    dd 0e50h, 0e54h     ; SRBM_STATUS
    dd 0e60h, 0e64h     ; SRBM_SOFT_RESET
    dd 1400h, 1414h     ; VM_L2_CNTL .. VM_CONTEXT0_CNTL
    dd 2004h, 2008h     ; CHMAP
    dd 2180h, 21a0h     ; MC_VM_FB_LOCATION, MC_VM_AGP_*, system aperture, L1 TLB
    dd 2408h, 240ch     ; RAMCFG
    dd 2c00h, 2c10h     ; HDP_HOST_PATH_CNTL, HDP_NONSURFACE_*
    dd 3f00h, 3f04h     ; RLC_CNTL
    dd 5420h, 542ch     ; BUS_CNTL, CONFIG_CNTL, CONFIG_MEMSIZE
    dd 8010h, 8024h     ; GRBM_STATUS, GRBM_STATUS2, GRBM_SOFT_RESET
    dd 8500h, 8520h     ; SCRATCH_REG0-7
    dd 8540h, 8544h     ; SCRATCH_UMSK
    dd 8680h, 8684h     ; CP_STAT
    dd 86d8h, 86dch     ; CP_ME_CNTL
    dd 8700h, 8708h     ; CP_RB_RPTR, CP_RB_WPTR_DELAY
    dd 8760h, 8764h     ; CP_QUEUE_THRESHOLDS
    dd 8950h, 8958h     ; CC_GC_SHADER_PIPE_CONFIG, GC_USER_SHADER_PIPE_CONFIG
    dd 98f0h, 98f8h     ; GB_TILING_CONFIG, CC_RB_BACKEND_DISABLE
    dd 0c100h, 0c120h   ; CP_RB_BASE .. CP_RB_WPTR_ADDR_HI
    dd 0d000h, 0d038h   ; DMA_RB_CNTL .. DMA_STATUS_REG
    ; D1
    dd 6000h, 60f0h     ; D1CRTC_*
    dd 60f8h, 6100h     ; DC_CRTC_MASTER_EN, DC_CRTC_TV_CONTROL
    dd 6100h, 614ch     ; D1GRPH_ENABLE .. D1GRPH_FLIP_CONTROL
    dd 6400h, 6428h     ; D1CUR_*
    dd 6480h, 648ch     ; DC_LUT_RW_SELECT, DC_LUT_RW_MODE, DC_LUT_RW_INDEX
    dd 6498h, 64a4h     ; DC_LUT_READ_PIPE_SELECT .. DC_LUT_AUTOFILL
    dd 64c0h, 64dch     ; DC_LUTA_*
    dd 6520h, 6590h     ; DC_LB_MEMORY_SPLIT, D1MODE_*
    ; D2 (+0800h)
    dd 6800h, 68f0h
    dd 6900h, 694ch
    dd 6c00h, 6c28h
    dd 6cc0h, 6cdch     ; DC_LUTB_*
    dd 6d20h, 6d90h
RV_WINDOW_COUNT         equ 34

RVResult label dword
    dd RV_MAGIC                 ; 00 magic
    dd 0                        ; 01 status
    dd 0                        ; 02 id dword at configuration 00h
    dd 0                        ; 03 BAR2 as read
    dd 0                        ; 04 offsets accepted
    dd 0                        ; 05 CONFIG_MEMSIZE through the BAR
    dd 0                        ; 06 0CF8h found and restored
    dd 0                        ; 07 first offset refused, if any
    dd 0                        ; 08 configuration address used
    dd 0                        ; 09 BAR2 high dword (1Ch), 64-bit BARs only
    dd 0                        ; 10 reserved
    dd 0                        ; 11 reserved
    dd 0                        ; 12 reserved
    dd 0                        ; 13 reserved
    dd 0                        ; 14 reserved
    dd 0                        ; 15 reserved
RVOffsets    dd RV_OFFSET_MAX dup (0)
RVFirst      dd RV_OFFSET_MAX dup (0)
RVSecond     dd RV_OFFSET_MAX dup (0)
RVConfig     dd 0
RVMmio       dd 0
VxD_LOCKED_DATA_ENDS

VxD_LOCKED_CODE_SEG

; EAX = PCI configuration address. Returns EAX = configuration dword.
; 0CF8h saved and restored with interrupts off.
BeginProc RV_Pci_Read
    push    ebx
    push    edx
    pushfd
    cli
    mov     ebx, eax
    mov     dx, 0cf8h
    in      eax, dx
    mov     RVResult[24], eax
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
EndProc RV_Pci_Read

; EDX = offset. Returns CF clear if it lies in an accepted window.
BeginProc RV_Offset_Allowed
    push    ecx
    push    esi
    test    edx, 3
    jnz     short RV_Offset_Refused
    mov     esi, OFFSET32 RVWindows
    mov     ecx, RV_WINDOW_COUNT
RV_Offset_Next:
    cmp     edx, [esi]
    jb      short RV_Offset_Skip
    cmp     edx, [esi+4]
    jb      short RV_Offset_Accepted
RV_Offset_Skip:
    add     esi, 8
    dec     ecx
    jnz     short RV_Offset_Next
RV_Offset_Refused:
    pop     esi
    pop     ecx
    stc
    ret
RV_Offset_Accepted:
    pop     esi
    pop     ecx
    clc
    ret
EndProc RV_Offset_Allowed

; EBP = DIOC input buffer, ECX = its byte count. Copies the offset list,
; refusing the whole list if any entry is unaligned or outside the windows.
BeginProc RV_Accept_Offsets
    pushad
    mov     RVResult[16], 0
    sub     ecx, RV_IN_OFFSETS
    shr     ecx, 2
    mov     eax, [ebp+RV_IN_COUNT]
    cmp     eax, RV_OFFSET_MAX
    ja      short RV_Accept_Refused
    cmp     eax, ecx
    ja      short RV_Accept_Refused
    mov     ecx, eax
    lea     esi, [ebp+RV_IN_OFFSETS]
    mov     edi, OFFSET32 RVOffsets
    jecxz   short RV_Accept_Done
RV_Accept_Next:
    mov     edx, [esi]
    call    RV_Offset_Allowed
    jc      short RV_Accept_Refused_At
    mov     [edi], edx
    add     esi, 4
    add     edi, 4
    dec     ecx
    jnz     short RV_Accept_Next
RV_Accept_Done:
    mov     eax, [ebp+RV_IN_COUNT]
    mov     RVResult[16], eax
    popad
    ret
RV_Accept_Refused_At:
    mov     RVResult[28], edx
RV_Accept_Refused:
    or      RVResult[4], RV_ST_OFFSETS_REFUSED
    popad
    ret
EndProc RV_Accept_Offsets

; EDI = destination. Reads every accepted offset through the mapping.
BeginProc RV_Read_Snapshot
    pushad
    mov     ecx, RVResult[16]
    mov     ebx, OFFSET32 RVOffsets
    mov     esi, RVMmio
    jecxz   short RV_Read_Snapshot_Done
RV_Read_Snapshot_Next:
    mov     eax, [ebx]
    mov     eax, [esi+eax]
    mov     [edi], eax
    add     ebx, 4
    add     edi, 4
    dec     ecx
    jnz     short RV_Read_Snapshot_Next
RV_Read_Snapshot_Done:
    popad
    ret
EndProc RV_Read_Snapshot

BeginProc RV_Capture
    pushad

    ; A configuration address: enable bit set, register field zero, and
    ; nothing in the reserved bits 30:24.
    mov     eax, RVConfig
    mov     RVResult[32], eax
    test    eax, 80000000h
    jz      RV_Capture_Bad_Address
    test    eax, 7f0000ffh
    jnz     RV_Capture_Bad_Address

    call    RV_Pci_Read
    mov     RVResult[8], eax
    cmp     ax, RV_VENDOR_ATI
    jne     RV_Capture_Bad_Base
    or      RVResult[4], RV_ST_ATI
    shr     eax, 16
    cmp     ax, RV_DEVICE_RV630
    je      short RV_Capture_Check_Base
    or      RVResult[4], RV_ST_NOT_ACCEPTED
    jmp     RV_Capture_Done

RV_Capture_Check_Base:
    mov     eax, RVConfig
    or      eax, RV_CFG_BAR2
    call    RV_Pci_Read
    mov     RVResult[12], eax
    test    eax, RV_BAR_IO
    jnz     RV_Capture_Bad_Base
    mov     edx, eax
    and     edx, RV_BAR_TYPE_MASK
    cmp     edx, RV_BAR_TYPE_64
    jne     short RV_Capture_Low_Base

    ; A 64-bit BAR: its high half must be zero.
    or      RVResult[4], RV_ST_BAR_64
    push    eax
    mov     eax, RVConfig
    or      eax, RV_CFG_BAR2_HIGH
    call    RV_Pci_Read
    mov     RVResult[36], eax
    test    eax, eax
    pop     eax
    jz      short RV_Capture_Low_Base
    or      RVResult[4], RV_ST_BAR_ABOVE_4G
    jmp     RV_Capture_Bad_Base

RV_Capture_Low_Base:
    and     eax, NOT 0fh
    jz      RV_Capture_Bad_Base
    test    eax, RV_BASE_ALIGN
    jnz     RV_Capture_Bad_Base

    VMMcall _MapPhysToLinear,<eax,RV_MMIO_BYTES,0>
    cmp     eax, 0ffffffffh
    je      RV_Capture_Done
    mov     RVMmio, eax
    or      RVResult[4], RV_ST_MMIO_MAPPED

    mov     edx, [eax+RV_CONFIG_MEMSIZE]
    mov     RVResult[20], edx
    test    edx, edx
    jz      short RV_Capture_Done
    cmp     edx, RV_MEMSIZE_MAX
    ja      short RV_Capture_Done
    or      RVResult[4], RV_ST_IDENTITY_MATCH

    mov     edi, OFFSET32 RVFirst
    call    RV_Read_Snapshot
    mov     edi, OFFSET32 RVSecond
    call    RV_Read_Snapshot
    or      RVResult[4], RV_ST_SNAPSHOT_TAKEN
    jmp     short RV_Capture_Done

RV_Capture_Bad_Address:
    or      RVResult[4], RV_ST_ADDRESS_INVALID
    jmp     short RV_Capture_Done
RV_Capture_Bad_Base:
    or      RVResult[4], RV_ST_BASE_INVALID
RV_Capture_Done:
    popad
    ret
EndProc RV_Capture

BeginProc RV_Reset_Result
    pushad
    mov     edi, OFFSET32 RVResult + 4
    mov     ecx, RV_RESULT_DWORDS - 1
    xor     eax, eax
    cld
    rep     stosd
    mov     RVMmio, eax
    popad
    ret
EndProc RV_Reset_Result

BeginProc RV_W32_DeviceIoControl
    cmp     ecx, DIOC_OPEN
    je      RV_Dioc_Ok
    cmp     ecx, DIOC_CLOSEHANDLE
    je      RV_Dioc_Ok
    cmp     ecx, RV_DIOC_CAPTURE
    jne     RV_Dioc_Fail

    pushad
    mov     ebx, esi
    mov     ebp, [ebx.lpvInBuffer]
    test    ebp, ebp
    jz      short RV_Dioc_Copy_Fail
    mov     ecx, [ebx.cbInBuffer]
    cmp     ecx, RV_IN_MIN_BYTES
    jb      short RV_Dioc_Copy_Fail
    mov     edi, [ebx.lpvOutBuffer]
    test    edi, edi
    jz      short RV_Dioc_Copy_Fail
    cmp     [ebx.cbOutBuffer], RV_RESULT_DWORDS * 4
    jb      short RV_Dioc_Copy_Fail

    call    RV_Reset_Result
    mov     eax, [ebp+RV_IN_CONFIG]
    mov     RVConfig, eax
    call    RV_Accept_Offsets
    test    RVResult[4], RV_ST_OFFSETS_REFUSED
    jnz     short RV_Dioc_Copy_Out
    call    RV_Capture

RV_Dioc_Copy_Out:
    mov     edi, [ebx.lpvOutBuffer]
    mov     esi, OFFSET32 RVResult
    mov     ecx, RV_RESULT_DWORDS
    cld
    rep     movsd
    mov     eax, [ebx.lpcbBytesReturned]
    test    eax, eax
    jz      short RV_Dioc_Copy_Done
    mov     dword ptr [eax], RV_RESULT_DWORDS * 4
RV_Dioc_Copy_Done:
    popad
RV_Dioc_Ok:
    xor     eax, eax
    ret

RV_Dioc_Copy_Fail:
    popad
RV_Dioc_Fail:
    mov     eax, 1
    ret
EndProc RV_W32_DeviceIoControl

BeginProc RV_Dynamic_Init
    clc
    ret
EndProc RV_Dynamic_Init

BeginProc RV_Dynamic_Exit
    clc
    ret
EndProc RV_Dynamic_Exit

Begin_Control_Dispatch RV
    Control_Dispatch Sys_Dynamic_Device_Init, RV_Dynamic_Init
    Control_Dispatch Sys_Dynamic_Device_Exit, RV_Dynamic_Exit
    Control_Dispatch W32_DeviceIoControl, RV_W32_DeviceIoControl
End_Control_Dispatch RV

VxD_LOCKED_CODE_ENDS

end
