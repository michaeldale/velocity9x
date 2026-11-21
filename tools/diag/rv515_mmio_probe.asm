; ATI Radeon X1300 (RV515, 1002:7146) register probe VxD.
;
; Phase 2 of docs\plans\ati-rv515-x1300.md. Maps the 64 KiB register BAR
; (configuration offset 18h: Linux radeon_device.c maps resource 2 for
; every chip before Bonaire) and reads a caller's list of offsets, each
; twice. The RS690 probe (rs690_mmio_probe.asm) is the model; the window
; list is the same register file, since the RV515 is the RS690's display
; and 2D reference (r500_reg.h).
;
; Writes: none to memory. On the device, only when the caller asks for
; memory-controller registers: MC_IND_INDEX (0070h) is written with
; 7F0000h | index, MC_IND_DATA (0074h) is read, and the index register is
; put back to the value it held before. That is rv515_mc_rreg in Linux
; rv515.c:196-208, which restores 0 instead; this probe restores what it
; found. Bit 24 (MC_IND_WR_EN, rv515d.h:37) is never set, so no MC
; register is written. Each read is done with interrupts off. 0CF8h is
; saved and restored around each configuration read.
;
; The caller names the device by its configuration address; the VxD reads
; the id and BAR2 itself and refuses anything that is not 1002:7146 with a
; 64 KiB-aligned memory BAR2 below 4 GiB. A 64-bit BAR (type bits 2:1 =
; 10b) must read 0 in its high half at 1Ch, or nothing is mapped.
;
; Identity check after mapping: CONFIG_MEMSIZE (00F8h, in bytes;
; rv515d.h:40) must be non-zero and at most 512 MiB, the largest X1300
; board. A window that decodes nothing reads FFFFFFFFh and fails it.
;
; Offsets are accepted only inside RVWindows below. Left out on purpose,
; because a read has a side effect or reads through an index:
;   0000h-0007h  MM_INDEX / MM_DATA;
;   0070h-0077h  MC_IND_INDEX / MC_IND_DATA, reached only through the
;                MC list and its own sequence above;
;   1720h        WAIT_UNTIL, a command register;
;   17C0h-17E3h  HOST_DATA0-7 and HOST_DATA_LAST, the 2D host data ports;
;   2000h        SE_PORT_DATA0, the 3D vertex port;
;   648Ch-6497h  the palette data ports, whose access advances the index.
; Offsets are Linux radeon's radeon_reg.h, r500_reg.h, r300_reg.h and
; rv515d.h (build\external\rs690\linux-radeon\), not checked against an
; AMD register guide.

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
RV_DEVICE_RV515         equ 7146h
RV_CFG_BAR2             equ 18h
RV_CFG_BAR2_HIGH        equ 1ch
RV_MMIO_BYTES           equ 00010000h
RV_BASE_ALIGN           equ 0000ffffh
RV_BAR_IO               equ 00000001h
RV_BAR_TYPE_MASK        equ 00000006h
RV_BAR_TYPE_64          equ 00000004h
RV_CONFIG_MEMSIZE       equ 00f8h
RV_MEMSIZE_MAX          equ 20000000h
RV_OFFSET_MAX           equ 256
RV_MC_IND_INDEX         equ 0070h
RV_MC_IND_DATA          equ 0074h
RV_MC_READ_SELECT       equ 007f0000h
RV_MC_MAX               equ 16
RV_MC_INDEX_LIMIT       equ 0040h

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
RV_ST_MC_REFUSED        equ 00000400h
RV_ST_MC_TAKEN          equ 00000800h

; Input from V9XRVMM.EXE.
RV_IN_CONFIG            equ 0
RV_IN_COUNT             equ 4
RV_IN_MC_COUNT          equ 8
RV_IN_MC                equ 12
RV_IN_OFFSETS           equ (RV_IN_MC + RV_MC_MAX * 4)
RV_IN_MIN_BYTES         equ RV_IN_OFFSETS

; Output layout, in dwords. Must match struct v9x_rvmm_result in
; rv515_mmio_probe_win32.c.
RV_HEADER_DWORDS        equ 16
RV_RESULT_DWORDS        equ (RV_HEADER_DWORDS + RV_OFFSET_MAX * 3 + RV_MC_MAX)

VxD_LOCKED_DATA_SEG

; Accepted windows: start, limit (exclusive). The second display
; controller's blocks sit 0800h above the first's (r500_reg.h).
RVWindows label dword
    dd 0030h, 0034h     ; BUS_CNTL
    dd 0040h, 0048h     ; GEN_INT_CNTL, GEN_INT_STATUS
    dd 00e0h, 0100h     ; CONFIG_CNTL, CONFIG_XSTRAP, RBBM_SOFT_RESET, CONFIG_MEMSIZE
    dd 0100h, 010ch     ; CONFIG_APER_0_BASE, CONFIG_APER_1_BASE, CONFIG_APER_SIZE
    dd 0130h, 0138h     ; HOST_PATH_CNTL, HDP_FB_LOCATION
    dd 0300h, 0340h     ; VGA_RENDER_CONTROL .. D2VGA_CONTROL
    dd 0700h, 0718h     ; CP_RB_BASE, CP_RB_CNTL, CP_RB_RPTR_ADDR, RPTR, WPTR
    dd 0740h, 0744h     ; CP_CSQ_CNTL
    dd 0b00h, 0b04h     ; SURFACE_CNTL
    dd 0e40h, 0e44h     ; RBBM_STATUS
    dd 0f00h, 0f10h     ; the configuration copy, if any
    dd 1400h, 1720h     ; 2D state: DST_*, SRC_*, DP_*, SC_*, DEFAULT_*
    dd 1724h, 17c0h     ; DSTCACHE_*, the rest of the 2D block below HOST_DATA0
    dd 2080h, 2088h     ; VAP_CNTL, VAP_VF_CNTL
    dd 2140h, 2144h     ; VAP_CNTL_STATUS
    dd 342ch, 3430h     ; RB2D_DSTCACHE_CTLSTAT
    dd 4000h, 4030h     ; GB_VAP_RASTER_VTX_FMT .. R400_GB_PIPE_SELECT
    dd 4e4ch, 4e50h     ; RB3D_DSTCACHE_CTLSTAT
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
RV_WINDOW_COUNT         equ 31

RVResult label dword
    dd RV_MAGIC                 ; 00 magic
    dd 0                        ; 01 status
    dd 0                        ; 02 id dword at configuration 00h
    dd 0                        ; 03 BAR2 as read
    dd 0                        ; 04 offsets accepted
    dd 0                        ; 05 CONFIG_MEMSIZE through the BAR
    dd 0                        ; 06 0CF8h found and restored
    dd 0                        ; 07 first offset or MC index refused, if any
    dd 0                        ; 08 configuration address used
    dd 0                        ; 09 BAR2 high dword (1Ch), 64-bit BARs only
    dd 0                        ; 10 MC registers read
    dd 0                        ; 11 MC_IND_INDEX before the MC reads
    dd 0                        ; 12 MC_IND_INDEX after they were restored
    dd 0                        ; 13 reserved
    dd 0                        ; 14 reserved
    dd 0                        ; 15 reserved
RVOffsets    dd RV_OFFSET_MAX dup (0)
RVFirst      dd RV_OFFSET_MAX dup (0)
RVSecond     dd RV_OFFSET_MAX dup (0)
RVMcValues   dd RV_MC_MAX dup (0)
RVMcIndex    dd RV_MC_MAX dup (0)
RVMcCount    dd 0
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

; EBP = DIOC input buffer. Copies the MC index list; every index must be
; below RV_MC_INDEX_LIMIT, the range rv515d.h names (MC_FB_LOCATION 01h
; to MC_MISC_LAT_TIMER 09h, with room for its neighbours).
BeginProc RV_Accept_Mc
    pushad
    mov     RVMcCount, 0
    mov     ecx, [ebp+RV_IN_MC_COUNT]
    cmp     ecx, RV_MC_MAX
    ja      short RV_Accept_Mc_Refused
    lea     esi, [ebp+RV_IN_MC]
    mov     edi, OFFSET32 RVMcIndex
    mov     ebx, ecx
    jecxz   short RV_Accept_Mc_Done
RV_Accept_Mc_Next:
    mov     edx, [esi]
    cmp     edx, RV_MC_INDEX_LIMIT
    jae     short RV_Accept_Mc_Refused_At
    mov     [edi], edx
    add     esi, 4
    add     edi, 4
    dec     ecx
    jnz     short RV_Accept_Mc_Next
RV_Accept_Mc_Done:
    mov     RVMcCount, ebx
    popad
    ret
RV_Accept_Mc_Refused_At:
    mov     RVResult[28], edx
RV_Accept_Mc_Refused:
    or      RVResult[4], RV_ST_MC_REFUSED
    popad
    ret
EndProc RV_Accept_Mc

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

; Reads each accepted MC index through MC_IND_INDEX / MC_IND_DATA, as
; rv515_mc_rreg does, then puts MC_IND_INDEX back to what it held. The
; whole sequence runs with interrupts off so nothing else on this CPU can
; see the index changed.
BeginProc RV_Read_Mc
    pushad
    mov     ecx, RVMcCount
    jecxz   short RV_Read_Mc_Done
    mov     esi, RVMmio
    mov     ebx, OFFSET32 RVMcIndex
    mov     edi, OFFSET32 RVMcValues
    pushfd
    cli
    mov     ebp, [esi+RV_MC_IND_INDEX]
    mov     RVResult[44], ebp
RV_Read_Mc_Next:
    mov     eax, [ebx]
    or      eax, RV_MC_READ_SELECT
    mov     [esi+RV_MC_IND_INDEX], eax
    mov     eax, [esi+RV_MC_IND_DATA]
    mov     [edi], eax
    add     ebx, 4
    add     edi, 4
    dec     ecx
    jnz     short RV_Read_Mc_Next
    mov     [esi+RV_MC_IND_INDEX], ebp
    mov     eax, [esi+RV_MC_IND_INDEX]
    mov     RVResult[48], eax
    popfd
    mov     eax, RVMcCount
    mov     RVResult[40], eax
    or      RVResult[4], RV_ST_MC_TAKEN
RV_Read_Mc_Done:
    popad
    ret
EndProc RV_Read_Mc

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
    cmp     ax, RV_DEVICE_RV515
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
    call    RV_Read_Mc
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
    mov     RVMcCount, eax
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
    call    RV_Accept_Mc
    test    RVResult[4], RV_ST_MC_REFUSED
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
