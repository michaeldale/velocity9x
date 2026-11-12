; Gen3 texture pool, Phase 0 probe VxD: allocation and GTT, nothing written.
;
; docs\plans\intel-gen3-system-memory-textures.md, Phase 0; authorised by
; docs\decisions\2026-11-12-intel-gen3-texture-pool-decisions.md. Loaded
; dynamically by V9XG3PL.EXE, which passes BAR3 (the 945's GTTADR, config
; 1Ch), a window of GTT entries to read and a page count to allocate.
;
; It does three things and writes nothing to the device:
;
;  1. Reads the GTT through BAR3: GTT_WINDOW raw entries from the window's
;     first entry, and over [first, first + span) a valid count, a count of
;     entries equal to the window's first one (the VBIOS's scratch fill),
;     and an FNV-1a hash, so a run before and after a mode change says
;     whether the mode set rewrote the entries the pool will use.
;  2. Asks _PageAllocate for the page count as fixed, zeroed PG_SYS memory,
;     timed with Get_System_Time, with _GetFreePageCount before and after.
;  3. Copies the allocation's page table with _CopyPageTable - the physical
;     addresses the pool would map - then frees the pages.
;
; The pages are touched only by the VMM's zero fill. Nothing maps them into
; the GTT; that is Phase 2.

.386p

.xlist
include VMM.INC
include VWIN32.INC
.list

Declare_Virtual_Device G3PL, 1, 0, G3_Control, \
                       Undefined_Device_ID, Undefined_Init_Order, , ,

G3_MAGIC                equ 4c503347h ; "G3PL"
G3_DIOC_CAPTURE         equ 1
G3_IGD_ADDRESS          equ 80001000h ; bus 0, device 2, function 0
G3_VENDOR_INTEL         equ 8086h
G3_GTT_BYTES            equ 00040000h ; 945: 256 KiB, 65,536 entries
G3_GTT_ENTRIES          equ 65536
G3_GTT_ALIGN            equ 0003ffffh ; the BAR's own alignment
G3_GTT_WINDOW           equ 64
G3_PAGE_MAX             equ 8192      ; 32 MiB, decision 1
G3_FNV_OFFSET           equ 811c9dc5h
G3_FNV_PRIME            equ 01000193h

; Status bits returned to the publisher.
G3_ST_IGD_INTEL         equ 00000001h
G3_ST_BASE_INVALID      equ 00000002h
G3_ST_NOT_GEN3          equ 00000004h
G3_ST_GTT_MAPPED        equ 00000008h
G3_ST_GTT_READ          equ 00000010h
G3_ST_ARGS_REFUSED      equ 00000020h
G3_ST_ALLOCATED         equ 00000040h
G3_ST_ALLOC_FAILED      equ 00000080h
G3_ST_TABLE_COPIED      equ 00000100h
G3_ST_TABLE_FAILED      equ 00000200h
G3_ST_FREED             equ 00000400h
G3_ST_FREE_FAILED       equ 00000800h

; Input from V9XG3PL.EXE.
G3_IN_BASE              equ 0
G3_IN_FIRST             equ 4
G3_IN_SPAN              equ 8
G3_IN_PAGES             equ 12
G3_IN_BYTES             equ 16

; Output layout, in dwords. Must match struct v9x_g3pl_result in
; gen3_pool_probe_win32.c.
G3_HEADER_DWORDS        equ 32
G3_RESULT_DWORDS        equ (G3_HEADER_DWORDS + G3_GTT_WINDOW + G3_PAGE_MAX)

VxD_LOCKED_DATA_SEG
; Gen3 IGD device ids, function 0, the ones this family claims: 915GM 2592,
; 945GM 27A2, 945GME/GSE 27AE, Pineview A011, G33 29C2.
G3Devices label word
    dw 2592h, 27a2h, 27aeh, 0a011h, 29c2h
G3_DEVICE_COUNT         equ 5

G3Result label dword
    dd G3_MAGIC                 ; 00 magic
    dd 0                        ; 01 status
    dd 0                        ; 02 IGD vendor/device as read
    dd 0                        ; 03 BAR3 used
    dd 0                        ; 04 window first entry
    dd 0                        ; 05 span
    dd 0                        ; 06 valid entries in the span
    dd 0                        ; 07 entries equal to the first in the span
    dd 0                        ; 08 FNV-1a of the span
    dd 0                        ; 09 pages asked for
    dd 0                        ; 10 free pages before
    dd 0                        ; 11 lockable pages before
    dd 0                        ; 12 free pages after allocating
    dd 0                        ; 13 lockable pages after allocating
    dd 0                        ; 14 allocation milliseconds
    dd 0                        ; 15 allocation linear address
    dd 0                        ; 16 _CopyPageTable result
    dd 0                        ; 17 free pages after freeing
    dd 0                        ; 18 0CF8h found and restored
    dd 0                        ; 19 reserved
    dd 0                        ; 20 reserved
    dd 0                        ; 21 reserved
    dd 0                        ; 22 reserved
    dd 0                        ; 23 reserved
    dd 0                        ; 24 reserved
    dd 0                        ; 25 reserved
    dd 0                        ; 26 reserved
    dd 0                        ; 27 reserved
    dd 0                        ; 28 reserved
    dd 0                        ; 29 reserved
    dd 0                        ; 30 reserved
    dd 0                        ; 31 reserved
G3Window     dd G3_GTT_WINDOW dup (0)
G3Pages      dd G3_PAGE_MAX dup (0)
G3Base       dd 0
G3First      dd 0
G3Span       dd 0
G3Count      dd 0
G3GttLinear  dd 0
G3Handle     dd 0
G3Linear     dd 0
G3Started    dd 0
VxD_LOCKED_DATA_ENDS

VxD_LOCKED_CODE_SEG

; EAX = PCI configuration address. Returns EAX = configuration dword.
; 0CF8h saved and restored with interrupts off.
BeginProc G3_Pci_Read
    push    ebx
    push    edx
    pushfd
    cli
    mov     ebx, eax
    mov     dx, 0cf8h
    in      eax, dx
    mov     G3Result[72], eax
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
EndProc G3_Pci_Read

; AX = device id. Returns CF clear if it is in G3Devices.
BeginProc G3_Device_Known
    push    ecx
    push    esi
    mov     esi, OFFSET32 G3Devices
    mov     ecx, G3_DEVICE_COUNT
G3_Device_Next:
    cmp     ax, [esi]
    je      short G3_Device_Found
    add     esi, 2
    dec     ecx
    jnz     short G3_Device_Next
    pop     esi
    pop     ecx
    stc
    ret
G3_Device_Found:
    pop     esi
    pop     ecx
    clc
    ret
EndProc G3_Device_Known

; Copy G3_GTT_WINDOW entries from G3First, then count and hash
; [G3First, G3First + G3Span). The bounds were checked by the caller.
BeginProc G3_Read_Gtt
    pushad
    mov     esi, G3First
    shl     esi, 2
    add     esi, G3GttLinear
    mov     edi, OFFSET32 G3Window
    mov     ecx, G3_GTT_WINDOW
    cld
    rep     movsd

    mov     esi, G3First
    shl     esi, 2
    add     esi, G3GttLinear
    mov     ecx, G3Span
    mov     ebx, [esi]                  ; the first entry, for the fill count
    xor     edi, edi                    ; valid
    xor     ebp, ebp                    ; equal to the first
    mov     edx, G3_FNV_OFFSET
G3_Read_Gtt_Next:
    mov     eax, [esi]
    test    eax, 1
    jz      short G3_Read_Gtt_Invalid
    inc     edi
G3_Read_Gtt_Invalid:
    cmp     eax, ebx
    jne     short G3_Read_Gtt_Differs
    inc     ebp
G3_Read_Gtt_Differs:
    push    ecx
    mov     ecx, 4
G3_Read_Gtt_Byte:
    xor     dl, al
    push    eax
    mov     eax, edx
    imul    eax, eax, G3_FNV_PRIME
    mov     edx, eax
    pop     eax
    shr     eax, 8
    dec     ecx
    jnz     short G3_Read_Gtt_Byte
    pop     ecx
    add     esi, 4
    dec     ecx
    jnz     short G3_Read_Gtt_Next
    mov     G3Result[24], edi
    mov     G3Result[28], ebp
    mov     G3Result[32], edx
    or      G3Result[4], G3_ST_GTT_READ
    popad
    ret
EndProc G3_Read_Gtt

; Allocate G3Count fixed, zeroed system pages, copy their page table, free
; them. Each step's outcome is a status bit.
BeginProc G3_Allocate
    pushad
    mov     eax, G3Count
    mov     G3Result[36], eax
    test    eax, eax
    jz      G3_Allocate_Done

    VMMcall _GetFreePageCount,<0>
    mov     G3Result[40], eax
    mov     G3Result[44], edx

    VMMcall Get_System_Time
    mov     G3Started, eax
    VMMcall _PageAllocate,<G3Count,PG_SYS,0,0,0,0,0,PAGEFIXED+PAGEZEROINIT>
    test    eax, eax
    jz      G3_Allocate_Failed
    mov     G3Handle, eax
    mov     G3Linear, edx
    mov     G3Result[60], edx
    VMMcall Get_System_Time
    sub     eax, G3Started
    mov     G3Result[56], eax
    or      G3Result[4], G3_ST_ALLOCATED

    VMMcall _GetFreePageCount,<0>
    mov     G3Result[48], eax
    mov     G3Result[52], edx

    mov     eax, G3Linear
    shr     eax, PAGESHIFT
    VMMcall _CopyPageTable,<eax,G3Count,<OFFSET32 G3Pages>,0>
    mov     G3Result[64], eax
    test    eax, eax
    jz      short G3_Allocate_Table_Failed
    or      G3Result[4], G3_ST_TABLE_COPIED
    jmp     short G3_Allocate_Free
G3_Allocate_Table_Failed:
    or      G3Result[4], G3_ST_TABLE_FAILED

G3_Allocate_Free:
    VMMcall _PageFree,<G3Handle,0>
    test    eax, eax
    jz      short G3_Allocate_Free_Failed
    or      G3Result[4], G3_ST_FREED
    mov     G3Handle, 0
    VMMcall _GetFreePageCount,<0>
    mov     G3Result[68], eax
    jmp     short G3_Allocate_Done
G3_Allocate_Free_Failed:
    or      G3Result[4], G3_ST_FREE_FAILED
    jmp     short G3_Allocate_Done

G3_Allocate_Failed:
    or      G3Result[4], G3_ST_ALLOC_FAILED
G3_Allocate_Done:
    popad
    ret
EndProc G3_Allocate

BeginProc G3_Capture
    pushad

    ; The IGD must be one of the Gen3 parts this family claims, and BAR3
    ; carry its own 256 KiB alignment, before anything is mapped.
    mov     eax, G3_IGD_ADDRESS
    call    G3_Pci_Read
    mov     G3Result[8], eax
    cmp     ax, G3_VENDOR_INTEL
    jne     G3_Capture_Bad_Base
    or      G3Result[4], G3_ST_IGD_INTEL
    shr     eax, 16
    call    G3_Device_Known
    jnc     short G3_Capture_Check_Args
    or      G3Result[4], G3_ST_NOT_GEN3
    jmp     G3_Capture_Done

G3_Capture_Check_Args:
    mov     eax, G3First
    mov     G3Result[16], eax
    mov     ecx, G3Span
    mov     G3Result[20], ecx
    cmp     ecx, G3_GTT_WINDOW
    jb      short G3_Capture_Refuse
    cmp     eax, G3_GTT_ENTRIES
    jae     short G3_Capture_Refuse
    mov     edx, G3_GTT_ENTRIES
    sub     edx, eax
    cmp     ecx, edx
    ja      short G3_Capture_Refuse
    cmp     G3Count, G3_PAGE_MAX
    ja      short G3_Capture_Refuse

    mov     eax, G3Base
    test    eax, eax
    jz      short G3_Capture_Bad_Base
    test    eax, G3_GTT_ALIGN
    jnz     short G3_Capture_Bad_Base
    mov     G3Result[12], eax

    VMMcall _MapPhysToLinear,<eax,G3_GTT_BYTES,0>
    cmp     eax, 0ffffffffh
    je      short G3_Capture_Allocate
    mov     G3GttLinear, eax
    or      G3Result[4], G3_ST_GTT_MAPPED
    call    G3_Read_Gtt

G3_Capture_Allocate:
    call    G3_Allocate
    jmp     short G3_Capture_Done

G3_Capture_Refuse:
    or      G3Result[4], G3_ST_ARGS_REFUSED
    jmp     short G3_Capture_Done
G3_Capture_Bad_Base:
    or      G3Result[4], G3_ST_BASE_INVALID
G3_Capture_Done:
    popad
    ret
EndProc G3_Capture

BeginProc G3_Reset_Result
    pushad
    mov     edi, OFFSET32 G3Result + 4
    mov     ecx, G3_RESULT_DWORDS - 1
    xor     eax, eax
    cld
    rep     stosd
    mov     G3GttLinear, eax
    mov     G3Handle, eax
    mov     G3Linear, eax
    popad
    ret
EndProc G3_Reset_Result

BeginProc G3_W32_DeviceIoControl
    cmp     ecx, DIOC_OPEN
    je      G3_Dioc_Ok
    cmp     ecx, DIOC_CLOSEHANDLE
    je      G3_Dioc_Ok
    cmp     ecx, G3_DIOC_CAPTURE
    jne     G3_Dioc_Fail

    pushad
    mov     ebx, esi
    mov     ebp, [ebx.lpvInBuffer]
    test    ebp, ebp
    jz      short G3_Dioc_Copy_Fail
    cmp     [ebx.cbInBuffer], G3_IN_BYTES
    jb      short G3_Dioc_Copy_Fail
    mov     edi, [ebx.lpvOutBuffer]
    test    edi, edi
    jz      short G3_Dioc_Copy_Fail
    cmp     [ebx.cbOutBuffer], G3_RESULT_DWORDS * 4
    jb      short G3_Dioc_Copy_Fail

    call    G3_Reset_Result
    mov     eax, [ebp+G3_IN_BASE]
    mov     G3Base, eax
    mov     eax, [ebp+G3_IN_FIRST]
    mov     G3First, eax
    mov     eax, [ebp+G3_IN_SPAN]
    mov     G3Span, eax
    mov     eax, [ebp+G3_IN_PAGES]
    mov     G3Count, eax
    call    G3_Capture

    mov     edi, [ebx.lpvOutBuffer]
    mov     esi, OFFSET32 G3Result
    mov     ecx, G3_RESULT_DWORDS
    cld
    rep     movsd
    mov     eax, [ebx.lpcbBytesReturned]
    test    eax, eax
    jz      short G3_Dioc_Copy_Done
    mov     dword ptr [eax], G3_RESULT_DWORDS * 4
G3_Dioc_Copy_Done:
    popad
G3_Dioc_Ok:
    xor     eax, eax
    ret

G3_Dioc_Copy_Fail:
    popad
G3_Dioc_Fail:
    mov     eax, 1
    ret
EndProc G3_W32_DeviceIoControl

BeginProc G3_Dynamic_Init
    clc
    ret
EndProc G3_Dynamic_Init

BeginProc G3_Dynamic_Exit
    clc
    ret
EndProc G3_Dynamic_Exit

Begin_Control_Dispatch G3
    Control_Dispatch Sys_Dynamic_Device_Init, G3_Dynamic_Init
    Control_Dispatch Sys_Dynamic_Device_Exit, G3_Dynamic_Exit
    Control_Dispatch W32_DeviceIoControl, G3_W32_DeviceIoControl
End_Control_Dispatch G3

VxD_LOCKED_CODE_ENDS

end
