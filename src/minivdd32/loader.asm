; Velocity9x first boot-loadable Windows 9x mini-VDD.
;
; This stage verifies the master VDD ABI, installs legacy VESA and Windows 98
; monitor-power callbacks, and emits bounded COM1 diagnostics. It advertises
; D0 only because the VESA BIOS resume path can blank an S3 ViRGE display
; without reliably restoring the active high-resolution framebuffer.

; .586p, not .386p, for CPUID and RDMSR in the memory-type inspection below.
; Nothing here executes either instruction without first proving at run time
; that this CPU has it: assembling an opcode and reaching it are different
; questions, and the 386 and early 486 this project still runs on answer the
; second one with an exception.
.586p

.xlist
include VMM.INC
include MINIVDD.INC
IFDEF V9X_IO_TRACE
; DIOCParams, for the trace readout's Win32 DeviceIoControl channel.
include VWIN32.INC
ENDIF
.list

; The device id, handshake magic, contract version, function numbers, cache
; bounds and packed record layout of the API below. Shared verbatim with
; src\display16\runtime.asm, which is the only other program that knows any of
; these numbers; the include itself says why it is included rather than copied.
include V9XMAPI.INC

; The ADVFUNC shield (a V86 VM's write to 4AE8H is swallowed; see the handler)
; ships in every build unless -NoShieldAdvFunc takes it out. It has two forms
; because Install_IO_Handler admits one handler per port: with -IoTrace the
; trace handler owns 4AE8H and carries the shield as a branch; without it a
; single-port handler of its own is installed. V9X_ADVFUNC_SHIELD selects the
; second form.
IFNDEF V9X_NO_IO_SHIELD
IFNDEF V9X_IO_TRACE
V9X_ADVFUNC_SHIELD equ 1
ENDIF
ENDIF

; Stage 1 deliberately makes fewer no-timeout BIOS calls than the frozen ABI
; maximum permits. QEMU's measured 93-entry list still fits; later rollout can
; raise this toward V9X_VBE_MODE_QUERY_MAX after the guest gate is stable.
V9X_STAGE1_QUERY_MAX EQU 96
; How many modes the set-and-ask-again sweep may try. Each one is a 4F02h into
; the real BIOS, so this is a budget on damage as much as on time: the eighteen
; OEM numbers a Pineview BIOS lists fit inside it with room to spare.
V9X_STAGE1_SWEEP_MAX EQU 32

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
; Stage 1 keeps list-derived records separate from the generated baseline
; rescue probes. Indexed enumeration can see only the first cache; by-mode
; lookup searches both so an unusable BIOS list cannot take the static LFB
; path's aperture answer away.
V9xVbeListStage dw V9X_VBE_MODE_LIST_MAX dup (0)
V9xVbeCache     db V9X_VBE_CACHE_BYTES dup (0)
V9xVbeProbeCache db V9X_VBE_PROBE_BYTES dup (0)
; EDID block 0, collected once through 4F15h at init after the mode scan and
; served back in register-only 16-byte chunks. Meaningful only while
; V9X_VBE_ST_EDID_VALID is set in V9xVbeStatus.
V9xVbeEdid      db V9X_VBE_EDID_BYTES dup (0)

IFNDEF V9X_NO_VBE_COLLECT
include V9XPROBE.INC
ENDIF

V9xVbeListed    dw 0
V9xVbeQueried   dw 0
V9xVbeCached    dw 0
V9xVbeProbed    dw 0
V9xVbeFailed    dw 0
V9xVbeOverflow  dw 0
IFDEF V9X_NO_VBE_COLLECT
V9xVbeStatus    dw V9X_VBE_ST_COLLECT_OFF
ELSE
V9xVbeStatus    dw 0
ENDIF

V9xVbeCtrlValid dw 0
V9xVbeCtrlVer   dw 0
V9xVbeCtrl64K   dw 0
V9xVbeCtrlCaps  dd 0
V9xVbeCtrlOemRev dw 0
V9xVbeListOff   dw 0
V9xVbeListSeg   dw 0
V9xVbeListSelf  dw 0

; The memory-type registers, exactly as read at init.
;
; Read once, for the same reason the VBE answers are: they describe the machine
; rather than any mode, so nothing about them changes later, and reading them
; at init keeps the API a table lookup. Ring 0 decides nothing about them - see
; V9XMAPI.INC and include\velocity9x\mtrr.h for why the rules are elsewhere.
;
; V9xMtrrHigh carries one bit per pair, set when that pair's PHYSBASE had a
; non-zero high dword: the range starts above 4 GiB and cannot reach anything
; a 32-bit driver maps.
V9xMtrrFlags    dw 0
V9xMtrrCount    dw 0
V9xMtrrHigh     dw 0
V9xMtrrCap      dd 0
V9xMtrrDefType  dd 0
V9xMtrrBase     dd V9X_MTRR_RANGE_MAX dup (0)
V9xMtrrMask     dd V9X_MTRR_RANGE_MAX dup (0)

IFDEF V9X_INTEL_MMIO_FINGERPRINT
; Intel Gen3 Phase 1: one fixed read-only allowlist, captured twice. The BAR is
; supplied from a fresh PCI config read by the display driver on each enable;
; this layer owns the physical mapping and never writes through it.
V9X_I9XX_MMIO_BYTES equ 00080000h
V9xI9xxMmioBase   dd 0
V9xI9xxMmioLinear dd 0
V9xI9xxValid      dw 0
V9xI9xxOffsets dd 00002020h, 00002030h, 00002034h, 00002038h
                dd 0000203ch, 00002080h
                dd 00070008h, 00060000h, 0006000ch, 0006001ch
                dd 00070180h, 00070184h, 00070188h
                dd 00071008h, 00061000h, 0006100ch, 0006101ch
                dd 00071180h, 00071184h, 00071188h
V9xI9xxFirst  dd V9X_I9XX_SNAPSHOT_DWORDS dup (0)
V9xI9xxSecond dd V9X_I9XX_SNAPSHOT_DWORDS dup (0)

; Intel Gen3 Phase 2: BAR3 is a separate 256-KiB, 65536-entry GTT. Keep no
; table copy in the VxD: hash two complete passes, then serve four live PTEs
; per query so the Win16 side can stream the binary artefact.
V9X_I9XX_GTT_BYTES equ 00040000h
V9X_I9XX_GTT_CHUNKS equ V9X_I9XX_GTT_ENTRY_COUNT / V9X_I9XX_GTT_CHUNK_DWORDS
V9xI9xxGttBase    dd 0
V9xI9xxGttLinear  dd 0
V9xI9xxGttHashA   dd 0
V9xI9xxGttHashB   dd 0
V9xI9xxGttValid   dw 0

; Phase 3 retains a bounded chronological journal in locked VxD storage.
; Once full it stops rather than overwriting the cold-boot baseline; Dropped
; makes an incomplete matrix impossible to mistake for a complete one.
V9xI9xxEventOffsets dd 00002020h, 00002030h, 00002034h, 00002038h
                     dd 0000203ch, 00002080h
                     dd 00002000h, 00002004h, 00002008h, 0000200ch
                     dd 00002010h, 00002014h, 00002018h, 0000201ch
V9xI9xxEventRecords dd V9X_I9XX_EVENT_MAX * V9X_I9XX_EVENT_DWORDS dup (0)
V9xI9xxEventCount   dw 0
V9xI9xxEventDropped dw 0
V9xI9xxEventBusy    dw 0
V9xI9xxEventResult  dw 0
V9xI9xxEventKind    dd 0
V9xI9xxEventContext dd 0

; Phase 4 staging has a physical-RAM write path but no register write path.
; The single tested machine's reserve is fixed here; a moved BSM refuses.
V9xI9xxRingLinear   dd 0

; The read-only reserve hash (API v7). It keeps its OWN mapping, separate
; from the staging one, for two reasons: it must work in a build with no
; executor compiled in at all, and a mapping that is never used to write
; cannot be confused with one that is. Mapped once, on first use.
V9xI9xxHashLinear   dd 0

; Phase 5 staging state. TWO counters and two ring regions, so a Phase 4
; dword can never land in a Phase 5 slot, and the two streams cannot
; overlap in the ring even if a counter were wrong.
V9xI9xxP5Staged     dw 0
V9xI9xxStagePhase   dd 0
V9xI9xxStageTable   dd 0
V9xI9xxStageBound   dd 0
V9xI9xxStageRingOff dd 0
V9xI9xxHashFail     dd 0
V9xI9xxHashPassA    dd 0
V9xI9xxHashPassB    dd 0
V9xI9xxRingStaged   dw 0
V9xI9xxRingResult   dw 0
; The Intel Phase 4 and Phase 5 arm tables, their CRCs and the reserve
; offsets, rendered from the compiled C builders in src\chipsets\intel by
; scripts\gen-intel-3d-stream.ps1. It is included HERE rather than with
; the contract includes at the top because it emits data and must land in
; this segment. Hand-maintained copies of these numbers drifted three ways
; at the Phase 5 layout move; this is the fix.
include i9xx3d.inc

; The reserve's physical address is the GMADR base plus the GENERATED
; offset. The base is a PCI BAR value measured on the one tested machine
; and cannot come from the C builders; the offset can and does, so the two
; halves cannot drift apart. Every path that maps the reserve composes it
; here rather than spelling a literal.
V9X_I9XX_GMADR_BASE         EQU 0d0000000h
V9X_I9XX_RESERVE_PHYS       EQU V9X_I9XX_GMADR_BASE + V9X_I9XX_RESERVE_OFFSET
V9X_I9XX_RESERVE_BYTES_TOTAL EQU 000100000h
V9X_I9XX_RESERVE_DWORDS     EQU V9X_I9XX_RESERVE_BYTES_TOTAL / 4
; Phase 5 stages its stream 4 KiB into the ring, clear of the ten dwords
; Phase 4 stages at offset zero. Disjoint regions as well as disjoint
; tables and counters: three independent reasons the two streams cannot
; be confused, because one is not enough for something that feeds a GPU
; command parser.
V9X_I9XX_P5_RING_OFFSET     EQU 000001000h

; Every existing reference keeps working through this alias, and the
; table itself has exactly one definition.
V9xI9xxRingExpected EQU V9xI9xxPhase4Table
V9xI9xxRingDiagOffsets dd 00002088h, 0000208ch, 00002090h
                        dd 000020c8h, 000020b0h, 000020b4h, 000020b8h
                        dd 0000203ch, 00002038h
V9xI9xxRingStep     dw 0
V9xI9xxRingPoison   dw 0
V9xI9xxRingBusy     dw 0
V9xI9xxRingWant     dd 0
V9xI9xxRingHead     dd 0
V9xI9xxRingTail     dd 0
V9xI9xxRingStartMs  dd 0
V9xI9xxRingElapsed  dd 0
V9xI9xxRingPolls    dd 0
V9xI9xxRingFailure  dd 0
V9xI9xxRingStageFail dd 0
V9xI9xxRingStageRead dd 0
V9xI9xxRingExecStep dd 0
V9xI9xxRingExecCrc  dd 0
ENDIF

; Real-mode segment of the V86 scratch the BIOS fills in, or 0 if it could not
; be had. Allocated at init and never freed.
V9xVbeBufSeg    dw 0
; The client BX for the next V9xMini_Vbe_Call. Parked here because EBX is
; taken by Get_Cur_VM_Handle inside the call itself.
V9xVbeCallBx    dw 0
; The client BX the last V9xMini_Vbe_Call came back with. 4F03h returns the
; current mode there and nothing else uses it, which is how the sweep learns
; the mode it has to put back.
V9xVbeCallRetBx dw 0
; The sweep's own state. Declared whether or not the sweep is assembled in, so
; the data segment does not change shape between the two builds.
V9xVbeSwept     dw 0
V9xVbeEntryMode dw 0
; The card's total video memory, as the display driver reported it through
; VDD_REGISTER_DISPLAY_DRIVER_INFO, and 0 until it does.
;
; This is the only route by which a chip-agnostic mini-VDD can know the size:
; it cannot read it off an unknown chip, and the driver above it already
; establishes it from 4F00h or from a family's own hook before it registers.
; GET_TOTAL_VRAM_SIZE hands it to the main VDD, which is what lets the VDD
; reserve an off-screen area for itself - measured as reserving nothing at all
; while these two callbacks were missing. See
; docs\decisions\2026-08-29-dos-box-exit-tier0.md.
V9xTotalVramBytes dd 0
; One-shot latch for the size query's trace line.
V9xVramAsked    dw 0
IFDEF V9X_IO_TRACE
; -IoTrace: port I/O traps on the 8514/A register file, V86 VMs only.
;
; Instrument for docs\issues\2026-09-06-dos-box-doubles-the-desktop-on-
; physical-trio64.md. A windowed DOS box on a physical Trio64 rewrites the
; visible framebuffer at twice its stride, more readily once the engine ports
; have been touched from ring 3. The only actors with the framebuffer in reach
; are the DOS VM's real video BIOS, whose non-VGA port writes the main VDD does
; not trap, and the main VDD itself at ring 0. Trapping the engine ports for
; V86 VMs and logging every access - value, direction, width, VM - says which:
; a log with the BIOS driving the engine names the first, an empty log across
; a doubling names the second. Every access is passed through unchanged, in
; its original width, so the trace does not alter what it observes.
;
; The System VM is exempted with Disable_Local_Trapping at Sys_VM_Init, so the
; display driver's own port I/O never enters the handler. Ring-0 I/O is never
; trapped by the VMM at all, which is why an empty log is informative.
V9X_IOTRACE_ENTRIES equ 512
V9X_IOTRACE_PORTS   equ 21
V9xIoTraceCount  dd 0          ; accesses seen, including those not logged
V9xIoTraceNext   dd 0          ; next log slot, saturates at ENTRIES
V9xIoTraceInstalled dd 0       ; ports whose handler installed
V9xIoTraceSysVmOff dd 0        ; ports whose System-VM trapping was disabled
; Three dwords per entry: VM handle; type in the high word and port in the
; low; the value written, or the value read back.
V9xIoTraceLog    dd V9X_IOTRACE_ENTRIES * 3 dup (0)
; The 8514/A register file at its ..E8H stride, plus the two pixel transfer
; ports. Word ports; the handler passes any width through.
V9xIoTracePorts  dw 042e8h, 046e8h, 04ae8h, 082e8h, 086e8h, 08ae8h, 08ee8h
                 dw 092e8h, 096e8h, 09ae8h, 09ee8h, 0a2e8h, 0a6e8h, 0aae8h
                 dw 0aee8h, 0b2e8h, 0b6e8h, 0bae8h, 0bee8h, 0e2e8h, 0e2eah
ENDIF
IFDEF V9X_ADVFUNC_SHIELD
; V86 writes to ADVFUNC_CNTL swallowed since boot. Not read out by anything
; yet; the -IoTrace build's log is the instrument that counts them.
V9xAdvFuncShieldCount dd 0
ENDIF
VxD_LOCKED_DATA_ENDS

VxD_LOCKED_CODE_SEG

IFDEF V9X_IO_TRACE
; The trapped-port handler. VMM entry: EAX = data for an output, EBX = the VM,
; ECX = the I/O type, EDX = the port, EBP -> the client registers. Exit: EAX =
; the value for an input. String and repeated forms go back to Simulate_IO,
; which breaks them into single accesses that re-enter here, so the log sees
; every word of a REP OUTSW at its true width.
BeginProc V9xMini_IoTrace_Handler
    test    ecx, STRING_IO OR REP_IO
    jz      short V9xMini_IoTrace_Single
    VMMJmp  Simulate_IO

V9xMini_IoTrace_Single:
    push    esi
    push    edi
    ; The System VM is not what this watches, and Disable_Local_Trapping at
    ; Sys_VM_Init was measured not to take (SysVmOff=0 with Installed=21), so
    ; it is told apart here instead: its accesses pass through unlogged.
    push    ebx
    push    eax
    VMMcall Get_Sys_VM_Handle
    mov     edi, ebx
    pop     eax
    pop     ebx
    cmp     edi, ebx
    je      short V9xMini_IoTrace_Passthrough
IFNDEF V9X_NO_IO_SHIELD
    ; The ADVFUNC shield, trace form: a V86 VM's write to ADVFUNC_CNTL (4AE8H)
    ; is swallowed. -NoShieldAdvFunc restores the pass-through for an A/B.
    ; Measured 2026-09-06 on A8U4I5: the DOS VM's video BIOS writes 02H there
    ; and nothing else on the engine file, and that single write drops the
    ; Trio64 out of enhanced mode - after which the same framebuffer bytes are
    ; addressed as VGA planes by CPU and CRTC alike, which is the "doubled
    ; desktop", and an in-flight CPU-data engine command never completes,
    ; which is the BARRY hang. The VM's BIOS believes its write landed; the
    ; hardware stays in the mode the display driver set. Reads still return the
    ; real register. Logged like everything else, so the count says how often
    ; the shield fired.
    cmp     dx, 04ae8h
    jne     short V9xMini_IoTrace_Perform
    test    ecx, OUTPUT
    jz      short V9xMini_IoTrace_Perform
    jmp     short V9xMini_IoTrace_Log
ENDIF
V9xMini_IoTrace_Perform:
    mov     edi, ecx
    and     edi, 018h                   ; width field: 0 byte, 8 word, 10h dword
    test    ecx, OUTPUT
    jnz     short V9xMini_IoTrace_Out
    cmp     edi, WORD_INPUT
    je      short V9xMini_IoTrace_InW
    cmp     edi, DWORD_INPUT
    je      short V9xMini_IoTrace_InD
    in      al, dx
    jmp     short V9xMini_IoTrace_Log
V9xMini_IoTrace_InW:
    in      ax, dx
    jmp     short V9xMini_IoTrace_Log
V9xMini_IoTrace_InD:
    in      eax, dx
    jmp     short V9xMini_IoTrace_Log

V9xMini_IoTrace_Out:
    cmp     edi, WORD_OUTPUT AND 018h
    je      short V9xMini_IoTrace_OutW
    cmp     edi, DWORD_OUTPUT AND 018h
    je      short V9xMini_IoTrace_OutD
    out     dx, al
    jmp     short V9xMini_IoTrace_Log
V9xMini_IoTrace_OutW:
    out     dx, ax
    jmp     short V9xMini_IoTrace_Log
V9xMini_IoTrace_OutD:
    out     dx, eax

V9xMini_IoTrace_Log:
    inc     V9xIoTraceCount
    mov     edi, V9xIoTraceNext
    cmp     edi, V9X_IOTRACE_ENTRIES
    jae     short V9xMini_IoTrace_Done
    inc     V9xIoTraceNext
    lea     edi, [edi + edi * 2]        ; entry * 3 dwords
    shl     edi, 2
    add     edi, OFFSET32 V9xIoTraceLog
    mov     [edi], ebx
    mov     esi, ecx
    shl     esi, 16
    mov     si, dx
    mov     [edi + 4], esi
    mov     [edi + 8], eax

V9xMini_IoTrace_Done:
    pop     edi
    pop     esi
    ret

    ; System VM: the access at its width, nothing recorded.
V9xMini_IoTrace_Passthrough:
    mov     edi, ecx
    and     edi, 018h
    test    ecx, OUTPUT
    jnz     short V9xMini_IoTrace_PassOut
    cmp     edi, 8
    je      short V9xMini_IoTrace_PassInW
    cmp     edi, 010h
    je      short V9xMini_IoTrace_PassInD
    in      al, dx
    jmp     short V9xMini_IoTrace_Done
V9xMini_IoTrace_PassInW:
    in      ax, dx
    jmp     short V9xMini_IoTrace_Done
V9xMini_IoTrace_PassInD:
    in      eax, dx
    jmp     short V9xMini_IoTrace_Done
V9xMini_IoTrace_PassOut:
    cmp     edi, 8
    je      short V9xMini_IoTrace_PassOutW
    cmp     edi, 010h
    je      short V9xMini_IoTrace_PassOutD
    out     dx, al
    jmp     short V9xMini_IoTrace_Done
V9xMini_IoTrace_PassOutW:
    out     dx, ax
    jmp     short V9xMini_IoTrace_Done
V9xMini_IoTrace_PassOutD:
    out     dx, eax
    jmp     short V9xMini_IoTrace_Done
EndProc V9xMini_IoTrace_Handler

; Sys_VM_Init: EBX = the System VM. Turn the traps off for it alone, so the
; display driver's ring-3 port I/O runs at full speed and never appears here.
BeginProc MiniVDD_Sys_VM_Init
    pushad
    mov     esi, OFFSET32 V9xIoTracePorts
    mov     ecx, V9X_IOTRACE_PORTS
V9xMini_IoTrace_SysVm_Next:
    movzx   edx, word ptr [esi]
    VMMcall Disable_Local_Trapping
    jc      short V9xMini_IoTrace_SysVm_Skip
    inc     V9xIoTraceSysVmOff
V9xMini_IoTrace_SysVm_Skip:
    add     esi, 2
    dec     ecx
    jnz     short V9xMini_IoTrace_SysVm_Next
    popad
    clc
    ret
EndProc MiniVDD_Sys_VM_Init

; Win32 readout. CreateFile("\\.\V9XMINI") reaches this statically loaded
; device by name; code 1 copies the header and the log into the caller's
; buffer, code 2 clears the log. ESI -> DIOCParams.
V9X_IOTRACE_DIOC_READ  equ 1
V9X_IOTRACE_DIOC_RESET equ 2
BeginProc MiniVDD_W32_DeviceIoControl
    ; Near jumps: the read path below is longer than a short jump reaches.
    cmp     ecx, DIOC_OPEN
    je      V9xMini_Dioc_Ok
    cmp     ecx, DIOC_CLOSEHANDLE
    je      V9xMini_Dioc_Ok
    cmp     ecx, V9X_IOTRACE_DIOC_RESET
    je      V9xMini_Dioc_Reset
    cmp     ecx, V9X_IOTRACE_DIOC_READ
    jne     V9xMini_Dioc_Fail

    pushad
    mov     ebp, esi                    ; DIOCParams
    mov     edi, [ebp.lpvOutBuffer]
    mov     ecx, [ebp.cbOutBuffer]
    xor     edx, edx                    ; bytes returned
    test    edi, edi
    jz      short V9xMini_Dioc_Read_Done
    ; Header: count, logged, installed, sys-vm-off. Then the log, as many
    ; whole bytes of it as the buffer has room for.
    cmp     ecx, 16
    jb      short V9xMini_Dioc_Read_Done
    mov     eax, V9xIoTraceCount
    mov     [edi], eax
    mov     eax, V9xIoTraceNext
    mov     [edi + 4], eax
    mov     eax, V9xIoTraceInstalled
    mov     [edi + 8], eax
    mov     eax, V9xIoTraceSysVmOff
    mov     [edi + 12], eax
    sub     ecx, 16
    add     edi, 16
    mov     edx, 16
    mov     eax, V9xIoTraceNext
    lea     eax, [eax + eax * 2]
    shl     eax, 2                      ; bytes of log in use
    cmp     ecx, eax
    jb      short V9xMini_Dioc_Read_Copy
    mov     ecx, eax
V9xMini_Dioc_Read_Copy:
    add     edx, ecx
    shr     ecx, 2
    mov     esi, OFFSET32 V9xIoTraceLog
    cld
    rep     movsd
V9xMini_Dioc_Read_Done:
    mov     eax, [ebp.lpcbBytesReturned]
    test    eax, eax
    jz      short V9xMini_Dioc_Read_Exit
    mov     [eax], edx
V9xMini_Dioc_Read_Exit:
    popad
    xor     eax, eax
    ret

V9xMini_Dioc_Reset:
    mov     V9xIoTraceCount, 0
    mov     V9xIoTraceNext, 0
V9xMini_Dioc_Ok:
    xor     eax, eax
    ret

V9xMini_Dioc_Fail:
    mov     eax, 1
    ret
EndProc MiniVDD_W32_DeviceIoControl
ENDIF

IFDEF V9X_ADVFUNC_SHIELD
; The ADVFUNC shield, shipping form: one trap on 4AE8H, ADVFUNC_CNTL.
;
; Why: a windowed DOS box on a physical Trio64 writes 02H to 4AE8H from its
; V86 VM's video BIOS - the one access that VM makes to the 8514/A file, per
; the -IoTrace log of 2026-09-06 on A8U4I5 - and that write clears ENB EHFC.
; The chip leaves enhanced mode with the desktop still displayed, so CPU and
; CRTC address the same DRAM as VGA planes (the "doubled desktop"), and a
; CPU-data engine command caught mid-transfer never completes (the BARRY
; hang). The system VDD traps the VGA ports for a V86 VM and not this one, so
; the write reaches the hardware unless something else stands in the way.
; docs\issues\2026-09-06-dos-box-doubles-the-desktop-on-physical-trio64.md.
;
; What: the System VM's accesses pass through at their width, so the display
; driver's own writes to 4AE8H (its mode set) land unchanged. Any other VM's
; write is swallowed and counted; its reads still return the real register,
; so a BIOS that reads back sees the display driver's state rather than a
; shadow that disagrees with the hardware. String and repeated forms go back
; to Simulate_IO, which re-enters here one access at a time.
;
; The mini-VDD is one binary for every family, so the trap is installed on
; cards that do not decode 4AE8H too (ati, matrox-m2, vbe). There the write
; would have reached nothing, and swallowing it changes nothing; the one
; card class where it would matter, an 8514/A-compatible driven by its own
; DOS software, is not one this project drives.
;
; VMM entry: EAX = data for an output, EBX = the VM, ECX = the I/O type,
; EDX = the port. Exit: EAX = the value for an input.
BeginProc V9xMini_AdvFunc_Handler
    test    ecx, STRING_IO OR REP_IO
    jz      short V9xMini_AdvFunc_Single
    VMMJmp  Simulate_IO

V9xMini_AdvFunc_Single:
    push    edi
    ; Disable_Local_Trapping for the System VM was measured not to take
    ; (SysVmOff=0 with Installed=21 on the trace build), so the System VM is
    ; told apart by handle on every access instead.
    push    ebx
    push    eax
    VMMcall Get_Sys_VM_Handle
    mov     edi, ebx
    pop     eax
    pop     ebx
    cmp     edi, ebx
    je      short V9xMini_AdvFunc_Perform
    test    ecx, OUTPUT
    jz      short V9xMini_AdvFunc_Perform
    inc     V9xAdvFuncShieldCount
    jmp     short V9xMini_AdvFunc_Done

    ; The access at its width. Bits 3-4 of the type are the width field:
    ; 0 byte, 8 word, 10H dword.
V9xMini_AdvFunc_Perform:
    mov     edi, ecx
    and     edi, 018h
    test    ecx, OUTPUT
    jnz     short V9xMini_AdvFunc_Out
    cmp     edi, 8
    je      short V9xMini_AdvFunc_InW
    cmp     edi, 010h
    je      short V9xMini_AdvFunc_InD
    in      al, dx
    jmp     short V9xMini_AdvFunc_Done
V9xMini_AdvFunc_InW:
    in      ax, dx
    jmp     short V9xMini_AdvFunc_Done
V9xMini_AdvFunc_InD:
    in      eax, dx
    jmp     short V9xMini_AdvFunc_Done
V9xMini_AdvFunc_Out:
    cmp     edi, 8
    je      short V9xMini_AdvFunc_OutW
    cmp     edi, 010h
    je      short V9xMini_AdvFunc_OutD
    out     dx, al
    jmp     short V9xMini_AdvFunc_Done
V9xMini_AdvFunc_OutW:
    out     dx, ax
    jmp     short V9xMini_AdvFunc_Done
V9xMini_AdvFunc_OutD:
    out     dx, eax

V9xMini_AdvFunc_Done:
    pop     edi
    ret
EndProc V9xMini_AdvFunc_Handler
ENDIF

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

; Receive the display driver's VDD_REGISTER_DISPLAY_DRIVER_INFO call.
;
; MINIVDD routes that PM API service to this callback, function 0, and the
; parameters are whatever the driver and its own mini-VDD agree on - the DDK's
; three samples pass three different things. Ours passes the card's total video
; memory in ECX and nothing else; EAX and EBX are reserved by the contract.
;
; Entry: EBP -> the client register structure. Exit: nothing assumed, and the
; DDK's own handler simply rets.
;
; Values arrive through the client registers, not the live ones: the caller is
; ring-3 display-driver code whose register state the VMM has captured.
BeginProc MiniVDD_RegisterDisplayDriver
    pushfd
    pushad

    mov     eax, [ebp.Client_ECX]
    mov     V9xTotalVramBytes, eax

    ; Says in a capture that the main VDD routed the call here, which its EAX
    ; return does not: it answers the service number whether or not a mini-VDD
    ; handled it. Registration-time, so one line per boot.
    mov     esi, OFFSET32 V9xMiniRegDdLine
    mov     ecx, V9xMiniRegDdLineLength
    call    V9xMini_Serial_Write

    popad
    popfd
    ret
EndProc MiniVDD_RegisterDisplayDriver

; Tell the main VDD how much video memory the card has.
;
; Entry: EBX = the current VM handle, EBP -> its client registers.
; Exit:  CY and ECX = the size on success, NC when it is not known. Everything
;        except ECX is preserved, which is the contract MINIVDD documents and
;        the DDK's s3v mini-VDD implements in three instructions.
;
; Answering NC rather than zero when the driver has not registered yet is
; deliberate: zero is a size, and a VDD that believed it would be worse than a
; VDD that knows it was not told.
BeginProc MiniVDD_GetTotalVRAMSize
    ; Traced for the first few calls rather than once: the first query arrives
    ; before the display driver has reported a size, so what matters is whether
    ; the VDD asks again afterwards. Bounded so a capture stays readable.
    cmp     V9xVramAsked, 4
    jae     short V9xMini_Vram_Answer
    inc     V9xVramAsked
    pushfd
    pushad
    mov     esi, OFFSET32 V9xMiniVramAskLine
    mov     ecx, V9xMiniVramAskLineLength
    call    V9xMini_Serial_Write
    popad
    popfd

V9xMini_Vram_Answer:
    mov     ecx, V9xTotalVramBytes
    test    ecx, ecx
    jz      short V9xMini_Vram_Unknown
    stc
    ret
V9xMini_Vram_Unknown:
    clc
    ret
EndProc MiniVDD_GetTotalVRAMSize

; Update the S3 DPMS state without changing the active video mode.
;
; CL contains the S3 SR0D DPMS bits: bit 4 disables horizontal sync and bit 6
; disables vertical sync.  Windows power states map to 00h (D0), 10h (D1),
; 40h (D2), and 50h (D3).  The routine also clears CR56[2:1], the alternate
; S3 DPMS controls, and clears SR01[5] on wake in case the BIOS used the
; generic VGA screen-off bit.  All registers and flags are preserved.
;
; This is a positive family gate: only an S3 package build defines
; V9X_S3_DPMS.  The generic VBE mini-VDD runs on unknown silicon, including
; Intel, where these indexes do not name the registers above.  Keeping the
; body out of every non-S3 image makes an accidental foreign-register write
; impossible even if one of the four callers is reached.
BeginProc V9xMini_Set_Dpms
IFNDEF V9X_S3_DPMS
    ; A bare ret satisfies the documented contract by construction.
    ret
ELSE
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
ENDIF
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
IFDEF V9X_INTEL_MMIO_FINGERPRINT
    mov     eax, V9X_I9XX_EVENT_KIND_DPMS
    xor     ebx, ebx
    call    V9xMini_I9xx_Event_Capture
ENDIF
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
IFDEF V9X_INTEL_MMIO_FINGERPRINT
    mov     eax, V9X_I9XX_EVENT_KIND_DPMS
    movzx   ebx, cx
    call    V9xMini_I9xx_Event_Capture
ENDIF
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
IFDEF V9X_VESA_TRACE
    ; Says whether Windows routes a VESA call through us on the DOS-box round
    ; trip. This hook is already installed and shipping, so the trace touches
    ; no new dispatch slot - which matters, because installing one on a
    ; screen-switch callback breaks the transition outright
    ; (docs\decisions\2026-08-28-dos-box-exit-ninth-dot.md, experiment 3).
    ;
    ; At the top of the proc, before the function test: an insertion here does
    ; not lengthen the short jumps below, because it sits before both the jumps
    ; and their targets. Every call is traced, not just 4F10h - "some VESA call
    ; arrived and it was not the one we handle" is an answer too.
    pushfd
    pushad

    mov     esi, OFFSET32 V9xMiniVesaEntryLine
    mov     ecx, V9xMiniVesaEntryLineLength
    call    V9xMini_Serial_Write

    popad
    popfd
ENDIF
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
IFDEF V9X_INTEL_MMIO_FINGERPRINT
    mov     eax, V9X_I9XX_EVENT_KIND_DPMS
    movzx   ebx, byte ptr [ebp.Client_BH]
    call    V9xMini_I9xx_Event_Capture
ENDIF
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
IFDEF V9X_VESA_TRACE
    pushfd
    pushad

    mov     esi, OFFSET32 V9xMiniVesaPostLine
    mov     ecx, V9xMiniVesaPostLineLength
    call    V9xMini_Serial_Write

    popad
    popfd
ENDIF
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
IFDEF V9X_INTEL_MMIO_FINGERPRINT
    mov     eax, V9X_I9XX_EVENT_KIND_DPMS
    xor     ebx, ebx
    call    V9xMini_I9xx_Event_Capture
ENDIF
    pop     ecx
V9xMini_Vesa_Post_Restore:
    pop     eax
V9xMini_Vesa_Post_Done:
    ret
EndProc MiniVDD_VESACallPostProcessing

IFDEF V9X_INTEL_MMIO_FINGERPRINT
; EAX = current BAR0 physical base. Returns AX=1 after two complete reads.
; Refuses a moved BAR rather than leaking another permanent physical mapping.
BeginProc V9xMini_I9xx_Capture
    pushad
    mov     V9xI9xxValid, 0

    cmp     eax, 01000000h
    jb      V9xMini_I9xx_Capture_Done
    cmp     eax, 0fff80000h
    ja      V9xMini_I9xx_Capture_Done
    test    eax, V9X_I9XX_MMIO_BYTES - 1
    jnz     V9xMini_I9xx_Capture_Done

    cmp     V9xI9xxMmioLinear, 0
    je      short V9xMini_I9xx_Capture_Map
    cmp     eax, V9xI9xxMmioBase
    jne     V9xMini_I9xx_Capture_Done
    jmp     short V9xMini_I9xx_Capture_Read

V9xMini_I9xx_Capture_Map:
    mov     V9xI9xxMmioBase, eax
    VMMcall _MapPhysToLinear,<eax,V9X_I9XX_MMIO_BYTES,0>
    cmp     eax, 0ffffffffh
    je      short V9xMini_I9xx_Capture_Map_Failed
    mov     V9xI9xxMmioLinear, eax
    jmp     short V9xMini_I9xx_Capture_Read
V9xMini_I9xx_Capture_Map_Failed:
    mov     V9xI9xxMmioBase, 0
    jmp     V9xMini_I9xx_Capture_Done

V9xMini_I9xx_Capture_Read:
    mov     esi, V9xI9xxMmioLinear
    mov     edi, OFFSET32 V9xI9xxOffsets
    mov     edx, OFFSET32 V9xI9xxFirst
    mov     ecx, V9X_I9XX_SNAPSHOT_DWORDS
V9xMini_I9xx_Capture_First:
    mov     ebx, [edi]
    mov     eax, [esi+ebx]
    mov     [edx], eax
    add     edi, 4
    add     edx, 4
    dec     ecx
    jnz     short V9xMini_I9xx_Capture_First

    mov     edi, OFFSET32 V9xI9xxOffsets
    mov     edx, OFFSET32 V9xI9xxSecond
    mov     ecx, V9X_I9XX_SNAPSHOT_DWORDS
V9xMini_I9xx_Capture_Second:
    mov     ebx, [edi]
    mov     eax, [esi+ebx]
    mov     [edx], eax
    add     edi, 4
    add     edx, 4
    dec     ecx
    jnz     short V9xMini_I9xx_Capture_Second
    mov     V9xI9xxValid, 1

V9xMini_I9xx_Capture_Done:
    popad
    movzx   eax, V9xI9xxValid
    ret
EndProc V9xMini_I9xx_Capture

; EAX=value, EDX=FNV-1a state. Returns the updated state in EDX.
BeginProc V9xMini_I9xx_Hash_Dword
    push    ecx
    mov     ecx, 4
V9xMini_I9xx_Hash_Byte:
    movzx   ebx, al
    xor     edx, ebx
    imul    edx, edx, 01000193h
    shr     eax, 8
    loop    V9xMini_I9xx_Hash_Byte
    pop     ecx
    ret
EndProc V9xMini_I9xx_Hash_Dword

; EAX = linear base of the reserve, EBX = byte offset, ECX = dword count.
; Returns the FNV-1a hash in EDX. READ-ONLY: the only memory access is the
; load below, and check-tree.ps1 asserts there are exactly two call sites
; and no store through a register-indirect destination anywhere in it.
BeginProc V9xMini_I9xx_Hash_Range
    push    eax
    push    ebx
    push    ecx
    push    esi
    mov     esi, eax
    add     esi, ebx
    mov     edx, 0811c9dc5h
V9xMini_I9xx_Hash_Range_Next:
    mov     eax, [esi]
    call    V9xMini_I9xx_Hash_Dword
    add     esi, 4
    loop    V9xMini_I9xx_Hash_Range_Next
    pop     esi
    pop     ecx
    pop     ebx
    pop     eax
    ret
EndProc V9xMini_I9xx_Hash_Range

; EAX = current BAR3 physical base. Maps exactly 256 KiB, then hashes two
; complete read-only passes. A moved BAR is refused rather than mapped again.
BeginProc V9xMini_I9xx_Gtt_Capture
    pushad
    mov     V9xI9xxGttValid, 0
    cmp     eax, 01000000h
    jb      V9xMini_I9xx_Gtt_Done
    cmp     eax, 0fffC0000h
    ja      V9xMini_I9xx_Gtt_Done
    test    eax, V9X_I9XX_GTT_BYTES - 1
    jnz     V9xMini_I9xx_Gtt_Done

    cmp     V9xI9xxGttLinear, 0
    je      short V9xMini_I9xx_Gtt_Map
    cmp     eax, V9xI9xxGttBase
    jne     V9xMini_I9xx_Gtt_Done
    jmp     short V9xMini_I9xx_Gtt_Read
V9xMini_I9xx_Gtt_Map:
    mov     V9xI9xxGttBase, eax
    VMMcall _MapPhysToLinear,<eax,V9X_I9XX_GTT_BYTES,0>
    cmp     eax, 0ffffffffh
    je      short V9xMini_I9xx_Gtt_Map_Failed
    mov     V9xI9xxGttLinear, eax
    jmp     short V9xMini_I9xx_Gtt_Read
V9xMini_I9xx_Gtt_Map_Failed:
    mov     V9xI9xxGttBase, 0
    jmp     V9xMini_I9xx_Gtt_Done

V9xMini_I9xx_Gtt_Read:
    mov     edi, V9xI9xxGttLinear
    mov     edx, 0811c9dc5h
    mov     ecx, V9X_I9XX_GTT_ENTRY_COUNT
V9xMini_I9xx_Gtt_Hash_A:
    mov     eax, [edi]
    call    V9xMini_I9xx_Hash_Dword
    add     edi, 4
    dec     ecx
    jnz     short V9xMini_I9xx_Gtt_Hash_A
    mov     V9xI9xxGttHashA, edx

    mov     edi, V9xI9xxGttLinear
    mov     edx, 0811c9dc5h
    mov     ecx, V9X_I9XX_GTT_ENTRY_COUNT
V9xMini_I9xx_Gtt_Hash_B:
    mov     eax, [edi]
    call    V9xMini_I9xx_Hash_Dword
    add     edi, 4
    dec     ecx
    jnz     short V9xMini_I9xx_Gtt_Hash_B
    mov     V9xI9xxGttHashB, edx
    mov     V9xI9xxGttValid, 1

V9xMini_I9xx_Gtt_Done:
    popad
    movzx   eax, V9xI9xxGttValid
    ret
EndProc V9xMini_I9xx_Gtt_Capture

; EAX=event kind, EBX=event context. Capture only after both Phase 1 mappings
; exist. Every MMIO field is read twice and the full GTT is hashed twice; no
; store targets either mapped aperture. Returns EAX=1 when retained.
BeginProc V9xMini_I9xx_Event_Capture
    cmp     V9xI9xxEventBusy, 0
    jne     V9xMini_I9xx_Event_Drop
    mov     V9xI9xxEventBusy, 1
    mov     V9xI9xxEventResult, 0
    mov     V9xI9xxEventKind, eax
    mov     V9xI9xxEventContext, ebx
    pushad

    cmp     V9xI9xxValid, 0
    je      V9xMini_I9xx_Event_Unavailable_Saved
    cmp     V9xI9xxGttValid, 0
    je      V9xMini_I9xx_Event_Unavailable_Saved
    movzx   eax, V9xI9xxEventCount
    cmp     eax, V9X_I9XX_EVENT_MAX
    jae     V9xMini_I9xx_Event_Drop_Saved
    imul    eax, eax, V9X_I9XX_EVENT_BYTES
    mov     edi, OFFSET32 V9xI9xxEventRecords
    add     edi, eax
    movzx   eax, V9xI9xxEventCount
    inc     eax
    mov     [edi], eax
    mov     eax, V9xI9xxEventKind
    mov     [edi+4], eax
    mov     eax, V9xI9xxEventContext
    mov     [edi+8], eax

    mov     ebp, V9X_I9XX_EVENT_COMPLETE or V9X_I9XX_EVENT_MMIO_STABLE
    mov     esi, V9xI9xxMmioLinear
    mov     edx, OFFSET32 V9xI9xxEventOffsets
    lea     ebx, [edi+16]
    mov     ecx, 14
V9xMini_I9xx_Event_Mmio_First:
    mov     eax, [edx]
    mov     eax, [esi+eax]
    mov     [ebx], eax
    add     edx, 4
    add     ebx, 4
    dec     ecx
    jnz     short V9xMini_I9xx_Event_Mmio_First

    mov     edx, OFFSET32 V9xI9xxEventOffsets
    lea     ebx, [edi+16]
    mov     ecx, 14
V9xMini_I9xx_Event_Mmio_Second:
    mov     eax, [edx]
    mov     eax, [esi+eax]
    cmp     eax, [ebx]
    je      short V9xMini_I9xx_Event_Mmio_Same
    and     ebp, 0fffffffdh
V9xMini_I9xx_Event_Mmio_Same:
    add     edx, 4
    add     ebx, 4
    dec     ecx
    jnz     short V9xMini_I9xx_Event_Mmio_Second

    mov     esi, V9xI9xxGttLinear
    mov     edx, 0811c9dc5h
    mov     ecx, V9X_I9XX_GTT_ENTRY_COUNT
V9xMini_I9xx_Event_Gtt_A:
    mov     eax, [esi]
    call    V9xMini_I9xx_Hash_Dword
    add     esi, 4
    dec     ecx
    jnz     short V9xMini_I9xx_Event_Gtt_A
    mov     [edi+72], edx

    mov     esi, V9xI9xxGttLinear
    mov     edx, 0811c9dc5h
    mov     ecx, V9X_I9XX_GTT_ENTRY_COUNT
V9xMini_I9xx_Event_Gtt_B:
    mov     eax, [esi]
    call    V9xMini_I9xx_Hash_Dword
    add     esi, 4
    dec     ecx
    jnz     short V9xMini_I9xx_Event_Gtt_B
    mov     [edi+76], edx
    cmp     edx, [edi+72]
    jne     short V9xMini_I9xx_Event_Gtt_Compared
    or      ebp, V9X_I9XX_EVENT_GTT_STABLE
V9xMini_I9xx_Event_Gtt_Compared:
    mov     eax, [edi+20]
    and     eax, 001ffff8h
    mov     ebx, [edi+24]
    and     ebx, 001ffff8h
    cmp     eax, ebx
    jne     short V9xMini_I9xx_Event_Ring_Compared
    or      ebp, V9X_I9XX_EVENT_RING_IDLE
V9xMini_I9xx_Event_Ring_Compared:
    test    dword ptr [edi+32], 1
    jnz     short V9xMini_I9xx_Event_Ring_Controlled
    or      ebp, V9X_I9XX_EVENT_RING_DISABLED
V9xMini_I9xx_Event_Ring_Controlled:
    test    dword ptr [edi+16], 1
    jz      short V9xMini_I9xx_Event_Pgtbl_Checked
    or      ebp, V9X_I9XX_EVENT_PGTBL_VALID
V9xMini_I9xx_Event_Pgtbl_Checked:
    mov     [edi+12], ebp
    inc     V9xI9xxEventCount
    mov     V9xI9xxEventResult, 1
    jmp     short V9xMini_I9xx_Event_Done_Saved

V9xMini_I9xx_Event_Drop_Saved:
    inc     V9xI9xxEventDropped
V9xMini_I9xx_Event_Unavailable_Saved:
V9xMini_I9xx_Event_Done_Saved:
    popad
    mov     V9xI9xxEventBusy, 0
    movzx   eax, V9xI9xxEventResult
    ret
V9xMini_I9xx_Event_Drop:
    inc     V9xI9xxEventDropped
    xor     eax, eax
    ret
EndProc V9xMini_I9xx_Event_Capture

; EAX=reserve physical, ECX=next stream index, EDX=dword. Every value is
; checked against the approved ten-dword stream before entering the ring.
; Stage 0 also fills the scratch page with the guard pattern. This does not
; touch MMIO and is reachable only after the 16-bit side persisted intent.
; Every refusal below records why in V9xI9xxRingStageFail, which the API hands
; back in Client_EBX. A bare "the VxD said no" cost an armed boot on
; 2026-09-14: seven conditions, one return value, and no serial port to ask.
BeginProc V9xMini_I9xx_Ring_Stage
    pushad
    mov     V9xI9xxRingResult, 0

    ; Phase selector in ESI. Two phases, TWO TABLES and TWO COUNTERS, so a
    ; Phase 4 dword can never be staged into a Phase 5 slot - and the two
    ; streams occupy disjoint regions of the ring, so they cannot overlap
    ; even if a counter were wrong.
    mov     V9xI9xxRingStageFail, 11
    cmp     esi, 4
    je      short V9xMini_I9xx_Ring_Stage_Phase4
    cmp     esi, 5
    jne     V9xMini_I9xx_Ring_Stage_Done
IFDEF V9X_I9XX_PHASE5_SUBMIT
    mov     V9xI9xxStageTable, OFFSET32 V9xI9xxPhase5Table
    mov     V9xI9xxStageBound, V9X_I9XX_P5_DWORDS
    mov     V9xI9xxStageRingOff, V9X_I9XX_P5_RING_OFFSET
    movzx   ebx, V9xI9xxP5Staged
    jmp     short V9xMini_I9xx_Ring_Stage_Selected
ELSE
    ; Phase 5 staging is compiled out. The verb refuses rather than
    ; silently accepting a dword it would never submit.
    jmp     V9xMini_I9xx_Ring_Stage_Done
ENDIF
V9xMini_I9xx_Ring_Stage_Phase4:
    mov     V9xI9xxStageTable, OFFSET32 V9xI9xxPhase4Table
    mov     V9xI9xxStageBound, V9X_I9XX_P4_DWORDS
    mov     V9xI9xxStageRingOff, 0
    movzx   ebx, V9xI9xxRingStaged
V9xMini_I9xx_Ring_Stage_Selected:
    mov     V9xI9xxStagePhase, esi

    mov     V9xI9xxRingStageFail, 1
    cmp     eax, V9X_I9XX_RESERVE_PHYS
    jne     V9xMini_I9xx_Ring_Stage_Done
    mov     V9xI9xxRingStageFail, 2
    cmp     V9xI9xxMmioBase, 0fe980000h
    jne     V9xMini_I9xx_Ring_Stage_Done
    mov     V9xI9xxRingStageFail, 3
    cmp     V9xI9xxValid, 1
    jne     V9xMini_I9xx_Ring_Stage_Done
    ; Strictly in order, against THIS phase's counter.
    mov     V9xI9xxRingStageFail, 4
    cmp     ecx, ebx
    jne     V9xMini_I9xx_Ring_Stage_Done
    ; Within THIS phase's table.
    mov     V9xI9xxRingStageFail, 5
    cmp     ecx, V9xI9xxStageBound
    jae     V9xMini_I9xx_Ring_Stage_Done
    ; And exactly the dword the generated table says belongs there.
    mov     V9xI9xxRingStageFail, 6
    mov     ebx, V9xI9xxStageTable
    cmp     edx, [ebx+ecx*4]
    jne     V9xMini_I9xx_Ring_Stage_Done

    cmp     ecx, 0
    jne     short V9xMini_I9xx_Ring_Stage_Write
    cmp     V9xI9xxRingLinear, 0
    jne     short V9xMini_I9xx_Ring_Stage_Write
    mov     V9xI9xxRingStageFail, 7
    ; GMADR + the reserve offset, not BSM + it. Measured 2026-09-14: a CPU
    ; access aimed at stolen memory's own physical addresses is not routed,
    ; while the aperture translated by the GTT reaches the same pages. This
    ; window is a device BAR like the two already mapped here.
    mov     eax, V9X_I9XX_RESERVE_PHYS
    VMMcall _MapPhysToLinear,<eax,V9X_I9XX_RESERVE_BYTES_TOTAL,0>
    cmp     eax, 0ffffffffh
    je      V9xMini_I9xx_Ring_Stage_Done
    mov     V9xI9xxRingLinear, eax
    ; The scratch guard belongs to Phase 4 and is written once, on its
    ; first staged dword. Phase 5 never reaches here, because the chain
    ; requires Phase 4 to have run first and the mapping already exists.
    cmp     V9xI9xxStagePhase, 4
    jne     short V9xMini_I9xx_Ring_Stage_Write
    mov     edi, eax
    add     edi, 00011000h     ; scratch at GTT 6C1000
    mov     eax, 0a5a5a5a5h
    mov     ecx, 1024
V9xMini_I9xx_Ring_Stage_Guard:
    mov     [edi], eax
    add     edi, 4
    dec     ecx
    jnz     short V9xMini_I9xx_Ring_Stage_Guard
    xor     ecx, ecx
    xor     edx, edx          ; stream word 0 is MI_NOOP
V9xMini_I9xx_Ring_Stage_Write:
    mov     V9xI9xxRingStageFail, 8
    mov     edi, V9xI9xxRingLinear
    cmp     edi, 0
    je      V9xMini_I9xx_Ring_Stage_Done
    ; The single store. Both phases converge here, which is what keeps
    ; "every store lives in one place" true after adding a second stream.
    ; The phase-selected ring offset is what keeps them apart.
    add     edi, V9xI9xxStageRingOff
    mov     [edi+ecx*4], edx
    mov     eax, [edi+ecx*4]
    mov     V9xI9xxRingStageRead, eax
    mov     V9xI9xxRingStageFail, 0
    cmp     eax, edx
    je      short V9xMini_I9xx_Ring_Stage_Ok
    ; Measured 2026-09-14: a ring-0 store to stolen memory through a
    ; _MapPhysToLinear mapping of BSM does not read back. Recorded rather than
    ; fatal, because the bytes the GPU fetches are now written through the
    ; GMADR aperture, which is the CPU's documented path into this memory.
    ; The value check above is what gates the stream; this round trip never
    ; was a security property.
    mov     V9xI9xxRingStageFail, 10
V9xMini_I9xx_Ring_Stage_Ok:
    cmp     V9xI9xxStagePhase, 4
    jne     short V9xMini_I9xx_Ring_Stage_OkP5
    inc     V9xI9xxRingStaged
    jmp     short V9xMini_I9xx_Ring_Stage_OkDone
V9xMini_I9xx_Ring_Stage_OkP5:
IFDEF V9X_I9XX_PHASE5_SUBMIT
    inc     V9xI9xxP5Staged
ENDIF
V9xMini_I9xx_Ring_Stage_OkDone:
    mov     V9xI9xxRingResult, 1
V9xMini_I9xx_Ring_Stage_Done:
    popad
    movzx   eax, V9xI9xxRingResult
    ret
EndProc V9xMini_I9xx_Ring_Stage

IFDEF V9X_I9XX_FIRST_WRITE_EXECUTOR
; Poll a submitted tail. Returns EAX=1 only when HEAD reached the exact
; expected byte offset; time and iteration bounds are both mandatory.
BeginProc V9xMini_I9xx_Ring_Wait
    VMMcall Get_System_Time
    mov     V9xI9xxRingStartMs, eax
    mov     V9xI9xxRingPolls, 0
    mov     V9xI9xxRingElapsed, 0
V9xMini_I9xx_Ring_Wait_Next:
    mov     esi, V9xI9xxMmioLinear
    mov     eax, [esi+02034h]
    mov     V9xI9xxRingHead, eax
    mov     ecx, [esi+02030h]
    mov     V9xI9xxRingTail, ecx
    and     eax, 001ffffch       ; HEAD_ADDR, not HEAD_WRAP_COUNT
    cmp     eax, V9xI9xxRingWant
    je      short V9xMini_I9xx_Ring_Wait_Success
    inc     V9xI9xxRingPolls
    cmp     V9xI9xxRingPolls, 1000000
    jae     short V9xMini_I9xx_Ring_Wait_Timeout
    VMMcall Get_System_Time
    sub     eax, V9xI9xxRingStartMs
    mov     V9xI9xxRingElapsed, eax
    cmp     eax, 200
    jb      V9xMini_I9xx_Ring_Wait_Next
V9xMini_I9xx_Ring_Wait_Timeout:
    xor     eax, eax
    ret
V9xMini_I9xx_Ring_Wait_Success:
    VMMcall Get_System_Time
    sub     eax, V9xI9xxRingStartMs
    mov     V9xI9xxRingElapsed, eax
    mov     eax, 1
    ret
EndProc V9xMini_I9xx_Ring_Wait

; The only Intel MMIO write procedure. All card-state stores are the five
; reviewed ring registers. No handler retries a failed step, and timeout
; leaves the ring enabled but permanently poisons this session.
; EBX=full execution CRC, ECX=step (5,6,7,8,9,11).
BeginProc V9xMini_I9xx_Ring_Execute
    pushad
    cmp     V9xI9xxRingBusy, 0
    jne     V9xMini_I9xx_Ring_Execute_Busy
    mov     V9xI9xxRingBusy, 1
    mov     V9xI9xxRingResult, 0
    mov     V9xI9xxRingElapsed, 0
    mov     V9xI9xxRingPolls, 0
    mov     V9xI9xxRingExecCrc, ebx
    mov     V9xI9xxRingExecStep, ecx
    ; One code per refusal. Failure 3 used to cover every condition from here
    ; to the step dispatch, which is how S05Failure=3 said nothing on
    ; 2026-09-14 beyond "not today".
    mov     V9xI9xxRingFailure, 4
    cmp     V9xI9xxRingPoison, 0
    jne     V9xMini_I9xx_Ring_Execute_Done
    mov     V9xI9xxRingFailure, 5
    cmp     V9xI9xxRingStaged, 10
    jne     V9xMini_I9xx_Ring_Execute_Done
    mov     V9xI9xxRingFailure, 6
    cmp     V9xI9xxRingExecCrc, V9X_I9XX_P4_CRC
    jne     V9xMini_I9xx_Ring_Execute_Done
    mov     V9xI9xxRingFailure, 7
    cmp     V9xI9xxMmioBase, 0fe980000h
    jne     V9xMini_I9xx_Ring_Execute_Done
    mov     V9xI9xxRingFailure, 8
    mov     esi, V9xI9xxMmioLinear
    test    esi, esi
    jz      V9xMini_I9xx_Ring_Execute_Done
    mov     V9xI9xxRingFailure, 9
    mov     edi, V9xI9xxRingLinear
    test    edi, edi
    jz      V9xMini_I9xx_Ring_Execute_Done
    mov     V9xI9xxRingFailure, 10
    mov     ecx, V9xI9xxRingExecStep
    cmp     ecx, 5
    je      V9xMini_I9xx_Ring_Execute_Program
    cmp     ecx, 6
    je      V9xMini_I9xx_Ring_Execute_Probe
    cmp     ecx, 7
    je      V9xMini_I9xx_Ring_Execute_Wrap
    cmp     ecx, 8
    je      V9xMini_I9xx_Ring_Execute_Reprobe
    cmp     ecx, 9
    je      V9xMini_I9xx_Ring_Execute_Blt
    cmp     ecx, 11
    je      V9xMini_I9xx_Ring_Execute_Teardown
    jmp     V9xMini_I9xx_Ring_Execute_Done

V9xMini_I9xx_Ring_Execute_Program:
    mov     V9xI9xxRingFailure, 11
    cmp     V9xI9xxRingStep, 0
    jne     V9xMini_I9xx_Ring_Execute_Done
    mov     V9xI9xxRingFailure, 12
    mov     ecx, 10
    mov     ebx, OFFSET32 V9xI9xxRingExpected
    mov     edx, edi
V9xMini_I9xx_Ring_Execute_CheckStream:
    mov     eax, [ebx]
    cmp     eax, [edx]
    jne     V9xMini_I9xx_Ring_Execute_Done
    add     ebx, 4
    add     edx, 4
    dec     ecx
    jnz     short V9xMini_I9xx_Ring_Execute_CheckStream
    mov     V9xI9xxRingFailure, 13
    cmp     dword ptr [edi+00011000h], 0a5a5a5a5h
    jne     V9xMini_I9xx_Ring_Execute_Done
    cmp     dword ptr [edi+00011ffch], 0a5a5a5a5h
    jne     V9xMini_I9xx_Ring_Execute_Done
    mov     V9xI9xxRingFailure, 14
    cmp     dword ptr [esi+02030h], 0
    jne     V9xMini_I9xx_Ring_Execute_Done
    cmp     dword ptr [esi+02034h], 0
    jne     V9xMini_I9xx_Ring_Execute_Done
    cmp     dword ptr [esi+02038h], 0
    jne     V9xMini_I9xx_Ring_Execute_Done
    cmp     dword ptr [esi+0203ch], 0
    jne     V9xMini_I9xx_Ring_Execute_Done
    mov     V9xI9xxRingFailure, 1
    mov     dword ptr [esi+0203ch], 0
    cmp     dword ptr [esi+0203ch], 0
    jne     V9xMini_I9xx_Ring_Execute_Poison
    mov     dword ptr [esi+02034h], 0
    cmp     dword ptr [esi+02034h], 0
    jne     V9xMini_I9xx_Ring_Execute_Poison
    mov     dword ptr [esi+02030h], 0
    cmp     dword ptr [esi+02030h], 0
    jne     V9xMini_I9xx_Ring_Execute_Poison
    mov     dword ptr [esi+02038h], V9X_I9XX_RING_START
    cmp     dword ptr [esi+02038h], V9X_I9XX_RING_START
    jne     V9xMini_I9xx_Ring_Execute_Poison
    mov     dword ptr [esi+0203ch], 0000f001h
    mov     eax, [esi+0203ch]
    and     eax, 0fffff7ffh     ; ignore dynamic RING_WAIT status bit 11
    cmp     eax, 0000f001h
    jne     V9xMini_I9xx_Ring_Execute_Poison
    jmp     V9xMini_I9xx_Ring_Execute_Success

V9xMini_I9xx_Ring_Execute_Probe:
    cmp     V9xI9xxRingStep, 5
    jne     V9xMini_I9xx_Ring_Execute_Done
    cmp     dword ptr [esi+02034h], 0
    jne     V9xMini_I9xx_Ring_Execute_Poison
    mov     dword ptr [esi+02030h], 8
    cmp     dword ptr [esi+02030h], 8
    jne     V9xMini_I9xx_Ring_Execute_Poison
    mov     V9xI9xxRingWant, 8
    jmp     V9xMini_I9xx_Ring_Execute_Wait

V9xMini_I9xx_Ring_Execute_Wrap:
    cmp     V9xI9xxRingStep, 6
    jne     V9xMini_I9xx_Ring_Execute_Done
    cmp     dword ptr [esi+02034h], 8
    jne     V9xMini_I9xx_Ring_Execute_Poison
    cmp     dword ptr [esi+02030h], 8
    jne     V9xMini_I9xx_Ring_Execute_Poison
    add     edi, 8
    mov     ecx, 16382
    xor     eax, eax
V9xMini_I9xx_Ring_Execute_Wrap_Fill:
    mov     [edi], eax
    add     edi, 4
    dec     ecx
    jnz     short V9xMini_I9xx_Ring_Execute_Wrap_Fill
    mov     dword ptr [esi+02030h], 0
    cmp     dword ptr [esi+02030h], 0
    jne     V9xMini_I9xx_Ring_Execute_Poison
    mov     V9xI9xxRingWant, 0
    jmp     V9xMini_I9xx_Ring_Execute_Wait

V9xMini_I9xx_Ring_Execute_Reprobe:
    cmp     V9xI9xxRingStep, 7
    jne     V9xMini_I9xx_Ring_Execute_Done
    mov     eax, [esi+02034h]
    and     eax, 001ffffch
    cmp     eax, 0
    jne     V9xMini_I9xx_Ring_Execute_Poison
    mov     dword ptr [edi], 0
    mov     dword ptr [edi+4], 02000000h
    mov     dword ptr [esi+02030h], 8
    cmp     dword ptr [esi+02030h], 8
    jne     V9xMini_I9xx_Ring_Execute_Poison
    mov     V9xI9xxRingWant, 8
    jmp     V9xMini_I9xx_Ring_Execute_Wait

V9xMini_I9xx_Ring_Execute_Blt:
    cmp     V9xI9xxRingStep, 8
    jne     V9xMini_I9xx_Ring_Execute_Done
    mov     eax, [esi+02034h]
    and     eax, 001ffffch
    cmp     eax, 8
    jne     V9xMini_I9xx_Ring_Execute_Poison
    mov     ecx, 8
    mov     ebx, OFFSET32 V9xI9xxRingExpected
    add     ebx, 8
    add     edi, 8
V9xMini_I9xx_Ring_Execute_Blt_Copy:
    mov     eax, [ebx]
    mov     [edi], eax
    add     ebx, 4
    add     edi, 4
    dec     ecx
    jnz     short V9xMini_I9xx_Ring_Execute_Blt_Copy
    mov     dword ptr [esi+02030h], 40
    cmp     dword ptr [esi+02030h], 40
    jne     V9xMini_I9xx_Ring_Execute_Poison
    mov     V9xI9xxRingWant, 40
    jmp     V9xMini_I9xx_Ring_Execute_Wait

V9xMini_I9xx_Ring_Execute_Wait:
    call    V9xMini_I9xx_Ring_Wait
    or      eax, eax
    jz      short V9xMini_I9xx_Ring_Execute_Timeout
    jmp     V9xMini_I9xx_Ring_Execute_Success
V9xMini_I9xx_Ring_Execute_Timeout:
    mov     V9xI9xxRingFailure, 2
    jmp     short V9xMini_I9xx_Ring_Execute_Poison

V9xMini_I9xx_Ring_Execute_Teardown:
    cmp     V9xI9xxRingStep, 9
    jne     V9xMini_I9xx_Ring_Execute_Done
    mov     eax, [esi+02034h]
    and     eax, 001ffffch
    cmp     eax, 40
    jne     V9xMini_I9xx_Ring_Execute_Poison
    cmp     dword ptr [esi+02030h], 40
    jne     V9xMini_I9xx_Ring_Execute_Poison
    mov     dword ptr [esi+0203ch], 0
    cmp     dword ptr [esi+0203ch], 0
    jne     V9xMini_I9xx_Ring_Execute_Poison
    mov     dword ptr [esi+02034h], 0
    cmp     dword ptr [esi+02034h], 0
    jne     V9xMini_I9xx_Ring_Execute_Poison
    mov     dword ptr [esi+02030h], 0
    cmp     dword ptr [esi+02030h], 0
    jne     V9xMini_I9xx_Ring_Execute_Poison
    mov     dword ptr [esi+02038h], 0
    cmp     dword ptr [esi+02038h], 0
    jne     V9xMini_I9xx_Ring_Execute_Poison
    jmp     short V9xMini_I9xx_Ring_Execute_Success

V9xMini_I9xx_Ring_Execute_Poison:
    mov     V9xI9xxRingPoison, 1
    jmp     short V9xMini_I9xx_Ring_Execute_Done
V9xMini_I9xx_Ring_Execute_Success:
    mov     eax, V9xI9xxRingExecStep
    mov     V9xI9xxRingStep, ax
    mov     V9xI9xxRingFailure, 0
    mov     V9xI9xxRingResult, 1
V9xMini_I9xx_Ring_Execute_Done:
    mov     esi, V9xI9xxMmioLinear
    test    esi, esi
    jz      short V9xMini_I9xx_Ring_Execute_NoSnapshot
    mov     eax, [esi+02034h]
    mov     V9xI9xxRingHead, eax
    mov     eax, [esi+02030h]
    mov     V9xI9xxRingTail, eax
V9xMini_I9xx_Ring_Execute_NoSnapshot:
    mov     V9xI9xxRingBusy, 0
    jmp     short V9xMini_I9xx_Ring_Execute_Return
V9xMini_I9xx_Ring_Execute_Busy:
    popad
    xor     eax, eax
    ret
V9xMini_I9xx_Ring_Execute_Return:
    popad
    mov     ebx, V9xI9xxRingHead
    mov     ecx, V9xI9xxRingTail
    mov     edx, V9xI9xxRingElapsed
    mov     esi, V9xI9xxRingPolls
    mov     edi, V9xI9xxRingFailure
    movzx   eax, V9xI9xxRingResult
    ret
EndProc V9xMini_I9xx_Ring_Execute
ENDIF
ENDIF

; Protected-mode API, reached from the 16-bit display driver through
; INT 2Fh AX=1684h BX=V9XMINI_DEVICE_ID.
;
; Entry: EBP = Client_Reg_Struc, client AX = function.
;
; The v1/v2 functions read tables collected at init. The Intel-only v3 capture
; maps a freshly supplied PCI BAR0 and performs two reads of its fixed MMIO
; allowlist; it writes no card state. Nothing here calls the BIOS or allocates.
; An unknown function returns AX=0 rather than failing the call.
BeginProc MiniVDD_PM_API

    movzx   eax, [ebp.Client_AX]

    cmp     ax, V9XMINI_FN_HANDSHAKE
    je      V9xMini_Api_Handshake
    cmp     ax, V9XMINI_FN_CONTROLLER
    je      V9xMini_Api_Controller
    cmp     ax, V9XMINI_FN_MODE_INFO
    je      V9xMini_Api_ModeInfo
    cmp     ax, V9XMINI_FN_STATUS
    je      V9xMini_Api_Status
    cmp     ax, V9XMINI_FN_MODE_AT
    je      V9xMini_Api_ModeAt
    cmp     ax, V9XMINI_FN_MODE_MASKS
    je      V9xMini_Api_ModeMasks
    cmp     ax, V9XMINI_FN_EDID_CHUNK
    je      V9xMini_Api_EdidChunk
    cmp     ax, V9XMINI_FN_MTRR_INFO
    je      V9xMini_Api_MtrrInfo
    cmp     ax, V9XMINI_FN_MTRR_RANGE
    je      V9xMini_Api_MtrrRange
    cmp     ax, V9XMINI_FN_I9XX_CAPTURE
    je      V9xMini_Api_I9xxCapture
    cmp     ax, V9XMINI_FN_I9XX_DWORD
    je      V9xMini_Api_I9xxDword
    cmp     ax, V9XMINI_FN_I9XX_GTT_CAPTURE
    je      V9xMini_Api_I9xxGttCapture
    cmp     ax, V9XMINI_FN_I9XX_GTT_INFO
    je      V9xMini_Api_I9xxGttInfo
    cmp     ax, V9XMINI_FN_I9XX_GTT_CHUNK
    je      V9xMini_Api_I9xxGttChunk
    cmp     ax, V9XMINI_FN_I9XX_EVENT_CAPTURE
    je      V9xMini_Api_I9xxEventCapture
    cmp     ax, V9XMINI_FN_I9XX_EVENT_INFO
    je      V9xMini_Api_I9xxEventInfo
    cmp     ax, V9XMINI_FN_I9XX_EVENT_DWORD
    je      V9xMini_Api_I9xxEventDword
    cmp     ax, V9XMINI_FN_I9XX_RING_STAGE
    je      V9xMini_Api_I9xxRingStage
    cmp     ax, V9XMINI_FN_I9XX_RING_MEMORY
    je      V9xMini_Api_I9xxRingMemory
    cmp     ax, V9XMINI_FN_I9XX_RING_EXECUTE
    je      V9xMini_Api_I9xxRingExecute
    cmp     ax, V9XMINI_FN_I9XX_RING_DIAG
    je      V9xMini_Api_I9xxRingDiag
    cmp     ax, V9XMINI_FN_I9XX_RING_HASH
    je      V9xMini_Api_I9xxRingHash

    ; Unknown function.
    mov     [ebp.Client_AX], 0
    ret

V9xMini_Api_I9xxCapture:
IFDEF V9X_INTEL_MMIO_FINGERPRINT
    mov     eax, [ebp.Client_EBX]
    call    V9xMini_I9xx_Capture
    mov     [ebp.Client_AX], ax
ELSE
    mov     [ebp.Client_AX], 0
ENDIF
    ret

V9xMini_Api_I9xxDword:
IFDEF V9X_INTEL_MMIO_FINGERPRINT
    cmp     V9xI9xxValid, 0
    je      short V9xMini_Api_I9xxDword_Missing
    movzx   eax, [ebp.Client_CX]
    cmp     ax, V9X_I9XX_SNAPSHOT_DWORDS
    jae     short V9xMini_Api_I9xxDword_Missing
    mov     ebx, V9xI9xxFirst[eax*4]
    mov     [ebp.Client_EBX], ebx
    mov     ecx, V9xI9xxSecond[eax*4]
    mov     [ebp.Client_ECX], ecx
    mov     edx, V9xI9xxOffsets[eax*4]
    mov     [ebp.Client_EDX], edx
    mov     esi, V9xI9xxMmioBase
    mov     [ebp.Client_ESI], esi
    mov     [ebp.Client_AX], 1
    ret
V9xMini_Api_I9xxDword_Missing:
ENDIF
    mov     [ebp.Client_AX], 0
    ret

; v6 stays a clean refusal until each Phase 4 arm and executor gate is wired.
V9xMini_Api_I9xxRingStage:
IFDEF V9X_INTEL_MMIO_FINGERPRINT
    mov     eax, [ebp.Client_EBX]
    mov     ecx, [ebp.Client_ECX]
    mov     edx, [ebp.Client_EDX]
    ; ESI carries the phase. A caller that does not set it gets neither 4 nor
    ; 5 and is refused, which is the right default for a verb that writes to
    ; memory a GPU command parser will read.
    mov     esi, [ebp.Client_ESI]
    call    V9xMini_I9xx_Ring_Stage
    mov     ebx, V9xI9xxRingStageFail
    mov     [ebp.Client_EBX], ebx
    mov     ecx, V9xI9xxRingStageRead
    mov     [ebp.Client_ECX], ecx
    mov     [ebp.Client_AX], ax
ELSE
    mov     [ebp.Client_AX], 0
ENDIF
    ret
V9xMini_Api_I9xxRingMemory:
IFDEF V9X_INTEL_MMIO_FINGERPRINT
    cmp     V9xI9xxRingStaged, 10
    jne     short V9xMini_Api_I9xxRingMemory_Missing
    mov     eax, V9xI9xxRingLinear
    test    eax, eax
    jz      short V9xMini_Api_I9xxRingMemory_Missing
    mov     ecx, [ebp.Client_ECX]
    cmp     ecx, 000ffffch
    ja      short V9xMini_Api_I9xxRingMemory_Missing
    test    ecx, 3
    jnz     short V9xMini_Api_I9xxRingMemory_Missing
    mov     ebx, [eax+ecx]
    mov     [ebp.Client_EBX], ebx
    mov     [ebp.Client_AX], 1
    ret
V9xMini_Api_I9xxRingMemory_Missing:
ENDIF
    mov     [ebp.Client_AX], 0
    ret
V9xMini_Api_I9xxRingExecute:
IFDEF V9X_I9XX_FIRST_WRITE_EXECUTOR
    mov     ebx, [ebp.Client_EBX]
    mov     ecx, [ebp.Client_ECX]
    call    V9xMini_I9xx_Ring_Execute
    mov     [ebp.Client_EBX], ebx
    mov     [ebp.Client_ECX], ecx
    mov     [ebp.Client_EDX], edx
    mov     [ebp.Client_ESI], esi
    mov     [ebp.Client_EDI], edi
    mov     [ebp.Client_AX], ax
    ret
ENDIF
    mov     [ebp.Client_AX], 0
    ret

V9xMini_Api_I9xxRingDiag:
IFDEF V9X_INTEL_MMIO_FINGERPRINT
    cmp     V9xI9xxValid, 1
    jne     short V9xMini_Api_I9xxRingDiag_Missing
    movzx   ecx, [ebp.Client_CX]
    cmp     ecx, 9
    jae     short V9xMini_Api_I9xxRingDiag_Missing
    mov     edx, V9xI9xxRingDiagOffsets[ecx*4]
    mov     eax, V9xI9xxMmioLinear
    test    eax, eax
    jz      short V9xMini_Api_I9xxRingDiag_Missing
    mov     ebx, [eax+edx]
    mov     [ebp.Client_EBX], ebx
    mov     [ebp.Client_AX], 1
    ret
V9xMini_Api_I9xxRingDiag_Missing:
ENDIF
    mov     [ebp.Client_AX], 0
    ret

V9xMini_Api_I9xxRingHash:
IFDEF V9X_INTEL_MMIO_FINGERPRINT
    ; Guarded by the FINGERPRINT define, not the executor one. This verb
    ; never writes, so it ships in the unarmed build and the unarmed boot
    ; proves the hash path before any armed boot depends on it.
    mov     V9xI9xxHashFail, 1
    cmp     V9xI9xxValid, 1
    jne     V9xMini_Api_I9xxRingHash_Refuse

    mov     V9xI9xxHashFail, 2
    mov     ebx, [ebp.Client_EBX]
    test    ebx, 3
    jnz     V9xMini_Api_I9xxRingHash_Refuse

    mov     V9xI9xxHashFail, 3
    mov     ecx, [ebp.Client_ECX]
    test    ecx, ecx
    jz      V9xMini_Api_I9xxRingHash_Refuse

    ; A per-call bound, so one call cannot spin over an arbitrary range.
    ; The whole reserve is the most anything may ask for.
    mov     V9xI9xxHashFail, 4
    cmp     ecx, V9X_I9XX_RESERVE_DWORDS
    ja      V9xMini_Api_I9xxRingHash_Refuse

    ; Length in bytes, then offset + length, both checked ON CARRY rather
    ; than by a signed compare: a signed test would accept a count whose
    ; byte length wrapped past 2 GiB and read wherever that landed.
    mov     V9xI9xxHashFail, 5
    mov     edx, ecx
    shl     edx, 2
    jc      V9xMini_Api_I9xxRingHash_Refuse
    add     edx, ebx
    jc      V9xMini_Api_I9xxRingHash_Refuse

    mov     V9xI9xxHashFail, 6
    cmp     edx, V9X_I9XX_RESERVE_BYTES_TOTAL
    ja      V9xMini_Api_I9xxRingHash_Refuse

    ; Map the reserve read-only, once. The address is composed from the
    ; GENERATED reserve offset, so it cannot drift from what the driver
    ; and the arm table agree on.
    mov     V9xI9xxHashFail, 7
    mov     eax, V9xI9xxHashLinear
    test    eax, eax
    jnz     short V9xMini_Api_I9xxRingHash_Mapped
    mov     eax, V9X_I9XX_RESERVE_PHYS
    VMMcall _MapPhysToLinear,<eax,V9X_I9XX_RESERVE_BYTES_TOTAL,0>
    cmp     eax, 0ffffffffh
    je      V9xMini_Api_I9xxRingHash_Refuse
    mov     V9xI9xxHashLinear, eax
V9xMini_Api_I9xxRingHash_Mapped:

    ; Two complete passes over the same bytes, returned separately. An
    ; unstable read must be visible to the caller as two different numbers,
    ; not hidden behind one boolean - the same rule the GTT capture follows.
    push    eax
    push    ebx
    push    ecx
    call    V9xMini_I9xx_Hash_Range
    mov     V9xI9xxHashPassA, edx
    pop     ecx
    pop     ebx
    pop     eax
    push    eax
    push    ebx
    push    ecx
    call    V9xMini_I9xx_Hash_Range
    mov     V9xI9xxHashPassB, edx
    pop     ecx
    pop     ebx
    pop     eax

    mov     edx, V9xI9xxHashPassA
    mov     [ebp.Client_EBX], edx
    mov     edx, V9xI9xxHashPassB
    mov     [ebp.Client_ECX], edx
    mov     [ebp.Client_EDX], 0
    mov     [ebp.Client_AX], 1
    ret
V9xMini_Api_I9xxRingHash_Refuse:
    mov     edx, V9xI9xxHashFail
    mov     [ebp.Client_EDX], edx
ENDIF
    mov     [ebp.Client_AX], 0
    ret

V9xMini_Api_I9xxGttCapture:
IFDEF V9X_INTEL_MMIO_FINGERPRINT
    mov     eax, [ebp.Client_EBX]
    call    V9xMini_I9xx_Gtt_Capture
    mov     [ebp.Client_AX], ax
ELSE
    mov     [ebp.Client_AX], 0
ENDIF
    ret

V9xMini_Api_I9xxGttInfo:
IFDEF V9X_INTEL_MMIO_FINGERPRINT
    cmp     V9xI9xxGttValid, 0
    je      short V9xMini_Api_I9xxGttInfo_Missing
    mov     [ebp.Client_EBX], V9X_I9XX_GTT_ENTRY_COUNT
    mov     eax, V9xI9xxGttHashA
    mov     [ebp.Client_ECX], eax
    mov     eax, V9xI9xxGttHashB
    mov     [ebp.Client_EDX], eax
    mov     eax, V9xI9xxGttBase
    mov     [ebp.Client_ESI], eax
    mov     [ebp.Client_AX], 1
    ret
V9xMini_Api_I9xxGttInfo_Missing:
ENDIF
    mov     [ebp.Client_AX], 0
    ret

V9xMini_Api_I9xxGttChunk:
IFDEF V9X_INTEL_MMIO_FINGERPRINT
    cmp     V9xI9xxGttValid, 0
    je      short V9xMini_Api_I9xxGttChunk_Missing
    movzx   eax, [ebp.Client_CX]
    cmp     eax, V9X_I9XX_GTT_CHUNKS
    jae     short V9xMini_Api_I9xxGttChunk_Missing
    shl     eax, 4
    add     eax, V9xI9xxGttLinear
    mov     ebx, [eax]
    mov     [ebp.Client_EBX], ebx
    mov     ecx, [eax+4]
    mov     [ebp.Client_ECX], ecx
    mov     edx, [eax+8]
    mov     [ebp.Client_EDX], edx
    mov     esi, [eax+12]
    mov     [ebp.Client_ESI], esi
    mov     [ebp.Client_AX], 1
    ret
V9xMini_Api_I9xxGttChunk_Missing:
ENDIF
    mov     [ebp.Client_AX], 0
    ret

V9xMini_Api_I9xxEventCapture:
IFDEF V9X_INTEL_MMIO_FINGERPRINT
    movzx   eax, [ebp.Client_CX]
    movzx   ebx, [ebp.Client_DX]
    call    V9xMini_I9xx_Event_Capture
    mov     [ebp.Client_AX], ax
ELSE
    mov     [ebp.Client_AX], 0
ENDIF
    ret

V9xMini_Api_I9xxEventInfo:
IFDEF V9X_INTEL_MMIO_FINGERPRINT
    movzx   eax, V9xI9xxEventCount
    mov     [ebp.Client_EBX], eax
    movzx   eax, V9xI9xxEventDropped
    mov     [ebp.Client_ECX], eax
    mov     [ebp.Client_EDX], V9X_I9XX_EVENT_DWORDS
    mov     [ebp.Client_ESI], V9X_I9XX_EVENT_MAX
    mov     [ebp.Client_AX], 1
ELSE
    mov     [ebp.Client_AX], 0
ENDIF
    ret

V9xMini_Api_I9xxEventDword:
IFDEF V9X_INTEL_MMIO_FINGERPRINT
    movzx   eax, [ebp.Client_CX]
    cmp     ax, V9xI9xxEventCount
    jae     short V9xMini_Api_I9xxEventDword_Missing
    imul    eax, eax, V9X_I9XX_EVENT_BYTES
    movzx   ebx, [ebp.Client_DX]
    cmp     ebx, V9X_I9XX_EVENT_DWORDS
    jae     short V9xMini_Api_I9xxEventDword_Missing
    mov     ebx, V9xI9xxEventRecords[eax+ebx*4]
    mov     [ebp.Client_EBX], ebx
    mov     [ebp.Client_AX], 1
    ret
V9xMini_Api_I9xxEventDword_Missing:
ENDIF
    mov     [ebp.Client_AX], 0
    ret

; What the CPU admits to and what the two global memory-type registers hold.
; Reported, never interpreted; see V9XMAPI.INC.
V9xMini_Api_MtrrInfo:
    push    ecx
    push    edx
    push    esi
    mov     [ebp.Client_AX], 1
    mov     eax, V9xMtrrCap
    mov     [ebp.Client_EBX], eax
    mov     ecx, V9xMtrrDefType
    mov     [ebp.Client_ECX], ecx
    movzx   edx, V9xMtrrFlags
    mov     [ebp.Client_EDX], edx
    movzx   esi, V9xMtrrCount
    mov     [ebp.Client_ESI], esi
    pop     esi
    pop     edx
    pop     ecx
    ret

; In:  client CX = pair index. Out: AX=0 at or beyond the reported count.
V9xMini_Api_MtrrRange:
    push    ecx
    push    edx
    movzx   ecx, [ebp.Client_CX]
    cmp     cx, V9xMtrrCount
    jae     short V9xMini_Api_MtrrRange_Missing
    mov     [ebp.Client_AX], 1
    mov     eax, V9xMtrrBase[ecx*4]
    mov     [ebp.Client_EBX], eax
    mov     eax, V9xMtrrMask[ecx*4]
    mov     [ebp.Client_ECX], eax
    xor     edx, edx
    mov     ax, V9xMtrrHigh
    bt      ax, cx
    jnc     short V9xMini_Api_MtrrRange_Low
    mov     edx, 1
V9xMini_Api_MtrrRange_Low:
    mov     [ebp.Client_EDX], edx
    pop     edx
    pop     ecx
    ret
V9xMini_Api_MtrrRange_Missing:
    mov     [ebp.Client_AX], 0
    pop     edx
    pop     ecx
    ret

; What the init-time collection actually managed, so a failure can be diagnosed
; from the guest instead of guessed at from the host.
;
; See V9XMAPI.INC for the packed counts and flags.
V9xMini_Api_Status:
    push    ecx
    push    edx
    push    esi
    mov     [ebp.Client_AX], 1
    movzx   eax, V9xVbeBufSeg
    mov     [ebp.Client_EBX], eax
    movzx   ecx, V9xVbeQueried
    shl     ecx, 16
    mov     cx, V9xVbeListed
    mov     [ebp.Client_ECX], ecx
    movzx   edx, V9xVbeProbed
    shl     edx, 16
    mov     dx, V9xVbeCached
    mov     [ebp.Client_EDX], edx
    movzx   esi, V9xVbeStatus
    mov     [ebp.Client_ESI], esi
    pop     esi
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
    cmp     V9xVbeCtrlValid, 0
    je      short V9xMini_Api_Controller_Missing
    mov     [ebp.Client_AX], 1
    movzx   eax, V9xVbeCtrlVer
    mov     [ebp.Client_EBX], eax
    movzx   eax, V9xVbeCtrl64K
    mov     [ebp.Client_ECX], eax
    mov     eax, V9xVbeCtrlCaps
    mov     [ebp.Client_EDX], eax
    movzx   eax, V9xVbeCtrlOemRev
    mov     [ebp.Client_ESI], eax
    ret
V9xMini_Api_Controller_Missing:
    mov     [ebp.Client_AX], 0
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
    movzx   edx, V9xVbeCached
    mov     edi, OFFSET32 V9xVbeCache
V9xMini_Api_Mode_Cache_Next:
    test    edx, edx
    jz      short V9xMini_Api_Mode_Probe_Start
    cmp     word ptr [edi+V9X_VBE_REC_MODE_NUMBER], cx
    je      short V9xMini_Api_Mode_Found
    add     edi, V9X_VBE_REC_BYTES
    dec     edx
    jmp     short V9xMini_Api_Mode_Cache_Next

V9xMini_Api_Mode_Probe_Start:
    movzx   edx, V9xVbeProbed
    mov     edi, OFFSET32 V9xVbeProbeCache
V9xMini_Api_Mode_Probe_Next:
    test    edx, edx
    jz      short V9xMini_Api_Mode_Missing
    cmp     word ptr [edi+V9X_VBE_REC_MODE_NUMBER], cx
    je      short V9xMini_Api_Mode_Found
    add     edi, V9X_VBE_REC_BYTES
    dec     edx
    jmp     short V9xMini_Api_Mode_Probe_Next

V9xMini_Api_Mode_Missing:
    mov     [ebp.Client_AX], 0
    jmp     short V9xMini_Api_Mode_Done

V9xMini_Api_Mode_Found:
    mov     [ebp.Client_AX], 1
    mov     ebx, dword ptr [edi+V9X_VBE_REC_PHYS_BASE]
    mov     [ebp.Client_EBX], ebx
    movzx   ecx, word ptr [edi+V9X_VBE_REC_ATTRIBUTES]
    shl     ecx, 16
    mov     cx, word ptr [edi+V9X_VBE_REC_BYTES_PER_LINE]
    mov     [ebp.Client_ECX], ecx
    movzx   edx, word ptr [edi+V9X_VBE_REC_HEIGHT]
    shl     edx, 16
    mov     dx, word ptr [edi+V9X_VBE_REC_WIDTH]
    mov     [ebp.Client_EDX], edx
    movzx   esi, word ptr [edi+V9X_VBE_REC_MEMORY_MODEL]
    shl     esi, 16
    mov     si, word ptr [edi+V9X_VBE_REC_STORAGE_DEPTH]
    mov     [ebp.Client_ESI], esi

V9xMini_Api_Mode_Done:
    pop     edi
    pop     esi
    pop     edx
    pop     ecx
    pop     ebx
    ret

; Indexed list-derived record facts. Rescue probes are intentionally absent.
V9xMini_Api_ModeAt:
    push    ebx
    push    ecx
    push    edx
    push    esi
    push    edi
    movzx   edi, [ebp.Client_CX]
    movzx   eax, V9xVbeCached
    cmp     edi, eax
    jae     short V9xMini_Api_ModeAt_Missing
    shl     edi, V9X_VBE_REC_SHIFT
    add     edi, OFFSET32 V9xVbeCache
    mov     [ebp.Client_AX], 1
    mov     ebx, dword ptr [edi+V9X_VBE_REC_PHYS_BASE]
    mov     [ebp.Client_EBX], ebx
    movzx   ecx, word ptr [edi+V9X_VBE_REC_LIN_BYTES]
    shl     ecx, 16
    mov     cx, word ptr [edi+V9X_VBE_REC_BYTES_PER_LINE]
    mov     [ebp.Client_ECX], ecx
    movzx   edx, word ptr [edi+V9X_VBE_REC_HEIGHT]
    shl     edx, 16
    mov     dx, word ptr [edi+V9X_VBE_REC_WIDTH]
    mov     [ebp.Client_EDX], edx
    movzx   esi, word ptr [edi+V9X_VBE_REC_SIGNIF_DEPTH]
    shl     esi, 16
    mov     si, word ptr [edi+V9X_VBE_REC_STORAGE_DEPTH]
    mov     [ebp.Client_ESI], esi
    movzx   eax, word ptr [edi+V9X_VBE_REC_ATTRIBUTES]
    shl     eax, 16
    mov     ax, word ptr [edi+V9X_VBE_REC_MODE_NUMBER]
    mov     [ebp.Client_EDI], eax
    jmp     short V9xMini_Api_ModeAt_Done
V9xMini_Api_ModeAt_Missing:
    mov     [ebp.Client_AX], 0
V9xMini_Api_ModeAt_Done:
    pop     edi
    pop     esi
    pop     edx
    pop     ecx
    pop     ebx
    ret

V9xMini_Api_ModeMasks:
    push    ebx
    push    ecx
    push    edx
    push    edi
    movzx   edi, [ebp.Client_CX]
    movzx   eax, V9xVbeCached
    cmp     edi, eax
    jae     short V9xMini_Api_ModeMasks_Missing
    shl     edi, V9X_VBE_REC_SHIFT
    add     edi, OFFSET32 V9xVbeCache
    mov     [ebp.Client_AX], 1
    mov     ebx, dword ptr [edi+V9X_VBE_REC_RED]
    mov     [ebp.Client_EBX], ebx
    mov     ecx, dword ptr [edi+V9X_VBE_REC_BLUE]
    mov     [ebp.Client_ECX], ecx
    movzx   edx, word ptr [edi+V9X_VBE_REC_FLAGS]
    shl     edx, 16
    mov     dx, word ptr [edi+V9X_VBE_REC_MEMORY_MODEL]
    mov     [ebp.Client_EDX], edx
    jmp     short V9xMini_Api_ModeMasks_Done
V9xMini_Api_ModeMasks_Missing:
    mov     [ebp.Client_AX], 0
V9xMini_Api_ModeMasks_Done:
    pop     edi
    pop     edx
    pop     ecx
    pop     ebx
    ret

; One 16-byte slice of the cached EDID block, registers only. See
; V9XMAPI.INC for the register map. No caller pointer crosses in either
; direction, and an index past the block or a boot with no valid block both
; answer AX=0 exactly like an unknown function.
V9xMini_Api_EdidChunk:
    test    V9xVbeStatus, V9X_VBE_ST_EDID_VALID
    jz      short V9xMini_Api_NoData
    push    ecx
    movzx   ecx, [ebp.Client_CX]
    cmp     ecx, V9X_VBE_EDID_CHUNKS
    jae     short V9xMini_Api_EdidChunk_Missing
    shl     ecx, 4
    add     ecx, OFFSET32 V9xVbeEdid
    mov     [ebp.Client_AX], 1
    mov     eax, [ecx]
    mov     [ebp.Client_EBX], eax
    mov     eax, [ecx+4]
    mov     [ebp.Client_ECX], eax
    mov     eax, [ecx+8]
    mov     [ebp.Client_EDX], eax
    mov     eax, [ecx+12]
    mov     [ebp.Client_ESI], eax
    pop     ecx
    ret
V9xMini_Api_EdidChunk_Missing:
    pop     ecx

V9xMini_Api_NoData:
    mov     [ebp.Client_AX], 0
    ret
EndProc MiniVDD_PM_API

VxD_LOCKED_CODE_ENDS

VxD_ICODE_SEG

; Everything down to MiniVDD_Dynamic_Init is the init-time VBE collection. It
; assembles away under V9X_NO_VBE_COLLECT: the 4F9Ch API above stays, and its
; zeroed cache is the designed "collection never ran" state it already reports
; as invalid, so families that never read the cache can ship without ever
; running the BIOS at boot.
IFNDEF V9X_NO_VBE_COLLECT

; Render AX as four ASCII hex digits at EDI, for the serial call markers.
; Preserves everything.
BeginProc V9xMini_Hex16
    push    eax
    push    ebx
    push    ecx
    push    edi
    mov     ebx, eax
    mov     ecx, 4
V9xMini_Hex16_Next:
    rol     bx, 4
    mov     al, bl
    and     al, 0Fh
    add     al, '0'
    cmp     al, '9'
    jbe     short V9xMini_Hex16_Store
    add     al, 'a' - '9' - 1
V9xMini_Hex16_Store:
    mov     [edi], al
    inc     edi
    dec     ecx
    jnz     short V9xMini_Hex16_Next
    pop     edi
    pop     ecx
    pop     ebx
    pop     eax
    ret
EndProc V9xMini_Hex16

; Run one buffered VBE call in V86 mode and leave the BIOS status in AX.
;
; In:  AX = VBE function (4F00h, 4F01h or 4F15h), CX = its argument,
;      BX = the client BX (4F15h's subfunction in BL; the earlier functions
;      ignore it and pass 0). Client DX is always 0 - for 4F15h that selects
;      EDID block zero, and 4F00h/4F01h ignore it.
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
    mov     V9xVbeCallBx, bx            ; hold the client BX (see above)

    ; Name the call on the wire before making it. If the BIOS never comes
    ; back, fn= and arg= are the last line of the boot and identify the
    ; killer call exactly.
    mov     eax, esi
    mov     edi, OFFSET32 V9xMiniVbeCallFnHex
    call    V9xMini_Hex16
    mov     eax, edx
    mov     edi, OFFSET32 V9xMiniVbeCallArgHex
    call    V9xMini_Hex16
    push    esi
    mov     esi, OFFSET32 V9xMiniVbeCallLine
    mov     ecx, V9xMiniVbeCallLineLength
    call    V9xMini_Serial_Write
    pop     esi

    VMMcall Get_Cur_VM_Handle           ; EBX = the VM to run this in
    mov     ebp, [ebx.CB_Client_Pointer]

    Push_Client_State
    VMMcall Begin_Nest_V86_Exec         ; V86, because this is a BIOS interrupt

    mov     [ebp.Client_AX], si
    mov     [ebp.Client_CX], dx
    mov     ax, V9xVbeCallBx
    mov     [ebp.Client_BX], ax
    mov     [ebp.Client_DX], 0
    mov     ax, V9xVbeBufSeg
    mov     [ebp.Client_ES], ax
    mov     [ebp.Client_DI], 0

    mov     eax, 10h
    VMMcall Exec_Int

    mov     ax, [ebp.Client_BX]         ; 4F03h's answer; the rest ignore it
    mov     V9xVbeCallRetBx, ax
    movzx   esi, [ebp.Client_AX]        ; reply, before the state is restored

    VMMcall End_Nest_Exec
    Pop_Client_State

    ; Report the reply. "ret=" arriving at all separates "hung in the BIOS"
    ; from "returned and died later".
    mov     eax, esi
    mov     edi, OFFSET32 V9xMiniVbeCallRetHex
    call    V9xMini_Hex16
    push    esi
    mov     esi, OFFSET32 V9xMiniVbeCallRetLine
    mov     ecx, V9xMiniVbeCallRetLineLength
    call    V9xMini_Serial_Write
    pop     esi

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

; Zero all 512 bytes before a 4F01h call. A failed BIOS call must not turn the
; preceding mode's bytes into a second record.
BeginProc V9xMini_Vbe_Clear_Scratch
    push    eax
    push    ecx
    push    edi
    movzx   edi, V9xVbeBufSeg
    shl     edi, 4
    xor     eax, eax
    mov     ecx, 128
    cld
    rep stosd
    pop     edi
    pop     ecx
    pop     eax
    ret
EndProc V9xMini_Vbe_Clear_Scratch

; Query one mode into a packed cache record.
;
; In: CX = bare VBE mode number, DX = V9X_VBE_RF_ORIGIN_*, EDI = destination.
; Out: AX=1 when a bounded record was written, AX=0 otherwise.
;
; Ring 0 performs only the cheap shape filter from the plan. Attributes, linear
; capability, layout and VRAM remain facts for the host-tested C policy; in
; particular an Attributes=0 answer is retained for contradiction diagnostics.
BeginProc V9xMini_Vbe_Query_Record
    push    ebx
    push    ecx
    push    edx
    push    esi
    push    edi

    call    V9xMini_Vbe_Clear_Scratch
    mov     ax, 4F01h
    xor     bx, bx
    call    V9xMini_Vbe_Call
    cmp     ax, 004Fh
    je      short V9xMini_Vbe_Query_Record_Answered
    test    dx, V9X_VBE_RF_ORIGIN_LIST
    jz      short V9xMini_Vbe_Query_Record_Bios_Failed
    or      V9xVbeStatus, V9X_VBE_ST_QUERY_FAILED
V9xMini_Vbe_Query_Record_Bios_Failed:
    inc     V9xVbeFailed
    jmp     V9xMini_Vbe_Query_Record_Reject_Done

V9xMini_Vbe_Query_Record_Answered:
    movzx   esi, V9xVbeBufSeg
    shl     esi, 4
    cmp     word ptr [esi+18], 0        ; XResolution
    je      V9xMini_Vbe_Query_Record_Reject
    cmp     word ptr [esi+20], 0        ; YResolution
    je      V9xMini_Vbe_Query_Record_Reject
    mov     al, byte ptr [esi+25]       ; BitsPerPixel
    cmp     al, 8
    je      short V9xMini_Vbe_Query_Record_Depth_Ok
    cmp     al, 16
    je      short V9xMini_Vbe_Query_Record_Depth_Ok
    cmp     al, 24
    je      short V9xMini_Vbe_Query_Record_Depth_Ok
    cmp     al, 32
    jne     V9xMini_Vbe_Query_Record_Reject
V9xMini_Vbe_Query_Record_Depth_Ok:
    mov     al, byte ptr [esi+27]       ; MemoryModel
    cmp     al, 4                       ; packed pixel
    je      short V9xMini_Vbe_Query_Record_Model_Ok
    cmp     al, 6                       ; direct colour
    jne     V9xMini_Vbe_Query_Record_Reject
V9xMini_Vbe_Query_Record_Model_Ok:
    mov     word ptr [edi+V9X_VBE_REC_MODE_NUMBER], cx
    mov     ax, word ptr [esi+0]
    mov     word ptr [edi+V9X_VBE_REC_ATTRIBUTES], ax
    mov     ax, word ptr [esi+16]
    mov     word ptr [edi+V9X_VBE_REC_BYTES_PER_LINE], ax
    mov     ax, word ptr [esi+50]
    mov     word ptr [edi+V9X_VBE_REC_LIN_BYTES], ax
    mov     ax, word ptr [esi+18]
    mov     word ptr [edi+V9X_VBE_REC_WIDTH], ax
    mov     ax, word ptr [esi+20]
    mov     word ptr [edi+V9X_VBE_REC_HEIGHT], ax
    movzx   ax, byte ptr [esi+27]
    mov     word ptr [edi+V9X_VBE_REC_MEMORY_MODEL], ax
    movzx   ax, byte ptr [esi+25]
    mov     word ptr [edi+V9X_VBE_REC_STORAGE_DEPTH], ax
    mov     word ptr [edi+V9X_VBE_REC_SIGNIF_DEPTH], 0
    mov     bx, dx
    cmp     word ptr [esi+50], 0
    je      short V9xMini_Vbe_Query_Record_No_Lin_Stride
    or      bx, V9X_VBE_RF_LIN_STRIDE
V9xMini_Vbe_Query_Record_No_Lin_Stride:
    mov     eax, dword ptr [esi+40]
    mov     dword ptr [edi+V9X_VBE_REC_PHYS_BASE], eax

    ; VBE 3 linear colour fields win when any was actually supplied. A VBE 3
    ; BIOS that leaves all eight zero falls back to the legacy set.
    cmp     V9xVbeCtrlVer, 0300h
    jb      short V9xMini_Vbe_Query_Record_Legacy_Masks
    mov     eax, dword ptr [esi+54]
    or      eax, dword ptr [esi+58]
    jz      short V9xMini_Vbe_Query_Record_Legacy_Masks
    mov     eax, dword ptr [esi+54]
    mov     dword ptr [edi+V9X_VBE_REC_RED], eax
    mov     eax, dword ptr [esi+58]
    mov     dword ptr [edi+V9X_VBE_REC_BLUE], eax
    or      bx, V9X_VBE_RF_MASKS_LINEAR
    jmp     short V9xMini_Vbe_Query_Record_Masks_Done
V9xMini_Vbe_Query_Record_Legacy_Masks:
    mov     eax, dword ptr [esi+31]
    mov     dword ptr [edi+V9X_VBE_REC_RED], eax
    mov     eax, dword ptr [esi+35]
    mov     dword ptr [edi+V9X_VBE_REC_BLUE], eax
    or      bx, V9X_VBE_RF_MASKS_LEGACY
V9xMini_Vbe_Query_Record_Masks_Done:
    mov     word ptr [edi+V9X_VBE_REC_FLAGS], bx

    pop     edi
    pop     esi
    pop     edx
    pop     ecx
    pop     ebx
    mov     ax, 1
    ret

V9xMini_Vbe_Query_Record_Reject:
    inc     V9xVbeFailed
V9xMini_Vbe_Query_Record_Reject_Done:
    pop     edi
    pop     esi
    pop     edx
    pop     ecx
    pop     ebx
    xor     ax, ax
    ret
EndProc V9xMini_Vbe_Query_Record

IFDEF V9X_VBE_MODE_SWEEP
; Set each listed mode the query pass could not describe, and ask again.
;
; VBE lets a BIOS answer 4F01h with the mode-supported bit clear for a mode
; that is in its table but not available in the current hardware
; configuration. Pineview does exactly that for all eighteen of its OEM
; numbers, including whichever one the panel's own EDID is asking for
; (docs\decisions\2026-08-28-pineview-vbe-mode-list.md). Some BIOSes will
; describe such a mode once it is the active one, so this sets it and asks
; again; a record that comes back usable goes into the same cache as any
; other and the host-tested admit rules judge it on its content.
;
; The cost is stated plainly. 4F02h is a heavier call than the collection's
; 4F00h and 4F01h, it runs the real BIOS at Device_Init with no timeout, and
; it changes the display mid-boot. That is why it is assembled in rather than
; always on. Every attempt is named on the wire by V9xMini_Vbe_Call before it
; is made, so a boot that dies names the mode that killed it.
;
; The entry mode is captured with 4F03h first and put back at the end. A BIOS
; that will not say what mode it is in is not swept at all: without that
; answer there is nothing to restore to.
BeginProc V9xMini_Vbe_Sweep
    push    ebx
    push    ecx
    push    edx
    push    esi
    push    edi

    or      V9xVbeStatus, V9X_VBE_ST_SWEEP_RAN

    mov     ax, 4F03h
    xor     bx, bx
    xor     cx, cx
    call    V9xMini_Vbe_Call
    cmp     ax, 004Fh
    jne     V9xMini_Vbe_Sweep_Done
    mov     ax, V9xVbeCallRetBx
    mov     V9xVbeEntryMode, ax

    xor     edi, edi                    ; staged-list index
V9xMini_Vbe_Sweep_Next:
    cmp     di, V9xVbeListed
    jae     V9xMini_Vbe_Sweep_Restore
    cmp     V9xVbeSwept, V9X_STAGE1_SWEEP_MAX
    jae     V9xMini_Vbe_Sweep_Restore
    cmp     V9xVbeCached, V9X_VBE_CACHE_MAX
    jae     V9xMini_Vbe_Sweep_Restore

    mov     cx, V9xVbeListStage[edi*2]

    ; A mode this list already appeared at was tried on its first appearance.
    xor     ebx, ebx
V9xMini_Vbe_Sweep_Duplicate_Next:
    cmp     ebx, edi
    jae     short V9xMini_Vbe_Sweep_Cached_Check
    cmp     cx, V9xVbeListStage[ebx*2]
    je      V9xMini_Vbe_Sweep_Skip
    inc     ebx
    jmp     short V9xMini_Vbe_Sweep_Duplicate_Next

    ; A mode the query pass described needs no setting to describe it.
V9xMini_Vbe_Sweep_Cached_Check:
    movzx   edx, V9xVbeCached
    mov     esi, OFFSET32 V9xVbeCache
V9xMini_Vbe_Sweep_Cached_Find:
    test    edx, edx
    jz      short V9xMini_Vbe_Sweep_Set
    cmp     word ptr [esi+V9X_VBE_REC_MODE_NUMBER], cx
    je      V9xMini_Vbe_Sweep_Skip
    add     esi, V9X_VBE_REC_BYTES
    dec     edx
    jmp     short V9xMini_Vbe_Sweep_Cached_Find

V9xMini_Vbe_Sweep_Set:
    inc     V9xVbeSwept
    mov     bx, cx
    or      bx, 0C000h                  ; linear framebuffer, do not clear
    mov     ax, 4F02h
    push    ecx
    xor     cx, cx
    call    V9xMini_Vbe_Call
    pop     ecx
    cmp     ax, 004Fh
    jne     short V9xMini_Vbe_Sweep_Skip

    movzx   eax, V9xVbeCached
    shl     eax, V9X_VBE_REC_SHIFT
    add     eax, OFFSET32 V9xVbeCache
    push    edi
    mov     edi, eax
    mov     dx, V9X_VBE_RF_ORIGIN_SWEEP
    inc     V9xVbeQueried
    call    V9xMini_Vbe_Query_Record
    pop     edi
    or      ax, ax
    jz      short V9xMini_Vbe_Sweep_Skip
    inc     V9xVbeCached

V9xMini_Vbe_Sweep_Skip:
    inc     edi
    jmp     V9xMini_Vbe_Sweep_Next

V9xMini_Vbe_Sweep_Restore:
    mov     bx, V9xVbeEntryMode
    mov     ax, 4F02h
    xor     cx, cx
    call    V9xMini_Vbe_Call
    cmp     ax, 004Fh
    je      short V9xMini_Vbe_Sweep_Done
    or      V9xVbeStatus, V9X_VBE_ST_SWEEP_UNRESTORED

V9xMini_Vbe_Sweep_Done:
    pop     edi
    pop     esi
    pop     edx
    pop     ecx
    pop     ebx
    ret
EndProc V9xMini_Vbe_Sweep
ENDIF

; Collect 4F00h, stage its complete bounded VideoModePtr list before any 4F01h
; can overwrite the controller block, then fill the dynamic and rescue caches.
; Nothing here is fatal: every failure leaves the static family table intact.
BeginProc V9xMini_Vbe_Collect

    push    ebx
    push    ecx
    push    edx
    push    esi
    push    edi

    mov     esi, OFFSET32 V9xMiniVbeStartLine
    mov     ecx, V9xMiniVbeStartLineLength
    call    V9xMini_Serial_Write

    ; 512 bytes is what 4F00h may write. VMM_ICODE only, hence init.
    ; Paragraph alignment because the shr below must be exact: a byte-aligned
    ; block truncates to a segment that starts before the allocation, skewing
    ; every peek and letting the stamp and the BIOS write outside it.
    ; Zero-init so a BIOS that answers 004Fh without writing cannot leave
    ; stale bytes to be read back as answers.
    VMMcall _Allocate_Global_V86_Data_Area, <512, GVDAParaAlign + GVDAZeroInit>
    test    eax, eax
    jz      V9xMini_Vbe_Collect_Done
    mov     edx, eax                    ; keep the ring-0 linear for the stamp
    shr     eax, 4                      ; paragraph-aligned first MiB -> segment
    mov     V9xVbeBufSeg, ax

    ; 4F00h. Stamp "VBE2" first so a 2.0-aware BIOS fills in the longer block.
    mov     dword ptr [edx], 32454256h  ; 'VBE2'
    mov     ax, 4F00h
    xor     bx, bx
    xor     cx, cx
    call    V9xMini_Vbe_Call
    cmp     ax, 004Fh
    jne     V9xMini_Vbe_Collect_Done

    ; Accept only a real VESA 2.0-or-later answer, matching vbe_parse.c.
    mov     ax, 0
    call    V9xMini_Vbe_Peek_Word
    cmp     ax, 4556h                   ; 'EV' - first half of "VESA"
    jne     V9xMini_Vbe_Collect_Done
    mov     ax, 2
    call    V9xMini_Vbe_Peek_Word
    cmp     ax, 4153h                   ; 'SA' - second half of "VESA"
    jne     V9xMini_Vbe_Collect_Done
    mov     ax, 4
    call    V9xMini_Vbe_Peek_Word
    cmp     ax, 0200h
    jb      V9xMini_Vbe_Collect_Done
    mov     V9xVbeCtrlVer, ax
    mov     ax, 18
    call    V9xMini_Vbe_Peek_Word
    test    ax, ax
    jz      V9xMini_Vbe_Collect_Done
    mov     V9xVbeCtrl64K, ax
    mov     V9xVbeCtrlValid, 1
    or      V9xVbeStatus, V9X_VBE_ST_CTRL_VALID
    mov     eax, dword ptr [edx+10]
    mov     V9xVbeCtrlCaps, eax
    mov     ax, word ptr [edx+20]
    mov     V9xVbeCtrlOemRev, ax

    ; Resolve VideoModePtr as segment:offset and reject any word that would
    ; cross the first-megabyte boundary. Null and unreachable pointers are
    ; diagnostics, not invitations to query the rescue list as enumeration.
    movzx   eax, word ptr [edx+16]      ; segment
    movzx   esi, word ptr [edx+14]      ; offset
    mov     V9xVbeListSeg, ax
    mov     V9xVbeListOff, si
    mov     ebx, eax
    or      ebx, esi
    jz      V9xMini_Vbe_Collect_List_Unreached
    shl     eax, 4
    add     esi, eax
    movzx   ebx, V9xVbeBufSeg
    shl     ebx, 4
    cmp     esi, ebx
    jb      short V9xMini_Vbe_Collect_List_Classified
    add     ebx, 512
    cmp     esi, ebx
    jae     short V9xMini_Vbe_Collect_List_Classified
    mov     V9xVbeListSelf, 1
V9xMini_Vbe_Collect_List_Classified:
    xor     edi, edi
V9xMini_Vbe_Collect_Stage_Next:
    cmp     edi, V9X_VBE_MODE_LIST_MAX
    jae     V9xMini_Vbe_Collect_List_Overflow
    cmp     esi, 000ffffeh
    ja      V9xMini_Vbe_Collect_List_Unreached
    mov     ax, word ptr [esi]
    cmp     ax, 0ffffh
    je      V9xMini_Vbe_Collect_List_Terminated
    test    ax, 0c000h
    jnz     V9xMini_Vbe_Collect_List_Flagged
    mov     V9xVbeListStage[edi*2], ax
    inc     edi
    inc     V9xVbeListed
    add     esi, 2
    jmp     V9xMini_Vbe_Collect_Stage_Next

V9xMini_Vbe_Collect_List_Unreached:
    or      V9xVbeStatus, V9X_VBE_ST_LIST_UNREACHED
    jmp     V9xMini_Vbe_Collect_Probes
V9xMini_Vbe_Collect_List_Overflow:
    or      V9xVbeStatus, V9X_VBE_ST_LIST_OVERFLOW
    jmp     V9xMini_Vbe_Collect_Probes
V9xMini_Vbe_Collect_List_Flagged:
    or      V9xVbeStatus, V9X_VBE_ST_LIST_FLAGGED
    jmp     V9xMini_Vbe_Collect_Probes
V9xMini_Vbe_Collect_List_Terminated:
    or      V9xVbeStatus, V9X_VBE_ST_LIST_TERM + V9X_VBE_ST_LIST_VALID

    xor     edi, edi                   ; staged-list index
V9xMini_Vbe_Collect_List_Next:
    cmp     di, V9xVbeListed
    jae     V9xMini_Vbe_Collect_Probes
    cmp     V9xVbeQueried, V9X_STAGE1_QUERY_MAX
    jae     V9xMini_Vbe_Collect_Query_Limit
    cmp     V9xVbeCached, V9X_VBE_CACHE_MAX
    jae     V9xMini_Vbe_Collect_Cache_Full

    ; Duplicate mode numbers are ignored after their first appearance.
    xor     ebx, ebx
    mov     ax, V9xVbeListStage[edi*2]
V9xMini_Vbe_Collect_Duplicate_Next:
    cmp     ebx, edi
    jae     short V9xMini_Vbe_Collect_Query_List_Mode
    cmp     ax, V9xVbeListStage[ebx*2]
    je      V9xMini_Vbe_Collect_List_Skip
    inc     ebx
    jmp     short V9xMini_Vbe_Collect_Duplicate_Next

V9xMini_Vbe_Collect_Query_List_Mode:
    mov     cx, ax
    movzx   eax, V9xVbeCached
    shl     eax, V9X_VBE_REC_SHIFT
    add     eax, OFFSET32 V9xVbeCache
    push    edi
    mov     edi, eax
    mov     dx, V9X_VBE_RF_ORIGIN_LIST
    inc     V9xVbeQueried
    call    V9xMini_Vbe_Query_Record
    pop     edi
    or      ax, ax
    jz      short V9xMini_Vbe_Collect_List_Skip
    inc     V9xVbeCached
V9xMini_Vbe_Collect_List_Skip:
    inc     edi
    jmp     V9xMini_Vbe_Collect_List_Next

V9xMini_Vbe_Collect_Query_Limit:
    or      V9xVbeStatus, V9X_VBE_ST_QUERY_LIMIT
    mov     ax, V9xVbeListed
    sub     ax, di
    add     V9xVbeOverflow, ax
    jmp     short V9xMini_Vbe_Collect_Probes
V9xMini_Vbe_Collect_Cache_Full:
    or      V9xVbeStatus, V9X_VBE_ST_CACHE_FULL
    mov     ax, V9xVbeListed
    sub     ax, di
    add     V9xVbeOverflow, ax

V9xMini_Vbe_Collect_Probes:
    xor     esi, esi
V9xMini_Vbe_Collect_Probe_Next:
    cmp     esi, V9xVbeProbeCount
    jae     V9xMini_Vbe_Collect_Edid
    mov     cx, V9xVbeProbeList[esi*2]

    ; A list-derived answer already satisfies by-mode lookup.
    movzx   edx, V9xVbeCached
    mov     edi, OFFSET32 V9xVbeCache
V9xMini_Vbe_Collect_Probe_Find:
    test    edx, edx
    jz      short V9xMini_Vbe_Collect_Probe_Query
    cmp     word ptr [edi+V9X_VBE_REC_MODE_NUMBER], cx
    je      short V9xMini_Vbe_Collect_Probe_Skip
    add     edi, V9X_VBE_REC_BYTES
    dec     edx
    jmp     short V9xMini_Vbe_Collect_Probe_Find

V9xMini_Vbe_Collect_Probe_Query:
    movzx   eax, V9xVbeProbed
    shl     eax, V9X_VBE_REC_SHIFT
    add     eax, OFFSET32 V9xVbeProbeCache
    mov     edi, eax
    mov     dx, V9X_VBE_RF_ORIGIN_PROBE
    call    V9xMini_Vbe_Query_Record
    or      ax, ax
    jz      short V9xMini_Vbe_Collect_Probe_Skip
    inc     V9xVbeProbed
V9xMini_Vbe_Collect_Probe_Skip:
    inc     esi
    jmp     V9xMini_Vbe_Collect_Probe_Next

; EDID block 0 through 4F15h, after mode collection so a hung DDC read cannot
; cost the mode table. DDC failure is non-fatal and never changes mode-scan
; validity: the three EDID status bits are the whole story.
;
; BL=00h asks whether DDC is supported at all; a BIOS that refuses it gets
; the no-DDC bit and no read attempt. BL=01h reads block CX=0 into ES:DI -
; the same 512-byte scratch, cleared first so a lying BIOS that answers 004Fh
; without writing produces 128 zero bytes, which the host-tested parser
; refuses on the header. Client DX is zero (block zero) by V9xMini_Vbe_Call.
V9xMini_Vbe_Collect_Edid:
    mov     ax, 4F15h
    xor     bx, bx                      ; BL=00h: DDC capability probe
    xor     cx, cx
    call    V9xMini_Vbe_Call
    cmp     ax, 004Fh
    je      short V9xMini_Vbe_Collect_Edid_Read
    or      V9xVbeStatus, V9X_VBE_ST_EDID_NO_DDC
    jmp     V9xMini_Vbe_Collect_Done

V9xMini_Vbe_Collect_Edid_Read:
    call    V9xMini_Vbe_Clear_Scratch
    mov     ax, 4F15h
    mov     bx, 1                       ; BL=01h: read EDID
    xor     cx, cx                      ; controller unit 0
    call    V9xMini_Vbe_Call
    cmp     ax, 004Fh
    je      short V9xMini_Vbe_Collect_Edid_Copy
    or      V9xVbeStatus, V9X_VBE_ST_EDID_FAILED
    jmp     V9xMini_Vbe_Collect_Done

V9xMini_Vbe_Collect_Edid_Copy:
    movzx   esi, V9xVbeBufSeg
    shl     esi, 4
    mov     edi, OFFSET32 V9xVbeEdid
    mov     ecx, V9X_VBE_EDID_BYTES / 4
    cld
    rep movsd
    or      V9xVbeStatus, V9X_VBE_ST_EDID_VALID

IFDEF V9X_VBE_MODE_SWEEP
    ; Last, and only on the path where the list was trustworthy: a sweep over
    ; a list that could not be walked would be setting arbitrary numbers.
    call    V9xMini_Vbe_Sweep
ENDIF

V9xMini_Vbe_Collect_Done:
    mov     ax, V9xVbeListSeg
    mov     edi, OFFSET32 V9xMiniVbeStatusPtrSegHex
    call    V9xMini_Hex16
    mov     ax, V9xVbeListOff
    mov     edi, OFFSET32 V9xMiniVbeStatusPtrOffHex
    call    V9xMini_Hex16
    mov     ax, V9xVbeListSelf
    mov     edi, OFFSET32 V9xMiniVbeStatusPtrSelfHex
    call    V9xMini_Hex16
    mov     ax, V9xVbeListed
    mov     edi, OFFSET32 V9xMiniVbeStatusListedHex
    call    V9xMini_Hex16
    mov     ax, V9xVbeQueried
    mov     edi, OFFSET32 V9xMiniVbeStatusQueriedHex
    call    V9xMini_Hex16
    mov     ax, V9xVbeCached
    mov     edi, OFFSET32 V9xMiniVbeStatusCachedHex
    call    V9xMini_Hex16
    mov     ax, V9xVbeFailed
    mov     edi, OFFSET32 V9xMiniVbeStatusFailedHex
    call    V9xMini_Hex16
    mov     ax, V9xVbeOverflow
    mov     edi, OFFSET32 V9xMiniVbeStatusOverflowHex
    call    V9xMini_Hex16
    mov     ax, V9xVbeProbed
    mov     edi, OFFSET32 V9xMiniVbeStatusProbedHex
    call    V9xMini_Hex16
    mov     ax, V9xVbeStatus
    mov     edi, OFFSET32 V9xMiniVbeStatusFlagsHex
    call    V9xMini_Hex16
    mov     esi, OFFSET32 V9xMiniVbeStatusLine
    mov     ecx, V9xMiniVbeStatusLineLength
    call    V9xMini_Serial_Write
    mov     esi, OFFSET32 V9xMiniVbeDoneLine
    mov     ecx, V9xMiniVbeDoneLineLength
    call    V9xMini_Serial_Write
    pop     edi
    pop     esi
    pop     edx
    pop     ecx
    pop     ebx
    ret
EndProc V9xMini_Vbe_Collect

ENDIF ; IFNDEF V9X_NO_VBE_COLLECT

public MiniVDD_Dynamic_Init
; Establish what the CPU admits to, then read the memory-type registers.
;
; Reads only. Nothing here writes an MTRR: Stage A of
; docs\plans\tier0-quality.md deliberately stops at reporting, because the
; rules that would decide a write are host-tested C and their answers have to
; be seen to be right on real machines before any of them acts. A wrong rule
; that has already written shows up as corruption somewhere else entirely.
;
; Each step is the licence for the next, and the ordering is a safety property:
; CPUID is an invalid opcode without the EFLAGS.ID test, RDMSR is one without
; the MSR feature bit, and the MTRR registers exist only when the MTRR bit is
; set. The serial markers bracket the reads for the same reason the vbe-call
; markers bracket a BIOS call - if a CPU whose CPUID claims MTRR still faults
; on RDMSR, a capture names the step instead of leaving a silent boot hang.
;
; Deliberately outside the V9X_NO_VBE_COLLECT gate: that gate is about nested
; BIOS calls, and none of this is one. Every family gets the diagnostics.
BeginProc V9xMini_Mtrr_Inspect
    pushad

    ; EFLAGS.ID (bit 21) will not toggle on a CPU without CPUID.
    pushfd
    pop     eax
    mov     ecx, eax
    xor     eax, 200000h
    push    eax
    popfd
    pushfd
    pop     eax
    push    ecx
    popfd                               ; restore the caller's EFLAGS
    xor     eax, ecx
    test    eax, 200000h
    jz      V9xMini_Mtrr_Done
    or      V9xMtrrFlags, V9X_MTRR_CPU_CPUID

    xor     eax, eax
    cpuid
    test    eax, eax
    jz      V9xMini_Mtrr_Done           ; leaf 1 does not exist
    mov     eax, 1
    cpuid
    test    edx, 20h                    ; MSR
    jz      short V9xMini_Mtrr_No_Msr
    or      V9xMtrrFlags, V9X_MTRR_CPU_MSR
V9xMini_Mtrr_No_Msr:
    test    edx, 1000h                  ; MTRR
    jz      short V9xMini_Mtrr_No_Mtrr
    or      V9xMtrrFlags, V9X_MTRR_CPU_MTRR
V9xMini_Mtrr_No_Mtrr:
    test    edx, 2000h                  ; PGE, which a Stage B write sequence
    jz      short V9xMini_Mtrr_No_Pge   ; would need before touching CR4
    or      V9xMtrrFlags, V9X_MTRR_CPU_PGE
V9xMini_Mtrr_No_Pge:

    ; Both bits, or no MSR is read at all.
    mov     ax, V9xMtrrFlags
    and     ax, V9X_MTRR_CPU_MSR OR V9X_MTRR_CPU_MTRR
    cmp     ax, V9X_MTRR_CPU_MSR OR V9X_MTRR_CPU_MTRR
    jne     V9xMini_Mtrr_Done

    mov     esi, OFFSET32 V9xMiniMtrrReadLine
    mov     ecx, V9xMiniMtrrReadLineLength
    call    V9xMini_Serial_Write

    mov     ecx, 0FEh                   ; IA32_MTRRCAP
    rdmsr
    mov     V9xMtrrCap, eax
    mov     ecx, 2FFh                   ; IA32_MTRR_DEF_TYPE
    rdmsr
    mov     V9xMtrrDefType, eax

    mov     eax, V9xMtrrCap
    and     eax, 0FFh                   ; VCNT
    cmp     eax, V9X_MTRR_RANGE_MAX
    jbe     short V9xMini_Mtrr_Count_Ok
    mov     eax, V9X_MTRR_RANGE_MAX
V9xMini_Mtrr_Count_Ok:
    mov     V9xMtrrCount, ax

    xor     esi, esi
V9xMini_Mtrr_Pair:
    cmp     si, V9xMtrrCount
    jae     short V9xMini_Mtrr_Read_Done
    mov     ecx, 200h                   ; IA32_MTRR_PHYSBASEn
    lea     ecx, [ecx+esi*2]
    rdmsr
    mov     V9xMtrrBase[esi*4], eax
    test    edx, edx
    jz      short V9xMini_Mtrr_Base_Low
    bts     word ptr V9xMtrrHigh, si    ; the range starts above 4 GiB
V9xMini_Mtrr_Base_Low:
    mov     ecx, 201h                   ; IA32_MTRR_PHYSMASKn
    lea     ecx, [ecx+esi*2]
    rdmsr
    mov     V9xMtrrMask[esi*4], eax
    inc     esi
    jmp     short V9xMini_Mtrr_Pair

V9xMini_Mtrr_Read_Done:
    mov     esi, OFFSET32 V9xMiniMtrrDoneLine
    mov     ecx, V9xMiniMtrrDoneLineLength
    call    V9xMini_Serial_Write

V9xMini_Mtrr_Done:
    popad
    ret
EndProc V9xMini_Mtrr_Inspect

IFDEF V9X_NO_SCREEN_SWITCH
; Refuse to switch a DOS box to full screen at all.
;
; Not a workaround invented here: the Windows 98 DDK's own XGA mini-VDD does
; exactly this, and says why - "The XGA's HiRes screen cannot be reliably
; saved and restored if we're in a VESA mode ... So.... we disallow switching
; away from any VESA HiRes mode DOS box." Tier-0 is in that position by
; construction. It drives an unknown chip through its VESA BIOS, has no
; register-level backend to save and restore state with, and on the GMA 950
; the BIOS mode set that would return the adapter to text hangs the machine -
; measured from every mode the panel offers, and again with the driver issuing
; the mode set itself.
;
; Entry per MINIVDD: EAX = -1 in a VESA mode, EBX = the VM being switched away
; from, ECX = the mode number. Exit: CY prohibits the switch, NC allows it.
;
; Refused unconditionally rather than on EAX, deliberately: this build's job is
; to establish whether refusing prevents the hang at all. Whether the main VDD
; reports a driver-set linear-framebuffer mode as "a VESA mode" is exactly the
; kind of assumption that has been wrong twice on this round trip, so it is not
; assumed here. Narrowing the condition is the next build's problem, and only
; worth having if this one works.
BeginProc MiniVDD_CheckScreenSwitchOK
    stc
    ret
EndProc MiniVDD_CheckScreenSwitchOK
ENDIF

IFDEF V9X_VGA_RETURN
; Differential build for docs\issues\2026-08-28-dos-box-entry-hang-gma950.md:
; return the adapter to standard VGA ourselves, before the main VDD tries.
;
; A full-screen DOS box hangs that machine from every Velocity9x mode measured
; - 1024x576 at 32 and 16bpp and 640x480 at 8bpp - while a windowed box is
; clean and the stock VGA driver survives the same transition. The one
; structural difference is that we are sitting in a VESA linear-framebuffer
; mode the main VDD did not set and knows nothing about.
;
; What this is NOT: a way to stop the main VDD doing its own work. These
; callbacks are additive, not overriding - the DDK's own mini-VDDs just `ret`
; from them and the VDD proceeds regardless. Hooking them with no-ops would
; measure nothing, which is what the first draft of this experiment would have
; done.
;
; What it is: the job MINIVDD's PreHiResToVGA comment actually asks for -
; "if your hardware does not return to a standard VGA mode via a call to
; INT 10H, function 0, make sure to do whatever it takes to restore your
; hardware to a standard VGA mode at this time". So set mode 3 through the
; BIOS here, from a point where nothing else is mid-transition.
;
; Reading the outcome: if the hang goes, the main VDD's own route out of an
; LFB mode was the wedge and getting there first avoids it. If it still hangs,
; a BIOS mode set from VxD context is the wedge whoever issues it, and the
; next build has to leave the BIOS out of the path entirely.
;
; Exec_Int runs the real video BIOS with no timeout, exactly as the mode sweep
; does. This build can hang a machine that did not hang before.
BeginProc MiniVDD_PreHiResToVGA
    pushfd
    pushad

    mov     ax, 0003h                   ; INT 10h AH=00h, AL=03h: 80x25 text
    xor     bx, bx
    xor     cx, cx
    call    V9xMini_Vbe_Call

    popad
    popfd
    ret
EndProc MiniVDD_PreHiResToVGA
ENDIF

IFDEF V9X_SCREEN_SWITCH_HOOKS
; Observer hooks for docs\issues\2026-08-28-dos-box-entry-hang-gma950.md, so
; the DOS-box round trip can be watched from the VxD side instead of inferred.
;
; The display driver's own nine-point trace writes nothing on this path and
; neither does the serial log, which says the driver is never entered - but not
; whether the main VDD got as far as intending to. These five callbacks are the
; VDD's own narration of the round trip.
;
; They are notifications, which is what licenses a body that only observes:
; MINIVDD documents PRE_HIRES_TO_VGA, POST_HIRES_TO_VGA, PRE_VGA_TO_HIRES and
; POST_VGA_TO_HIRES as "we are notified", with "Exit: Nothing assumed", and
; CHECK_SCREEN_SWITCH_OK as carry-prohibits / no-carry-allows, so a CLC is the
; same answer as not hooking it at all.
;
; Two variants, because the first one changed the outcome. With the serial
; writes in (-ScreenSwitchTrace) the box never reaches full screen at all and
; the Win16 side wedges, twice out of two, and not one byte is emitted - while
; the plain mini-VDD on the same guest and the same serial device goes full
; screen cleanly. Two mechanisms fit that: a dispatch entry in one of these
; slots changing the main VDD's path, or V9xMini_Serial_Write's raw port I/O on
; 0x3F8-0x3FB deadlocking because it runs inside a callback belonging to the VM
; being switched, whose COM1 the VCD is virtualising. -ScreenSwitchQuiet
; assembles the same hooks with the writes omitted, so the outcome alone
; separates them: a round trip that behaves like the plain build means hooking
; is free and the serial write was the wedge, which would in turn mean a hook
; is a legitimate place to put a repair.
;
; The one measurement already in hand: -NoScreenSwitch installs a single
; refusing hook on CHECK_SCREEN_SWITCH_OK, and the box goes full screen anyway
; with the agent healthy. So the main VDD does not consult that callback on
; this path, and whatever the trace build broke was one of the other four.
;
; What is deliberately NOT hooked: SAVE_REGISTERS and RESTORE_REGISTERS. Those
; are work, not narration - hooking them tells the main VDD the mini-VDD will
; save and restore the state itself, and an observer that only traced would
; destroy exactly the state this round trip is failing to bring back.
;
; Fixed strings only: V9xMini_Hex16 is in ICODE, discarded after init, and
; these run long afterwards.
BeginProc MiniVDD_TraceCheckScreenSwitchOK
IFDEF V9X_SCREEN_SWITCH_TRACE
    pushfd
    pushad

    mov     esi, OFFSET32 V9xMiniSsCheckLine
    mov     ecx, V9xMiniSsCheckLineLength
    call    V9xMini_Serial_Write

    popad
    popfd
ENDIF
    clc                                 ; allow the switch, as no hook does
    ret
EndProc MiniVDD_TraceCheckScreenSwitchOK

BeginProc MiniVDD_TracePreHiResToVGA
IFDEF V9X_SCREEN_SWITCH_TRACE
    pushfd
    pushad

    mov     esi, OFFSET32 V9xMiniSsPreToVgaLine
    mov     ecx, V9xMiniSsPreToVgaLineLength
    call    V9xMini_Serial_Write

    popad
    popfd
ENDIF
    ret
EndProc MiniVDD_TracePreHiResToVGA

BeginProc MiniVDD_TracePostHiResToVGA
IFDEF V9X_SCREEN_SWITCH_TRACE
    pushfd
    pushad

    mov     esi, OFFSET32 V9xMiniSsPostToVgaLine
    mov     ecx, V9xMiniSsPostToVgaLineLength
    call    V9xMini_Serial_Write

    popad
    popfd
ENDIF
    ret
EndProc MiniVDD_TracePostHiResToVGA

BeginProc MiniVDD_TracePreVGAToHiRes
IFDEF V9X_SCREEN_SWITCH_TRACE
    pushfd
    pushad

    mov     esi, OFFSET32 V9xMiniSsPreToHiResLine
    mov     ecx, V9xMiniSsPreToHiResLineLength
    call    V9xMini_Serial_Write

    popad
    popfd
ENDIF
    ret
EndProc MiniVDD_TracePreVGAToHiRes

BeginProc MiniVDD_TracePostVGAToHiRes
IFDEF V9X_SCREEN_SWITCH_TRACE
    pushfd
    pushad

    mov     esi, OFFSET32 V9xMiniSsPostToHiResLine
    mov     ecx, V9xMiniSsPostToHiResLineLength
    call    V9xMini_Serial_Write

    popad
    popfd
ENDIF
    ret
EndProc MiniVDD_TracePostVGAToHiRes
ENDIF

BeginProc MiniVDD_Dynamic_Init
    mov     esi, OFFSET32 V9xMiniInitLine
    mov     ecx, V9xMiniInitLineLength
    call    V9xMini_Serial_Write

IFDEF V9X_SCREEN_SWITCH_HOOKS
    ; Both hook variants name themselves here, and the text says which one is
    ; running - the quiet build writes nothing from the hooks themselves, so
    ; this is the only line that identifies it.
    ;
    ; Before the dispatch table is fetched, deliberately: further down, ECX
    ; holds the table size the power-callback test needs, and code inserted
    ; between the short jumps below and their targets pushes them out of range.
    ; Device_Init is also the one context on this path known to be safe for a
    ; serial write, which is the whole question the quiet variant exists to ask.
    mov     esi, OFFSET32 V9xMiniSsTraceLine
    mov     ecx, V9xMiniSsTraceLineLength
    call    V9xMini_Serial_Write
ENDIF

    VxDCall VDD_Get_Mini_Dispatch_Table
    test    edi, edi
    ; Not short jumps: the dispatch block below them can carry the
    ; screen-switch trace's five extra table writes, which is more than the
    ; -128 byte reach spans. Same instruction either way, two bytes wider.
    jz      V9xMini_Init_Failed
    cmp     ecx, NBR_MINI_VDD_FUNCTIONS
    jb      V9xMini_Init_Failed

    ; These callbacks exist in the legacy 49-entry table.  VESA_SUPPORT
    ; prevents the problematic BIOS DPMS call; the post hook is a D0 safety
    ; net for calls that another component sends directly to the BIOS.
    MiniVDDDispatch VESA_SUPPORT, VESASupport
    MiniVDDDispatch VESA_CALL_POST_PROCESSING, VESACallPostProcessing

    ; Functions 0 and 36, the memory-size pair. Without them the main VDD is
    ; never told the card has anything beyond the visible screen, and it was
    ; measured reserving nothing for itself - so a full-screen DOS box has no
    ; off-screen area for its state to be saved into. Neither is on the
    ; screen-switch path: one runs at registration, the other answers a query.
IFNDEF V9X_NO_VRAM_SIZE
    ; -NoVramSize leaves the pair out. Differential build for
    ; docs\issues\2026-09-06-dos-box-doubles-the-desktop-on-physical-trio64.md:
    ; a windowed DOS box on real Trio64 silicon rewrites the framebuffer at
    ; twice its stride, first seen with the build that added these two entries,
    ; and the dispatch entry alone has already been measured changing the main
    ; VDD's behaviour on a related path.
    MiniVDDDispatch REGISTER_DISPLAY_DRIVER, RegisterDisplayDriver
    MiniVDDDispatch GET_TOTAL_VRAM_SIZE, GetTotalVRAMSize
ENDIF

IFDEF V9X_VGA_RETURN
    ; Experiment only; see MiniVDD_PreHiResToVGA above. Function 4 is in the
    ; 40-entry table as well as the 49-entry one, so the count checked above
    ; already covers it.
    MiniVDDDispatch PRE_HIRES_TO_VGA, PreHiResToVGA
ENDIF

IFDEF V9X_NO_SCREEN_SWITCH
    ; Function 43, inside the 49-entry table the count above already required.
    MiniVDDDispatch CHECK_SCREEN_SWITCH_OK, CheckScreenSwitchOK
ENDIF

IFDEF V9X_SCREEN_SWITCH_HOOKS
    ; Functions 4 to 7 and 43; see the observer bodies above. Functions 4 and 5
    ; also exist in the 40-entry table, 6, 7 and 43 only in the 49-entry one
    ; the count checked above already required.
    MiniVDDDispatch CHECK_SCREEN_SWITCH_OK, TraceCheckScreenSwitchOK
    MiniVDDDispatch PRE_HIRES_TO_VGA, TracePreHiResToVGA
    MiniVDDDispatch POST_HIRES_TO_VGA, TracePostHiResToVGA
    MiniVDDDispatch PRE_VGA_TO_HIRES, TracePreVGAToHiRes
    MiniVDDDispatch POST_VGA_TO_HIRES, TracePostVGAToHiRes
ENDIF

    ; Power callbacks were added to the Windows 98 (4.1) dispatch table.
    ; Older tables are covered by the VESA post hook above.
    cmp     ecx, GET_MONITOR_POWER_STATE_CAPS + 1
    jb      short V9xMini_Power_Defaults
    MiniVDDDispatch SET_MONITOR_POWER_STATE, SetMonitorPowerState
    MiniVDDDispatch GET_MONITOR_POWER_STATE_CAPS, GetMonitorPowerStateCaps

    mov     esi, OFFSET32 V9xMiniPowerCallbacksLine
    mov     ecx, V9xMiniPowerCallbacksLineLength
    call    V9xMini_Serial_Write
IFNDEF V9X_S3_DPMS
    ; Names the family guard in the image, so the build audit can assert that
    ; the S3 register body is absent and a capture says which image is running.
    mov     esi, OFFSET32 V9xMiniDpmsDisabledLine
    mov     ecx, V9xMiniDpmsDisabledLineLength
    call    V9xMini_Serial_Write
ENDIF
IFDEF V9X_INTEL_MMIO_FINGERPRINT
    mov     esi, OFFSET32 V9xMiniIntelMmioLine
    mov     ecx, V9xMiniIntelMmioLineLength
    call    V9xMini_Serial_Write
ENDIF
    jmp     short V9xMini_Init_Succeeded

V9xMini_Power_Defaults:

    mov     esi, OFFSET32 V9xMiniDefaultsLine
    mov     ecx, V9xMiniDefaultsLineLength
    call    V9xMini_Serial_Write
V9xMini_Init_Succeeded:
IFDEF V9X_IO_TRACE
    ; Install the port traps. Global for every VM; the System VM is exempted
    ; at Sys_VM_Init. A port whose install fails is counted by omission -
    ; Installed says how many took.
    push    esi
    push    ecx
    push    edx
    mov     ecx, V9X_IOTRACE_PORTS
    mov     edi, OFFSET32 V9xIoTracePorts
V9xMini_IoTrace_Install_Next:
    movzx   edx, word ptr [edi]
    mov     esi, OFFSET32 V9xMini_IoTrace_Handler
    VMMcall Install_IO_Handler
    jc      short V9xMini_IoTrace_Install_Skip
    inc     V9xIoTraceInstalled
V9xMini_IoTrace_Install_Skip:
    add     edi, 2
    dec     ecx
    jnz     short V9xMini_IoTrace_Install_Next
    pop     edx
    pop     ecx
    pop     esi
ENDIF
IFDEF V9X_ADVFUNC_SHIELD
    ; Install the ADVFUNC shield on its one port, global for every VM; the
    ; handler exempts the System VM by handle. The serial line is written
    ; only when the install took, so its absence from a boot log is the
    ; report of a failure.
    push    esi
    push    ecx
    push    edx
    mov     edx, 04ae8h
    mov     esi, OFFSET32 V9xMini_AdvFunc_Handler
    VMMcall Install_IO_Handler
    jc      short V9xMini_AdvFunc_Install_Skip
    mov     esi, OFFSET32 V9xMiniShieldLine
    mov     ecx, V9xMiniShieldLineLength
    call    V9xMini_Serial_Write
V9xMini_AdvFunc_Install_Skip:
    pop     edx
    pop     ecx
    pop     esi
ENDIF
    ; Read-only, and no BIOS call, so it runs for every family and cannot fail
    ; the init any more than the collection below can.
    call    V9xMini_Mtrr_Inspect
IFNDEF V9X_NO_VBE_COLLECT
    ; Collect the VBE answers for the tier-0 driver. Deliberately last, and
    ; deliberately not able to fail the init: an adapter with no usable VESA
    ; BIOS should still get the DPMS and power callbacks above.
    ;
    ; What cannot be engineered away: Exec_Int runs the real video BIOS and
    ; has no timeout, so a BIOS that never IRETs hangs the boot. The vbe-call
    ; markers exist so a serial capture names the exact call.
    call    V9xMini_Vbe_Collect
ELSE
    ; Assembled without the collection. Families whose drivers read the
    ; aperture from hardware never consult the 4F9Ch cache; its zeroed
    ; entries are the designed "collection never ran" answer.
    mov     esi, OFFSET32 V9xMiniVbeDisabledLine
    mov     ecx, V9xMiniVbeDisabledLineLength
    call    V9xMini_Serial_Write
ENDIF
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
IFDEF V9X_IO_TRACE
    Control_Dispatch Sys_VM_Init, MiniVDD_Sys_VM_Init
    Control_Dispatch W32_DeviceIoControl, MiniVDD_W32_DeviceIoControl
ENDIF
End_Control_Dispatch MiniVDD
VxD_LOCKED_CODE_ENDS

end
