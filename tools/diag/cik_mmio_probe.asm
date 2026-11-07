; AMD Mullins (Radeon R3E, GCN 2) read-only register probe VxD.
;
; Phase 2 of docs\plans\ati-mullins-gfx7.md. A copy of the Carrizo probe
; (carrizo_mmio_probe.asm) with CIK's windows. Maps the 256 KiB register
; BAR (BAR5, at configuration 24h, on every GCN chip from Bonaire on: Linux
; amdgpu_device.c) and reads a caller's list of offsets, each twice.
;
; Writes nothing on the device or in memory. Carrizo's SMU header read is
; left out: on CIK, 600h is GPIOPAD_SW_INT_STAT, not an SMU index
; (smu_7_0_1_d.h puts SMC_IND_INDEX_0 at 200h), and CIK takes its microcode
; without the SMU (Linux cik_sdma.c, gfx_v7_0.c). 0CF8h is saved and
; restored around each configuration read. Mapping a physical range with
; _MapPhysToLinear changes no device state.
;
; The caller names the device by its configuration address; the VxD reads
; the id and BAR5 itself and refuses anything that is not 1002:9854 with a
; 256 KiB-aligned 32-bit memory BAR5 below 4 GiB. BAR5 cannot be a 64-bit
; BAR (its high half would be 28h, the CardBus CIS pointer), so one that
; claims to be is refused.
;
; The identity check after mapping is Evergreen's: CONFIG_MEMSIZE (5428h,
; MiB, Linux gmc_v7_0.c gmc_v7_0_mc_init) must be non-zero and at most 4096.
; A window that decodes nothing reads FFFFFFFFh and fails it.
;
; Offsets are accepted only inside CKWindows below, and only for the first
; two display controllers (dce_8_0_d.h: the second at +C00h). Left out on
; purpose, because a read has a side effect or reads through an index:
;   0000h-001Bh  MM_INDEX / MM_DATA and MM_INDEX_HI (bif_4_1_d.h);
;   0200h-0207h  SMC_IND_INDEX_0 / _DATA_0;
;   0314h, 031Ch VGA_TEST_DEBUG_INDEX / _DATA;
;   5480h        HDP_MEM_COHERENCY_FLUSH_CNTL, a flush trigger;
;   6E84h-6E8Bh  CRTC_PIXEL_DATA_READBACK0/1;
;   C150h-C17Fh  the CP microcode address/data ports (CIK's place for them);
;   C388h-C38Fh  RLC_GPM_UCODE_ADDR / _DATA;
;   D000h-D007h  SDMA0_UCODE_ADDR / _DATA, and SDMA1's at D800h;
;   D060h-D067h  SDMA_PGFSM_WRITE / _READ.
; Offsets are Linux amdgpu's register headers (dword indices times four;
; build\external\carrizo\linux\), not checked against any AMD register
; guide: none for these blocks was found.

.386p

.xlist
include VMM.INC
include VWIN32.INC
.list

Declare_Virtual_Device CKMM, 1, 0, CK_Control, \
                       Undefined_Device_ID, Undefined_Init_Order, , ,

CK_MAGIC                equ 4d4d4b43h ; "CKMM"
CK_DIOC_CAPTURE         equ 1
CK_VENDOR_ATI           equ 1002h
CK_DEVICE_MULLINS       equ 9854h
CK_CFG_BAR5             equ 24h
CK_MMIO_BYTES           equ 00040000h
CK_BASE_ALIGN           equ 0003ffffh
CK_BAR_IO               equ 00000001h
CK_BAR_TYPE_MASK        equ 00000006h
CK_CONFIG_MEMSIZE       equ 5428h
CK_MEMSIZE_MAX_MIB      equ 4096
CK_OFFSET_MAX           equ 256


; Status bits returned to the publisher.
CK_ST_ATI               equ 00000001h
CK_ST_BASE_INVALID      equ 00000002h
CK_ST_MMIO_MAPPED       equ 00000004h
CK_ST_IDENTITY_MATCH    equ 00000008h
CK_ST_OFFSETS_REFUSED   equ 00000010h
CK_ST_SNAPSHOT_TAKEN    equ 00000020h
CK_ST_BAR_64            equ 00000040h
CK_ST_NOT_MULLINS       equ 00000080h
CK_ST_ADDRESS_INVALID   equ 00000100h

; Input from V9XCKMM.EXE.
CK_IN_CONFIG            equ 0
CK_IN_COUNT             equ 4
CK_IN_OFFSETS           equ 8
CK_IN_MIN_BYTES         equ 8

; Output layout, in dwords. Must match struct v9x_czmm_result in
; carrizo_mmio_probe_win32.c.
CK_HEADER_DWORDS        equ 16
CK_RESULT_DWORDS        equ (CK_HEADER_DWORDS + CK_OFFSET_MAX * 3)

VxD_LOCKED_DATA_SEG

; Accepted windows: start, limit (exclusive). The display blocks repeat at
; the controller offsets 0 and C00h (dce_8_0_d.h: DCP1_GRPH_ENABLE 1D00h and
; CRTC1_CRTC_CONTROL 1E9Ch, dword indices).
CKWindows label dword
    dd 0300h, 0314h     ; VGA_RENDER_CONTROL .. VGA_MEMORY_BASE_ADDRESS
    dd 0318h, 031ch     ; VGA_DISPBUF1_SURFACE_ADDR
    dd 0320h, 0340h     ; VGA_DISPBUF2 .. VGA_HW_DEBUG
    dd 0e4ch, 0e54h     ; SRBM_STATUS2, SRBM_STATUS
    dd 0e60h, 0e64h     ; SRBM_SOFT_RESET
    dd 1400h, 1404h     ; VM_L2_CNTL
    dd 1410h, 1414h     ; VM_CONTEXT0_CNTL
    dd 153ch, 1540h     ; VM_CONTEXT0_PAGE_TABLE_BASE_ADDR
    dd 155ch, 1560h     ; VM_CONTEXT0_PAGE_TABLE_START_ADDR
    dd 157ch, 1580h     ; VM_CONTEXT0_PAGE_TABLE_END_ADDR
    dd 2004h, 2008h     ; MC_SHARED_CHMAP
    dd 2024h, 2040h     ; MC_VM_FB_LOCATION, AGP_TOP/BOT/BASE, SYSTEM_APERTURE_*
    dd 2064h, 206ch     ; MC_VM_MX_L1_TLB_CNTL, MC_VM_FB_OFFSET
    dd 2760h, 2764h     ; MC_ARB_RAMCFG
    dd 2c04h, 2c10h     ; HDP_NONSURFACE_BASE/INFO/SIZE
    dd 5428h, 542ch     ; CONFIG_MEMSIZE
    dd 5490h, 5494h     ; BIF_FB_EN
    dd 8000h, 801ch     ; GRBM_CNTL .. GRBM_STATUS_SE1
    dd 8020h, 8024h     ; GRBM_SOFT_RESET
    dd 8210h, 8214h     ; CP_CPC_STATUS
    dd 821ch, 8220h     ; CP_CPF_STATUS
    dd 8234h, 8238h     ; CP_MEC_CNTL
    dd 8674h, 8678h     ; CP_STALLED_STAT1
    dd 867ch, 8684h     ; CP_BUSY_STAT, CP_STAT
    dd 86d8h, 86dch     ; CP_ME_CNTL
    dd 8700h, 8704h     ; CP_RB0_RPTR
    dd 8708h, 870ch     ; CP_RB_WPTR_POLL_CNTL
    dd 89bch, 89c4h     ; CC_GC_SHADER_ARRAY_CONFIG, GC_USER_SHADER_ARRAY_CONFIG
    dd 98f4h, 98fch     ; CC_RB_BACKEND_DISABLE, GB_ADDR_CONFIG
    dd 9910h, 99d0h     ; GB_TILE_MODE0-31, GB_MACROTILE_MODE0-15
    dd 0c100h, 0c118h   ; CP_RB0_BASE .. CP_RB0_WPTR
    dd 0c144h, 0c148h   ; CP_RB_VMID
    dd 0c1a8h, 0c1ach   ; CP_INT_CNTL_RING0
    dd 0c2c4h, 0c2c8h   ; CP_RB0_BASE_HI
    dd 0d010h, 0d01ch   ; SDMA0_CNTL, CHICKEN_BITS, TILING_CONFIG
    dd 0d034h, 0d03ch   ; SDMA0_STATUS_REG, STATUS1_REG
    dd 0d048h, 0d04ch   ; SDMA0_F32_CNTL
    dd 0d058h, 0d05ch   ; SDMA_POWER_GATING
    dd 0d200h, 0d22ch   ; SDMA0_GFX_RB_CNTL .. SDMA0_GFX_IB_CNTL
    dd 0d810h, 0d81ch   ; SDMA1 (+800h)
    dd 0d834h, 0d83ch
    dd 0d848h, 0d84ch
    dd 0da00h, 0da2ch
    dd 30100h, 30120h   ; SCRATCH_REG0-7
    dd 30800h, 30804h   ; GRBM_GFX_INDEX
    dd 0c300h, 0c304h   ; RLC_CNTL
    dd 0c310h, 0c314h   ; RLC_STAT
    dd 0c400h, 0c404h   ; RLC_GPM_STAT
    dd 0c40ch, 0c410h   ; RLC_PG_CNTL
    dd 0c414h, 0c418h   ; RLC_GPM_THREAD_ENABLE
    dd 0c424h, 0c428h   ; RLC_CGCG_CGLS_CTRL
    dd 0c44ch, 0c450h   ; RLC_PG_ALWAYS_ON_CU_MASK
    dd 0c484h, 0c490h   ; RLC_SERDES_CU_MASTER_BUSY .. RLC_GPM_GENERAL_0
    dd 0c4e8h, 0c4ech   ; RLC_SAFE_MODE
    ; Controller 0
    dd 6800h, 6850h     ; GRPH_ENABLE .. GRPH_SURFACE_ADDRESS_INUSE
    dd 6998h, 69bch     ; CUR_CONTROL .. CUR_UPDATE
    dd 6d70h, 6d78h     ; VIEWPORT_START, VIEWPORT_SIZE
    dd 6e00h, 6e04h     ; CRTC_H_TOTAL
    dd 6e1ch, 6e20h     ; CRTC_V_TOTAL
    dd 6e34h, 6e38h     ; CRTC_V_BLANK_START_END
    dd 6e70h, 6e78h     ; CRTC_CONTROL, CRTC_BLANK_CONTROL
    dd 6e8ch, 6e9ch     ; CRTC_STATUS .. CRTC_STATUS_VF_COUNT
    dd 6ea0h, 6ea4h     ; CRTC_STATUS_HV_COUNT
    dd 6ed4h, 6ed8h     ; CRTC_UPDATE_LOCK
    dd 6ef4h, 6efch     ; CRTC_MASTER_UPDATE_LOCK, _MODE
    ; Controller 1 (+C00h)
    dd 7400h, 7450h
    dd 7598h, 75bch
    dd 7970h, 7978h
    dd 7a00h, 7a04h
    dd 7a1ch, 7a20h
    dd 7a34h, 7a38h
    dd 7a70h, 7a78h
    dd 7a8ch, 7a9ch
    dd 7aa0h, 7aa4h
    dd 7ad4h, 7ad8h
    dd 7af4h, 7afch
CK_WINDOW_COUNT         equ 76

CKResult label dword
    dd CK_MAGIC                 ; 00 magic
    dd 0                        ; 01 status
    dd 0                        ; 02 id dword at configuration 00h
    dd 0                        ; 03 BAR5 as read
    dd 0                        ; 04 offsets accepted
    dd 0                        ; 05 CONFIG_MEMSIZE through the BAR
    dd 0                        ; 06 0CF8h found and restored
    dd 0                        ; 07 first offset refused, if any
    dd 0                        ; 08 configuration address used
    dd 0                        ; reserved (Carrizo's SMU fields)
    dd 0                        ; reserved (Carrizo's SMU fields)
    dd 0                        ; reserved (Carrizo's SMU fields)
    dd 0                        ; reserved (Carrizo's SMU fields)
    dd 0                        ; 13 reserved
    dd 0                        ; 14 reserved
    dd 0                        ; 15 reserved
CKOffsets    dd CK_OFFSET_MAX dup (0)
CKFirst      dd CK_OFFSET_MAX dup (0)
CKSecond     dd CK_OFFSET_MAX dup (0)
CKConfig     dd 0
CKMmio       dd 0
VxD_LOCKED_DATA_ENDS

VxD_LOCKED_CODE_SEG

; EAX = PCI configuration address. Returns EAX = configuration dword.
; 0CF8h saved and restored with interrupts off.
BeginProc CK_Pci_Read
    push    ebx
    push    edx
    pushfd
    cli
    mov     ebx, eax
    mov     dx, 0cf8h
    in      eax, dx
    mov     CKResult[24], eax
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
EndProc CK_Pci_Read

; EDX = offset. Returns CF clear if it lies in an accepted window.
BeginProc CK_Offset_Allowed
    push    ecx
    push    esi
    test    edx, 3
    jnz     short CK_Offset_Refused
    mov     esi, OFFSET32 CKWindows
    mov     ecx, CK_WINDOW_COUNT
CK_Offset_Next:
    cmp     edx, [esi]
    jb      short CK_Offset_Skip
    cmp     edx, [esi+4]
    jb      short CK_Offset_Accepted
CK_Offset_Skip:
    add     esi, 8
    dec     ecx
    jnz     short CK_Offset_Next
CK_Offset_Refused:
    pop     esi
    pop     ecx
    stc
    ret
CK_Offset_Accepted:
    pop     esi
    pop     ecx
    clc
    ret
EndProc CK_Offset_Allowed

; EBP = DIOC input buffer, ECX = its byte count. Copies the offset list,
; refusing the whole list if any entry is unaligned or outside the windows.
BeginProc CK_Accept_Offsets
    pushad
    mov     CKResult[16], 0
    sub     ecx, CK_IN_OFFSETS
    shr     ecx, 2
    mov     eax, [ebp+CK_IN_COUNT]
    cmp     eax, CK_OFFSET_MAX
    ja      short CK_Accept_Refused
    cmp     eax, ecx
    ja      short CK_Accept_Refused
    mov     ecx, eax
    lea     esi, [ebp+CK_IN_OFFSETS]
    mov     edi, OFFSET32 CKOffsets
    jecxz   short CK_Accept_Done
CK_Accept_Next:
    mov     edx, [esi]
    call    CK_Offset_Allowed
    jc      short CK_Accept_Refused_At
    mov     [edi], edx
    add     esi, 4
    add     edi, 4
    dec     ecx
    jnz     short CK_Accept_Next
CK_Accept_Done:
    mov     eax, [ebp+CK_IN_COUNT]
    mov     CKResult[16], eax
    popad
    ret
CK_Accept_Refused_At:
    mov     CKResult[28], edx
CK_Accept_Refused:
    or      CKResult[4], CK_ST_OFFSETS_REFUSED
    popad
    ret
EndProc CK_Accept_Offsets

; EDI = destination. Reads every accepted offset through the mapping.
BeginProc CK_Read_Snapshot
    pushad
    mov     ecx, CKResult[16]
    mov     ebx, OFFSET32 CKOffsets
    mov     esi, CKMmio
    jecxz   short CK_Read_Snapshot_Done
CK_Read_Snapshot_Next:
    mov     eax, [ebx]
    mov     eax, [esi+eax]
    mov     [edi], eax
    add     ebx, 4
    add     edi, 4
    dec     ecx
    jnz     short CK_Read_Snapshot_Next
CK_Read_Snapshot_Done:
    popad
    ret
EndProc CK_Read_Snapshot
BeginProc CK_Capture
    pushad

    ; A configuration address: enable bit set, register field zero, and
    ; nothing in the reserved bits 30:24.
    mov     eax, CKConfig
    mov     CKResult[32], eax
    test    eax, 80000000h
    jz      CK_Capture_Bad_Address
    test    eax, 7f0000ffh
    jnz     CK_Capture_Bad_Address

    call    CK_Pci_Read
    mov     CKResult[8], eax
    cmp     ax, CK_VENDOR_ATI
    jne     CK_Capture_Bad_Base
    or      CKResult[4], CK_ST_ATI
    shr     eax, 16
    cmp     ax, CK_DEVICE_MULLINS
    je      short CK_Capture_Check_Base
    or      CKResult[4], CK_ST_NOT_MULLINS
    jmp     CK_Capture_Done

CK_Capture_Check_Base:
    mov     eax, CKConfig
    or      eax, CK_CFG_BAR5
    call    CK_Pci_Read
    mov     CKResult[12], eax
    test    eax, CK_BAR_IO
    jnz     CK_Capture_Bad_Base
    test    eax, CK_BAR_TYPE_MASK
    jz      short CK_Capture_Low_Base
    or      CKResult[4], CK_ST_BAR_64
    jmp     CK_Capture_Bad_Base

CK_Capture_Low_Base:
    and     eax, NOT 0fh
    jz      CK_Capture_Bad_Base
    test    eax, CK_BASE_ALIGN
    jnz     CK_Capture_Bad_Base

    VMMcall _MapPhysToLinear,<eax,CK_MMIO_BYTES,0>
    cmp     eax, 0ffffffffh
    je      CK_Capture_Done
    mov     CKMmio, eax
    or      CKResult[4], CK_ST_MMIO_MAPPED

    mov     edx, [eax+CK_CONFIG_MEMSIZE]
    mov     CKResult[20], edx
    test    edx, edx
    jz      short CK_Capture_Done
    cmp     edx, CK_MEMSIZE_MAX_MIB
    ja      short CK_Capture_Done
    or      CKResult[4], CK_ST_IDENTITY_MATCH

    mov     edi, OFFSET32 CKFirst
    call    CK_Read_Snapshot
    mov     edi, OFFSET32 CKSecond
    call    CK_Read_Snapshot
    or      CKResult[4], CK_ST_SNAPSHOT_TAKEN
    jmp     short CK_Capture_Done

CK_Capture_Bad_Address:
    or      CKResult[4], CK_ST_ADDRESS_INVALID
    jmp     short CK_Capture_Done
CK_Capture_Bad_Base:
    or      CKResult[4], CK_ST_BASE_INVALID
CK_Capture_Done:
    popad
    ret
EndProc CK_Capture

BeginProc CK_Reset_Result
    pushad
    mov     edi, OFFSET32 CKResult + 4
    mov     ecx, CK_RESULT_DWORDS - 1
    xor     eax, eax
    cld
    rep     stosd
    mov     CKMmio, eax
    popad
    ret
EndProc CK_Reset_Result

BeginProc CK_W32_DeviceIoControl
    cmp     ecx, DIOC_OPEN
    je      CK_Dioc_Ok
    cmp     ecx, DIOC_CLOSEHANDLE
    je      CK_Dioc_Ok
    cmp     ecx, CK_DIOC_CAPTURE
    jne     CK_Dioc_Fail

    pushad
    mov     ebx, esi
    mov     ebp, [ebx.lpvInBuffer]
    test    ebp, ebp
    jz      short CK_Dioc_Copy_Fail
    mov     ecx, [ebx.cbInBuffer]
    cmp     ecx, CK_IN_MIN_BYTES
    jb      short CK_Dioc_Copy_Fail
    mov     edi, [ebx.lpvOutBuffer]
    test    edi, edi
    jz      short CK_Dioc_Copy_Fail
    cmp     [ebx.cbOutBuffer], CK_RESULT_DWORDS * 4
    jb      short CK_Dioc_Copy_Fail

    call    CK_Reset_Result
    mov     eax, [ebp+CK_IN_CONFIG]
    mov     CKConfig, eax
    call    CK_Accept_Offsets
    test    CKResult[4], CK_ST_OFFSETS_REFUSED
    jnz     short CK_Dioc_Copy_Out
    call    CK_Capture

CK_Dioc_Copy_Out:
    mov     edi, [ebx.lpvOutBuffer]
    mov     esi, OFFSET32 CKResult
    mov     ecx, CK_RESULT_DWORDS
    cld
    rep     movsd
    mov     eax, [ebx.lpcbBytesReturned]
    test    eax, eax
    jz      short CK_Dioc_Copy_Done
    mov     dword ptr [eax], CK_RESULT_DWORDS * 4
CK_Dioc_Copy_Done:
    popad
CK_Dioc_Ok:
    xor     eax, eax
    ret

CK_Dioc_Copy_Fail:
    popad
CK_Dioc_Fail:
    mov     eax, 1
    ret
EndProc CK_W32_DeviceIoControl

BeginProc CK_Dynamic_Init
    clc
    ret
EndProc CK_Dynamic_Init

BeginProc CK_Dynamic_Exit
    clc
    ret
EndProc CK_Dynamic_Exit

Begin_Control_Dispatch CK
    Control_Dispatch Sys_Dynamic_Device_Init, CK_Dynamic_Init
    Control_Dispatch Sys_Dynamic_Device_Exit, CK_Dynamic_Exit
    Control_Dispatch W32_DeviceIoControl, CK_W32_DeviceIoControl
End_Control_Dispatch CK

VxD_LOCKED_CODE_ENDS

end
