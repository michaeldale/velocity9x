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
V9X_FORWARD Output,                    DIB_Output
V9X_FORWARD Pixel,                     DIB_Pixel
V9X_FORWARD_PDEVICE RealizeObject,     DIB_RealizeObjectExt
V9X_FORWARD StrBlt,                    DIB_StrBlt
V9X_FORWARD ScanLR,                    DIB_ScanLR
V9X_FORWARD DeviceMode,                DIB_DeviceMode
V9X_FORWARD ExtTextOut,                DIB_ExtTextOut
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

END
