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
; What the mini-VDD's cached BIOS answers said, written by the API callers
; below and read by enable16.c. Defined in C, like the stage code above.
EXTRN _v9x_minivdd_base:DWORD
EXTRN _v9x_minivdd_bytes:WORD
EXTRN _v9x_minivdd_lin_bytes:WORD
EXTRN _v9x_minivdd_attr:WORD
EXTRN _v9x_minivdd_mode_number:WORD
EXTRN _v9x_minivdd_width:WORD
EXTRN _v9x_minivdd_height:WORD
EXTRN _v9x_minivdd_bpp:WORD
EXTRN _v9x_minivdd_significant:WORD
EXTRN _v9x_minivdd_model:WORD
EXTRN _v9x_minivdd_record_flags:WORD
EXTRN _v9x_minivdd_red:WORD
EXTRN _v9x_minivdd_green:WORD
EXTRN _v9x_minivdd_blue:WORD
EXTRN _v9x_minivdd_rsvd:WORD
EXTRN _v9x_minivdd_version:WORD
EXTRN _v9x_minivdd_total64k:WORD
EXTRN _v9x_minivdd_capabilities:DWORD
EXTRN _v9x_minivdd_oem_revision:WORD
EXTRN _v9x_minivdd_bufseg:WORD
EXTRN _v9x_minivdd_listed:WORD
EXTRN _v9x_minivdd_queried:WORD
EXTRN _v9x_minivdd_cached:WORD
EXTRN _v9x_minivdd_probed:WORD
EXTRN _v9x_minivdd_status:WORD
; One 16-byte EDID chunk, as the four dwords the API hands back.
EXTRN _v9x_minivdd_edid0:DWORD
EXTRN _v9x_minivdd_edid1:DWORD
EXTRN _v9x_minivdd_edid2:DWORD
EXTRN _v9x_minivdd_edid3:DWORD
V9xScreenSelector dw 0
V9xLinearAddress  dd 0
V9xPhysicalBase   dd 0
V9xEnableResult   dw 0
V9xVddEntryPoint dd 0
; Our own mini-VDD's PM API entry point, and how far we have got with it:
; 0 = not looked yet, 1 = usable, 2 = looked and refused.
V9xMiniApiEntry  dd 0
V9xMiniApiState  dw 0
V9xVmHandle       dw 0
V9xVddRegistered dw 0
V9xHardwareStageCode dw 0
V9xCreateDibReturn dd 0
V9xDdSharedSel   dw 0
V9xDdSharedLin   dd 0
; One LDT descriptor over the ViRGE new-MMIO window, for GDI acceleration.
; Allocated lazily by V9XENGINESELECTOR and held for the driver's lifetime,
; for the same reason V9xScreenSelector is (see V9XHARDWAREDISABLE): a
; descriptor handed back to the LDT and re-acquired later is a descriptor that
; could belong to anything in between.
V9xEngineSel dw 0

; GDI acceleration state that has to survive a live mode switch.
;
; It lives here, in DGROUP, and not in the PDEVICE and not in V9X_DD_SHARED.
; Two concrete reasons: V9X_DD_SHARED does not exist before the HAL DLL loads,
; and ReEnable rebuilds the PDEVICE in place on a live mode switch, so anything
; latched inside it would be silently cleared by a resolution change. The
; poison latch is required to survive mode switches; DGROUP is what makes that
; true.
;
; Defined in C (src\display16\gdi_accel.c) and only read here, so the
; two-instruction fast path below and the C policy cannot disagree about which
; word they mean.
EXTRN _v9x_gdi_engine_dirty:WORD

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

; Our mini-VDD's private device id and API contract. Shared verbatim with
; src\minivdd32\loader.asm rather than copied from it: a driver reading a field
; from an offset the mini-VDD does not write assembles, links and installs
; cleanly, and then misreads the BIOS answers at boot with nothing pointing
; here.
include V9XMAPI.INC

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

; The GDI engine drain, in C. Called only from the two BeginAccess entries
; below and only when the dirty flag is set; it may touch MMIO, ports and
; DGROUP and nothing else, because it can run at interrupt time on a
; software-cursor draw.
EXTRN V9XGDIBEGINACCESSSLOW:FAR

; Drain pending engine work before the CPU touches the framebuffer.
;
; This macro is what both deBeginAccess entry points get. The DIB Engine calls
; through the PDEVICE with DS holding whatever its caller had, so the flag is
; reached through ES and an explicit DGROUP load - the same idiom every thunk
; in dib_thunks.asm uses, and the reason this is four instructions on the fast
; path rather than the two the plan describes. AX and ES are the caller-scratch
; registers of the Pascal convention and DIB_BeginAccess may clobber them
; itself, so tail-jumping to it after using them is transparent.
V9X_BEGIN_ACCESS_DRAIN MACRO
    LOCAL V9xDrainPending
    mov ax,DGROUP
    mov es,ax
    cmp word ptr es:_v9x_gdi_engine_dirty,0
    jne short V9xDrainPending
    jmp DIB_BeginAccess
V9xDrainPending:
    call V9XGDIBEGINACCESSSLOW
    jmp DIB_BeginAccess
ENDM

PUBLIC V9XDIBBEGINACCESS
V9XDIBBEGINACCESS PROC FAR
    V9X_BEGIN_ACCESS_DRAIN
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

; Typed forward of the DIB Engine's BitBlt, for the C dispatcher's decline
; branch in gdi_accel.c.
;
; Ordinal 1 used to be an unconditional `jmp DIB_BitBlt` thunk in
; dib_thunks.asm. It is a C function now, and a C function cannot name
; DIB_BitBlt directly: this driver compiles PASCAL exports with their names
; uppercased (which is what `export Control.3=CONTROL` in the build script is
; about), so a C `extern WORD FAR PASCAL DIB_BitBlt(...)` would ask the linker
; for DIB_BITBLT and DIBENG.LIB supplies DIB_BitBlt. Every other DIBENG routine
; C code calls reaches it through a wrapper like this one for the same reason.
;
; Stack-transparent: the caller's eleven Pascal arguments are already below our
; far return address, and DIB_BitBlt pops them itself, so the jump lands the
; DIB Engine's return straight back at the C caller.
EXTRN DIB_BitBlt:FAR
PUBLIC V9XDIBBITBLTCALL
V9XDIBBITBLTCALL PROC FAR
    jmp DIB_BitBlt
V9XDIBBITBLTCALL ENDP

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

; ---------------------------------------------------------------------------
; ViRGE new-MMIO access for GDI acceleration.
;
; These three routines exist because of one measured compiler fact, not for
; convenience: this driver is built without a -3, so wcc emits 8086 code, and a
; `volatile DWORD FAR *` store compiles to TWO 16-bit writes. On the ViRGE that
; is wrong rather than merely slow - CMD_SET starts the blit, so a split write
; would trigger the engine on the low half with a stale high half, and a split
; SUBSYS_STAT read would sample the FIFO count and the idle bit at different
; instants. Every 32-bit engine register therefore goes through here, where
; .386p guarantees one bus cycle.
;
; Offsets are WORDs: every 2D register in include\velocity9x\s3_engine_regs.h
; is below 0x10000, which is exactly the constraint that makes one 64 KiB
; selector enough.

; Allocate (once) and return the MMIO window selector in AX, 0 on failure.
;
; Base is V9xLinearAddress + 16 MiB: V9XMAPAPERTURE maps the whole 64 MiB PCI
; BAR and the new-MMIO window sits at BAR + 0x01000000. Limit is 0xFFFF, one
; 64 KiB window. Callers that are not on a ViRGE never call this, so a
; default-off build allocates no descriptor at all.
PUBLIC V9XENGINESELECTOR
V9XENGINESELECTOR PROC FAR
    push    bx
    push    cx
    push    dx

    mov     ax, V9xEngineSel
    cmp     ax, 0
    jne     short V9xEngineSelectorDone
    ; No aperture mapped means no window to describe.
    cmp     V9xLinearAddress, 0
    je      short V9xEngineSelectorFailed

    xor     ax, ax
    mov     cx, 1
    int     31h
    jc      short V9xEngineSelectorFailed
    mov     V9xEngineSel, ax

    mov     bx, ax
    mov     eax, V9xLinearAddress
    add     eax, 01000000h
    mov     dx, ax
    shr     eax, 16
    mov     cx, ax
    mov     ax, 0007h
    int     31h
    jc      short V9xEngineSelectorFree

    mov     bx, V9xEngineSel
    xor     cx, cx
    mov     dx, 0ffffh
    mov     ax, 0008h
    int     31h
    jc      short V9xEngineSelectorFree

    mov     ax, V9xEngineSel
    jmp     short V9xEngineSelectorDone

V9xEngineSelectorFree:
    mov     bx, V9xEngineSel
    mov     ax, 0001h
    int     31h
    mov     V9xEngineSel, 0
V9xEngineSelectorFailed:
    xor     ax, ax
V9xEngineSelectorDone:
    pop     dx
    pop     cx
    pop     bx
    retf
V9XENGINESELECTOR ENDP

; V9xEngineRead(WORD offset) -> DWORD in DX:AX, the Watcom 16-bit convention.
;
; FAR PASCAL pushes arguments left to right, so with the four-byte far return
; address below them the single WORD argument sits at [bp+6]. Reading through
; an unallocated selector would fault, so a zero selector answers 0 - which
; every caller already treats as "the engine is not there".
PUBLIC V9XENGINEREAD
V9XENGINEREAD PROC FAR
    push    bp
    mov     bp, sp
    push    bx
    push    es

    xor     eax, eax
    mov     bx, V9xEngineSel
    or      bx, bx
    je      short V9xEngineReadDone
    mov     es, bx
    mov     bx, word ptr [bp+6]
    mov     eax, es:[bx]
V9xEngineReadDone:
    mov     dx, ax
    shr     eax, 16
    xchg    ax, dx

    pop     es
    pop     bx
    pop     bp
    retf    2
V9XENGINEREAD ENDP

; V9xEngineWrite(WORD offset, DWORD value). One 32-bit store, or nothing.
;
; Pushed left to right, so value (four bytes, last pushed) is at [bp+6] and
; offset is above it at [bp+10].
PUBLIC V9XENGINEWRITE
V9XENGINEWRITE PROC FAR
    push    bp
    mov     bp, sp
    push    bx
    push    es

    mov     bx, V9xEngineSel
    or      bx, bx
    je      short V9xEngineWriteDone
    mov     es, bx
    mov     bx, word ptr [bp+10]
    mov     eax, dword ptr [bp+6]
    mov     es:[bx], eax
V9xEngineWriteDone:
    pop     es
    pop     bx
    pop     bp
    retf    6
V9XENGINEWRITE ENDP

; V9xEngineImageRow(WORD source_selector, WORD source_offset, WORD bytes)
;
; Push one scanline of image data into the ViRGE's image-transfer window, which
; S3.INC:688 places at MMIO offset 0 - inside the 64 KiB window V9xEngineSel
; already covers, so this needs no mapping of its own.
;
; Three things about this are copied from the reference driver's ColorSourceBlt
; (98DDK\src\display\mini\s3v\S3BLT.ASM:1400-1460) rather than invented:
;
;  - The destination offset restarts at 0 for every row. The window is a FIFO
;    port, not memory; writing further into it is meaningless.
;  - There is no FIFO polling in the loop. The engine throttles the bus itself
;    and the writes stall until it can accept them, so a pacing loop here would
;    be a slower way to do nothing.
;  - The tail is handled without ever reading past the source. A row is not
;    necessarily a whole number of dwords, and `rep movsd` rounded up would read
;    up to three bytes beyond the bitmap - which in a display driver is a GP
;    fault, not a rounding error. Whole dwords go first, then the remaining one
;    to three bytes are assembled into a register and written as a single dword,
;    because the window takes dword writes.
;
; The engine consumes exactly the pixel count the command told it to expect, so
; the padding bits in that final dword are discarded by the hardware.
PUBLIC V9XENGINEIMAGEROW
V9XENGINEIMAGEROW PROC FAR
    push    bp
    mov     bp, sp
    push    ds
    push    si
    push    di
    push    cx
    push    bx
    push    ax

    mov     ax, V9xEngineSel
    or      ax, ax
    je      short V9xImageRowDone

    mov     es, ax
    xor     di, di                  ; ES:DI -> IMAGE_XFER, offset 0 always
    mov     cx, word ptr [bp+6]     ; bytes
    mov     ds, word ptr [bp+10]    ; source selector
    mov     si, word ptr [bp+8]     ; source offset

    mov     bx, cx
    shr     cx, 2                   ; whole dwords
    jz      short V9xImageRowTail
    cld
    rep     movsd

V9xImageRowTail:
    and     bx, 3                   ; leftover 1..3 bytes, or none
    jz      short V9xImageRowDone
    ; Assemble the tail a byte at a time, from the last byte down to the first,
    ; shifting left as we go. Reading a whole dword is what would fault here;
    ; the reference avoids that by reading from *before* the end and shifting
    ; down, which needs the row to be at least a dword long. Building the value
    ; upward needs no such assumption and no compensating shift.
    ;
    ; Traced, because the obvious alternative is wrong: loading each byte into
    ; AH and rotating right lands the bytes correctly only for a full dword. For
    ; two bytes it leaves them at opposite ends of the register, and any single
    ; shift that rescues one discards the other.
    ;
    ; b0 must end up in the dword's byte 0, so with N bytes:
    ;   N=1 -> 0x000000b0, N=2 -> 0x0000b1b0, N=3 -> 0x00b2b1b0
    xor     eax, eax
    mov     cx, bx
    add     si, bx                  ; one past the row's last byte
V9xImageRowTailByte:
    dec     si
    shl     eax, 8
    mov     al, ds:[si]
    loop    V9xImageRowTailByte
    mov     es:[di], eax

V9xImageRowDone:
    pop     ax
    pop     bx
    pop     cx
    pop     di
    pop     si
    pop     ds
    pop     bp
    retf    6
V9XENGINEIMAGEROW ENDP

; Return the linear address of the DirectDraw shared block in DX:AX.
PUBLIC V9XDDSHAREDLINEAR
V9XDDSHAREDLINEAR PROC FAR
    mov     ax, word ptr V9xDdSharedLin
    mov     dx, word ptr V9xDdSharedLin+2
    retf
V9XDDSHAREDLINEAR ENDP

; The rectangle form of the same DIBENG routine, re-exported so C code can call
; it with the full argument list. It gets the same dirty check as the plain
; entry - not because a caller is known to race pending engine work, but
; because reasoning per caller about who cannot is a worse trade than a shared
; stub: this is the same four instructions either way.
PUBLIC V9XDIBBEGINACCESSRECT
V9XDIBBEGINACCESSRECT PROC FAR
    V9X_BEGIN_ACCESS_DRAIN
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

; Find our mini-VDD's PM API and satisfy ourselves that it is ours.
;
; Returns AX=1 when the entry point is usable. The result is latched either way,
; so a machine without the VxD pays for one INT 2Fh and no more.
;
; The handshake is not ceremony. The device id is private and unallocated, so
; another VxD may already own it; INT 2Fh 1684h would then hand back a stranger's
; entry point and calling it blind is how you corrupt an unrelated driver. A
; wrong magic or any contract version other than this package's v2 means refuse
; and never call again. The DRV and VxD ship as one mandatory pair.
V9xMiniApiInitialize PROC NEAR
    cmp     V9xMiniApiState, 0
    je      short V9xMiniApiProbe
    cmp     V9xMiniApiState, 1
    je      short V9xMiniApiUsable
    xor     ax, ax
    ret

V9xMiniApiProbe:
    push    bx
    push    cx
    push    dx
    push    esi
    push    edi
    push    es

    mov     V9xMiniApiState, 2          ; refuse unless everything below passes

    mov     ax, 1684h
    mov     bx, V9XMINI_DEVICE_ID
    int     2fh
    mov     ax, es
    or      ax, di
    jz      short V9xMiniApiProbeDone   ; no such VxD loaded

    mov     word ptr V9xMiniApiEntry, di
    mov     word ptr V9xMiniApiEntry+2, es

    mov     eax, V9XMINI_FN_HANDSHAKE
    call    dword ptr V9xMiniApiEntry
    or      ax, ax
    jz      short V9xMiniApiProbeDone
    cmp     ebx, V9XMINI_API_MAGIC
    jne     short V9xMiniApiProbeDone
    cmp     ecx, V9XMINI_API_VERSION
    jne     short V9xMiniApiProbeDone  ; v2 package pairs are mandatory

    mov     V9xMiniApiState, 1

V9xMiniApiProbeDone:
    pop     es
    pop     edi
    pop     esi
    pop     dx
    pop     cx
    pop     bx
    cmp     V9xMiniApiState, 1
    je      short V9xMiniApiUsable
    xor     ax, ax
    ret
V9xMiniApiUsable:
    mov     ax, 1
    ret
V9xMiniApiInitialize ENDP

; WORD FAR PASCAL V9xMiniVbeModeInfo(WORD mode)
;
; Ask the mini-VDD what the BIOS said about one VBE mode.
;
; Returns 1 and fills the _v9x_minivdd_* DGROUP variables; 2 when the API
; answered but has nothing cached for that mode; 0 when there is no usable API
; at all. Those last two are worth telling apart in the boot trace: one means
; the VxD is missing or is not ours, the other means it is there and its BIOS
; query came back empty, and the fixes are in different files.
;
; This lives in assembly because calling a VxD entry point needs 32-bit
; registers and the C in this driver is compiled for 8086. It hands the fields
; to C rather than judging them: whether the answer is credible, and whether it
; matches the family's table, stays in host-tested vbe_parse.c.
PUBLIC V9XMINIVBEMODEINFO
V9XMINIVBEMODEINFO PROC FAR
    push    bp
    mov     bp, sp
    push    bx
    push    cx
    push    dx
    push    esi
    push    edi
    push    es

    call    V9xMiniApiInitialize
    or      ax, ax
    jz      short V9xMiniVbeModeInfoFailed

    movzx   ecx, word ptr [bp+6]        ; the mode number
    mov     eax, V9XMINI_FN_MODE_INFO
    call    dword ptr V9xMiniApiEntry
    or      ax, ax
    jz      short V9xMiniVbeModeInfoNoData

    mov     _v9x_minivdd_base, ebx
    mov     _v9x_minivdd_bytes, cx
    shr     ecx, 16
    mov     _v9x_minivdd_attr, cx
    mov     _v9x_minivdd_width, dx
    shr     edx, 16
    mov     _v9x_minivdd_height, dx
    mov     _v9x_minivdd_bpp, si
    shr     esi, 16
    mov     _v9x_minivdd_model, si

    mov     ax, 1
    jmp     short V9xMiniVbeModeInfoDone
V9xMiniVbeModeInfoNoData:
    mov     ax, 2
    jmp     short V9xMiniVbeModeInfoDone
V9xMiniVbeModeInfoFailed:
    xor     ax, ax
V9xMiniVbeModeInfoDone:
    pop     es
    pop     edi
    pop     esi
    pop     dx
    pop     cx
    pop     bx
    pop     bp
    ret     2
V9XMINIVBEMODEINFO ENDP

; WORD FAR PASCAL V9xMiniVbeController(void)
;
; Returns 1 and fills _v9x_minivdd_version and _v9x_minivdd_total64k, or 0.
PUBLIC V9XMINIVBECONTROLLER
V9XMINIVBECONTROLLER PROC FAR
    push    bx
    push    cx
    push    dx
    push    esi
    push    edi
    push    es

    call    V9xMiniApiInitialize
    or      ax, ax
    jz      short V9xMiniVbeCtrlFailed

    mov     eax, V9XMINI_FN_CONTROLLER
    call    dword ptr V9xMiniApiEntry
    or      ax, ax
    jz      short V9xMiniVbeCtrlFailed

    mov     _v9x_minivdd_version, bx
    mov     _v9x_minivdd_total64k, cx
    mov     _v9x_minivdd_capabilities, edx
    mov     _v9x_minivdd_oem_revision, si
    mov     ax, 1
    jmp     short V9xMiniVbeCtrlDone
V9xMiniVbeCtrlFailed:
    xor     ax, ax
V9xMiniVbeCtrlDone:
    pop     es
    pop     edi
    pop     esi
    pop     dx
    pop     cx
    pop     bx
    retf
V9XMINIVBECONTROLLER ENDP

; WORD FAR PASCAL V9xMiniVbeStatus(void)
;
; Returns 1 and fills the bounded count/status globals with what the mini-VDD's
; init-time collection achieved, or
; 0 when there is no usable API. Diagnostic only: it exists so an empty cache
; can be told apart from a failed allocation without guessing.
PUBLIC V9XMINIVBESTATUS
V9XMINIVBESTATUS PROC FAR
    push    bx
    push    cx
    push    dx
    push    esi
    push    edi
    push    es

    call    V9xMiniApiInitialize
    or      ax, ax
    jz      short V9xMiniVbeStatusFailed

    mov     eax, V9XMINI_FN_STATUS
    call    dword ptr V9xMiniApiEntry
    or      ax, ax
    jz      short V9xMiniVbeStatusFailed

    mov     _v9x_minivdd_bufseg, bx
    mov     _v9x_minivdd_listed, cx
    shr     ecx, 16
    mov     _v9x_minivdd_queried, cx
    mov     _v9x_minivdd_cached, dx
    shr     edx, 16
    mov     _v9x_minivdd_probed, dx
    mov     _v9x_minivdd_status, si
    mov     ax, 1
    jmp     short V9xMiniVbeStatusDone
V9xMiniVbeStatusFailed:
    xor     ax, ax
V9xMiniVbeStatusDone:
    pop     es
    pop     edi
    pop     esi
    pop     dx
    pop     cx
    pop     bx
    retf
V9XMINIVBESTATUS ENDP

; WORD FAR PASCAL V9xMiniVbeModeAt(WORD index)
PUBLIC V9XMINIVBEMODEAT
V9XMINIVBEMODEAT PROC FAR
    push    bp
    mov     bp, sp
    push    bx
    push    cx
    push    dx
    push    esi
    push    edi
    push    es
    call    V9xMiniApiInitialize
    or      ax, ax
    jz      short V9xMiniVbeModeAtFailed
    movzx   ecx, word ptr [bp+6]
    mov     eax, V9XMINI_FN_MODE_AT
    call    dword ptr V9xMiniApiEntry
    or      ax, ax
    jz      short V9xMiniVbeModeAtFailed
    mov     _v9x_minivdd_base, ebx
    mov     _v9x_minivdd_bytes, cx
    shr     ecx, 16
    mov     _v9x_minivdd_lin_bytes, cx
    mov     _v9x_minivdd_width, dx
    shr     edx, 16
    mov     _v9x_minivdd_height, dx
    mov     _v9x_minivdd_bpp, si
    shr     esi, 16
    mov     _v9x_minivdd_significant, si
    mov     _v9x_minivdd_mode_number, di
    shr     edi, 16
    mov     _v9x_minivdd_attr, di
    mov     ax, 1
    jmp     short V9xMiniVbeModeAtDone
V9xMiniVbeModeAtFailed:
    xor     ax, ax
V9xMiniVbeModeAtDone:
    pop     es
    pop     edi
    pop     esi
    pop     dx
    pop     cx
    pop     bx
    pop     bp
    ret     2
V9XMINIVBEMODEAT ENDP

; WORD FAR PASCAL V9xMiniVbeModeMasks(WORD index)
PUBLIC V9XMINIVBEMODEMASKS
V9XMINIVBEMODEMASKS PROC FAR
    push    bp
    mov     bp, sp
    push    bx
    push    cx
    push    dx
    push    esi
    push    edi
    push    es
    call    V9xMiniApiInitialize
    or      ax, ax
    jz      short V9xMiniVbeModeMasksFailed
    movzx   ecx, word ptr [bp+6]
    mov     eax, V9XMINI_FN_MODE_MASKS
    call    dword ptr V9xMiniApiEntry
    or      ax, ax
    jz      short V9xMiniVbeModeMasksFailed
    mov     _v9x_minivdd_red, bx
    shr     ebx, 16
    mov     _v9x_minivdd_green, bx
    mov     _v9x_minivdd_blue, cx
    shr     ecx, 16
    mov     _v9x_minivdd_rsvd, cx
    mov     _v9x_minivdd_model, dx
    shr     edx, 16
    mov     _v9x_minivdd_record_flags, dx
    mov     ax, 1
    jmp     short V9xMiniVbeModeMasksDone
V9xMiniVbeModeMasksFailed:
    xor     ax, ax
V9xMiniVbeModeMasksDone:
    pop     es
    pop     edi
    pop     esi
    pop     dx
    pop     cx
    pop     bx
    pop     bp
    ret     2
V9XMINIVBEMODEMASKS ENDP

; WORD FAR PASCAL V9xMiniVbeEdidChunk(WORD index)
;
; One 16-byte slice of the mini-VDD's cached EDID block 0 into the four
; _v9x_minivdd_edidN dwords, low byte of EBX first. 0 when no valid block was
; collected or the index is out of range.
PUBLIC V9XMINIVBEEDIDCHUNK
V9XMINIVBEEDIDCHUNK PROC FAR
    push    bp
    mov     bp, sp
    push    bx
    push    cx
    push    dx
    push    esi
    push    edi
    push    es
    call    V9xMiniApiInitialize
    or      ax, ax
    jz      short V9xMiniVbeEdidChunkFailed
    movzx   ecx, word ptr [bp+6]
    mov     eax, V9XMINI_FN_EDID_CHUNK
    call    dword ptr V9xMiniApiEntry
    or      ax, ax
    jz      short V9xMiniVbeEdidChunkFailed
    mov     _v9x_minivdd_edid0, ebx
    mov     _v9x_minivdd_edid1, ecx
    mov     _v9x_minivdd_edid2, edx
    mov     _v9x_minivdd_edid3, esi
    mov     ax, 1
    jmp     short V9xMiniVbeEdidChunkDone
V9xMiniVbeEdidChunkFailed:
    xor     ax, ax
V9xMiniVbeEdidChunkDone:
    pop     es
    pop     edi
    pop     esi
    pop     dx
    pop     cx
    pop     bx
    pop     bp
    ret     2
V9XMINIVBEEDIDCHUNK ENDP

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

; Is there a PCI BIOS at all? Returns 1 when INT 1Ah AX=B101h answers.
;
; V9xFindPciDevice cannot tell "this machine has no PCI" from "this machine has
; PCI and none of our cards are in it": B102h fails the same way for both, and
; both land on V9xFindPciDeviceFailed. Those two situations want opposite
; responses. The first is a VESA Local Bus or ISA machine, where a family that
; can identify its own silicon should be allowed to try. The second is our
; package bound to somebody else's card, where reading that card's extended
; registers is exactly what should not happen.
;
; So the distinction is made here rather than inferred. CF clear with AH zero is
; the documented presence answer; the 'PCI ' signature B101h also returns lands
; in EDX, which is not needed for a yes/no.
PUBLIC V9XPCIBIOSPRESENT
V9XPCIBIOSPRESENT PROC FAR
    push    bx
    push    cx
    push    dx
    push    si
    mov     ax, 0b101h
    xor     si, si
    int     1ah
    jc      short V9xPciBiosAbsent
    or      ah, ah
    jne     short V9xPciBiosAbsent
    mov     ax, 1
    jmp     short V9xPciBiosDone
V9xPciBiosAbsent:
    xor     ax, ax
V9xPciBiosDone:
    pop     si
    pop     dx
    pop     cx
    pop     bx
    retf
V9XPCIBIOSPRESENT ENDP

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
