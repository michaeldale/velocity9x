; ATI Radeon X1200 IGP (RS690, 1002:791F) read-only register probe VxD.
;
; Phase 2 of docs\plans\ati-rs690-x1200.md. Maps the 64 KiB register BAR
; (BAR2: Linux radeon_device.c maps BAR2 for every chip before Bonaire)
; and reads a caller's list of offsets, each twice. The Evergreen probe
; (evergreen_mmio_probe.asm) is the model.
;
; Writes: none, to the device or to memory. 0CF8h is saved and restored
; around each configuration read. Mapping a physical range with
; _MapPhysToLinear changes no device state.
;
; The caller names the device by its configuration address; the VxD reads
; the id and BAR2 itself and refuses anything that is not 1002:791F with a
; 64 KiB-aligned memory BAR2 below 4 GiB. A 64-bit BAR (type bits 2:1 =
; 10b) must read 0 in its high half at 1Ch, or nothing is mapped.
;
; Identity check after mapping: CONFIG_MEMSIZE (00F8h, in bytes on the
; Radeon line; Linux rs690.c rs690_mc_init) must be non-zero and at most
; 512 MiB. A window that decodes nothing reads FFFFFFFFh and fails it.
; The dword at 0F00h, where the Radeon register file names a copy of
; configuration space (radeon_reg.h RADEON_VENDOR_ID), is read into the
; header too, as evidence, not as a check: no source says the RS690
; keeps that copy.
;
; Offsets are accepted only inside RSWindows below. Left out on purpose,
; because a read has a side effect or reads through an index:
;   0000h-0007h  MM_INDEX / MM_DATA;
;   0078h-007Fh  MC_INDEX / MC_DATA, the memory controller's index pair
;                (rs690d.h); reading the MC needs an index write, which
;                this probe does not make;
;   1720h        WAIT_UNTIL, a command register;
;   17C0h-17E3h  HOST_DATA0-7 and HOST_DATA_LAST, the 2D engine's host
;                data ports;
;   2000h        SE_PORT_DATA0, the 3D vertex port;
;   648Ch-6497h  DC_LUT_SEQ_COLOR, DC_LUT_PWL_DATA, DC_LUT_30_COLOR: the
;                palette data ports, whose access advances the LUT index.
; Offsets are Linux radeon's radeon_reg.h, r500_reg.h, r300_reg.h and
; rs690d.h (build\external\rs690\linux-radeon\), not checked against an
; AMD register guide.

.386p

.xlist
include VMM.INC
include VWIN32.INC
.list

Declare_Virtual_Device RSMM, 1, 0, RS_Control, \
                       Undefined_Device_ID, Undefined_Init_Order, , ,

RS_MAGIC                equ 4d4d5352h ; "RSMM"
RS_DIOC_CAPTURE         equ 1
RS_VENDOR_ATI           equ 1002h
RS_DEVICE_RS690         equ 791fh
RS_CFG_BAR2             equ 18h
RS_CFG_BAR2_HIGH        equ 1ch
RS_MMIO_BYTES           equ 00010000h
RS_BASE_ALIGN           equ 0000ffffh
RS_BAR_IO               equ 00000001h
RS_BAR_TYPE_MASK        equ 00000006h
RS_BAR_TYPE_64          equ 00000004h
RS_CONFIG_MEMSIZE       equ 00f8h
RS_PCI_COPY             equ 0f00h
RS_MEMSIZE_MAX          equ 20000000h
RS_OFFSET_MAX           equ 256

; Status bits returned to the publisher.
RS_ST_ATI               equ 00000001h
RS_ST_BASE_INVALID      equ 00000002h
RS_ST_MMIO_MAPPED       equ 00000004h
RS_ST_IDENTITY_MATCH    equ 00000008h
RS_ST_OFFSETS_REFUSED   equ 00000010h
RS_ST_SNAPSHOT_TAKEN    equ 00000020h
RS_ST_BAR_64            equ 00000040h
RS_ST_NOT_ACCEPTED      equ 00000080h
RS_ST_ADDRESS_INVALID   equ 00000100h
RS_ST_BAR_ABOVE_4G      equ 00000200h

; Input from V9XRSMM.EXE.
RS_IN_CONFIG            equ 0
RS_IN_COUNT             equ 4
RS_IN_OFFSETS           equ 8
RS_IN_MIN_BYTES         equ 8

; Output layout, in dwords. Must match struct v9x_rsmm_result in
; rs690_mmio_probe_win32.c.
RS_HEADER_DWORDS        equ 16
RS_RESULT_DWORDS        equ (RS_HEADER_DWORDS + RS_OFFSET_MAX * 3)

VxD_LOCKED_DATA_SEG

; Accepted windows: start, limit (exclusive). The second display
; controller's blocks sit 0800h above the first's (r500_reg.h).
RSWindows label dword
    dd 0030h, 0034h     ; BUS_CNTL
    dd 0040h, 0048h     ; GEN_INT_CNTL, GEN_INT_STATUS
    dd 00e0h, 0100h     ; CONFIG_CNTL, CONFIG_XSTRAP, RBBM_SOFT_RESET, CONFIG_MEMSIZE
    dd 0100h, 010ch     ; CONFIG_APER_0_BASE, CONFIG_APER_1_BASE, CONFIG_APER_SIZE
    dd 0130h, 0138h     ; HOST_PATH_CNTL, HDP_FB_LOCATION
    dd 0148h, 0150h     ; MC_FB_LOCATION, MC_AGP_LOCATION (the Radeon names)
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
RS_WINDOW_COUNT         equ 32

RSResult label dword
    dd RS_MAGIC                 ; 00 magic
    dd 0                        ; 01 status
    dd 0                        ; 02 id dword at configuration 00h
    dd 0                        ; 03 BAR2 as read
    dd 0                        ; 04 offsets accepted
    dd 0                        ; 05 CONFIG_MEMSIZE through the BAR
    dd 0                        ; 06 0CF8h found and restored
    dd 0                        ; 07 first offset refused, if any
    dd 0                        ; 08 configuration address used
    dd 0                        ; 09 BAR2 high dword (1Ch), 64-bit BARs only
    dd 0                        ; 10 dword at 0F00h through the BAR
    dd 0                        ; 11 reserved
    dd 0                        ; 12 reserved
    dd 0                        ; 13 reserved
    dd 0                        ; 14 reserved
    dd 0                        ; 15 reserved
RSOffsets    dd RS_OFFSET_MAX dup (0)
RSFirst      dd RS_OFFSET_MAX dup (0)
RSSecond     dd RS_OFFSET_MAX dup (0)
RSConfig     dd 0
RSMmio       dd 0
VxD_LOCKED_DATA_ENDS

VxD_LOCKED_CODE_SEG

; EAX = PCI configuration address. Returns EAX = configuration dword.
; 0CF8h saved and restored with interrupts off.
BeginProc RS_Pci_Read
    push    ebx
    push    edx
    pushfd
    cli
    mov     ebx, eax
    mov     dx, 0cf8h
    in      eax, dx
    mov     RSResult[24], eax
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
EndProc RS_Pci_Read

; EDX = offset. Returns CF clear if it lies in an accepted window.
BeginProc RS_Offset_Allowed
    push    ecx
    push    esi
    test    edx, 3
    jnz     short RS_Offset_Refused
    mov     esi, OFFSET32 RSWindows
    mov     ecx, RS_WINDOW_COUNT
RS_Offset_Next:
    cmp     edx, [esi]
    jb      short RS_Offset_Skip
    cmp     edx, [esi+4]
    jb      short RS_Offset_Accepted
RS_Offset_Skip:
    add     esi, 8
    dec     ecx
    jnz     short RS_Offset_Next
RS_Offset_Refused:
    pop     esi
    pop     ecx
    stc
    ret
RS_Offset_Accepted:
    pop     esi
    pop     ecx
    clc
    ret
EndProc RS_Offset_Allowed

; EBP = DIOC input buffer, ECX = its byte count. Copies the offset list,
; refusing the whole list if any entry is unaligned or outside the windows.
BeginProc RS_Accept_Offsets
    pushad
    mov     RSResult[16], 0
    sub     ecx, RS_IN_OFFSETS
    shr     ecx, 2
    mov     eax, [ebp+RS_IN_COUNT]
    cmp     eax, RS_OFFSET_MAX
    ja      short RS_Accept_Refused
    cmp     eax, ecx
    ja      short RS_Accept_Refused
    mov     ecx, eax
    lea     esi, [ebp+RS_IN_OFFSETS]
    mov     edi, OFFSET32 RSOffsets
    jecxz   short RS_Accept_Done
RS_Accept_Next:
    mov     edx, [esi]
    call    RS_Offset_Allowed
    jc      short RS_Accept_Refused_At
    mov     [edi], edx
    add     esi, 4
    add     edi, 4
    dec     ecx
    jnz     short RS_Accept_Next
RS_Accept_Done:
    mov     eax, [ebp+RS_IN_COUNT]
    mov     RSResult[16], eax
    popad
    ret
RS_Accept_Refused_At:
    mov     RSResult[28], edx
RS_Accept_Refused:
    or      RSResult[4], RS_ST_OFFSETS_REFUSED
    popad
    ret
EndProc RS_Accept_Offsets

; EDI = destination. Reads every accepted offset through the mapping.
BeginProc RS_Read_Snapshot
    pushad
    mov     ecx, RSResult[16]
    mov     ebx, OFFSET32 RSOffsets
    mov     esi, RSMmio
    jecxz   short RS_Read_Snapshot_Done
RS_Read_Snapshot_Next:
    mov     eax, [ebx]
    mov     eax, [esi+eax]
    mov     [edi], eax
    add     ebx, 4
    add     edi, 4
    dec     ecx
    jnz     short RS_Read_Snapshot_Next
RS_Read_Snapshot_Done:
    popad
    ret
EndProc RS_Read_Snapshot

BeginProc RS_Capture
    pushad

    ; A configuration address: enable bit set, register field zero, and
    ; nothing in the reserved bits 30:24.
    mov     eax, RSConfig
    mov     RSResult[32], eax
    test    eax, 80000000h
    jz      RS_Capture_Bad_Address
    test    eax, 7f0000ffh
    jnz     RS_Capture_Bad_Address

    call    RS_Pci_Read
    mov     RSResult[8], eax
    cmp     ax, RS_VENDOR_ATI
    jne     RS_Capture_Bad_Base
    or      RSResult[4], RS_ST_ATI
    shr     eax, 16
    cmp     ax, RS_DEVICE_RS690
    je      short RS_Capture_Check_Base
    or      RSResult[4], RS_ST_NOT_ACCEPTED
    jmp     RS_Capture_Done

RS_Capture_Check_Base:
    mov     eax, RSConfig
    or      eax, RS_CFG_BAR2
    call    RS_Pci_Read
    mov     RSResult[12], eax
    test    eax, RS_BAR_IO
    jnz     RS_Capture_Bad_Base
    mov     edx, eax
    and     edx, RS_BAR_TYPE_MASK
    cmp     edx, RS_BAR_TYPE_64
    jne     short RS_Capture_Low_Base

    ; A 64-bit BAR: its high half must be zero.
    or      RSResult[4], RS_ST_BAR_64
    push    eax
    mov     eax, RSConfig
    or      eax, RS_CFG_BAR2_HIGH
    call    RS_Pci_Read
    mov     RSResult[36], eax
    test    eax, eax
    pop     eax
    jz      short RS_Capture_Low_Base
    or      RSResult[4], RS_ST_BAR_ABOVE_4G
    jmp     RS_Capture_Bad_Base

RS_Capture_Low_Base:
    and     eax, NOT 0fh
    jz      RS_Capture_Bad_Base
    test    eax, RS_BASE_ALIGN
    jnz     RS_Capture_Bad_Base

    VMMcall _MapPhysToLinear,<eax,RS_MMIO_BYTES,0>
    cmp     eax, 0ffffffffh
    je      RS_Capture_Done
    mov     RSMmio, eax
    or      RSResult[4], RS_ST_MMIO_MAPPED

    mov     edx, [eax+RS_PCI_COPY]
    mov     RSResult[40], edx
    mov     edx, [eax+RS_CONFIG_MEMSIZE]
    mov     RSResult[20], edx
    test    edx, edx
    jz      short RS_Capture_Done
    cmp     edx, RS_MEMSIZE_MAX
    ja      short RS_Capture_Done
    or      RSResult[4], RS_ST_IDENTITY_MATCH

    mov     edi, OFFSET32 RSFirst
    call    RS_Read_Snapshot
    mov     edi, OFFSET32 RSSecond
    call    RS_Read_Snapshot
    or      RSResult[4], RS_ST_SNAPSHOT_TAKEN
    jmp     short RS_Capture_Done

RS_Capture_Bad_Address:
    or      RSResult[4], RS_ST_ADDRESS_INVALID
    jmp     short RS_Capture_Done
RS_Capture_Bad_Base:
    or      RSResult[4], RS_ST_BASE_INVALID
RS_Capture_Done:
    popad
    ret
EndProc RS_Capture

BeginProc RS_Reset_Result
    pushad
    mov     edi, OFFSET32 RSResult + 4
    mov     ecx, RS_RESULT_DWORDS - 1
    xor     eax, eax
    cld
    rep     stosd
    mov     RSMmio, eax
    popad
    ret
EndProc RS_Reset_Result

BeginProc RS_W32_DeviceIoControl
    cmp     ecx, DIOC_OPEN
    je      RS_Dioc_Ok
    cmp     ecx, DIOC_CLOSEHANDLE
    je      RS_Dioc_Ok
    cmp     ecx, RS_DIOC_CAPTURE
    jne     RS_Dioc_Fail

    pushad
    mov     ebx, esi
    mov     ebp, [ebx.lpvInBuffer]
    test    ebp, ebp
    jz      short RS_Dioc_Copy_Fail
    mov     ecx, [ebx.cbInBuffer]
    cmp     ecx, RS_IN_MIN_BYTES
    jb      short RS_Dioc_Copy_Fail
    mov     edi, [ebx.lpvOutBuffer]
    test    edi, edi
    jz      short RS_Dioc_Copy_Fail
    cmp     [ebx.cbOutBuffer], RS_RESULT_DWORDS * 4
    jb      short RS_Dioc_Copy_Fail

    call    RS_Reset_Result
    mov     eax, [ebp+RS_IN_CONFIG]
    mov     RSConfig, eax
    call    RS_Accept_Offsets
    test    RSResult[4], RS_ST_OFFSETS_REFUSED
    jnz     short RS_Dioc_Copy_Out
    call    RS_Capture

RS_Dioc_Copy_Out:
    mov     edi, [ebx.lpvOutBuffer]
    mov     esi, OFFSET32 RSResult
    mov     ecx, RS_RESULT_DWORDS
    cld
    rep     movsd
    mov     eax, [ebx.lpcbBytesReturned]
    test    eax, eax
    jz      short RS_Dioc_Copy_Done
    mov     dword ptr [eax], RS_RESULT_DWORDS * 4
RS_Dioc_Copy_Done:
    popad
RS_Dioc_Ok:
    xor     eax, eax
    ret

RS_Dioc_Copy_Fail:
    popad
RS_Dioc_Fail:
    mov     eax, 1
    ret
EndProc RS_W32_DeviceIoControl

BeginProc RS_Dynamic_Init
    clc
    ret
EndProc RS_Dynamic_Init

BeginProc RS_Dynamic_Exit
    clc
    ret
EndProc RS_Dynamic_Exit

Begin_Control_Dispatch RS
    Control_Dispatch Sys_Dynamic_Device_Init, RS_Dynamic_Init
    Control_Dispatch Sys_Dynamic_Device_Exit, RS_Dynamic_Exit
    Control_Dispatch W32_DeviceIoControl, RS_W32_DeviceIoControl
End_Control_Dispatch RS

VxD_LOCKED_CODE_ENDS

end
