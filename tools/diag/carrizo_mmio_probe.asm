; AMD Carrizo (Radeon R7E, GCN 3) read-only register probe VxD.
;
; Phase 2 of docs\plans\ati-carrizo-gfx8.md. Maps the 256 KiB register BAR
; (BAR5, at configuration 24h, on every GCN chip from Bonaire on: Linux
; amdgpu_device.c) and reads a caller's list of offsets, each twice. The
; Evergreen probe (evergreen_mmio_probe.asm) is the model.
;
; Writes: one register, the SMU's indirect index MP0PUB_IND_INDEX (600h),
; and only to read three dwords of the SMU firmware header through
; MP0PUB_IND_DATA (604h): its version, which engines' microcode it has
; loaded, and its signature (Linux smu8_smumgr.c smu8_check_fw_load_finish,
; smu8.h SMU8_Firmware_Header at 1001FF80h). The index is read first and
; written back afterwards, with interrupts off throughout; nothing else on
; Windows 98 uses it. Nothing else on the device or in memory is written.
; 0CF8h is saved and restored around each configuration read. Mapping a
; physical range with _MapPhysToLinear changes no device state.
;
; The caller names the device by its configuration address; the VxD reads
; the id and BAR5 itself and refuses anything that is not 1002:9874 with a
; 256 KiB-aligned 32-bit memory BAR5 below 4 GiB. BAR5 cannot be a 64-bit
; BAR (its high half would be 28h, the CardBus CIS pointer), so one that
; claims to be is refused.
;
; The identity check after mapping is Evergreen's: CONFIG_MEMSIZE (5428h,
; MiB, Linux gmc_v8_0.c gmc_v8_0_mc_init) must be non-zero and at most 4096.
; A window that decodes nothing reads FFFFFFFFh and fails it. The SMU reads
; run only after it passes.
;
; Offsets are accepted only inside CZWindows below, and only for the three
; display controllers Carrizo has (Linux dce_v11_0.c: num_crtc = 3 for
; CHIP_CARRIZO). Left out on purpose, because a read has a side effect or
; reads through an index:
;   0000h-001Bh  MM_INDEX / MM_DATA and MM_INDEX_HI (bif_5_0_d.h);
;   0314h, 031Ch VGA_TEST_DEBUG_INDEX / _DATA;
;   0600h-0607h  MP0PUB_IND_INDEX / _DATA, read only through CZ_Smu_Read;
;   5480h        HDP_MEM_COHERENCY_FLUSH_CNTL, a flush trigger;
;   6E84h-6E8Bh  CRTC_PIXEL_DATA_READBACK0/1;
;   D000h-D007h  SDMA0_UCODE_ADDR / _DATA, and SDMA1's at D800h;
;   D060h-D067h  SDMA_PGFSM_WRITE / _READ;
;   3E050h-3E0FFh the CP and RLC microcode address/data ports.
; Offsets are Linux amdgpu's register headers (dword indices times four;
; build\external\carrizo\linux\), not checked against any AMD register
; guide: none for these blocks was found.

.386p

.xlist
include VMM.INC
include VWIN32.INC
.list

Declare_Virtual_Device CZMM, 1, 0, CZ_Control, \
                       Undefined_Device_ID, Undefined_Init_Order, , ,

CZ_MAGIC                equ 4d4d5a43h ; "CZMM"
CZ_DIOC_CAPTURE         equ 1
CZ_VENDOR_ATI           equ 1002h
CZ_DEVICE_CARRIZO       equ 9874h
CZ_CFG_BAR5             equ 24h
CZ_MMIO_BYTES           equ 00040000h
CZ_BASE_ALIGN           equ 0003ffffh
CZ_BAR_IO               equ 00000001h
CZ_BAR_TYPE_MASK        equ 00000006h
CZ_CONFIG_MEMSIZE       equ 5428h
CZ_MEMSIZE_MAX_MIB      equ 4096
CZ_OFFSET_MAX           equ 256

; The SMU's indirect window and the firmware header behind it: smu8.h
; SMN_MP1_SRAM_START_ADDR 10000000h + SMU8_FIRMWARE_HEADER_LOCATION 1FF80h,
; then the fields' offsets in struct SMU8_Firmware_Header.
CZ_MP0PUB_IND_INDEX     equ 0600h
CZ_MP0PUB_IND_DATA      equ 0604h
CZ_SMU_HEADER           equ 1001ff80h
CZ_SMU_VERSION          equ 00h
CZ_SMU_LOAD_STATUS      equ 18h
CZ_SMU_SIGNATURE        equ 7ch

; Status bits returned to the publisher.
CZ_ST_ATI               equ 00000001h
CZ_ST_BASE_INVALID      equ 00000002h
CZ_ST_MMIO_MAPPED       equ 00000004h
CZ_ST_IDENTITY_MATCH    equ 00000008h
CZ_ST_OFFSETS_REFUSED   equ 00000010h
CZ_ST_SNAPSHOT_TAKEN    equ 00000020h
CZ_ST_BAR_64            equ 00000040h
CZ_ST_NOT_CARRIZO       equ 00000080h
CZ_ST_ADDRESS_INVALID   equ 00000100h
CZ_ST_SMU_READ          equ 00000200h

; Input from V9XCZMM.EXE.
CZ_IN_CONFIG            equ 0
CZ_IN_COUNT             equ 4
CZ_IN_OFFSETS           equ 8
CZ_IN_MIN_BYTES         equ 8

; Output layout, in dwords. Must match struct v9x_czmm_result in
; carrizo_mmio_probe_win32.c.
CZ_HEADER_DWORDS        equ 16
CZ_RESULT_DWORDS        equ (CZ_HEADER_DWORDS + CZ_OFFSET_MAX * 3)

VxD_LOCKED_DATA_SEG

; Accepted windows: start, limit (exclusive). The display blocks repeat at
; the controller offsets 0, 800h and 1000h (dce_11_0_d.h: DCP1_GRPH_ENABLE
; 1C00h and CRTC1_CRTC_CONTROL 1D9Ch, dword indices).
CZWindows label dword
    dd 0300h, 0314h     ; VGA_RENDER_CONTROL .. VGA_MEMORY_BASE_ADDRESS
    dd 0318h, 031ch     ; VGA_DISPBUF1_SURFACE_ADDR
    dd 0320h, 0340h     ; VGA_DISPBUF2 .. VGA_HW_DEBUG
    dd 0700h, 0704h     ; SMU_MP1_SRBM2P_MSG_0
    dd 0740h, 0744h     ; SMU_MP1_SRBM2P_RESP_0
    dd 0780h, 0784h     ; SMU_MP1_SRBM2P_ARG_0
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
    dd 0d070h, 0d078h   ; SDMA0_ID, SDMA0_VERSION
    dd 0d200h, 0d22ch   ; SDMA0_GFX_RB_CNTL .. SDMA0_GFX_IB_CNTL
    dd 0d248h, 0d24ch   ; SDMA0_GFX_DOORBELL
    dd 0d810h, 0d81ch   ; SDMA1 (+800h)
    dd 0d834h, 0d83ch
    dd 0d848h, 0d84ch
    dd 0d870h, 0d878h
    dd 0da00h, 0da2ch
    dd 0da48h, 0da4ch
    dd 30100h, 30120h   ; SCRATCH_REG0-7
    dd 30800h, 30804h   ; GRBM_GFX_INDEX
    dd 3b000h, 3b004h   ; RLC_CNTL
    dd 3b010h, 3b018h   ; RLC_STAT, RLC_SAFE_MODE
    dd 3b100h, 3b104h   ; RLC_GPM_STAT
    dd 3b10ch, 3b110h   ; RLC_PG_CNTL
    dd 3b114h, 3b118h   ; RLC_GPM_THREAD_ENABLE
    dd 3b124h, 3b128h   ; RLC_CGCG_CGLS_CTRL
    dd 3b14ch, 3b150h   ; RLC_PG_ALWAYS_ON_CU_MASK
    dd 3b184h, 3b190h   ; RLC_SERDES_CU_MASTER_BUSY .. RLC_GPM_GENERAL_0
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
    ; Controller 1 (+800h)
    dd 7000h, 7050h
    dd 7198h, 71bch
    dd 7570h, 7578h
    dd 7600h, 7604h
    dd 761ch, 7620h
    dd 7634h, 7638h
    dd 7670h, 7678h
    dd 768ch, 769ch
    dd 76a0h, 76a4h
    dd 76d4h, 76d8h
    dd 76f4h, 76fch
    ; Controller 2 (+1000h)
    dd 7800h, 7850h
    dd 7998h, 79bch
    dd 7d70h, 7d78h
    dd 7e00h, 7e04h
    dd 7e1ch, 7e20h
    dd 7e34h, 7e38h
    dd 7e70h, 7e78h
    dd 7e8ch, 7e9ch
    dd 7ea0h, 7ea4h
    dd 7ed4h, 7ed8h
    dd 7ef4h, 7efch
CZ_WINDOW_COUNT         equ 93

CZResult label dword
    dd CZ_MAGIC                 ; 00 magic
    dd 0                        ; 01 status
    dd 0                        ; 02 id dword at configuration 00h
    dd 0                        ; 03 BAR5 as read
    dd 0                        ; 04 offsets accepted
    dd 0                        ; 05 CONFIG_MEMSIZE through the BAR
    dd 0                        ; 06 0CF8h found and restored
    dd 0                        ; 07 first offset refused, if any
    dd 0                        ; 08 configuration address used
    dd 0                        ; 09 MP0PUB_IND_INDEX found and restored
    dd 0                        ; 10 SMU firmware header: Version
    dd 0                        ; 11 SMU firmware header: UcodeLoadStatus
    dd 0                        ; 12 SMU firmware header: Signature
    dd 0                        ; 13 reserved
    dd 0                        ; 14 reserved
    dd 0                        ; 15 reserved
CZOffsets    dd CZ_OFFSET_MAX dup (0)
CZFirst      dd CZ_OFFSET_MAX dup (0)
CZSecond     dd CZ_OFFSET_MAX dup (0)
CZConfig     dd 0
CZMmio       dd 0
VxD_LOCKED_DATA_ENDS

VxD_LOCKED_CODE_SEG

; EAX = PCI configuration address. Returns EAX = configuration dword.
; 0CF8h saved and restored with interrupts off.
BeginProc CZ_Pci_Read
    push    ebx
    push    edx
    pushfd
    cli
    mov     ebx, eax
    mov     dx, 0cf8h
    in      eax, dx
    mov     CZResult[24], eax
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
EndProc CZ_Pci_Read

; EDX = offset. Returns CF clear if it lies in an accepted window.
BeginProc CZ_Offset_Allowed
    push    ecx
    push    esi
    test    edx, 3
    jnz     short CZ_Offset_Refused
    mov     esi, OFFSET32 CZWindows
    mov     ecx, CZ_WINDOW_COUNT
CZ_Offset_Next:
    cmp     edx, [esi]
    jb      short CZ_Offset_Skip
    cmp     edx, [esi+4]
    jb      short CZ_Offset_Accepted
CZ_Offset_Skip:
    add     esi, 8
    dec     ecx
    jnz     short CZ_Offset_Next
CZ_Offset_Refused:
    pop     esi
    pop     ecx
    stc
    ret
CZ_Offset_Accepted:
    pop     esi
    pop     ecx
    clc
    ret
EndProc CZ_Offset_Allowed

; EBP = DIOC input buffer, ECX = its byte count. Copies the offset list,
; refusing the whole list if any entry is unaligned or outside the windows.
BeginProc CZ_Accept_Offsets
    pushad
    mov     CZResult[16], 0
    sub     ecx, CZ_IN_OFFSETS
    shr     ecx, 2
    mov     eax, [ebp+CZ_IN_COUNT]
    cmp     eax, CZ_OFFSET_MAX
    ja      short CZ_Accept_Refused
    cmp     eax, ecx
    ja      short CZ_Accept_Refused
    mov     ecx, eax
    lea     esi, [ebp+CZ_IN_OFFSETS]
    mov     edi, OFFSET32 CZOffsets
    jecxz   short CZ_Accept_Done
CZ_Accept_Next:
    mov     edx, [esi]
    call    CZ_Offset_Allowed
    jc      short CZ_Accept_Refused_At
    mov     [edi], edx
    add     esi, 4
    add     edi, 4
    dec     ecx
    jnz     short CZ_Accept_Next
CZ_Accept_Done:
    mov     eax, [ebp+CZ_IN_COUNT]
    mov     CZResult[16], eax
    popad
    ret
CZ_Accept_Refused_At:
    mov     CZResult[28], edx
CZ_Accept_Refused:
    or      CZResult[4], CZ_ST_OFFSETS_REFUSED
    popad
    ret
EndProc CZ_Accept_Offsets

; EDI = destination. Reads every accepted offset through the mapping.
BeginProc CZ_Read_Snapshot
    pushad
    mov     ecx, CZResult[16]
    mov     ebx, OFFSET32 CZOffsets
    mov     esi, CZMmio
    jecxz   short CZ_Read_Snapshot_Done
CZ_Read_Snapshot_Next:
    mov     eax, [ebx]
    mov     eax, [esi+eax]
    mov     [edi], eax
    add     ebx, 4
    add     edi, 4
    dec     ecx
    jnz     short CZ_Read_Snapshot_Next
CZ_Read_Snapshot_Done:
    popad
    ret
EndProc CZ_Read_Snapshot

; The SMU firmware header's three fields through MP0PUB_IND_INDEX/_DATA.
; The index is saved first and put back last, interrupts off throughout,
; so whatever pointed it elsewhere finds it where it left it.
BeginProc CZ_Smu_Read
    pushad
    pushfd
    cli
    mov     esi, CZMmio
    mov     eax, [esi+CZ_MP0PUB_IND_INDEX]
    mov     CZResult[36], eax
    mov     ebx, eax

    mov     dword ptr [esi+CZ_MP0PUB_IND_INDEX], CZ_SMU_HEADER + CZ_SMU_VERSION
    mov     eax, [esi+CZ_MP0PUB_IND_DATA]
    mov     CZResult[40], eax
    mov     dword ptr [esi+CZ_MP0PUB_IND_INDEX], CZ_SMU_HEADER + CZ_SMU_LOAD_STATUS
    mov     eax, [esi+CZ_MP0PUB_IND_DATA]
    mov     CZResult[44], eax
    mov     dword ptr [esi+CZ_MP0PUB_IND_INDEX], CZ_SMU_HEADER + CZ_SMU_SIGNATURE
    mov     eax, [esi+CZ_MP0PUB_IND_DATA]
    mov     CZResult[48], eax

    mov     [esi+CZ_MP0PUB_IND_INDEX], ebx
    popfd
    or      CZResult[4], CZ_ST_SMU_READ
    popad
    ret
EndProc CZ_Smu_Read

BeginProc CZ_Capture
    pushad

    ; A configuration address: enable bit set, register field zero, and
    ; nothing in the reserved bits 30:24.
    mov     eax, CZConfig
    mov     CZResult[32], eax
    test    eax, 80000000h
    jz      CZ_Capture_Bad_Address
    test    eax, 7f0000ffh
    jnz     CZ_Capture_Bad_Address

    call    CZ_Pci_Read
    mov     CZResult[8], eax
    cmp     ax, CZ_VENDOR_ATI
    jne     CZ_Capture_Bad_Base
    or      CZResult[4], CZ_ST_ATI
    shr     eax, 16
    cmp     ax, CZ_DEVICE_CARRIZO
    je      short CZ_Capture_Check_Base
    or      CZResult[4], CZ_ST_NOT_CARRIZO
    jmp     CZ_Capture_Done

CZ_Capture_Check_Base:
    mov     eax, CZConfig
    or      eax, CZ_CFG_BAR5
    call    CZ_Pci_Read
    mov     CZResult[12], eax
    test    eax, CZ_BAR_IO
    jnz     CZ_Capture_Bad_Base
    test    eax, CZ_BAR_TYPE_MASK
    jz      short CZ_Capture_Low_Base
    or      CZResult[4], CZ_ST_BAR_64
    jmp     CZ_Capture_Bad_Base

CZ_Capture_Low_Base:
    and     eax, NOT 0fh
    jz      CZ_Capture_Bad_Base
    test    eax, CZ_BASE_ALIGN
    jnz     CZ_Capture_Bad_Base

    VMMcall _MapPhysToLinear,<eax,CZ_MMIO_BYTES,0>
    cmp     eax, 0ffffffffh
    je      CZ_Capture_Done
    mov     CZMmio, eax
    or      CZResult[4], CZ_ST_MMIO_MAPPED

    mov     edx, [eax+CZ_CONFIG_MEMSIZE]
    mov     CZResult[20], edx
    test    edx, edx
    jz      short CZ_Capture_Done
    cmp     edx, CZ_MEMSIZE_MAX_MIB
    ja      short CZ_Capture_Done
    or      CZResult[4], CZ_ST_IDENTITY_MATCH

    mov     edi, OFFSET32 CZFirst
    call    CZ_Read_Snapshot
    mov     edi, OFFSET32 CZSecond
    call    CZ_Read_Snapshot
    or      CZResult[4], CZ_ST_SNAPSHOT_TAKEN
    call    CZ_Smu_Read
    jmp     short CZ_Capture_Done

CZ_Capture_Bad_Address:
    or      CZResult[4], CZ_ST_ADDRESS_INVALID
    jmp     short CZ_Capture_Done
CZ_Capture_Bad_Base:
    or      CZResult[4], CZ_ST_BASE_INVALID
CZ_Capture_Done:
    popad
    ret
EndProc CZ_Capture

BeginProc CZ_Reset_Result
    pushad
    mov     edi, OFFSET32 CZResult + 4
    mov     ecx, CZ_RESULT_DWORDS - 1
    xor     eax, eax
    cld
    rep     stosd
    mov     CZMmio, eax
    popad
    ret
EndProc CZ_Reset_Result

BeginProc CZ_W32_DeviceIoControl
    cmp     ecx, DIOC_OPEN
    je      CZ_Dioc_Ok
    cmp     ecx, DIOC_CLOSEHANDLE
    je      CZ_Dioc_Ok
    cmp     ecx, CZ_DIOC_CAPTURE
    jne     CZ_Dioc_Fail

    pushad
    mov     ebx, esi
    mov     ebp, [ebx.lpvInBuffer]
    test    ebp, ebp
    jz      short CZ_Dioc_Copy_Fail
    mov     ecx, [ebx.cbInBuffer]
    cmp     ecx, CZ_IN_MIN_BYTES
    jb      short CZ_Dioc_Copy_Fail
    mov     edi, [ebx.lpvOutBuffer]
    test    edi, edi
    jz      short CZ_Dioc_Copy_Fail
    cmp     [ebx.cbOutBuffer], CZ_RESULT_DWORDS * 4
    jb      short CZ_Dioc_Copy_Fail

    call    CZ_Reset_Result
    mov     eax, [ebp+CZ_IN_CONFIG]
    mov     CZConfig, eax
    call    CZ_Accept_Offsets
    test    CZResult[4], CZ_ST_OFFSETS_REFUSED
    jnz     short CZ_Dioc_Copy_Out
    call    CZ_Capture

CZ_Dioc_Copy_Out:
    mov     edi, [ebx.lpvOutBuffer]
    mov     esi, OFFSET32 CZResult
    mov     ecx, CZ_RESULT_DWORDS
    cld
    rep     movsd
    mov     eax, [ebx.lpcbBytesReturned]
    test    eax, eax
    jz      short CZ_Dioc_Copy_Done
    mov     dword ptr [eax], CZ_RESULT_DWORDS * 4
CZ_Dioc_Copy_Done:
    popad
CZ_Dioc_Ok:
    xor     eax, eax
    ret

CZ_Dioc_Copy_Fail:
    popad
CZ_Dioc_Fail:
    mov     eax, 1
    ret
EndProc CZ_W32_DeviceIoControl

BeginProc CZ_Dynamic_Init
    clc
    ret
EndProc CZ_Dynamic_Init

BeginProc CZ_Dynamic_Exit
    clc
    ret
EndProc CZ_Dynamic_Exit

Begin_Control_Dispatch CZ
    Control_Dispatch Sys_Dynamic_Device_Init, CZ_Dynamic_Init
    Control_Dispatch Sys_Dynamic_Device_Exit, CZ_Dynamic_Exit
    Control_Dispatch W32_DeviceIoControl, CZ_W32_DeviceIoControl
End_Control_Dispatch CZ

VxD_LOCKED_CODE_ENDS

end
