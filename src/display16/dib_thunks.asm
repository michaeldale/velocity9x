; Original stack-transparent forwarding thunks for the Windows 98 DIB Engine.
; The external import library supplies the DIB_* targets; no DDK object code is
; copied into this source file.

.286
.model compact
.386
.code

V9X_FORWARD MACRO public_name, target_name
    PUBLIC public_name
    EXTRN target_name:FAR
public_name PROC FAR
    jmp target_name
public_name ENDP
ENDM

; Some extended DIB Engine entry points require the screen PDevice appended
; immediately below the original far return address.
V9X_FORWARD_PDEVICE MACRO public_name, target_name
    PUBLIC public_name
    EXTRN target_name:FAR
public_name PROC FAR
    mov ax,DGROUP
    mov es,ax
    pop ecx
    push dword ptr es:_v9x_driver_pdevice
    push ecx
    jmp target_name
public_name ENDP
ENDM

; Cursor entry points can be called while the display is disabled, after
; Disable has intentionally cleared _v9x_driver_pdevice. DIBENG's extended
; cursor routines dereference the appended PDevice unconditionally, so ignore
; those calls until Enable/ReEnable publishes a live screen PDevice again.
V9X_FORWARD_PDEVICE_GUARDED MACRO public_name, target_name
    LOCAL V9xCursorDone
    PUBLIC public_name
    EXTRN target_name:FAR
public_name PROC FAR
    mov ax,DGROUP
    mov es,ax
    cmp dword ptr es:_v9x_driver_pdevice,0
    je short V9xCursorDone
    pop ecx
    push dword ptr es:_v9x_driver_pdevice
    push ecx
    jmp target_name
V9xCursorDone:
    ; SetCursor takes one far pointer and MoveCursor takes two WORDs: both
    ; public Pascal entry points therefore own four bytes of caller args.
    retf 4
public_name ENDP
ENDM

; DIB_DibBltExt takes the current palettized-state word as its extra argument.
V9X_FORWARD_PALETTIZED MACRO public_name, target_name
    PUBLIC public_name
    EXTRN target_name:FAR
public_name PROC FAR
    mov ax,DGROUP
    mov es,ax
    pop ecx
    push word ptr es:_v9x_palettized
    push ecx
    jmp target_name
public_name ENDP
ENDM

EXTRN _v9x_driver_pdevice:DWORD
EXTRN _v9x_palettized:WORD

; BitBlt (ordinal 1) is implemented in C (gdi_accel.c): it is the GDI
; acceleration dispatcher, and its decline branch forwards to DIB_BitBlt
; through the typed V9XDIBBITBLTCALL wrapper in runtime.asm. The unconditional
; `V9X_FORWARD BitBlt, DIB_BitBlt` that used to be here is what that dispatcher
; replaced - see docs\plans\gdi-acceleration.md.
V9X_FORWARD ColorInfo,                 DIB_ColorInfo
; Control (ordinal 3) is implemented in C (dd16.c) for DirectDraw escapes
; and forwards non-DirectDraw functions to DIB_Control itself.
V9X_FORWARD EnumDFonts,                DIB_EnumDFonts
V9X_FORWARD_PDEVICE EnumObj,           DIB_EnumObjExt
IFNDEF V9X_SCREEN_SHADOW
V9X_FORWARD Output,                    DIB_Output
ELSE
; The vbe screen shadow: shadow16.c's V9xShadowOutput takes Output's own
; frame, bounds what the call can draw from its points, and calls DIB_Output
; through the typed forward below. The DIB Engine's deBeginAccess rectangle
; does not bound an Output call (shadow.h).
PUBLIC Output
EXTRN V9XSHADOWOUTPUT:FAR
Output PROC FAR
    jmp V9XSHADOWOUTPUT
Output ENDP

PUBLIC V9XDIBOUTPUTCALL
EXTRN DIB_Output:FAR
V9XDIBOUTPUTCALL PROC FAR
    jmp DIB_Output
V9XDIBOUTPUTCALL ENDP
ENDIF
V9X_FORWARD Pixel,                     DIB_Pixel
V9X_FORWARD_PDEVICE RealizeObject,     DIB_RealizeObjectExt
V9X_FORWARD StrBlt,                    DIB_StrBlt
V9X_FORWARD ScanLR,                    DIB_ScanLR
V9X_FORWARD DeviceMode,                DIB_DeviceMode
; ExtTextOut (ordinal 14) is implemented in C (gdi_accel.c) since build 005:
; it routes screen text through DIB_ExtTextOutExt with the driver's expansion
; callbacks, and its decline branch forwards to DIB_ExtTextOut through the
; typed V9XDIBEXTTEXTOUTCALL wrapper in runtime.asm. StrBlt stays a forward:
; the DIB Engine's own StrBlt does not come back through the driver.
V9X_FORWARD GetCharWidth,              DIB_GetCharWidth
V9X_FORWARD DeviceBitmap,              DIB_DeviceBitmap
V9X_FORWARD FastBorder,                DIB_FastBorder
V9X_FORWARD SetAttribute,              DIB_SetAttribute
V9X_FORWARD_PALETTIZED DibBlt,         DIB_DibBltExt
V9X_FORWARD CreateDIBitmap,            DIB_CreateDIBitmap
V9X_FORWARD DibToDevice,               DIB_DibToDevice
V9X_FORWARD_PDEVICE GetPalette,        DIB_GetPaletteExt
V9X_FORWARD_PDEVICE SetPaletteTranslate, DIB_SetPaletteTranslateExt
V9X_FORWARD_PDEVICE GetPaletteTranslate, DIB_GetPaletteTranslateExt
V9X_FORWARD_PDEVICE UpdateColors,      DIB_UpdateColorsExt
V9X_FORWARD StretchBlt,                DIB_StretchBlt
V9X_FORWARD StretchDIBits,             DIB_StretchDIBits
V9X_FORWARD SelectBitmap,              DIB_SelectBitmap
V9X_FORWARD BitmapBits,                DIB_BitmapBits
V9X_FORWARD Inquire,                   DIB_Inquire
IFNDEF V9X_SCREEN_SHADOW
V9X_FORWARD_PDEVICE_GUARDED SetCursor,  DIB_SetCursorExt
V9X_FORWARD_PDEVICE_GUARDED MoveCursor, DIB_MoveCursorExt

; DIBENG may poll CheckCursor while the display is disabled. Match the sample
; minidriver's guarded behavior instead of injecting a null PDevice.
PUBLIC CheckCursor
EXTRN DIB_CheckCursorExt:FAR
CheckCursor PROC FAR
    mov ax,DGROUP
    mov es,ax
    cmp dword ptr es:_v9x_driver_pdevice,0
    je short V9xCheckCursorDone
    pop ecx
    push dword ptr es:_v9x_driver_pdevice
    push ecx
    jmp DIB_CheckCursorExt
V9xCheckCursorDone:
    retf
CheckCursor ENDP
ELSE
; The vbe screen shadow (src\display16\shadow16.c). The DIB Engine draws the
; software cursor into the shadow, outside any deBeginAccess bracket, so each
; cursor entry calls the DIB Engine and then has shadow16.c copy the cursor's
; rectangles to the frame buffer. Same guard as the forwards above, and the
; same frames: the DIB Engine's extended entries take the original arguments
; followed by the screen PDevice and pop both.
EXTRN V9XSHADOWSETCURSOR:FAR
EXTRN V9XSHADOWMOVECURSOR:FAR
EXTRN V9XSHADOWCHECKCURSOR:FAR

; SetCursor(LPVOID lpCursorShape): the pointer at [bp+6].
PUBLIC SetCursor
EXTRN DIB_SetCursorExt:FAR
SetCursor PROC FAR
    mov ax,DGROUP
    mov es,ax
    cmp dword ptr es:_v9x_driver_pdevice,0
    je short V9xSetCursorDone
    push bp
    mov bp,sp
    push dword ptr [bp+6]
    push dword ptr es:_v9x_driver_pdevice
    call DIB_SetCursorExt
    push dword ptr [bp+6]
    call V9XSHADOWSETCURSOR
    pop bp
V9xSetCursorDone:
    retf 4
SetCursor ENDP

; MoveCursor(WORD absX, WORD absY): absY at [bp+6], absX at [bp+8].
PUBLIC MoveCursor
EXTRN DIB_MoveCursorExt:FAR
MoveCursor PROC FAR
    mov ax,DGROUP
    mov es,ax
    cmp dword ptr es:_v9x_driver_pdevice,0
    je short V9xMoveCursorDone
    push bp
    mov bp,sp
    push word ptr [bp+8]
    push word ptr [bp+6]
    push dword ptr es:_v9x_driver_pdevice
    call DIB_MoveCursorExt
    push word ptr [bp+8]
    push word ptr [bp+6]
    call V9XSHADOWMOVECURSOR
    pop bp
V9xMoveCursorDone:
    retf 4
MoveCursor ENDP

PUBLIC CheckCursor
EXTRN DIB_CheckCursorExt:FAR
CheckCursor PROC FAR
    mov ax,DGROUP
    mov es,ax
    cmp dword ptr es:_v9x_driver_pdevice,0
    je short V9xCheckCursorDone
    push dword ptr es:_v9x_driver_pdevice
    call DIB_CheckCursorExt
    call V9XSHADOWCHECKCURSOR
V9xCheckCursorDone:
    retf
CheckCursor ENDP
ENDIF

END
