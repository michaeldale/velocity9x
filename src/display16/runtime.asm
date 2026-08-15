; Original Velocity9x Win16 DIB Engine and framebuffer runtime glue.
;
; The DIB Engine names are supplied by the external Windows 98 DDK import
; library. Hardware bring-up is restricted to an audited VBE mode table and a
; validated framebuffer aperture. S3 reads CR59/CR5A; the guarded Matrox
; Millennium II build reads the direct-framebuffer PCI BAR without touching
; any Matrox MMIO register.

.model compact
.386p

.data
EXTRN _v9x_active_vbe_mode:WORD
EXTRN _v9x_active_visible_bytes:DWORD
EXTRN _v9x_active_width:WORD
EXTRN _v9x_active_pitch:WORD
; Stamped from the family's v9x_hw16 table at load time (ddi.c). Keeping the
; PCI identity, the VBE mode-set flags and the aperture size out of the
; instruction stream is what lets one binary serve more than one card.
EXTRN _v9x_pci_vendor:WORD
EXTRN _v9x_pci_device:WORD
EXTRN _v9x_pci_count:WORD
EXTRN _v9x_vbe_mode_flags:WORD
EXTRN _v9x_map_pages_hi:WORD
EXTRN _v9x_map_pages_lo:WORD
V9xScreenSelector dw 0
V9xLinearAddress  dd 0
V9xPhysicalBase   dd 0
V9xEnableResult   dw 0
V9xVddEntryPoint dd 0
V9xVmHandle       dw 0
V9xVddRegistered dw 0
V9xHardwareStageCode dw 0
V9xCreateDibReturn dd 0
V9xDdSharedSel   dw 0
V9xDdSharedLin   dd 0

; DirectDraw shared-block size: sizeof(V9X_DD_SHARED) rounded up.
; Must match the v9x_dd_assert_shared_fits_dpmi_block bound in
; include/velocity9x/win9x_ddraw_abi.h.
V9X_DD_SHARED_BYTES EQU 4096

.code

EXTRN DIB_Enable:FAR
EXTRN CreateDIBPDevice:FAR
EXTRN DIB_BeginAccess:FAR
EXTRN DIB_EndAccess:FAR
EXTRN DIB_SetPaletteExt:FAR
EXTRN DIB_SetPaletteTranslateExt:FAR
EXTRN RESETHIRESMODE:FAR

VDD_DEVICE_ID          EQU 000ah
VDD_DRIVER_REGISTER    EQU 0080h
VDD_DRIVER_UNREGISTER  EQU 0081h
VDD_SAVE_DRIVER_STATE  EQU 0082h
VDD_GET_DISPLAY_CONFIG EQU 0085h
VDD_PRE_MODE_CHANGE    EQU 0086h
VDD_POST_MODE_CHANGE   EQU 0087h
STOP_IO_TRAP           EQU 4000h
START_IO_TRAP          EQU 4007h

PUBLIC V9XDIBENABLECALL
V9XDIBENABLECALL PROC FAR
    jmp DIB_Enable
V9XDIBENABLECALL ENDP

PUBLIC V9XCREATEDIBPDEVICECALL
V9XCREATEDIBPDEVICECALL PROC FAR
    ; CreateDIBPDevice is an unusual Win16 API: DIBENG returns its DWORD in
    ; EAX, while Open Watcom's 16-bit C ABI expects DWORD results in DX:AX.
    ; Convert explicitly; a transparent jump can turn a valid selector:0
    ; result into a false zero depending on the stale value in DX.
    ; Remove this wrapper's far return address so DIBENG sees the original
    ; Pascal argument frame immediately below its own return address.
    pop  dword ptr V9xCreateDibReturn
    call CreateDIBPDevice
    push dword ptr V9xCreateDibReturn
    mov  dx, ax
    shr  eax, 16
    xchg ax, dx
    retf
V9XCREATEDIBPDEVICECALL ENDP

PUBLIC V9XDIBBEGINACCESS
V9XDIBBEGINACCESS PROC FAR
    jmp DIB_BeginAccess
V9XDIBBEGINACCESS ENDP

PUBLIC V9XDIBENDACCESS
V9XDIBENDACCESS PROC FAR
    jmp DIB_EndAccess
V9XDIBENDACCESS ENDP

; Typed entry points for cursor exclusion around a live mode switch. They
; are the same DIBENG routines as the deBeginAccess/deEndAccess pointers,
; re-exported so C code can call them with the full argument list.
; Typed forward of the DIB engine Control handler for the C escape
; dispatcher in dd16.c.
EXTRN DIB_Control:FAR
PUBLIC V9XDIBCONTROLCALL
V9XDIBCONTROLCALL PROC FAR
    jmp DIB_Control
V9XDIBCONTROLCALL ENDP

; Return the linear address of the mapped framebuffer aperture in DX:AX.
PUBLIC V9XLINEARBASE
V9XLINEARBASE PROC FAR
    mov     ax, word ptr V9xLinearAddress
    mov     dx, word ptr V9xLinearAddress+2
    retf
V9XLINEARBASE ENDP

; Allocate the DirectDraw shared block once: DPMI linear memory plus one
; LDT descriptor addressing it. Returns the selector in AX (0 on failure).
PUBLIC V9XDDSHAREDALLOC
V9XDDSHAREDALLOC PROC FAR
    push    bx
    push    cx
    push    dx
    push    si
    push    di

    cmp     V9xDdSharedSel, 0
    jne     short V9xDdSharedReady

    mov     bx, 0
    mov     cx, V9X_DD_SHARED_BYTES
    mov     ax, 0501h
    int     31h
    jc      short V9xDdSharedFailed
    mov     word ptr V9xDdSharedLin, cx
    mov     word ptr V9xDdSharedLin+2, bx

    xor     ax, ax
    mov     cx, 1
    int     31h
    jc      short V9xDdSharedFailed
    mov     V9xDdSharedSel, ax

    mov     bx, ax
    mov     dx, word ptr V9xDdSharedLin
    mov     cx, word ptr V9xDdSharedLin+2
    mov     ax, 0007h
    int     31h
    jc      short V9xDdSharedFreeSelector

    mov     bx, V9xDdSharedSel
    xor     cx, cx
    mov     dx, V9X_DD_SHARED_BYTES - 1
    mov     ax, 0008h
    int     31h
    jc      short V9xDdSharedFreeSelector

V9xDdSharedReady:
    mov     ax, V9xDdSharedSel
    jmp     short V9xDdSharedDone

V9xDdSharedFreeSelector:
    mov     bx, V9xDdSharedSel
    mov     ax, 0001h
    int     31h
    mov     V9xDdSharedSel, 0
V9xDdSharedFailed:
    xor     ax, ax
V9xDdSharedDone:
    pop     di
    pop     si
    pop     dx
    pop     cx
    pop     bx
    retf
V9XDDSHAREDALLOC ENDP

; Return the linear address of the DirectDraw shared block in DX:AX.
PUBLIC V9XDDSHAREDLINEAR
V9XDDSHAREDLINEAR PROC FAR
    mov     ax, word ptr V9xDdSharedLin
    mov     dx, word ptr V9xDdSharedLin+2
    retf
V9XDDSHAREDLINEAR ENDP

PUBLIC V9XDIBBEGINACCESSRECT
V9XDIBBEGINACCESSRECT PROC FAR
    jmp DIB_BeginAccess
V9XDIBBEGINACCESSRECT ENDP

PUBLIC V9XDIBENDACCESSRECT
V9XDIBENDACCESSRECT PROC FAR
    jmp DIB_EndAccess
V9XDIBENDACCESSRECT ENDP

PUBLIC V9XDIBSETPALETTECALL
V9XDIBSETPALETTECALL PROC FAR
    jmp DIB_SetPaletteExt
V9XDIBSETPALETTECALL ENDP

PUBLIC V9XDIBSETPALETTETRANSLATECALL
V9XDIBSETPALETTETRANSLATECALL PROC FAR
    jmp DIB_SetPaletteTranslateExt
V9XDIBSETPALETTETRANSLATECALL ENDP

V9xVddInitialize PROC NEAR
    cmp     V9xVddEntryPoint, 0
    jne     short V9xVddInitializeReady

    mov     ax, 1684h
    mov     bx, VDD_DEVICE_ID
    int     2fh
    mov     word ptr V9xVddEntryPoint, di
    mov     word ptr V9xVddEntryPoint+2, es
    mov     ax, es
    or      ax, di
    jz      short V9xVddInitializeFailed

    mov     ax, 1683h
    int     2fh
    mov     V9xVmHandle, bx

V9xVddInitializeReady:
    mov     ax, 1
    ret
V9xVddInitializeFailed:
    mov     V9xVddEntryPoint, 0
    xor     ax, ax
    ret
V9xVddInitialize ENDP

PUBLIC V9XVDDGETDISPLAYCONFIG
V9XVDDGETDISPLAYCONFIG PROC FAR
    push    bp
    mov     bp, sp
    push    bx
    push    cx
    push    dx
    push    esi
    push    edi
    push    es

    call    V9xVddInitialize
    or      ax, ax
    jz      short V9xVddGetConfigFailed
    movzx   edi, word ptr [bp+6]
    mov     es, word ptr [bp+8]
    mov     eax, VDD_GET_DISPLAY_CONFIG
    movzx   ebx, V9xVmHandle
    mov     ecx, 34
    xor     edx, edx
    call    dword ptr V9xVddEntryPoint
    cmp     eax, VDD_GET_DISPLAY_CONFIG
    je      short V9xVddGetConfigFailed
    inc     eax
    jz      short V9xVddGetConfigFailed
    mov     ax, 1
    jmp     short V9xVddGetConfigDone
V9xVddGetConfigFailed:
    xor     ax, ax
V9xVddGetConfigDone:
    pop     es
    pop     edi
    pop     esi
    pop     dx
    pop     cx
    pop     bx
    pop     bp
    retf    4
V9XVDDGETDISPLAYCONFIG ENDP

PUBLIC V9XVDDPREMODE
V9XVDDPREMODE PROC FAR
    push    bx
    push    cx
    push    dx
    push    si
    push    di
    push    es

    call    V9xVddInitialize
    or      ax, ax
    jz      short V9xVddPreModeFailed
    mov     eax, VDD_PRE_MODE_CHANGE
    movzx   ebx, V9xVmHandle
    call    dword ptr V9xVddEntryPoint
    mov     ax, 1
    jmp     short V9xVddPreModeDone
V9xVddPreModeFailed:
    xor     ax, ax
V9xVddPreModeDone:
    pop     es
    pop     di
    pop     si
    pop     dx
    pop     cx
    pop     bx
    retf
V9XVDDPREMODE ENDP

PUBLIC V9XVDDREGISTER
V9XVDDREGISTER PROC FAR
    push    bx
    push    cx
    push    dx
    push    di
    push    es

    cmp     V9xVddRegistered, 0
    jne     short V9xVddRegisterReady
    call    V9xVddInitialize
    or      ax, ax
    jz      short V9xVddRegisterFailed

    mov     ax, STOP_IO_TRAP
    int     2fh

    ; Load ES:DI before EAX: the SEG fixup goes through AX and would
    ; otherwise overwrite the low word of the service code (the working
    ; vmdisp9x reference loads the callback pointer first for this reason).
    mov     ax, SEG RESETHIRESMODE
    mov     es, ax
    mov     di, OFFSET RESETHIRESMODE
    mov     eax, VDD_DRIVER_REGISTER
    movzx   ebx, V9xVmHandle
    mov     ecx, _v9x_active_visible_bytes
    xor     edx, edx
    call    dword ptr V9xVddEntryPoint
    cmp     eax, VDD_DRIVER_REGISTER
    je      short V9xVddRegisterRestartTrap

    mov     V9xVddRegistered, 1
    mov     eax, VDD_POST_MODE_CHANGE
    movzx   ebx, V9xVmHandle
    call    dword ptr V9xVddEntryPoint
    mov     eax, VDD_SAVE_DRIVER_STATE
    movzx   ebx, V9xVmHandle
    call    dword ptr V9xVddEntryPoint

V9xVddRegisterReady:
    mov     ax, 1
    jmp     short V9xVddRegisterDone

V9xVddRegisterRestartTrap:
    mov     ax, START_IO_TRAP
    int     2fh
V9xVddRegisterFailed:
    xor     ax, ax
V9xVddRegisterDone:
    pop     es
    pop     di
    pop     dx
    pop     cx
    pop     bx
    retf
V9XVDDREGISTER ENDP

; Re-issue VDD_DRIVER_REGISTER with the new visible-byte count during a live
; mode switch. The VDD accepts a repeated registration and updates its
; save/restore state; I/O trapping stays stopped because the driver never
; released the display. Fails when the driver is not registered yet.
PUBLIC V9XVDDREREGISTER
V9XVDDREREGISTER PROC FAR
    push    bx
    push    cx
    push    dx
    push    di
    push    es

    cmp     V9xVddRegistered, 0
    je      short V9xVddReregisterFailed

    mov     ax, SEG RESETHIRESMODE
    mov     es, ax
    mov     di, OFFSET RESETHIRESMODE
    mov     eax, VDD_DRIVER_REGISTER
    movzx   ebx, V9xVmHandle
    mov     ecx, _v9x_active_visible_bytes
    xor     edx, edx
    call    dword ptr V9xVddEntryPoint
    cmp     eax, VDD_DRIVER_REGISTER
    je      short V9xVddReregisterFailed

    mov     eax, VDD_POST_MODE_CHANGE
    movzx   ebx, V9xVmHandle
    call    dword ptr V9xVddEntryPoint
    mov     eax, VDD_SAVE_DRIVER_STATE
    movzx   ebx, V9xVmHandle
    call    dword ptr V9xVddEntryPoint

    mov     ax, 1
    jmp     short V9xVddReregisterDone
V9xVddReregisterFailed:
    xor     ax, ax
V9xVddReregisterDone:
    pop     es
    pop     di
    pop     dx
    pop     cx
    pop     bx
    retf
V9XVDDREREGISTER ENDP

PUBLIC V9XVDDPOSTMODE
V9XVDDPOSTMODE PROC FAR
    push    bx
    cmp     V9xVddRegistered, 0
    je      short V9xVddPostModeDone
    mov     eax, VDD_POST_MODE_CHANGE
    movzx   ebx, V9xVmHandle
    call    dword ptr V9xVddEntryPoint
    mov     eax, VDD_SAVE_DRIVER_STATE
    movzx   ebx, V9xVmHandle
    call    dword ptr V9xVddEntryPoint
V9xVddPostModeDone:
    pop     bx
    retf
V9XVDDPOSTMODE ENDP

PUBLIC V9XVDDUNREGISTER
V9XVDDUNREGISTER PROC FAR
    push    bx
    cmp     V9xVddRegistered, 0
    je      short V9xVddUnregisterDone
    mov     ax, START_IO_TRAP
    int     2fh
    mov     eax, VDD_DRIVER_UNREGISTER
    movzx   ebx, V9xVmHandle
    call    dword ptr V9xVddEntryPoint
    mov     V9xVddRegistered, 0
V9xVddUnregisterDone:
    pop     bx
    retf
V9XVDDUNREGISTER ENDP

V9xSetVbeMode PROC NEAR
    mov     ax, 4f02h
    mov     bx, _v9x_active_vbe_mode
    ; 8000h (S3/VBE no-clear) or 4000h (generic linear framebuffer), chosen by
    ; the family table rather than by a build-time define.
    or      bx, _v9x_vbe_mode_flags
    int     10h
    cmp     ax, 004fh
    jne     short V9xSetVbeModeFailed
    mov     ax, 1
    ret
V9xSetVbeModeFailed:
    xor     ax, ax
    ret
V9xSetVbeMode ENDP

IFDEF V9X_TARGET_MATROX_MILLENNIUM2
V9xSetMatroxScanLinePitch PROC NEAR
    ; VBE 4F06h subfunction 00h sets the logical scan-line length in pixels.
    ; Force the packed pitch selected by the audited mode table. Reject a BIOS
    ; that silently selects a different stride.
    mov     ax, 4f06h
    xor     bx, bx
    mov     cx, _v9x_active_width
    int     10h
    cmp     ax, 004fh
    jne     short V9xSetMatroxScanLinePitchFailed
    cmp     bx, _v9x_active_pitch
    jne     short V9xSetMatroxScanLinePitchFailed
    mov     ax, 1
    ret
V9xSetMatroxScanLinePitchFailed:
    xor     ax, ax
    ret
V9xSetMatroxScanLinePitch ENDP
ENDIF

; Walk the family's device list and stop at the first card present.
;
; PCI BIOS B102h returns the bus and device/function in BH/BL, and
; V9xReadMatroxAperture reads that straight after calling here, so BX must
; carry the successful call's result out. DI is the table cursor and is saved
; and restored; SI is zeroed for the call, as it always was.
V9xFindPciDevice PROC NEAR
    push    di
    xor     di, di
V9xFindPciDeviceNext:
    mov     ax, _v9x_pci_count
    shl     ax, 1
    cmp     di, ax
    jae     short V9xFindPciDeviceFailed
    mov     cx, _v9x_pci_device[di]
    mov     dx, _v9x_pci_vendor[di]
    mov     ax, 0b102h
    xor     si, si
    int     1ah
    jc      short V9xFindPciDeviceTryNext
    or      ah, ah
    je      short V9xFindPciDeviceFound
V9xFindPciDeviceTryNext:
    add     di, 2
    jmp     short V9xFindPciDeviceNext
V9xFindPciDeviceFound:
    pop     di
    mov     ax, 1
    ret
V9xFindPciDeviceFailed:
    pop     di
    xor     ax, ax
    ret
V9xFindPciDevice ENDP

PUBLIC V9XHARDWAREPRESENT
V9XHARDWAREPRESENT PROC FAR
    push    bx
    push    cx
    push    dx
    push    si
    call    V9xFindPciDevice
    pop     si
    pop     dx
    pop     cx
    pop     bx
    retf
V9XHARDWAREPRESENT ENDP

IFNDEF V9X_TARGET_MATROX_MILLENNIUM2
V9xReadS3Aperture PROC NEAR
    mov     dx, 03d4h
    mov     ax, 4838h
    out     dx, ax
    mov     ax, 0a039h
    out     dx, ax

    mov     al, 59h
    out     dx, al
    inc     dx
    in      al, dx
    mov     bh, al
    dec     dx
    mov     al, 5ah
    out     dx, al
    inc     dx
    in      al, dx
    mov     bl, al

    xor     eax, eax
    mov     ax, bx
    shl     eax, 16
    cmp     eax, 01000000h
    jb      short V9xReadS3ApertureFailed
    cmp     eax, 0ffc00000h
    ja      short V9xReadS3ApertureFailed
    ret
V9xReadS3ApertureFailed:
    xor     eax, eax
    ret
V9xReadS3Aperture ENDP
ENDIF

IFDEF V9X_TARGET_MATROX_MILLENNIUM2
V9xReadMatroxAperture PROC NEAR
    ; PCI BIOS B102h leaves the selected bus/device-function in BH/BL. BAR0
    ; (configuration offset 10h) is MGABASE2, the direct framebuffer aperture.
    call    V9xFindPciDevice
    or      ax, ax
    jz      short V9xReadMatroxApertureFailed
    mov     ax, 0b10ah
    mov     di, 0010h
    int     1ah
    jc      short V9xReadMatroxApertureFailed
    or      ah, ah
    jne     short V9xReadMatroxApertureFailed
    test    cl, 1
    jnz     short V9xReadMatroxApertureFailed
    mov     eax, ecx
    and     eax, 0fffffff0h
    cmp     eax, 01000000h
    jb      short V9xReadMatroxApertureFailed
    cmp     eax, 0fe000000h
    ja      short V9xReadMatroxApertureFailed
    test    eax, 00ffffffh
    jnz     short V9xReadMatroxApertureFailed
    ret
V9xReadMatroxApertureFailed:
    xor     eax, eax
    ret
V9xReadMatroxAperture ENDP

ENDIF

IFNDEF V9X_TARGET_MATROX_MILLENNIUM2
V9xEnableS3LinearAperture PROC NEAR
    ; Unlock the S3 system-extension registers, select a 4-MiB aperture in
    ; CR58[1:0], and explicitly enable linear addressing in CR58[4]. This is
    ; the sequence used by the Windows 98 S3 display sample after 4F02h.
    mov     dx, 03d4h
    mov     ax, 4838h
    out     dx, ax
    mov     ax, 0a039h
    out     dx, ax

    mov     al, 58h
    out     dx, al
    inc     dx
    in      al, dx
    and     al, 0fch
    or      al, 13h
    out     dx, al
    in      al, dx
    and     al, 13h
    cmp     al, 13h
    jne     short V9xEnableS3LinearApertureFailed

    ; Enable the S3 graphics engine and the ViRGE "new MMIO" window. Without
    ; CR40[0] and CR53[3], offsets such as 8504h/A500h address framebuffer
    ; memory rather than engine registers.
    dec     dx
    mov     al, 40h
    out     dx, al
    inc     dx
    in      al, dx
    or      al, 01h
    out     dx, al
    in      al, dx
    test    al, 01h
    jz      short V9xEnableS3LinearApertureFailed

IFNDEF V9X_TARGET_S3_TRIO64
    dec     dx
    mov     al, 53h
    out     dx, al
    inc     dx
    in      al, dx
    or      al, 08h
    out     dx, al
    in      al, dx
    test    al, 08h
    jz      short V9xEnableS3LinearApertureFailed
ENDIF
    mov     ax, 1
    ret
V9xEnableS3LinearApertureFailed:
    xor     ax, ax
    ret
V9xEnableS3LinearAperture ENDP
ENDIF

PUBLIC V9XHARDWAREENABLE
V9XHARDWAREENABLE PROC FAR
    push    bx
    push    cx
    push    dx
    push    si
    push    di
    push    es

    mov     V9xEnableResult, 0
    mov     V9xHardwareStageCode, 1
    call    V9xFindPciDevice
    or      ax, ax
    jnz     short V9xHardwareDeviceFound
    jmp     V9xHardwareEnableDone
V9xHardwareDeviceFound:
    mov     V9xHardwareStageCode, 2
    call    V9xSetVbeMode
    or      ax, ax
    jnz     short V9xHardwareModeSet
    jmp     V9xHardwareEnableDone
V9xHardwareModeSet:
IFDEF V9X_TARGET_MATROX_MILLENNIUM2
    mov     V9xHardwareStageCode, 9
    call    V9xSetMatroxScanLinePitch
    or      ax, ax
    jnz     short V9xHardwarePitchSet
    jmp     V9xHardwareEnableDone
V9xHardwarePitchSet:
ENDIF
    mov     V9xHardwareStageCode, 3
IFDEF V9X_TARGET_MATROX_MILLENNIUM2
    call    V9xReadMatroxAperture
ELSE
    call    V9xReadS3Aperture
ENDIF
    or      eax, eax
    jnz     short V9xHardwareBaseValid
    jmp     V9xHardwareEnableDone
V9xHardwareBaseValid:
IFDEF V9X_TARGET_MATROX_MILLENNIUM2
    ; VBE 4F02h enabled the linear framebuffer. Do not write MGA control
    ; registers during this conservative first activation.
    mov     V9xHardwareStageCode, 4
ELSE
    mov     V9xHardwareStageCode, 8
    push    eax
    call    V9xEnableS3LinearAperture
    mov     dx, ax
    pop     eax
    or      dx, dx
    jnz     short V9xHardwareApertureEnabled
    jmp     V9xHardwareEnableDone
V9xHardwareApertureEnabled:
    mov     V9xHardwareStageCode, 4
ENDIF

    cmp     V9xScreenSelector, 0
    je      short V9xHardwareAllocate
    cmp     eax, V9xPhysicalBase
    je      short V9xHardwareReuse
    jmp     V9xHardwareEnableDone
V9xHardwareReuse:
    mov     V9xHardwareStageCode, 0
    mov     ax, V9xScreenSelector
    mov     V9xEnableResult, ax
    jmp     V9xHardwareEnableDone

V9xHardwareAllocate:
    mov     V9xPhysicalBase, eax
    xor     ax, ax
    mov     cx, 1
    int     31h
    jnc     short V9xHardwareSelectorAllocated
    jmp     V9xHardwareMapFailed
V9xHardwareSelectorAllocated:
    mov     V9xHardwareStageCode, 5
    mov     V9xScreenSelector, ax

    mov     eax, V9xPhysicalBase
    mov     ebx, eax
    shr     ebx, 16
    mov     cx, ax
    ; Aperture size in pages, from the family table. Every family maps the
    ; complete 64-MiB PCI BAR today: the first 4 MiB is allocatable VRAM and
    ; the ViRGE new-MMIO window is at BAR + 16 MiB.
    mov     si, _v9x_map_pages_hi
    mov     di, _v9x_map_pages_lo
    mov     ax, 0800h
    int     31h
    jc      short V9xHardwareFreeSelector
    mov     word ptr V9xLinearAddress, cx
    mov     word ptr V9xLinearAddress+2, bx
    mov     V9xHardwareStageCode, 6

    mov     bx, V9xScreenSelector
    mov     dx, word ptr V9xLinearAddress
    mov     cx, word ptr V9xLinearAddress+2
    mov     ax, 0007h
    int     31h
    jc      short V9xHardwareUnmap
    mov     V9xHardwareStageCode, 7

    mov     bx, V9xScreenSelector
    mov     cx, _v9x_map_pages_hi
    mov     dx, _v9x_map_pages_lo
    mov     ax, 0008h
    int     31h
    jc      short V9xHardwareUnmap

    mov     V9xHardwareStageCode, 0
    mov     ax, V9xScreenSelector
    mov     V9xEnableResult, ax
    jmp     short V9xHardwareEnableDone

V9xHardwareUnmap:
    mov     bx, word ptr V9xLinearAddress+2
    mov     cx, word ptr V9xLinearAddress
    mov     ax, 0801h
    int     31h
    mov     V9xLinearAddress, 0

V9xHardwareFreeSelector:
    mov     bx, V9xScreenSelector
    mov     ax, 0001h
    int     31h
    mov     V9xScreenSelector, 0

V9xHardwareMapFailed:
    mov     V9xPhysicalBase, 0

V9xHardwareEnableDone:
    pop     es
    pop     di
    pop     si
    pop     dx
    pop     cx
    pop     bx
    mov     ax, V9xEnableResult
    retf
V9XHARDWAREENABLE ENDP

PUBLIC V9XHARDWARESTAGE
V9XHARDWARESTAGE PROC FAR
    mov     ax, V9xHardwareStageCode
    retf
V9XHARDWARESTAGE ENDP

PUBLIC V9XHARDWARERESET
V9XHARDWARERESET PROC FAR
    push    bx
    call    V9xSetVbeMode
IFDEF V9X_TARGET_MATROX_MILLENNIUM2
    or      ax, ax
    jz      short V9xHardwareResetDone
    call    V9xSetMatroxScanLinePitch
    or      ax, ax
    jz      short V9xHardwareResetDone
V9xHardwareResetDone:
ELSE
    or      ax, ax
    jz      short V9xHardwareResetDone
    call    V9xEnableS3LinearAperture
V9xHardwareResetDone:
ENDIF
    pop     bx
    retf
V9XHARDWARERESET ENDP

PUBLIC V9XHARDWAREBASE
V9XHARDWAREBASE PROC FAR
    mov     ax, word ptr V9xPhysicalBase
    mov     dx, word ptr V9xPhysicalBase+2
    retf
V9XHARDWAREBASE ENDP

; Return the framebuffer to VGA text mode, but keep the LDT descriptor and
; the linear mapping that address it.
;
; Freeing the descriptor here used to change the framebuffer selector across
; every Disable/Enable cycle. The DIB Engine caches that selector inside the
; PDEVICE it builds, and it does not reacquire it on a later Enable, so once
; the value changed the engine was writing through a descriptor that had been
; returned to the LDT and could since belong to anything. That is the fault
; Hellbender hit: a general protection fault inside DIBENG's cursor code with
; ES holding the previous selector value. See
; docs/issues/2026-08-14-hellbender-dibeng-gpf.md.
;
; Enable already reuses a live selector (V9xHardwareReuse), and it re-enters
; the VBE mode and re-enables the linear aperture before that point, so
; holding one descriptor and one mapping for the driver's lifetime keeps the
; value stable without changing what Enable does.
PUBLIC V9XHARDWAREDISABLE
V9XHARDWAREDISABLE PROC FAR
    push    bx
    push    cx
    push    dx

    mov     ax, 0003h
    int     10h

    pop     dx
    pop     cx
    pop     bx
    retf
V9XHARDWAREDISABLE ENDP

END
