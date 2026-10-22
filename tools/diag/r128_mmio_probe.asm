; ATI Rage 128 read-only register probe VxD.
;
; Phase 2 of docs\plans\ati-rage128.md. Maps the 16 KiB register BAR
; (BAR2, REG_BASE) and reads a caller's list of offsets, each twice. The
; Ironlake probe (gen5_mmio_probe.asm) is the model.
;
; Writes: none, to the device or to memory. 0CF8h is saved and restored
; around each configuration read. Mapping a physical range with
; _MapPhysToLinear changes no device state.
;
; The caller names the device by its configuration address; the VxD reads
; the id and BAR2 itself and refuses anything that is not an ATI Rage 128
; with a 16 KiB-aligned memory BAR2. Once mapped, the read-only copy of
; configuration space at 0F00h (Rage 128 PRO RRG, Table 2-1) must return
; the same id dword, or nothing else is read: a window that decodes
; something else is never sampled.
;
; Offsets are accepted only inside R1Windows below. Left out on purpose:
;   0000h-0007h  MM_INDEX / MM_DATA, an indirect read of whatever the index
;                names;
;   000Ch        CLOCK_CNTL_DATA, likewise indirect (the PLL index);
;   00B4h        PALETTE_DATA, whose read advances the palette index;
;   1000h-13FFh  the CCE's FIFO ports, which a read would pop;
;   17C0h-17E0h  HOST_DATA.
; Offsets are the X driver's r128_reg.h (xf86-video-r128), checked against
; the Rage 128 PRO Register Reference Guide.

.386p

.xlist
include VMM.INC
include VWIN32.INC
.list

Declare_Virtual_Device R1MM, 1, 0, R1_Control, \
                       Undefined_Device_ID, Undefined_Init_Order, , ,

R1_MAGIC                equ 4d4d3152h ; "R1MM"
R1_DIOC_CAPTURE         equ 1
R1_VENDOR_ATI           equ 1002h
R1_CFG_BAR2             equ 18h
R1_MMIO_BYTES           equ 00004000h
R1_BASE_ALIGN           equ 00003fffh
R1_BAR_IO               equ 00000001h
R1_PCI_COPY             equ 0f00h
R1_OFFSET_MAX           equ 256

; Status bits returned to the publisher.
R1_ST_ATI               equ 00000001h
R1_ST_BASE_INVALID      equ 00000002h
R1_ST_MMIO_MAPPED       equ 00000004h
R1_ST_IDENTITY_MATCH    equ 00000008h
R1_ST_OFFSETS_REFUSED   equ 00000010h
R1_ST_SNAPSHOT_TAKEN    equ 00000020h
R1_ST_NOT_RAGE128       equ 00000080h
R1_ST_ADDRESS_INVALID   equ 00000100h

; Input from V9XR1MM.EXE.
R1_IN_CONFIG            equ 0
R1_IN_COUNT             equ 4
R1_IN_OFFSETS           equ 8
R1_IN_MIN_BYTES         equ 8

; Output layout, in dwords. Must match struct v9x_r1mm_result in
; r128_mmio_probe_win32.c.
R1_HEADER_DWORDS        equ 16
R1_RESULT_DWORDS        equ (R1_HEADER_DWORDS + R1_OFFSET_MAX * 3)

VxD_LOCKED_DATA_SEG
; The Rage 128 ids: Linux aty128fb's table, which X's r128_probe.c agrees
; with (build\external\rage128\RESEARCH.md section 1).
R1Devices label word
    dw 5245h, 5246h, 5247h, 524bh, 524ch
    dw 5345h, 5346h, 5347h, 5348h, 534bh, 534ch, 534dh, 534eh
    dw 5041h, 5042h, 5043h, 5044h, 5045h, 5046h, 5047h, 5048h
    dw 5049h, 504ah, 504bh, 504ch, 504dh, 504eh, 504fh, 5050h
    dw 5051h, 5052h, 5053h, 5054h, 5055h, 5056h, 5057h, 5058h
    dw 5446h, 544ch, 5452h, 5453h, 5454h, 5455h
    dw 4c45h, 4c46h, 4d46h, 4d4ch
R1_DEVICE_COUNT         equ 47

; Accepted windows: start, limit (exclusive).
R1Windows label dword
    dd 0008h, 000ch     ; CLOCK_CNTL_INDEX
    dd 0010h, 0060h     ; BIOS scratch, BUS_CNTL, GEN_INT, CRTC_GEN/EXT, DAC_CNTL, CRTC_STATUS
    dd 0068h, 0070h     ; GPIO_MONID, GPIO_MONIDB
    dd 0094h, 0098h     ; I2C_CNTL_1
    dd 00b0h, 00b4h     ; PALETTE_INDEX
    dd 00e0h, 00fch     ; CONFIG_CNTL, XSTRAP, BONDS, GEN_RESET_CNTL, CONFIG_MEMSIZE
    dd 0108h, 0118h     ; CONFIG_APER_SIZE, REG_APER_SIZE, MEMSIZE_EMBEDDED
    dd 0120h, 0124h     ; TEST_DEBUG_CNTL
    dd 0130h, 0134h     ; HOST_PATH_CNTL
    dd 0140h, 015ch     ; MEM_CNTL .. MEM_SDRAM_MODE_REG
    dd 0170h, 017ch     ; AGP_BASE, AGP_CNTL, AGP_APER_OFFSET
    dd 0184h, 0188h     ; PC_NGUI_CTLSTAT
    dd 0200h, 0230h     ; CRTC timings, VLINE, frame, OFFSET, OFFSET_CNTL, PITCH
    dd 0250h, 0258h     ; FP_CRTC totals
    dd 0260h, 0274h     ; cursor
    dd 0284h, 0294h     ; FP_GEN_CNTL, panel, stretch
    dd 02c4h, 02cch     ; FP sync
    dd 02d0h, 02d4h     ; LVDS_GEN_CNTL
    dd 02e0h, 02e8h     ; DDA_CONFIG, DDA_ON_OFF
    dd 0700h, 0720h     ; PM4_BUFFER_*, PM4_VC_FPU_SETUP
    dd 07a4h, 07ach     ; PM4_VC_DEBUG_CONFIG, PM4_VC_STAT
    dd 07b8h, 07bch     ; PM4_STAT
    dd 07f0h, 07f4h     ; PM4_BUFFER_ADDR
    dd 07fch, 0800h     ; PM4_MICRO_CNTL
    dd 0f00h, 0f60h     ; configuration copy, AGP status/command
    dd 1404h, 140ch     ; DST_OFFSET, DST_PITCH
    dd 1428h, 1430h     ; SRC_/DST_PITCH_OFFSET
    dd 146ch, 1470h     ; DP_GUI_MASTER_CNTL
    dd 147ch, 1480h     ; DP_BRUSH_FRGD_CLR
    dd 15d8h, 15dch     ; DP_SRC_FRGD_CLR
    dd 1660h, 1664h     ; AUX_SC_CNTL
    dd 16c0h, 16d0h     ; DP_CNTL, DP_DATATYPE, DP_MIX, DP_WRITE_MASK
    dd 16e0h, 16f4h     ; DEFAULT_OFFSET/PITCH/SC_BOTTOM_RIGHT, SC_TOP_LEFT/BOTTOM_RIGHT
    dd 1740h, 1744h     ; GUI_STAT
    dd 1748h, 174ch     ; PC_GUI_CTLSTAT
    dd 1a00h, 1a04h     ; SCALE_3D_CNTL
    dd 1bc4h, 1bc8h     ; SETUP_CNTL
    dd 1bcch, 1bd0h     ; WINDOW_XY_OFFSET
    dd 1c80h, 1d48h     ; the _C context set
R1_WINDOW_COUNT         equ 39

R1Result label dword
    dd R1_MAGIC                 ; 00 magic
    dd 0                        ; 01 status
    dd 0                        ; 02 id dword at configuration 00h
    dd 0                        ; 03 BAR2 as read
    dd 0                        ; 04 offsets accepted
    dd 0                        ; 05 id dword at 0F00h through the BAR
    dd 0                        ; 06 0CF8h found and restored
    dd 0                        ; 07 first offset refused, if any
    dd 0                        ; 08 configuration address used
    dd 0                        ; 09 reserved
    dd 0                        ; 10 reserved
    dd 0                        ; 11 reserved
    dd 0                        ; 12 reserved
    dd 0                        ; 13 reserved
    dd 0                        ; 14 reserved
    dd 0                        ; 15 reserved
R1Offsets    dd R1_OFFSET_MAX dup (0)
R1First      dd R1_OFFSET_MAX dup (0)
R1Second     dd R1_OFFSET_MAX dup (0)
R1Config     dd 0
R1Mmio       dd 0
VxD_LOCKED_DATA_ENDS

VxD_LOCKED_CODE_SEG

; EAX = PCI configuration address. Returns EAX = configuration dword.
; 0CF8h saved and restored with interrupts off.
BeginProc R1_Pci_Read
    push    ebx
    push    edx
    pushfd
    cli
    mov     ebx, eax
    mov     dx, 0cf8h
    in      eax, dx
    mov     R1Result[24], eax
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
EndProc R1_Pci_Read

; EDX = offset. Returns CF clear if it lies in an accepted window.
BeginProc R1_Offset_Allowed
    push    ecx
    push    esi
    test    edx, 3
    jnz     short R1_Offset_Refused
    mov     esi, OFFSET32 R1Windows
    mov     ecx, R1_WINDOW_COUNT
R1_Offset_Next:
    cmp     edx, [esi]
    jb      short R1_Offset_Skip
    cmp     edx, [esi+4]
    jb      short R1_Offset_Accepted
R1_Offset_Skip:
    add     esi, 8
    dec     ecx
    jnz     short R1_Offset_Next
R1_Offset_Refused:
    pop     esi
    pop     ecx
    stc
    ret
R1_Offset_Accepted:
    pop     esi
    pop     ecx
    clc
    ret
EndProc R1_Offset_Allowed

; AX = device id. Returns CF clear if it is in R1Devices.
BeginProc R1_Device_Known
    push    ecx
    push    esi
    mov     esi, OFFSET32 R1Devices
    mov     ecx, R1_DEVICE_COUNT
R1_Device_Next:
    cmp     ax, [esi]
    je      short R1_Device_Found
    add     esi, 2
    dec     ecx
    jnz     short R1_Device_Next
    pop     esi
    pop     ecx
    stc
    ret
R1_Device_Found:
    pop     esi
    pop     ecx
    clc
    ret
EndProc R1_Device_Known

; EBP = DIOC input buffer, ECX = its byte count. Copies the offset list,
; refusing the whole list if any entry is unaligned or outside the windows.
BeginProc R1_Accept_Offsets
    pushad
    mov     R1Result[16], 0
    sub     ecx, R1_IN_OFFSETS
    shr     ecx, 2
    mov     eax, [ebp+R1_IN_COUNT]
    cmp     eax, R1_OFFSET_MAX
    ja      short R1_Accept_Refused
    cmp     eax, ecx
    ja      short R1_Accept_Refused
    mov     ecx, eax
    lea     esi, [ebp+R1_IN_OFFSETS]
    mov     edi, OFFSET32 R1Offsets
    jecxz   short R1_Accept_Done
R1_Accept_Next:
    mov     edx, [esi]
    call    R1_Offset_Allowed
    jc      short R1_Accept_Refused_At
    mov     [edi], edx
    add     esi, 4
    add     edi, 4
    dec     ecx
    jnz     short R1_Accept_Next
R1_Accept_Done:
    mov     eax, [ebp+R1_IN_COUNT]
    mov     R1Result[16], eax
    popad
    ret
R1_Accept_Refused_At:
    mov     R1Result[28], edx
R1_Accept_Refused:
    or      R1Result[4], R1_ST_OFFSETS_REFUSED
    popad
    ret
EndProc R1_Accept_Offsets

; EDI = destination. Reads every accepted offset through the mapping.
BeginProc R1_Read_Snapshot
    pushad
    mov     ecx, R1Result[16]
    mov     ebx, OFFSET32 R1Offsets
    mov     esi, R1Mmio
    jecxz   short R1_Read_Snapshot_Done
R1_Read_Snapshot_Next:
    mov     eax, [ebx]
    mov     eax, [esi+eax]
    mov     [edi], eax
    add     ebx, 4
    add     edi, 4
    dec     ecx
    jnz     short R1_Read_Snapshot_Next
R1_Read_Snapshot_Done:
    popad
    ret
EndProc R1_Read_Snapshot

BeginProc R1_Capture
    pushad

    ; A configuration address: enable bit set, register field zero, and
    ; nothing in the reserved bits 30:24.
    mov     eax, R1Config
    mov     R1Result[32], eax
    test    eax, 80000000h
    jz      R1_Capture_Bad_Address
    test    eax, 7f0000ffh
    jnz     R1_Capture_Bad_Address

    call    R1_Pci_Read
    mov     R1Result[8], eax
    cmp     ax, R1_VENDOR_ATI
    jne     R1_Capture_Bad_Base
    or      R1Result[4], R1_ST_ATI
    shr     eax, 16
    call    R1_Device_Known
    jnc     short R1_Capture_Check_Base
    or      R1Result[4], R1_ST_NOT_RAGE128
    jmp     R1_Capture_Done

R1_Capture_Check_Base:
    mov     eax, R1Config
    or      eax, R1_CFG_BAR2
    call    R1_Pci_Read
    mov     R1Result[12], eax
    test    eax, R1_BAR_IO
    jnz     R1_Capture_Bad_Base
    and     eax, NOT 0fh
    jz      R1_Capture_Bad_Base
    test    eax, R1_BASE_ALIGN
    jnz     R1_Capture_Bad_Base

    VMMcall _MapPhysToLinear,<eax,R1_MMIO_BYTES,0>
    cmp     eax, 0ffffffffh
    je      R1_Capture_Done
    mov     R1Mmio, eax
    or      R1Result[4], R1_ST_MMIO_MAPPED

    mov     edx, [eax+R1_PCI_COPY]
    mov     R1Result[20], edx
    cmp     edx, R1Result[8]
    jne     short R1_Capture_Done
    or      R1Result[4], R1_ST_IDENTITY_MATCH

    mov     edi, OFFSET32 R1First
    call    R1_Read_Snapshot
    mov     edi, OFFSET32 R1Second
    call    R1_Read_Snapshot
    or      R1Result[4], R1_ST_SNAPSHOT_TAKEN
    jmp     short R1_Capture_Done

R1_Capture_Bad_Address:
    or      R1Result[4], R1_ST_ADDRESS_INVALID
    jmp     short R1_Capture_Done
R1_Capture_Bad_Base:
    or      R1Result[4], R1_ST_BASE_INVALID
R1_Capture_Done:
    popad
    ret
EndProc R1_Capture

BeginProc R1_Reset_Result
    pushad
    mov     edi, OFFSET32 R1Result + 4
    mov     ecx, R1_RESULT_DWORDS - 1
    xor     eax, eax
    cld
    rep     stosd
    mov     R1Mmio, eax
    popad
    ret
EndProc R1_Reset_Result

BeginProc R1_W32_DeviceIoControl
    cmp     ecx, DIOC_OPEN
    je      R1_Dioc_Ok
    cmp     ecx, DIOC_CLOSEHANDLE
    je      R1_Dioc_Ok
    cmp     ecx, R1_DIOC_CAPTURE
    jne     R1_Dioc_Fail

    pushad
    mov     ebx, esi
    mov     ebp, [ebx.lpvInBuffer]
    test    ebp, ebp
    jz      short R1_Dioc_Copy_Fail
    mov     ecx, [ebx.cbInBuffer]
    cmp     ecx, R1_IN_MIN_BYTES
    jb      short R1_Dioc_Copy_Fail
    mov     edi, [ebx.lpvOutBuffer]
    test    edi, edi
    jz      short R1_Dioc_Copy_Fail
    cmp     [ebx.cbOutBuffer], R1_RESULT_DWORDS * 4
    jb      short R1_Dioc_Copy_Fail

    call    R1_Reset_Result
    mov     eax, [ebp+R1_IN_CONFIG]
    mov     R1Config, eax
    call    R1_Accept_Offsets
    test    R1Result[4], R1_ST_OFFSETS_REFUSED
    jnz     short R1_Dioc_Copy_Out
    call    R1_Capture

R1_Dioc_Copy_Out:
    mov     edi, [ebx.lpvOutBuffer]
    mov     esi, OFFSET32 R1Result
    mov     ecx, R1_RESULT_DWORDS
    cld
    rep     movsd
    mov     eax, [ebx.lpcbBytesReturned]
    test    eax, eax
    jz      short R1_Dioc_Copy_Done
    mov     dword ptr [eax], R1_RESULT_DWORDS * 4
R1_Dioc_Copy_Done:
    popad
R1_Dioc_Ok:
    xor     eax, eax
    ret

R1_Dioc_Copy_Fail:
    popad
R1_Dioc_Fail:
    mov     eax, 1
    ret
EndProc R1_W32_DeviceIoControl

BeginProc R1_Dynamic_Init
    clc
    ret
EndProc R1_Dynamic_Init

BeginProc R1_Dynamic_Exit
    clc
    ret
EndProc R1_Dynamic_Exit

Begin_Control_Dispatch R1
    Control_Dispatch Sys_Dynamic_Device_Init, R1_Dynamic_Init
    Control_Dispatch Sys_Dynamic_Device_Exit, R1_Dynamic_Exit
    Control_Dispatch W32_DeviceIoControl, R1_W32_DeviceIoControl
End_Control_Dispatch R1

VxD_LOCKED_CODE_ENDS

end
