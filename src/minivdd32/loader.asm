; Velocity9x first boot-loadable Windows 9x mini-VDD.
;
; This stage verifies the master VDD ABI, installs legacy VESA and Windows 98
; monitor-power callbacks, and emits bounded COM1 diagnostics. It advertises
; D0 only because the VESA BIOS resume path can blank an S3 ViRGE display
; without reliably restoring the active high-resolution framebuffer.

.386p

.xlist
include VMM.INC
include MINIVDD.INC
.list

; Private device id, so the 16-bit driver can reach the API below through
; INT 2Fh AX=1684h - the same mechanism runtime.asm already uses to find the
; master VDD at id 000Ah.
;
; The id is not allocated by anyone: no third-party registry exists to ask, and
; a collision would hand the driver a foreign VxD's entry point. That is why
; function 0 is a handshake returning a magic value, and why the 16-bit side
; refuses to use the entry point until it has seen it. A wrong answer is then a
; clean refusal instead of a call into a stranger.
V9XMINI_DEVICE_ID   EQU 4F9Ch

; Handshake reply and API contract version. Bump the version when the meaning
; of any function changes; the 16-bit side checks it.
V9XMINI_API_MAGIC   EQU 39583956h          ; 'V9X9' little-endian
V9XMINI_API_VERSION EQU 0001h

; API functions, in the client's AX.
V9XMINI_FN_HANDSHAKE EQU 0000h
V9XMINI_FN_CONTROLLER EQU 0001h
V9XMINI_FN_MODE_INFO EQU 0002h
V9XMINI_FN_STATUS    EQU 0003h

Declare_Virtual_Device V9XMINI, 1, 0, MiniVDD_Control, \
                       V9XMINI_DEVICE_ID, VDD_Init_Order, , MiniVDD_PM_API,

VxD_LOCKED_DATA_SEG
include V9XBUILD.INC
public V9xMiniVddBuildId

; The cached VBE answers.
;
; 4F00h and 4F01h describe the adapter and its modes, not the mode currently
; programmed, so they are static for the life of the machine and are collected
; once at Device_Init. That is deliberate: it keeps the nested-execution BIOS
; call in init context, where calling the V86 BIOS is the ordinary idiom, and
; leaves the API a table lookup that cannot fault a display driver.
;
; The mode list is the seven standard VESA numbers every tier-0 family
; publishes. Parallel arrays rather than a struct: indexing is a shift and an
; add, and nothing here needs the packing rules.
V9X_VBE_CACHE_COUNT EQU 7
V9xVbeModeList  dw 0100h, 0101h, 0103h, 0105h, 0111h, 0114h, 0117h
V9xVbeValid     dw V9X_VBE_CACHE_COUNT dup (0)
V9xVbeAttr      dw V9X_VBE_CACHE_COUNT dup (0)
V9xVbeBytes     dw V9X_VBE_CACHE_COUNT dup (0)
V9xVbeWidth     dw V9X_VBE_CACHE_COUNT dup (0)
V9xVbeHeight    dw V9X_VBE_CACHE_COUNT dup (0)
V9xVbeBpp       dw V9X_VBE_CACHE_COUNT dup (0)
V9xVbeModel     dw V9X_VBE_CACHE_COUNT dup (0)
V9xVbeBase      dd V9X_VBE_CACHE_COUNT dup (0)

V9xVbeCtrlValid dw 0
V9xVbeCtrlVer   dw 0
V9xVbeCtrl64K   dw 0

; Real-mode segment of the V86 scratch the BIOS fills in, or 0 if it could not
; be had. Allocated at init and never freed.
V9xVbeBufSeg    dw 0
VxD_LOCKED_DATA_ENDS

VxD_LOCKED_CODE_SEG

; ESI points to ECX bytes. Preserve all registers and bound every UART wait.
BeginProc V9xMini_Serial_Write
    pushfd
    pushad

    mov     dx, 03fbh
    in      al, dx
    cmp     al, 0ffh
    je      short V9xMini_Serial_Done
    mov     ah, al
    and     al, 07fh
    out     dx, al

V9xMini_Serial_Next:
    test    ecx, ecx
    jz      short V9xMini_Serial_Restore
    mov     ebx, 0000ffffh

V9xMini_Serial_Wait:
    mov     dx, 03fdh
    in      al, dx
    test    al, 020h
    jnz     short V9xMini_Serial_Send
    dec     ebx
    jnz     short V9xMini_Serial_Wait
    jmp     short V9xMini_Serial_Restore

V9xMini_Serial_Send:
    mov     al, [esi]
    mov     dx, 03f8h
    out     dx, al
    inc     esi
    dec     ecx
    jmp     short V9xMini_Serial_Next

V9xMini_Serial_Restore:
    mov     dx, 03fbh
    mov     al, ah
    out     dx, al

V9xMini_Serial_Done:
    popad
    popfd
    ret
EndProc V9xMini_Serial_Write

; Update the S3 ViRGE DPMS state without changing the active video mode.
;
; CL contains the S3 SR0D DPMS bits: bit 4 disables horizontal sync and bit 6
; disables vertical sync.  Windows power states map to 00h (D0), 10h (D1),
; 40h (D2), and 50h (D3).  The routine also clears CR56[2:1], the alternate
; S3 DPMS controls, and clears SR01[5] on wake in case the BIOS used the
; generic VGA screen-off bit.  All registers and flags are preserved.
BeginProc V9xMini_Set_Dpms
    pushfd
    pushad

    ; Save and unlock the extended sequencer registers.
    mov     dx, 03c4h
    in      al, dx
    mov     bl, al
    mov     al, 08h
    out     dx, al
    inc     dx
    in      al, dx
    mov     bh, al
    mov     al, 06h
    out     dx, al

    ; Program SR0D horizontal/vertical sync suppression.
    dec     dx
    mov     al, 0dh
    out     dx, al
    inc     dx
    in      al, dx
    and     al, 0afh
    or      al, cl
    out     dx, al

    ; D0 must also undo the generic VGA sequencer screen-off bit.
    test    cl, cl
    jnz     short V9xMini_Dpms_Seq_Restore
    dec     dx
    mov     al, 01h
    out     dx, al
    inc     dx
    in      al, dx
    and     al, 0dfh
    out     dx, al

V9xMini_Dpms_Seq_Restore:
    ; Restore the sequencer extension lock and caller's index.
    dec     dx
    mov     al, 08h
    out     dx, al
    inc     dx
    mov     al, bh
    out     dx, al
    dec     dx
    mov     al, bl
    out     dx, al

    ; On wake, clear CR56[2:1].  Some S3 BIOSes use these alternate DPMS
    ; controls in addition to SR0D.  Select the mono/color CRTC from 3CCh.
    test    cl, cl
    jnz     short V9xMini_Dpms_Done
    mov     dx, 03cch
    in      al, dx
    test    al, 01h
    jz      short V9xMini_Dpms_Mono
    mov     dx, 03d4h
    jmp     short V9xMini_Dpms_Crtc_Selected
V9xMini_Dpms_Mono:
    mov     dx, 03b4h
V9xMini_Dpms_Crtc_Selected:
    in      al, dx
    mov     bl, al

    ; Save CR38/CR39, unlock S3 system registers, clear CR56 DPMS bits,
    ; then restore the locks and the caller's CRTC index.
    mov     al, 38h
    out     dx, al
    inc     dx
    in      al, dx
    mov     bh, al
    dec     dx
    mov     al, 39h
    out     dx, al
    inc     dx
    in      al, dx
    mov     ch, al
    dec     dx
    mov     al, 38h
    out     dx, al
    inc     dx
    mov     al, 48h
    out     dx, al
    dec     dx
    mov     al, 39h
    out     dx, al
    inc     dx
    mov     al, 0a5h
    out     dx, al
    dec     dx
    mov     al, 56h
    out     dx, al
    inc     dx
    in      al, dx
    and     al, 0f9h
    out     dx, al
    dec     dx
    mov     al, 39h
    out     dx, al
    inc     dx
    mov     al, ch
    out     dx, al
    dec     dx
    mov     al, 38h
    out     dx, al
    inc     dx
    mov     al, bh
    out     dx, al
    dec     dx
    mov     al, bl
    out     dx, al

V9xMini_Dpms_Done:
    popad
    popfd
    ret
EndProc V9xMini_Set_Dpms

; Windows 98 DDK SET_MONITOR_POWER_STATE callback.
; Entry: [ESP+4] devnode, [ESP+8] CM_POWERSTATE_D0..D3.
; Exit:  EAX = CR_SUCCESS when handled, CR_DEFAULT for an unknown state.
BeginProc MiniVDD_SetMonitorPowerState
    mov     eax, [esp+8]
    cmp     eax, 00000001h             ; CM_POWERSTATE_D0
    je      short V9xMini_Set_Monitor_D0
    cmp     eax, 00000002h             ; CM_POWERSTATE_D1
    je      short V9xMini_Set_Monitor_D1
    cmp     eax, 00000004h             ; CM_POWERSTATE_D2
    je      short V9xMini_Set_Monitor_D2
    cmp     eax, 00000008h             ; CM_POWERSTATE_D3
    je      short V9xMini_Set_Monitor_D3
    mov     eax, 00000001h             ; CR_DEFAULT
    ret

V9xMini_Set_Monitor_D0:
    xor     ecx, ecx
    call    V9xMini_Set_Dpms
    mov     esi, OFFSET32 V9xMiniPowerOnLine
    mov     ecx, V9xMiniPowerOnLineLength
    call    V9xMini_Serial_Write
    xor     eax, eax                   ; CR_SUCCESS
    ret
V9xMini_Set_Monitor_D1:
    mov     ecx, 10h
    jmp     short V9xMini_Set_Monitor_Low_Power
V9xMini_Set_Monitor_D2:
    mov     ecx, 40h
    jmp     short V9xMini_Set_Monitor_Low_Power
V9xMini_Set_Monitor_D3:
    mov     ecx, 50h
V9xMini_Set_Monitor_Low_Power:
    call    V9xMini_Set_Dpms
    mov     esi, OFFSET32 V9xMiniPowerOffLine
    mov     ecx, V9xMiniPowerOffLineLength
    call    V9xMini_Serial_Write
    xor     eax, eax                   ; CR_SUCCESS
    ret
EndProc MiniVDD_SetMonitorPowerState

; Windows 98 DDK GET_MONITOR_POWER_STATE_CAPS callback. Resume through the
; Win98 VESA fallback is not reliable for this driver, so advertise D0 only.
BeginProc MiniVDD_GetMonitorPowerStateCaps
    mov     eax, 00000001h
    ret
EndProc MiniVDD_GetMonitorPowerStateCaps

; Handle VESA DPMS Set Display Power State before the video BIOS sees it.
; Legacy master VDDs use this route when their dispatch table predates the
; Windows 98 4.1 monitor-power entries.  Carry set means fully handled.
; Entry: AX=VESA function, EBP=Client_Reg_Struc (BL=0 query or BL=1 set).
BeginProc MiniVDD_VESASupport
    cmp     ax, 4f10h
    jne     short V9xMini_Vesa_Default
    cmp     [ebp.Client_BL], 00h
    je      short V9xMini_Vesa_Query
    cmp     [ebp.Client_BL], 01h
    jne     short V9xMini_Vesa_Default
    push    ecx
    mov     cl, [ebp.Client_BH]
    test    cl, cl
    jz      short V9xMini_Vesa_D0
    cmp     cl, 01h
    je      short V9xMini_Vesa_D1
    cmp     cl, 02h
    je      short V9xMini_Vesa_D2
    cmp     cl, 04h
    je      short V9xMini_Vesa_D3
    pop     ecx
    jmp     short V9xMini_Vesa_Default
V9xMini_Vesa_D0:
    xor     ecx, ecx
    jmp     short V9xMini_Vesa_Apply
V9xMini_Vesa_D1:
    mov     ecx, 10h
    jmp     short V9xMini_Vesa_Apply
V9xMini_Vesa_D2:
    mov     ecx, 40h
    jmp     short V9xMini_Vesa_Apply
V9xMini_Vesa_D3:
    mov     ecx, 50h
V9xMini_Vesa_Apply:
    call    V9xMini_Set_Dpms
    mov     [ebp.Client_AX], 004fh      ; VESA call supported and successful
    pop     ecx
    stc
    ret
V9xMini_Vesa_Query:
    mov     [ebp.Client_BX], 0000h      ; no low-power DPMS states supported
    mov     [ebp.Client_AX], 004fh
    stc
    ret
V9xMini_Vesa_Default:
    clc
    ret
EndProc MiniVDD_VESASupport

; Legacy Win9x master VDDs issue VESA 4F10h directly when the 4.1 monitor-
; power callbacks are unavailable.  The post hook runs after that BIOS call;
; on a Set Display Power State / D0 request, force the S3 DPMS controls back
; on so a BIOS/emulator combination cannot leave the display latched blank.
; Entry: DX = VESA function, EBP = Client_Reg_Struc.  Preserve used registers.
BeginProc MiniVDD_VESACallPostProcessing
    cmp     dx, 4f10h
    jne     short V9xMini_Vesa_Post_Done
    push    eax
    mov     ax, [ebp.Client_BX]
    cmp     al, 01h                   ; Set Display Power State
    jne     short V9xMini_Vesa_Post_Restore
    test    ah, ah                    ; BH=0 is D0 / monitor on
    jnz     short V9xMini_Vesa_Post_Restore
    push    ecx
    xor     ecx, ecx
    call    V9xMini_Set_Dpms
    pop     ecx
V9xMini_Vesa_Post_Restore:
    pop     eax
V9xMini_Vesa_Post_Done:
    ret
EndProc MiniVDD_VESACallPostProcessing

; Protected-mode API, reached from the 16-bit display driver through
; INT 2Fh AX=1684h BX=V9XMINI_DEVICE_ID.
;
; Entry: EBP = Client_Reg_Struc, client AX = function.
;
; Every function is a read of the table collected at init. Nothing here calls
; the BIOS, allocates, or touches a register of the card: the caller is a
; display driver part-way through GDI initialisation, and the whole reason the
; queries happen at init is so that this path cannot do anything that might
; fault one. An unknown function returns AX=0 rather than failing the call.
BeginProc MiniVDD_PM_API

    movzx   eax, [ebp.Client_AX]

    cmp     ax, V9XMINI_FN_HANDSHAKE
    je      short V9xMini_Api_Handshake
    cmp     ax, V9XMINI_FN_CONTROLLER
    je      short V9xMini_Api_Controller
    cmp     ax, V9XMINI_FN_MODE_INFO
    je      short V9xMini_Api_ModeInfo
    cmp     ax, V9XMINI_FN_STATUS
    je      short V9xMini_Api_Status

    ; Unknown function.
    mov     [ebp.Client_AX], 0
    ret

; What the init-time collection actually managed, so a failure can be diagnosed
; from the guest instead of guessed at from the host.
;
; Out: AX=1, EBX = the V86 segment it used (0 if it never got one),
;      ECX = how many mode entries came back valid, EDX = controller validity.
V9xMini_Api_Status:
    push    ecx
    push    edx
    push    edi

    mov     [ebp.Client_AX], 1
    movzx   eax, V9xVbeBufSeg
    mov     [ebp.Client_EBX], eax

    xor     ecx, ecx
    xor     edi, edi
V9xMini_Api_Status_Next:
    cmp     edi, V9X_VBE_CACHE_COUNT
    jae     short V9xMini_Api_Status_Done
    cmp     V9xVbeValid[edi*2], 0
    je      short V9xMini_Api_Status_Skip
    inc     ecx
V9xMini_Api_Status_Skip:
    inc     edi
    jmp     short V9xMini_Api_Status_Next

V9xMini_Api_Status_Done:
    mov     [ebp.Client_ECX], ecx
    movzx   eax, V9xVbeCtrlValid
    mov     [ebp.Client_EDX], eax

    pop     edi
    pop     edx
    pop     ecx
    ret

; Out: AX=1, EBX=magic, ECX=contract version.
V9xMini_Api_Handshake:
    mov     [ebp.Client_AX], 1
    mov     [ebp.Client_EBX], V9XMINI_API_MAGIC
    mov     [ebp.Client_ECX], V9XMINI_API_VERSION
    ret

; Out: AX=1 when 4F00h answered, EBX=VBE version, ECX=TotalMemory in 64 KiB
;      blocks. AX=0 when the query failed or never ran.
V9xMini_Api_Controller:
    mov     ax, V9xVbeCtrlValid
    mov     [ebp.Client_AX], ax
    movzx   eax, V9xVbeCtrlVer
    mov     [ebp.Client_EBX], eax
    movzx   eax, V9xVbeCtrl64K
    mov     [ebp.Client_ECX], eax
    ret

; In:  client CX = VBE mode number.
; Out: AX=1 when that mode is in the cache and 4F01h answered for it, and then
;      EBX = PhysBasePtr
;      ECX = BytesPerScanLine in the low word, ModeAttributes in the high word
;      EDX = Width in the low word, Height in the high word
;      ESI = BitsPerPixel in the low word, MemoryModel in the high word
;      AX=0 otherwise, with the other registers left alone.
;
; The 16-bit side rebuilds its own summary from these and applies the same
; credibility and stride checks it has always applied, in host-tested C. This
; hands over facts and keeps the judgement where it can be tested.
V9xMini_Api_ModeInfo:
    push    ebx
    push    ecx
    push    edx
    push    esi
    push    edi

    movzx   ecx, [ebp.Client_CX]
    xor     edi, edi
V9xMini_Api_Mode_Next:
    cmp     edi, V9X_VBE_CACHE_COUNT
    jae     short V9xMini_Api_Mode_Missing
    mov     ax, V9xVbeModeList[edi*2]
    cmp     ax, cx
    je      short V9xMini_Api_Mode_Found
    inc     edi
    jmp     short V9xMini_Api_Mode_Next

V9xMini_Api_Mode_Missing:
    mov     [ebp.Client_AX], 0
    jmp     short V9xMini_Api_Mode_Done

V9xMini_Api_Mode_Found:
    cmp     V9xVbeValid[edi*2], 0
    je      short V9xMini_Api_Mode_Missing

    mov     [ebp.Client_AX], 1

    mov     ebx, V9xVbeBase[edi*4]
    mov     [ebp.Client_EBX], ebx

    movzx   ecx, V9xVbeAttr[edi*2]
    shl     ecx, 16
    movzx   eax, V9xVbeBytes[edi*2]
    or      ecx, eax
    mov     [ebp.Client_ECX], ecx

    movzx   edx, V9xVbeHeight[edi*2]
    shl     edx, 16
    movzx   eax, V9xVbeWidth[edi*2]
    or      edx, eax
    mov     [ebp.Client_EDX], edx

    movzx   esi, V9xVbeModel[edi*2]
    shl     esi, 16
    movzx   eax, V9xVbeBpp[edi*2]
    or      esi, eax
    mov     [ebp.Client_ESI], esi

V9xMini_Api_Mode_Done:
    pop     edi
    pop     esi
    pop     edx
    pop     ecx
    pop     ebx
    ret
EndProc MiniVDD_PM_API

VxD_LOCKED_CODE_ENDS

VxD_ICODE_SEG

; Run one buffered VBE call in V86 mode and leave the BIOS status in AX.
;
; In:  AX = VBE function (4F00h or 4F01h), CX = its argument.
; Out: AX = the BIOS reply, or 0 if there was no buffer to hand it.
;
; The buffer has to be addressable by the real-mode BIOS, which is the entire
; difficulty this routine exists to remove: from ring 3 the driver could get
; neither a DOS block out of the DPMI host nor a working simulated interrupt.
; Here there is no DPMI host in the way - the V86 data area is allocated by the
; VMM and the interrupt runs through nested execution.
;
; Client state is saved and restored around the call, because the client
; registers belong to whatever was running and this borrows them.
;
; EBP has to be loaded from the VM control block first. Every Client_* reference
; is an offset off EBP, and at Device_Init nothing has set EBP up - it is the
; event handlers that get it for free. The first version of this routine assumed
; otherwise and wrote the BIOS request to wherever EBP happened to point, so the
; interrupt ran with whatever registers the VM already had and every call came
; back useless. That is what "s=1829 m=0 c=0" in the boot trace meant: a buffer
; allocated fine, and not one usable answer.
BeginProc V9xMini_Vbe_Call

    cmp     V9xVbeBufSeg, 0
    jne     short V9xMini_Vbe_Call_Ready
    xor     eax, eax
    ret

V9xMini_Vbe_Call_Ready:
    push    ebx
    push    ecx
    push    edx
    push    esi
    push    edi
    push    ebp

    movzx   esi, ax                     ; hold the function
    movzx   edx, cx                     ; hold its argument

    VMMcall Get_Cur_VM_Handle           ; EBX = the VM to run this in
    mov     ebp, [ebx.CB_Client_Pointer]

    Push_Client_State
    VMMcall Begin_Nest_V86_Exec         ; V86, because this is a BIOS interrupt

    mov     [ebp.Client_AX], si
    mov     [ebp.Client_CX], dx
    mov     ax, V9xVbeBufSeg
    mov     [ebp.Client_ES], ax
    mov     [ebp.Client_DI], 0

    mov     eax, 10h
    VMMcall Exec_Int

    movzx   esi, [ebp.Client_AX]        ; reply, before the state is restored

    VMMcall End_Nest_Exec
    Pop_Client_State

    mov     eax, esi

    pop     ebp
    pop     edi
    pop     esi
    pop     edx
    pop     ecx
    pop     ebx
    ret
EndProc V9xMini_Vbe_Call

; Read a word out of the V86 scratch at offset AX, returning it in AX.
;
; The scratch sits in the first megabyte, so its ring-0 linear address is the
; segment shifted left four - no mapping needed.
BeginProc V9xMini_Vbe_Peek_Word
    push    edx
    movzx   edx, V9xVbeBufSeg
    shl     edx, 4
    movzx   eax, ax
    add     edx, eax
    mov     ax, [edx]
    pop     edx
    ret
EndProc V9xMini_Vbe_Peek_Word

; Collect 4F00h and the seven standard 4F01h answers into the cache.
;
; Nothing here is fatal. A BIOS that refuses leaves the entries invalid, the API
; reports that, and tier-0 refuses at stage 3 exactly as it does today - which
; is the behaviour this replaces, not a regression on it.
BeginProc V9xMini_Vbe_Collect

    push    ebx
    push    ecx
    push    edx
    push    esi
    push    edi

    ; 512 bytes is what 4F00h may write. VMM_ICODE only, hence init.
    VMMcall _Allocate_Global_V86_Data_Area, <512, 0>
    test    eax, eax
    jz      V9xMini_Vbe_Collect_Done
    shr     eax, 4                      ; linear in the first MiB -> segment
    mov     V9xVbeBufSeg, ax

    ; 4F00h. Stamp "VBE2" first so a 2.0-aware BIOS fills in the longer block.
    movzx   edx, V9xVbeBufSeg
    shl     edx, 4
    mov     dword ptr [edx], 32454256h  ; 'VBE2'
    mov     ax, 4F00h
    xor     cx, cx
    call    V9xMini_Vbe_Call
    cmp     ax, 004Fh
    jne     short V9xMini_Vbe_Collect_Modes

    ; Accept only a real VESA 2.0-or-later answer, matching vbe_parse.c.
    mov     ax, 0
    call    V9xMini_Vbe_Peek_Word
    cmp     ax, 4556h                   ; 'EV' - first half of "VESA"
    jne     short V9xMini_Vbe_Collect_Modes
    mov     ax, 4
    call    V9xMini_Vbe_Peek_Word
    cmp     ax, 0200h
    jb      short V9xMini_Vbe_Collect_Modes
    mov     V9xVbeCtrlVer, ax
    mov     ax, 18
    call    V9xMini_Vbe_Peek_Word
    test    ax, ax
    jz      short V9xMini_Vbe_Collect_Modes
    mov     V9xVbeCtrl64K, ax
    mov     V9xVbeCtrlValid, 1

V9xMini_Vbe_Collect_Modes:
    xor     edi, edi
V9xMini_Vbe_Collect_Next:
    cmp     edi, V9X_VBE_CACHE_COUNT
    jae      V9xMini_Vbe_Collect_Done

    mov     ax, 4F01h
    mov     cx, V9xVbeModeList[edi*2]
    call    V9xMini_Vbe_Call
    cmp     ax, 004Fh
    jne      V9xMini_Vbe_Collect_Skip

    mov     ax, 0                       ; ModeAttributes
    call    V9xMini_Vbe_Peek_Word
    mov     V9xVbeAttr[edi*2], ax
    mov     ax, 16                      ; BytesPerScanLine
    call    V9xMini_Vbe_Peek_Word
    mov     V9xVbeBytes[edi*2], ax
    mov     ax, 18                      ; XResolution
    call    V9xMini_Vbe_Peek_Word
    mov     V9xVbeWidth[edi*2], ax
    mov     ax, 20                      ; YResolution
    call    V9xMini_Vbe_Peek_Word
    mov     V9xVbeHeight[edi*2], ax
    mov     ax, 25                      ; BitsPerPixel, a byte field
    call    V9xMini_Vbe_Peek_Word
    and     ax, 00FFh
    mov     V9xVbeBpp[edi*2], ax
    mov     ax, 27                      ; MemoryModel, a byte field
    call    V9xMini_Vbe_Peek_Word
    and     ax, 00FFh
    mov     V9xVbeModel[edi*2], ax

    ; PhysBasePtr, two words so this needs no aligned dword read.
    mov     ax, 40
    call    V9xMini_Vbe_Peek_Word
    movzx   ebx, ax
    mov     ax, 42
    call    V9xMini_Vbe_Peek_Word
    movzx   eax, ax
    shl     eax, 16
    or      ebx, eax
    mov     V9xVbeBase[edi*4], ebx

    mov     V9xVbeValid[edi*2], 1

V9xMini_Vbe_Collect_Skip:
    inc     edi
    jmp      V9xMini_Vbe_Collect_Next

V9xMini_Vbe_Collect_Done:
    pop     edi
    pop     esi
    pop     edx
    pop     ecx
    pop     ebx
    ret
EndProc V9xMini_Vbe_Collect

public MiniVDD_Dynamic_Init
BeginProc MiniVDD_Dynamic_Init
    mov     esi, OFFSET32 V9xMiniInitLine
    mov     ecx, V9xMiniInitLineLength
    call    V9xMini_Serial_Write

    VxDCall VDD_Get_Mini_Dispatch_Table
    test    edi, edi
    jz      short V9xMini_Init_Failed
    cmp     ecx, NBR_MINI_VDD_FUNCTIONS
    jb      short V9xMini_Init_Failed

    ; These callbacks exist in the legacy 49-entry table.  VESA_SUPPORT
    ; prevents the problematic BIOS DPMS call; the post hook is a D0 safety
    ; net for calls that another component sends directly to the BIOS.
    MiniVDDDispatch VESA_SUPPORT, VESASupport
    MiniVDDDispatch VESA_CALL_POST_PROCESSING, VESACallPostProcessing

    ; Power callbacks were added to the Windows 98 (4.1) dispatch table.
    ; Older tables are covered by the VESA post hook above.
    cmp     ecx, GET_MONITOR_POWER_STATE_CAPS + 1
    jb      short V9xMini_Power_Defaults
    MiniVDDDispatch SET_MONITOR_POWER_STATE, SetMonitorPowerState
    MiniVDDDispatch GET_MONITOR_POWER_STATE_CAPS, GetMonitorPowerStateCaps

    mov     esi, OFFSET32 V9xMiniPowerCallbacksLine
    mov     ecx, V9xMiniPowerCallbacksLineLength
    call    V9xMini_Serial_Write
    jmp     short V9xMini_Init_Succeeded

V9xMini_Power_Defaults:

    mov     esi, OFFSET32 V9xMiniDefaultsLine
    mov     ecx, V9xMiniDefaultsLineLength
    call    V9xMini_Serial_Write
V9xMini_Init_Succeeded:
    ; Collect the VBE answers for the tier-0 driver. Deliberately last, and
    ; deliberately not able to fail the init: an adapter with no usable VESA
    ; BIOS should still get the DPMS and power callbacks above.
    call    V9xMini_Vbe_Collect
    xor     eax, eax
    clc
    ret

V9xMini_Init_Failed:
    mov     esi, OFFSET32 V9xMiniFailLine
    mov     ecx, V9xMiniFailLineLength
    call    V9xMini_Serial_Write
    stc
    ret
EndProc MiniVDD_Dynamic_Init
VxD_ICODE_ENDS

VxD_LOCKED_CODE_SEG
Begin_Control_Dispatch MiniVDD
    Control_Dispatch Device_Init, MiniVDD_Dynamic_Init
    Control_Dispatch Sys_Dynamic_Device_Init, MiniVDD_Dynamic_Init
End_Control_Dispatch MiniVDD
VxD_LOCKED_CODE_ENDS

end
