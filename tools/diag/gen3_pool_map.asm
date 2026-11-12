; Gen3 texture pool, Phase 2 probe VxD: the pool mapped, no GPU use.
;
; docs\plans\intel-gen3-system-memory-textures.md, Phase 2; authorised on
; MICHAEL-NETBOOK by docs\decisions\2026-11-12-intel-gen3-texture-pool-
; decisions.md. Loaded dynamically by V9XG3PM.EXE, which drives it in four
; calls on one handle:
;
;  ALLOC   Map BAR3. Read the pool's GTT entries; refuse unless they all
;          hold one value (the VBIOS's scratch PTE), which is kept to
;          restore. Allocate the pages fixed and zeroed, copy their page
;          table out for the publisher to judge with the driver's policy.
;  MAP     Only with the publisher's acceptance, and only if every page is
;          present and outside stolen memory. WBINVD (the zero fill out of
;          the cache), write one PTE per page in page-table order, read
;          the last back to post them, check every PTE reads back. Map the
;          pool's aperture range, write a pattern through it, drain the
;          write-combining buffers, read it back through the aperture, then
;          WBINVD and read it through the pages' own linear mapping.
;  CHECK   After the publisher's mode changes: every PTE and the pattern
;          through the aperture again.
;  UNMAP   Write the saved PTE back to every entry, read them back, free.
;
; Sys_Dynamic_Device_Exit does UNMAP's work too, so a publisher that dies
; between calls still leaves the table as it found it and the pages freed.
;
; Nothing here touches a GPU register or the ring. The CPU reads and writes
; the pages only through the aperture after the one WBINVD, as the
; decision's conditions require, except for MAP's one read-only pass
; through the linear mapping, which is the check that the aperture reached
; the right RAM.

.386p

.xlist
include VMM.INC
include VWIN32.INC
.list

; WBINVD is a 486 instruction.
.486p

Declare_Virtual_Device G3PM, 1, 0, G3M_Control, \
                       Undefined_Device_ID, Undefined_Init_Order, , ,

G3M_MAGIC               equ 4d503347h ; "G3PM"
G3M_DIOC_ALLOC          equ 2
G3M_DIOC_MAP            equ 3
G3M_DIOC_CHECK          equ 4
G3M_DIOC_UNMAP          equ 5
G3M_IGD_ADDRESS         equ 80001000h
G3M_VENDOR_INTEL        equ 8086h
G3M_GTT_BYTES           equ 00040000h
G3M_GTT_ENTRIES         equ 65536
G3M_GTT_ALIGN           equ 0003ffffh
G3M_GMADR_ALIGN         equ 0fffffffh ; 256 MiB, the 945's GMADR
G3M_PAGE_MAX            equ 8192
G3M_PTE_VALID           equ 00000001h
G3M_FRAME_MASK          equ 0fffff000h
G3M_PATTERN             equ 5a3cc3a5h

; Status bits, cumulative across the four calls.
G3M_ST_IGD_INTEL        equ 00000001h
G3M_ST_BASE_INVALID     equ 00000002h
G3M_ST_NOT_GEN3         equ 00000004h
G3M_ST_GTT_MAPPED       equ 00000008h
G3M_ST_ARGS_REFUSED     equ 00000010h
G3M_ST_NOT_UNIFORM      equ 00000020h
G3M_ST_ALLOCATED        equ 00000040h
G3M_ST_ALLOC_FAILED     equ 00000080h
G3M_ST_TABLE_COPIED     equ 00000100h
G3M_ST_PAGE_REFUSED     equ 00000200h
G3M_ST_MAPPED           equ 00000400h
G3M_ST_APERTURE_FAILED  equ 00000800h
G3M_ST_PATTERN_DONE     equ 00001000h
G3M_ST_CHECKED          equ 00002000h
G3M_ST_RESTORED         equ 00004000h
G3M_ST_FREED            equ 00008000h
G3M_ST_STATE_REFUSED    equ 00010000h
G3M_ST_FREE_FAILED      equ 00020000h
G3M_ST_TABLE_FAILED     equ 00040000h
G3M_ST_NOT_ACCEPTED     equ 00080000h

; Input from V9XG3PM.EXE, the same block on every call.
G3M_IN_BAR3             equ 0
G3M_IN_FIRST            equ 4
G3M_IN_COUNT            equ 8
G3M_IN_GMADR            equ 12
G3M_IN_BSM              equ 16
G3M_IN_STOLEN           equ 20
G3M_IN_ACCEPT           equ 24
G3M_IN_BYTES            equ 28

; Output: header then the page table. Must match struct v9x_g3pm_result.
G3M_HEADER_DWORDS       equ 32
G3M_RESULT_DWORDS       equ (G3M_HEADER_DWORDS + G3M_PAGE_MAX)

; Header offsets, bytes.
H_STATUS                equ 4
H_IGD                   equ 8
H_SAVED                 equ 12
H_UNIFORM               equ 16
H_PAGES                 equ 20
H_ALLOC_MS              equ 24
H_LINEAR                equ 28
H_COPY                  equ 32
H_LAST_PTE              equ 36
H_PTE_BAD               equ 40
H_WRITE_MS              equ 44
H_AP_BAD                equ 48
H_AP_FIRST              equ 52
H_AP_ACTUAL             equ 56
H_RAM_BAD               equ 60
H_RAM_FIRST             equ 64
H_RAM_ACTUAL            equ 68
H_CHK_PTE_BAD           equ 72
H_CHK_PTE_FIRST         equ 76
H_CHK_PTE_ACTUAL        equ 80
H_CHK_AP_BAD            equ 84
H_RESTORE_BAD           equ 88
H_RESTORE_LAST          equ 92
H_REFUSED_INDEX         equ 96
H_REFUSED_FRAME         equ 100
H_READ_MS               equ 104
H_CF8                   equ 108
H_APERTURE              equ 112

VxD_LOCKED_DATA_SEG
G3MDevices label word
    dw 2592h, 27a2h, 27aeh, 0a011h, 29c2h
G3M_DEVICE_COUNT        equ 5

G3MResult    dd G3M_MAGIC
             dd (G3M_HEADER_DWORDS - 1) dup (0)
G3MPages     dd G3M_PAGE_MAX dup (0)
G3MBar3      dd 0
G3MFirst     dd 0
G3MCount     dd 0
G3MGmadr     dd 0
G3MBsm       dd 0
G3MStolen    dd 0
G3MAccept    dd 0
G3MGtt       dd 0      ; linear address of the GTT
G3MAperture  dd 0      ; linear address of the pool's aperture range
G3MHandle    dd 0
G3MLinear    dd 0
G3MSaved     dd 0
G3MMapped    dd 0
G3MStarted   dd 0
VxD_LOCKED_DATA_ENDS

VxD_LOCKED_CODE_SEG

BeginProc G3M_Pci_Read
    push    ebx
    push    edx
    pushfd
    cli
    mov     ebx, eax
    mov     dx, 0cf8h
    in      eax, dx
    mov     G3MResult[H_CF8], eax
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
EndProc G3M_Pci_Read

BeginProc G3M_Device_Known
    push    ecx
    push    esi
    mov     esi, OFFSET32 G3MDevices
    mov     ecx, G3M_DEVICE_COUNT
G3M_Device_Next:
    cmp     ax, [esi]
    je      short G3M_Device_Found
    add     esi, 2
    dec     ecx
    jnz     short G3M_Device_Next
    pop     esi
    pop     ecx
    stc
    ret
G3M_Device_Found:
    pop     esi
    pop     ecx
    clc
    ret
EndProc G3M_Device_Known

; ESI = address of the pool's first PTE. Returns ESI unchanged.
BeginProc G3M_Pte_Base
    mov     esi, G3MFirst
    shl     esi, 2
    add     esi, G3MGtt
    ret
EndProc G3M_Pte_Base

; Write G3MSaved to every pool entry, read the last back, count entries
; that do not read back as it. Clears G3MMapped.
BeginProc G3M_Restore
    pushad
    call    G3M_Pte_Base
    mov     edi, esi
    mov     ecx, G3MCount
    mov     eax, G3MSaved
    cld
    rep     stosd
    mov     eax, [edi-4]
    mov     G3MResult[H_RESTORE_LAST], eax
    mov     ecx, G3MCount
    mov     eax, G3MSaved
    xor     edx, edx
G3M_Restore_Check:
    cmp     [esi], eax
    je      short G3M_Restore_Same
    inc     edx
G3M_Restore_Same:
    add     esi, 4
    dec     ecx
    jnz     short G3M_Restore_Check
    mov     G3MResult[H_RESTORE_BAD], edx
    mov     G3MMapped, 0
    or      G3MResult[H_STATUS], G3M_ST_RESTORED
    popad
    ret
EndProc G3M_Restore

; Free the pages, if any.
BeginProc G3M_Free
    pushad
    mov     eax, G3MHandle
    test    eax, eax
    jz      short G3M_Free_Done
    VMMcall _PageFree,<eax,0>
    test    eax, eax
    jz      short G3M_Free_Failed
    mov     G3MHandle, 0
    mov     G3MLinear, 0
    or      G3MResult[H_STATUS], G3M_ST_FREED
    jmp     short G3M_Free_Done
G3M_Free_Failed:
    or      G3MResult[H_STATUS], G3M_ST_FREE_FAILED
G3M_Free_Done:
    popad
    ret
EndProc G3M_Free

; ESI = base of a mapping holding the pattern. Returns EAX = mismatches,
; EDX = first mismatching dword index, ECX = its value.
BeginProc G3M_Compare
    push    ebx
    push    edi
    push    ebp
    mov     ebp, G3MCount
    shl     ebp, 10                     ; dwords
    xor     ebx, ebx                    ; index
    xor     edi, edi                    ; mismatches
    mov     edx, 0ffffffffh
    xor     ecx, ecx
G3M_Compare_Next:
    mov     eax, ebx
    shl     eax, 2
    xor     eax, G3M_PATTERN
    cmp     eax, [esi]
    je      short G3M_Compare_Same
    test    edi, edi
    jnz     short G3M_Compare_Counted
    mov     edx, ebx
    mov     ecx, [esi]
G3M_Compare_Counted:
    inc     edi
G3M_Compare_Same:
    add     esi, 4
    inc     ebx
    cmp     ebx, ebp
    jb      short G3M_Compare_Next
    mov     eax, edi
    pop     ebp
    pop     edi
    pop     ebx
    ret
EndProc G3M_Compare

; Every pool PTE against what MAP wrote. Returns EAX = mismatches,
; EDX = first index, ECX = its value.
BeginProc G3M_Check_Ptes
    push    ebx
    push    esi
    push    edi
    push    ebp
    call    G3M_Pte_Base
    mov     edi, OFFSET32 G3MPages
    mov     ebp, G3MCount
    xor     ebx, ebx
    xor     eax, eax
    mov     edx, 0ffffffffh
    xor     ecx, ecx
G3M_Check_Ptes_Next:
    push    eax
    mov     eax, [edi]
    and     eax, G3M_FRAME_MASK
    or      eax, G3M_PTE_VALID
    cmp     eax, [esi]
    pop     eax
    je      short G3M_Check_Ptes_Same
    test    eax, eax
    jnz     short G3M_Check_Ptes_Counted
    mov     edx, ebx
    mov     ecx, [esi]
G3M_Check_Ptes_Counted:
    inc     eax
G3M_Check_Ptes_Same:
    add     esi, 4
    add     edi, 4
    inc     ebx
    cmp     ebx, ebp
    jb      short G3M_Check_Ptes_Next
    pop     ebp
    pop     edi
    pop     esi
    pop     ebx
    ret
EndProc G3M_Check_Ptes

BeginProc G3M_Alloc
    pushad
    cmp     G3MHandle, 0
    jne     G3M_Alloc_State
    cmp     G3MMapped, 0
    jne     G3M_Alloc_State

    mov     eax, G3M_IGD_ADDRESS
    call    G3M_Pci_Read
    mov     G3MResult[H_IGD], eax
    cmp     ax, G3M_VENDOR_INTEL
    jne     G3M_Alloc_Bad_Base
    or      G3MResult[H_STATUS], G3M_ST_IGD_INTEL
    shr     eax, 16
    call    G3M_Device_Known
    jnc     short G3M_Alloc_Args
    or      G3MResult[H_STATUS], G3M_ST_NOT_GEN3
    jmp     G3M_Alloc_Done

    ; The pool's entries inside the GTT, its pages at most G3M_PAGE_MAX,
    ; and its entries clear of the stolen run's.
G3M_Alloc_Args:
    mov     eax, G3MFirst
    mov     ecx, G3MCount
    test    ecx, ecx
    jz      G3M_Alloc_Refuse
    cmp     ecx, G3M_PAGE_MAX
    ja      G3M_Alloc_Refuse
    cmp     eax, G3M_GTT_ENTRIES
    jae     G3M_Alloc_Refuse
    mov     edx, G3M_GTT_ENTRIES
    sub     edx, eax
    cmp     ecx, edx
    ja      G3M_Alloc_Refuse
    mov     edx, G3MStolen
    shr     edx, 12
    cmp     eax, edx
    jb      G3M_Alloc_Refuse
    mov     eax, G3MBar3
    test    eax, eax
    jz      G3M_Alloc_Bad_Base
    test    eax, G3M_GTT_ALIGN
    jnz     G3M_Alloc_Bad_Base
    mov     eax, G3MGmadr
    test    eax, eax
    jz      G3M_Alloc_Bad_Base
    test    eax, G3M_GMADR_ALIGN
    jnz     G3M_Alloc_Bad_Base

    mov     eax, G3MBar3
    VMMcall _MapPhysToLinear,<eax,G3M_GTT_BYTES,0>
    cmp     eax, 0ffffffffh
    je      G3M_Alloc_Bad_Base
    mov     G3MGtt, eax
    or      G3MResult[H_STATUS], G3M_ST_GTT_MAPPED

    ; Every pool entry must hold the first one's value, which is what
    ; UNMAP and the exit path will write back.
    call    G3M_Pte_Base
    mov     eax, [esi]
    mov     G3MSaved, eax
    mov     G3MResult[H_SAVED], eax
    mov     ecx, G3MCount
    xor     edx, edx
G3M_Alloc_Uniform:
    cmp     [esi], eax
    jne     short G3M_Alloc_Differs
    inc     edx
G3M_Alloc_Differs:
    add     esi, 4
    dec     ecx
    jnz     short G3M_Alloc_Uniform
    mov     G3MResult[H_UNIFORM], edx
    cmp     edx, G3MCount
    je      short G3M_Alloc_Pages
    or      G3MResult[H_STATUS], G3M_ST_NOT_UNIFORM
    jmp     G3M_Alloc_Done

G3M_Alloc_Pages:
    mov     eax, G3MCount
    mov     G3MResult[H_PAGES], eax
    VMMcall Get_System_Time
    mov     G3MStarted, eax
    VMMcall _PageAllocate,<G3MCount,PG_SYS,0,0,0,0,0,PAGEFIXED+PAGEZEROINIT>
    test    eax, eax
    jz      short G3M_Alloc_Failed
    mov     G3MHandle, eax
    mov     G3MLinear, edx
    mov     G3MResult[H_LINEAR], edx
    VMMcall Get_System_Time
    sub     eax, G3MStarted
    mov     G3MResult[H_ALLOC_MS], eax
    or      G3MResult[H_STATUS], G3M_ST_ALLOCATED

    mov     eax, G3MLinear
    shr     eax, PAGESHIFT
    VMMcall _CopyPageTable,<eax,G3MCount,<OFFSET32 G3MPages>,0>
    mov     G3MResult[H_COPY], eax
    test    eax, eax
    jz      short G3M_Alloc_No_Table
    or      G3MResult[H_STATUS], G3M_ST_TABLE_COPIED
    jmp     short G3M_Alloc_Done
G3M_Alloc_No_Table:
    or      G3MResult[H_STATUS], G3M_ST_TABLE_FAILED
    call    G3M_Free
    jmp     short G3M_Alloc_Done

G3M_Alloc_Failed:
    or      G3MResult[H_STATUS], G3M_ST_ALLOC_FAILED
    jmp     short G3M_Alloc_Done
G3M_Alloc_Refuse:
    or      G3MResult[H_STATUS], G3M_ST_ARGS_REFUSED
    jmp     short G3M_Alloc_Done
G3M_Alloc_State:
    or      G3MResult[H_STATUS], G3M_ST_STATE_REFUSED
    jmp     short G3M_Alloc_Done
G3M_Alloc_Bad_Base:
    or      G3MResult[H_STATUS], G3M_ST_BASE_INVALID
G3M_Alloc_Done:
    popad
    ret
EndProc G3M_Alloc

BeginProc G3M_Map
    pushad
    cmp     G3MHandle, 0
    je      G3M_Map_State
    cmp     G3MMapped, 0
    jne     G3M_Map_State
    test    G3MResult[H_STATUS], G3M_ST_TABLE_COPIED
    jz      G3M_Map_State
    cmp     G3MAccept, 1
    je      short G3M_Map_Pages
    or      G3MResult[H_STATUS], G3M_ST_NOT_ACCEPTED
    jmp     G3M_Map_Done

    ; Every page present and outside [BSM, BSM + stolen), checked here
    ; whatever the publisher decided.
G3M_Map_Pages:
    mov     esi, OFFSET32 G3MPages
    mov     ecx, G3MCount
    xor     ebx, ebx
G3M_Map_Check_Page:
    mov     eax, [esi]
    test    eax, P_PRES
    jz      short G3M_Map_Page_Refused
    and     eax, G3M_FRAME_MASK
    jz      short G3M_Map_Page_Refused
    mov     edx, eax
    sub     edx, G3MBsm
    jb      short G3M_Map_Page_Ok
    cmp     edx, G3MStolen
    jb      short G3M_Map_Page_Refused
G3M_Map_Page_Ok:
    add     esi, 4
    inc     ebx
    dec     ecx
    jnz     short G3M_Map_Check_Page
    jmp     short G3M_Map_Write

G3M_Map_Page_Refused:
    mov     G3MResult[H_REFUSED_INDEX], ebx
    mov     eax, [esi]
    mov     G3MResult[H_REFUSED_FRAME], eax
    or      G3MResult[H_STATUS], G3M_ST_PAGE_REFUSED
    jmp     G3M_Map_Done

    ; The zero fill out of the CPU cache before anything reads the pages
    ; through the aperture, then one PTE per page in page-table order, so
    ; aperture page i is linear page i.
G3M_Map_Write:
    wbinvd
    call    G3M_Pte_Base
    mov     edi, esi
    mov     esi, OFFSET32 G3MPages
    mov     ecx, G3MCount
G3M_Map_Pte_Next:
    mov     eax, [esi]
    and     eax, G3M_FRAME_MASK
    or      eax, G3M_PTE_VALID
    mov     [edi], eax
    add     esi, 4
    add     edi, 4
    dec     ecx
    jnz     short G3M_Map_Pte_Next
    mov     G3MMapped, 1
    or      G3MResult[H_STATUS], G3M_ST_MAPPED
    ; The read that posts the writes, as Linux's intel-gtt does.
    mov     eax, [edi-4]
    mov     G3MResult[H_LAST_PTE], eax
    call    G3M_Check_Ptes
    mov     G3MResult[H_PTE_BAD], eax
    test    eax, eax
    jnz     G3M_Map_Done

    mov     eax, G3MFirst
    shl     eax, 12
    add     eax, G3MGmadr
    mov     ecx, G3MCount
    shl     ecx, 12
    VMMcall _MapPhysToLinear,<eax,ecx,0>
    cmp     eax, 0ffffffffh
    jne     short G3M_Map_Pattern
    or      G3MResult[H_STATUS], G3M_ST_APERTURE_FAILED
    jmp     G3M_Map_Done

G3M_Map_Pattern:
    mov     G3MAperture, eax
    mov     G3MResult[H_APERTURE], eax
    VMMcall Get_System_Time
    mov     G3MStarted, eax
    mov     edi, G3MAperture
    mov     ebp, G3MCount
    shl     ebp, 10
    xor     ebx, ebx
G3M_Map_Pattern_Next:
    mov     eax, ebx
    shl     eax, 2
    xor     eax, G3M_PATTERN
    mov     [edi], eax
    add     edi, 4
    inc     ebx
    cmp     ebx, ebp
    jb      short G3M_Map_Pattern_Next
    ; A locked operation drains the write-combining buffers.
    lock or dword ptr [esp], 0
    VMMcall Get_System_Time
    sub     eax, G3MStarted
    mov     G3MResult[H_WRITE_MS], eax

    VMMcall Get_System_Time
    mov     G3MStarted, eax
    mov     esi, G3MAperture
    call    G3M_Compare
    mov     G3MResult[H_AP_BAD], eax
    mov     G3MResult[H_AP_FIRST], edx
    mov     G3MResult[H_AP_ACTUAL], ecx
    VMMcall Get_System_Time
    sub     eax, G3MStarted
    mov     G3MResult[H_READ_MS], eax

    ; Whether the aperture reached these pages: no line of them can be in
    ; the cache from before, so after WBINVD the linear mapping reads RAM.
    wbinvd
    mov     esi, G3MLinear
    call    G3M_Compare
    mov     G3MResult[H_RAM_BAD], eax
    mov     G3MResult[H_RAM_FIRST], edx
    mov     G3MResult[H_RAM_ACTUAL], ecx
    ; The read just pulled lines into the cache; drop them so nothing
    ; cached is ever written back over the pool.
    wbinvd
    or      G3MResult[H_STATUS], G3M_ST_PATTERN_DONE
    jmp     short G3M_Map_Done

G3M_Map_State:
    or      G3MResult[H_STATUS], G3M_ST_STATE_REFUSED
G3M_Map_Done:
    popad
    ret
EndProc G3M_Map

BeginProc G3M_Check
    pushad
    cmp     G3MMapped, 0
    je      short G3M_Check_State
    call    G3M_Check_Ptes
    mov     G3MResult[H_CHK_PTE_BAD], eax
    mov     G3MResult[H_CHK_PTE_FIRST], edx
    mov     G3MResult[H_CHK_PTE_ACTUAL], ecx
    cmp     G3MAperture, 0
    je      short G3M_Check_Marked
    mov     esi, G3MAperture
    call    G3M_Compare
    mov     G3MResult[H_CHK_AP_BAD], eax
G3M_Check_Marked:
    or      G3MResult[H_STATUS], G3M_ST_CHECKED
    jmp     short G3M_Check_Done
G3M_Check_State:
    or      G3MResult[H_STATUS], G3M_ST_STATE_REFUSED
G3M_Check_Done:
    popad
    ret
EndProc G3M_Check

; Restore and free, whatever state the calls left. Also the exit path.
BeginProc G3M_Unmap
    cmp     G3MMapped, 0
    je      short G3M_Unmap_Free
    call    G3M_Restore
G3M_Unmap_Free:
    call    G3M_Free
    ret
EndProc G3M_Unmap

BeginProc G3M_W32_DeviceIoControl
    cmp     ecx, DIOC_OPEN
    je      G3M_Dioc_Ok
    cmp     ecx, DIOC_CLOSEHANDLE
    je      G3M_Dioc_Ok
    cmp     ecx, G3M_DIOC_ALLOC
    jb      G3M_Dioc_Fail
    cmp     ecx, G3M_DIOC_UNMAP
    ja      G3M_Dioc_Fail

    pushad
    mov     ebx, esi
    mov     ebp, [ebx.lpvInBuffer]
    test    ebp, ebp
    jz      G3M_Dioc_Copy_Fail
    cmp     [ebx.cbInBuffer], G3M_IN_BYTES
    jb      G3M_Dioc_Copy_Fail
    mov     edi, [ebx.lpvOutBuffer]
    test    edi, edi
    jz      G3M_Dioc_Copy_Fail
    cmp     [ebx.cbOutBuffer], G3M_RESULT_DWORDS * 4
    jb      G3M_Dioc_Copy_Fail

    ; ALLOC starts a fresh result and takes the layout; later calls take
    ; only the acceptance, so the layout cannot change under a mapping.
    cmp     ecx, G3M_DIOC_ALLOC
    jne     short G3M_Dioc_Later
    cmp     G3MHandle, 0
    jne     short G3M_Dioc_Do_Alloc
    push    ecx
    mov     edi, OFFSET32 G3MResult + 4
    mov     ecx, G3M_RESULT_DWORDS - 1
    xor     eax, eax
    cld
    rep     stosd
    pop     ecx
    mov     eax, [ebp+G3M_IN_BAR3]
    mov     G3MBar3, eax
    mov     eax, [ebp+G3M_IN_FIRST]
    mov     G3MFirst, eax
    mov     eax, [ebp+G3M_IN_COUNT]
    mov     G3MCount, eax
    mov     eax, [ebp+G3M_IN_GMADR]
    mov     G3MGmadr, eax
    mov     eax, [ebp+G3M_IN_BSM]
    mov     G3MBsm, eax
    mov     eax, [ebp+G3M_IN_STOLEN]
    mov     G3MStolen, eax
    mov     G3MAperture, 0
G3M_Dioc_Do_Alloc:
    call    G3M_Alloc
    jmp     short G3M_Dioc_Copy_Out
G3M_Dioc_Later:
    mov     eax, [ebp+G3M_IN_ACCEPT]
    mov     G3MAccept, eax
    cmp     ecx, G3M_DIOC_MAP
    jne     short G3M_Dioc_Not_Map
    call    G3M_Map
    jmp     short G3M_Dioc_Copy_Out
G3M_Dioc_Not_Map:
    cmp     ecx, G3M_DIOC_CHECK
    jne     short G3M_Dioc_Not_Check
    call    G3M_Check
    jmp     short G3M_Dioc_Copy_Out
G3M_Dioc_Not_Check:
    call    G3M_Unmap

G3M_Dioc_Copy_Out:
    mov     edi, [ebx.lpvOutBuffer]
    mov     esi, OFFSET32 G3MResult
    mov     ecx, G3M_RESULT_DWORDS
    cld
    rep     movsd
    mov     eax, [ebx.lpcbBytesReturned]
    test    eax, eax
    jz      short G3M_Dioc_Copy_Done
    mov     dword ptr [eax], G3M_RESULT_DWORDS * 4
G3M_Dioc_Copy_Done:
    popad
G3M_Dioc_Ok:
    xor     eax, eax
    ret

G3M_Dioc_Copy_Fail:
    popad
G3M_Dioc_Fail:
    mov     eax, 1
    ret
EndProc G3M_W32_DeviceIoControl

BeginProc G3M_Dynamic_Init
    clc
    ret
EndProc G3M_Dynamic_Init

BeginProc G3M_Dynamic_Exit
    pushad
    call    G3M_Unmap
    popad
    clc
    ret
EndProc G3M_Dynamic_Exit

Begin_Control_Dispatch G3M
    Control_Dispatch Sys_Dynamic_Device_Init, G3M_Dynamic_Init
    Control_Dispatch Sys_Dynamic_Device_Exit, G3M_Dynamic_Exit
    Control_Dispatch W32_DeviceIoControl, G3M_W32_DeviceIoControl
End_Control_Dispatch G3M

VxD_LOCKED_CODE_ENDS

end
