; Gen5 (Ironlake) EDID through the PCH's GMBUS controller.
;
; The VBE route has failed on W9QI2F4F at every boot: the mini-VDD's
; 4F15h BL=00h succeeds, BL=01h does not (V9XBOOT.INI VbeCache f=0407,
; V9X_VBE_ST_EDID_FAILED), where the 2026-09-28 DOS survey read a valid
; block through the same BIOS. This reads the monitor directly, with no
; BIOS in the way, so a failure here is the monitor or the cable.
;
; The sequence is Linux 6.6 intel_gmbus.c's for a 128-byte read at slave
; 50h, index 0, checked against the Ironlake PRM Vol 3 Part 3 2.2.3
; (GMBUS0-GMBUS4 at C5100h-C5110h):
;
;   GMBUS2 read          - takes the INUSE semaphore; a 1 means another
;                          owner, and the pin is left alone;
;   GMBUS1 = SW_CLR_INT, then 0
;                        - the controller's local reset;
;   GMBUS0 = pin pair    - 100 kHz, hold time 0;
;   GMBUS1 = SW_RDY | cycle 011b (index, no stop, wait) | 128 << 16 |
;            index 0 | 50h << 1 | read;
;   32 times: wait for HW_RDY (or NAK), read GMBUS3;
;   GMBUS1 = SW_RDY | cycle 100b (generate STOP); wait for ACTIVE clear;
;   GMBUS0 = 0; GMBUS2 = INUSE, which releases the semaphore.
;
; On a NAK or a timeout the controller is reset as i915 does it
; (SW_CLR_INT, then 0) before GMBUS0 and the semaphore are released.
;
; Writes: GMBUS0, GMBUS1 and GMBUS2 only, each left as found at reset
; (GMBUS0 0, GMBUS1 0, the semaphore free). Pin pairs 2-6 only: 1 is the
; SSC clock device (PRM table, "LCTRCLK"), 7 is reserved. 0CF8h is saved
; and restored around the one configuration read.

.386p

.xlist
include VMM.INC
include VWIN32.INC
.list

Declare_Virtual_Device G5DD, 1, 0, G5D_Control, \
                       Undefined_Device_ID, Undefined_Init_Order, , ,

G5D_MAGIC               equ 44443547h ; "G5DD"
G5D_DIOC_READ           equ 1
G5D_IGD_ADDRESS         equ 80001000h ; bus 0, device 2, function 0
G5D_VENDOR_INTEL        equ 8086h
G5D_MMIO_BYTES          equ 00100000h
G5D_BASE_ALIGN          equ 003fffffh

; PCH GMBUS (PRM Vol 3 Part 3 2.2.3).
G5D_GMBUS0              equ 0c5100h
G5D_GMBUS1              equ 0c5104h
G5D_GMBUS2              equ 0c5108h
G5D_GMBUS3              equ 0c510ch

G5D_GMBUS1_SW_CLR_INT   equ 80000000h
G5D_GMBUS1_SW_RDY       equ 40000000h
G5D_GMBUS1_INDEX_WAIT   equ 06000000h  ; cycle 011b
G5D_GMBUS1_STOP         equ 08000000h  ; cycle 100b
G5D_GMBUS1_EDID_READ    equ 008000a1h  ; 128 bytes, index 0, 50h, read

G5D_GMBUS2_INUSE        equ 00008000h
G5D_GMBUS2_HW_RDY       equ 00000800h
G5D_GMBUS2_NAK          equ 00000400h
G5D_GMBUS2_ACTIVE       equ 00000200h

; About a second of MMIO reads; 128 bytes at 100 kHz take 12 ms.
G5D_POLLS               equ 1000000
G5D_PIN_FIRST           equ 2
G5D_PIN_LAST            equ 6
G5D_EDID_DWORDS         equ 32

; Per-pin outcome codes, in the slot's dword 1.
G5D_OUT_NOT_ASKED       equ 0
G5D_OUT_OK              equ 1
G5D_OUT_IN_USE          equ 2
G5D_OUT_NAK             equ 3
G5D_OUT_TIMEOUT_DATA    equ 4
G5D_OUT_TIMEOUT_IDLE    equ 5

; Status bits.
G5D_ST_IGD_INTEL        equ 00000001h
G5D_ST_BASE_INVALID     equ 00000002h
G5D_ST_MMIO_MAPPED      equ 00000004h
G5D_ST_NOT_GEN5         equ 00000008h
G5D_ST_PINS_REFUSED     equ 00000010h

; Input from V9XG5DD.EXE: GTTMMADR, then a mask of pin pairs (bit n =
; pair n).
G5D_IN_BASE             equ 0
G5D_IN_PINS             equ 4
G5D_IN_BYTES            equ 8

; Output: an 8-dword header, then one 36-dword slot per pin pair 1-6
; (slot n-1 for pair n): pair, outcome, GMBUS2 at the end, bytes read,
; and 32 dwords of data. Must match gen5_ddc_probe_win32.c.
G5D_HEADER_DWORDS       equ 8
G5D_SLOT_DWORDS         equ 36
G5D_SLOTS               equ 6
G5D_RESULT_DWORDS       equ (G5D_HEADER_DWORDS + G5D_SLOTS * G5D_SLOT_DWORDS)

VxD_LOCKED_DATA_SEG
; Ironlake, and Ivy Bridge (0162h, 0166h) as the positive control: its
; Panther Point PCH has the same GMBUS at C5100h and pin pairs (Linux 6.6
; intel_gmbus.c drives every PCH through one path), its BAR0 the same 4 MB
; alignment, and W9QI4E14's monitor answers 4F15h, so a NAK there would
; say the probe is wrong.
G5DDevices label word
    dw 0042h, 0046h, 0162h, 0166h
G5D_DEVICE_COUNT        equ 4

G5DResult label dword
    dd G5D_MAGIC                ; 00 magic
    dd 0                        ; 01 status
    dd 0                        ; 02 IGD vendor/device as read
    dd 0                        ; 03 GTTMMADR base used
    dd 0                        ; 04 pin mask asked
    dd 0                        ; 05 0CF8h found and restored
    dd 0                        ; 06 reserved
    dd 0                        ; 07 reserved
G5DSlots     dd G5D_SLOTS * G5D_SLOT_DWORDS dup (0)
G5DBase      dd 0
G5DPins      dd 0
G5DMmio      dd 0
VxD_LOCKED_DATA_ENDS

VxD_LOCKED_CODE_SEG

; EAX = PCI configuration address. Returns EAX = configuration dword.
BeginProc G5D_Pci_Read
    push    ebx
    push    edx
    pushfd
    cli
    mov     ebx, eax
    mov     dx, 0cf8h
    in      eax, dx
    mov     G5DResult[20], eax
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
EndProc G5D_Pci_Read

; AX = device id. Returns CF clear if it is in G5DDevices.
BeginProc G5D_Device_Known
    push    ecx
    push    esi
    mov     esi, OFFSET32 G5DDevices
    mov     ecx, G5D_DEVICE_COUNT
G5D_Device_Next:
    cmp     ax, [esi]
    je      short G5D_Device_Found
    add     esi, 2
    dec     ecx
    jnz     short G5D_Device_Next
    pop     esi
    pop     ecx
    stc
    ret
G5D_Device_Found:
    pop     esi
    pop     ecx
    clc
    ret
EndProc G5D_Device_Known

; ESI = MMIO mapping, EBX = pin pair, EDI = its slot. One EDID read.
BeginProc G5D_Read_Pin
    pushad
    mov     [edi], ebx

    ; The semaphore: a zero read takes it, a one is someone else's.
    mov     eax, [esi+G5D_GMBUS2]
    test    eax, G5D_GMBUS2_INUSE
    jz      short G5D_Pin_Acquired
    mov     dword ptr [edi+4], G5D_OUT_IN_USE
    mov     [edi+8], eax
    popad
    ret

G5D_Pin_Acquired:
    mov     dword ptr [esi+G5D_GMBUS1], G5D_GMBUS1_SW_CLR_INT
    mov     dword ptr [esi+G5D_GMBUS1], 0
    mov     [esi+G5D_GMBUS0], ebx
    mov     dword ptr [esi+G5D_GMBUS1], G5D_GMBUS1_SW_RDY or \
                                        G5D_GMBUS1_INDEX_WAIT or \
                                        G5D_GMBUS1_EDID_READ
    xor     edx, edx                    ; bytes read
    lea     ebp, [edi+16]
    mov     ecx, G5D_EDID_DWORDS
G5D_Pin_Next_Dword:
    push    ecx
    mov     ecx, G5D_POLLS
G5D_Pin_Poll:
    mov     eax, [esi+G5D_GMBUS2]
    test    eax, G5D_GMBUS2_NAK
    jnz     short G5D_Pin_Nak
    test    eax, G5D_GMBUS2_HW_RDY
    jnz     short G5D_Pin_Ready
    dec     ecx
    jnz     short G5D_Pin_Poll
    pop     ecx
    mov     dword ptr [edi+4], G5D_OUT_TIMEOUT_DATA
    jmp     short G5D_Pin_Abort
G5D_Pin_Ready:
    pop     ecx
    mov     eax, [esi+G5D_GMBUS3]
    mov     [ebp], eax
    add     ebp, 4
    add     edx, 4
    dec     ecx
    jnz     short G5D_Pin_Next_Dword

    ; STOP, then wait for the bus to go idle.
    mov     dword ptr [esi+G5D_GMBUS1], G5D_GMBUS1_SW_RDY or G5D_GMBUS1_STOP
    mov     ecx, G5D_POLLS
G5D_Pin_Idle:
    mov     eax, [esi+G5D_GMBUS2]
    test    eax, G5D_GMBUS2_ACTIVE
    jz      short G5D_Pin_Done_Ok
    dec     ecx
    jnz     short G5D_Pin_Idle
    mov     dword ptr [edi+4], G5D_OUT_TIMEOUT_IDLE
    jmp     short G5D_Pin_Abort
G5D_Pin_Done_Ok:
    mov     dword ptr [edi+4], G5D_OUT_OK
    jmp     short G5D_Pin_Release

G5D_Pin_Nak:
    pop     ecx
    mov     dword ptr [edi+4], G5D_OUT_NAK
G5D_Pin_Abort:
    mov     dword ptr [esi+G5D_GMBUS1], G5D_GMBUS1_SW_CLR_INT
    mov     dword ptr [esi+G5D_GMBUS1], 0
G5D_Pin_Release:
    mov     [edi+12], edx
    mov     eax, [esi+G5D_GMBUS2]
    mov     [edi+8], eax
    mov     dword ptr [esi+G5D_GMBUS0], 0
    mov     dword ptr [esi+G5D_GMBUS2], G5D_GMBUS2_INUSE
    popad
    ret
EndProc G5D_Read_Pin

BeginProc G5D_Read
    pushad

    ; An Ironlake IGD at 00:02.0, and a base with the BAR's alignment.
    mov     eax, G5D_IGD_ADDRESS
    call    G5D_Pci_Read
    mov     G5DResult[8], eax
    cmp     ax, G5D_VENDOR_INTEL
    jne     G5D_Read_Bad_Base
    or      G5DResult[4], G5D_ST_IGD_INTEL
    shr     eax, 16
    call    G5D_Device_Known
    jnc     short G5D_Read_Check_Base
    or      G5DResult[4], G5D_ST_NOT_GEN5
    jmp     G5D_Read_Done

G5D_Read_Check_Base:
    mov     eax, G5DBase
    test    eax, eax
    jz      G5D_Read_Bad_Base
    test    eax, G5D_BASE_ALIGN
    jnz     G5D_Read_Bad_Base
    mov     G5DResult[12], eax

    VMMcall _MapPhysToLinear,<eax,G5D_MMIO_BYTES,0>
    cmp     eax, 0ffffffffh
    je      G5D_Read_Done
    mov     G5DMmio, eax
    or      G5DResult[4], G5D_ST_MMIO_MAPPED

    mov     esi, eax
    mov     ebx, G5D_PIN_FIRST
G5D_Read_Next_Pin:
    bt      G5DPins, ebx
    jnc     short G5D_Read_Skip_Pin
    lea     eax, [ebx-1]
    imul    eax, G5D_SLOT_DWORDS * 4
    lea     edi, G5DSlots[eax]
    call    G5D_Read_Pin
G5D_Read_Skip_Pin:
    inc     ebx
    cmp     ebx, G5D_PIN_LAST
    jbe     short G5D_Read_Next_Pin
    jmp     short G5D_Read_Done

G5D_Read_Bad_Base:
    or      G5DResult[4], G5D_ST_BASE_INVALID
G5D_Read_Done:
    popad
    ret
EndProc G5D_Read

BeginProc G5D_Reset_Result
    pushad
    mov     edi, OFFSET32 G5DResult + 4
    mov     ecx, G5D_RESULT_DWORDS - 1
    xor     eax, eax
    cld
    rep     stosd
    mov     G5DMmio, eax
    popad
    ret
EndProc G5D_Reset_Result

BeginProc G5D_W32_DeviceIoControl
    cmp     ecx, DIOC_OPEN
    je      G5D_Dioc_Ok
    cmp     ecx, DIOC_CLOSEHANDLE
    je      G5D_Dioc_Ok
    cmp     ecx, G5D_DIOC_READ
    jne     G5D_Dioc_Fail

    pushad
    mov     ebx, esi
    mov     ebp, [ebx.lpvInBuffer]
    test    ebp, ebp
    jz      short G5D_Dioc_Copy_Fail
    cmp     [ebx.cbInBuffer], G5D_IN_BYTES
    jb      short G5D_Dioc_Copy_Fail
    mov     edi, [ebx.lpvOutBuffer]
    test    edi, edi
    jz      short G5D_Dioc_Copy_Fail
    cmp     [ebx.cbOutBuffer], G5D_RESULT_DWORDS * 4
    jb      short G5D_Dioc_Copy_Fail

    call    G5D_Reset_Result
    mov     eax, [ebp+G5D_IN_BASE]
    mov     G5DBase, eax
    mov     eax, [ebp+G5D_IN_PINS]
    mov     G5DResult[16], eax
    ; Only pairs 2-6; asking for any other refuses the whole call.
    test    eax, NOT (((1 shl (G5D_PIN_LAST + 1)) - 1) AND \
                      NOT ((1 shl G5D_PIN_FIRST) - 1))
    jz      short G5D_Dioc_Pins_Ok
    or      G5DResult[4], G5D_ST_PINS_REFUSED
    jmp     short G5D_Dioc_Copy_Out
G5D_Dioc_Pins_Ok:
    mov     G5DPins, eax
    call    G5D_Read

G5D_Dioc_Copy_Out:
    mov     edi, [ebx.lpvOutBuffer]
    mov     esi, OFFSET32 G5DResult
    mov     ecx, G5D_RESULT_DWORDS
    cld
    rep     movsd
    mov     eax, [ebx.lpcbBytesReturned]
    test    eax, eax
    jz      short G5D_Dioc_Copy_Done
    mov     dword ptr [eax], G5D_RESULT_DWORDS * 4
G5D_Dioc_Copy_Done:
    popad
G5D_Dioc_Ok:
    xor     eax, eax
    ret

G5D_Dioc_Copy_Fail:
    popad
G5D_Dioc_Fail:
    mov     eax, 1
    ret
EndProc G5D_W32_DeviceIoControl

BeginProc G5D_Dynamic_Init
    clc
    ret
EndProc G5D_Dynamic_Init

BeginProc G5D_Dynamic_Exit
    clc
    ret
EndProc G5D_Dynamic_Exit

Begin_Control_Dispatch G5D
    Control_Dispatch Sys_Dynamic_Device_Init, G5D_Dynamic_Init
    Control_Dispatch Sys_Dynamic_Device_Exit, G5D_Dynamic_Exit
    Control_Dispatch W32_DeviceIoControl, G5D_W32_DeviceIoControl
End_Control_Dispatch G5D

VxD_LOCKED_CODE_ENDS

end
