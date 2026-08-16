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
EXTRN _v9x_pci_match:WORD
EXTRN _v9x_vbe_mode_flags:WORD
EXTRN _v9x_map_pages_hi:WORD
EXTRN _v9x_map_pages_lo:WORD
; Shared with enable16.c: it owns stages 1-3, 8 and 9 and V9XMAPAPERTURE owns
; 4-7, so the numbered sequence stays one variable.
EXTRN _v9x_hardware_stage_code:WORD
EXTRN _v9x_map_physical_base:DWORD
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
    ; Record which entry answered. DI is the byte offset into the parallel
    ; vendor/device arrays, so the index is DI/2. A family with more than one
    ; chip needs this to know whose hooks to call; a single-chip family lands
    ; on 0 and behaves exactly as before.
    shr     di, 1
    mov     _v9x_pci_match, di
    pop     di
    mov     ax, 1
    ret
V9xFindPciDeviceFailed:
    mov     _v9x_pci_match, 0FFFFh
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

; Read and validate PCI BAR0 of the family's card into the DWORD the caller
; points at. Returns 1 on success, 0 on refusal.
;
; This stays in assembly because PCI BIOS B10Ah returns its result in ECX and
; the validation masks are 32-bit, while the C is compiled for 8086. It is a
; chip-agnostic INT 1Ah primitive: which card it reads comes from the family's
; device list, not from a build-time define.
;
; A base below 16 MiB, above FE000000h, or not aligned to 16 MiB is a read
; that went wrong rather than an unusual slot, and is refused.
PUBLIC V9XPCIREADBAR0
V9XPCIREADBAR0 PROC FAR
    push    bp
    mov     bp, sp
    push    bx
    push    cx
    push    dx
    push    si
    push    di
    push    es

    call    V9xFindPciDevice
    or      ax, ax
    jz      short V9xPciReadBar0Failed

    mov     ax, 0b10ah
    mov     di, 0010h
    int     1ah
    jc      short V9xPciReadBar0Failed
    or      ah, ah
    jne     short V9xPciReadBar0Failed
    ; Bit 0 set marks an I/O BAR; this must be the memory aperture.
    test    cl, 1
    jnz     short V9xPciReadBar0Failed

    mov     eax, ecx
    and     eax, 0fffffff0h
    cmp     eax, 01000000h
    jb      short V9xPciReadBar0Failed
    cmp     eax, 0fe000000h
    ja      short V9xPciReadBar0Failed
    test    eax, 00ffffffh
    jnz     short V9xPciReadBar0Failed

    les     bx, dword ptr 6[bp]
    mov     es:[bx], eax
    mov     ax, 1
    jmp     short V9xPciReadBar0Done

V9xPciReadBar0Failed:
    xor     ax, ax
V9xPciReadBar0Done:
    pop     es
    pop     di
    pop     si
    pop     dx
    pop     cx
    pop     bx
    pop     bp
    retf    4
V9XPCIREADBAR0 ENDP

; Map the aperture in _v9x_map_physical_base and return its selector in AX,
; or 0 on failure. Stages 4 to 7 of the enable sequence.
;
; Reuse is deliberate: if a selector is already live and the physical base has
; not moved, the same selector is returned. The DIB Engine caches this value
; inside the PDEVICE it builds and never reacquires it, so handing back a
; different one leaves the engine writing through a stale descriptor. See
; docs/issues/2026-08-14-hellbender-dibeng-gpf.md.
PUBLIC V9XMAPAPERTURE
V9XMAPAPERTURE PROC FAR
    push    bx
    push    cx
    push    dx
    push    si
    push    di

    mov     V9xEnableResult, 0
    mov     eax, _v9x_map_physical_base

    cmp     V9xScreenSelector, 0
    je      short V9xMapAllocate
    cmp     eax, V9xPhysicalBase
    je      short V9xMapReuse
    ; A live selector against a moved aperture cannot be reconciled here.
    jmp     V9xMapDone
V9xMapReuse:
    mov     _v9x_hardware_stage_code, 0
    mov     ax, V9xScreenSelector
    mov     V9xEnableResult, ax
    jmp     V9xMapDone

V9xMapAllocate:
    mov     V9xPhysicalBase, eax
    xor     ax, ax
    mov     cx, 1
    int     31h
    jnc     short V9xMapSelectorAllocated
    jmp     V9xMapFailed
V9xMapSelectorAllocated:
    mov     _v9x_hardware_stage_code, 5
    mov     V9xScreenSelector, ax

    mov     eax, V9xPhysicalBase
    mov     ebx, eax
    shr     ebx, 16
    mov     cx, ax
    mov     si, _v9x_map_pages_hi
    mov     di, _v9x_map_pages_lo
    mov     ax, 0800h
    int     31h
    jc      short V9xMapFreeSelector
    mov     word ptr V9xLinearAddress, cx
    mov     word ptr V9xLinearAddress+2, bx
    mov     _v9x_hardware_stage_code, 6

    mov     bx, V9xScreenSelector
    mov     dx, word ptr V9xLinearAddress
    mov     cx, word ptr V9xLinearAddress+2
    mov     ax, 0007h
    int     31h
    jc      short V9xMapUnmap
    mov     _v9x_hardware_stage_code, 7

    mov     bx, V9xScreenSelector
    mov     cx, _v9x_map_pages_hi
    mov     dx, _v9x_map_pages_lo
    mov     ax, 0008h
    int     31h
    jc      short V9xMapUnmap

    mov     _v9x_hardware_stage_code, 0
    mov     ax, V9xScreenSelector
    mov     V9xEnableResult, ax
    jmp     short V9xMapDone

V9xMapUnmap:
    mov     bx, word ptr V9xLinearAddress+2
    mov     cx, word ptr V9xLinearAddress
    mov     ax, 0801h
    int     31h
    mov     V9xLinearAddress, 0

V9xMapFreeSelector:
    mov     bx, V9xScreenSelector
    mov     ax, 0001h
    int     31h
    mov     V9xScreenSelector, 0

V9xMapFailed:
    mov     V9xPhysicalBase, 0

V9xMapDone:
    pop     di
    pop     si
    pop     dx
    pop     cx
    pop     bx
    mov     ax, V9xEnableResult
    retf
V9XMAPAPERTURE ENDP

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
