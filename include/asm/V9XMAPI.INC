; Velocity9x mini-VDD protected-mode API: one definition of the contract
; between the two sides that speak it.
;
;   src\minivdd32\loader.asm    ring 0, assembled by the Windows 98 DDK's
;                               MASM 6 (ML.EXE)
;   src\display16\runtime.asm   the 16-bit display driver, assembled by Open
;                               Watcom's wasm
;
; Both sides used to carry their own copy of these numbers. Nothing detected a
; disagreement: a mini-VDD that renamed a function number, or a driver that
; read a field from the wrong offset, would assemble, link, install and then
; misread the BIOS answers at boot with no diagnostic that pointed here. The
; whole file therefore exists to make that class of bug impossible rather than
; merely unlikely, which is also why it is included rather than duplicated -
; see the contract assertions in scripts\check-tree.ps1.
;
; Only EQU definitions and comments appear below. That is the intersection of
; what MASM 6 and wasm both accept without qualification: no STRUC, no macros,
; no conditional assembly, no data definitions. A field layout is therefore
; expressed as byte offsets and a record size, which is what both users
; actually index with anyway.
;
; The C view of the same contract is include\velocity9x\vbe_cache.h. The two
; files must agree numerically; check-tree.ps1 asserts that they do.

; ---------------------------------------------------------------------------
; Identity and versioning
; ---------------------------------------------------------------------------

; Private device id for INT 2Fh AX=1684h. Not allocated by anyone - no
; third-party registry exists to ask - so function 0 is a handshake and the
; 16-bit side refuses the entry point until it has seen the right answer. A
; collision then reads as a clean refusal instead of a call into a stranger.
V9XMINI_DEVICE_ID   EQU 4F9Ch

; Handshake reply. 'V9X9' little-endian.
V9XMINI_API_MAGIC   EQU 39583956h

; The contract versions this project has defined.
;
; v1 was the Stage-0 contract: controller information, one mode looked up by
; VBE number, and a status word. Its cache was seven hand-written mode numbers.
;
; v2 adds indexed enumeration of a bounded scan cache, the VBE 3 linear stride
; and colour fields, the packed record below, and EDID block 0 in register-only
; chunks. V9XMINI_API_VERSION is what both packaged binaries implement and
; advertise; Stage 1 made v2 an exact-match package-pair contract.
;
; v3 adds the Intel-only, read-only i9xx MMIO fingerprint capture. Other
; family images implement the function numbers as clean refusals, so the
; version remains one exact-match package contract across every family.
;
; v4 adds the Intel-only read-only Gen3 GTT snapshot hash and four-PTE chunks.
; v5 adds the bounded Intel ownership-event journal.
; v6 reserves the Intel-only Phase 4 staging, memory and execution calls.
;
; v7 adds the Intel-only read-only reserve hash. It is READ-ONLY and ships
; in the unarmed build behind the fingerprint guard rather than the
; executor guard, deliberately: the unarmed boot proves the hash path
; before any armed boot depends on it to report what the GPU wrote.
V9XMINI_API_V1      EQU 0001h
V9XMINI_API_V2      EQU 0002h
V9XMINI_API_V3      EQU 0003h
V9XMINI_API_V4      EQU 0004h
V9XMINI_API_V5      EQU 0005h
V9XMINI_API_V6      EQU 0006h
V9XMINI_API_V7      EQU 0007h
V9XMINI_API_VERSION EQU V9XMINI_API_V7

; ---------------------------------------------------------------------------
; Functions, in the client's AX
; ---------------------------------------------------------------------------
;
; Every function is bounds checked. The v1/v2 functions read locked data
; collected at mini-VDD initialisation. The Intel-only v3 capture maps the
; caller's freshly read PCI BAR0 and reads a fixed allowlist twice; it never
; writes card state. No function calls the BIOS or allocates. An unknown
; function returns AX=0.
;
; v1 functions. MODE_INFO is retained in v2 rather than replaced: the static
; LFB path looks its active mode up by VBE number, and that lookup must also
; reach the baseline rescue probes, which indexed enumeration deliberately
; does not expose.
; CONTROLLER gains two registers in v2, both from fixed VbeInfoBlock offsets
; and both carried for identification rather than for any decision:
;
;   EDX = Capabilities. Bit 0 is the switchable-DAC claim, bit 1 says the
;         controller is not VGA-compatible - which is the assumption every
;         text-mode restore and Safe Mode fallback in this driver rests on, so
;         a controller that denies it must at least be visible in diagnostics.
;   ESI = OemSoftwareRev, the BIOS's own revision number.
;
; The revision is there because across the DOS conformance corpus defects
; tracked the video BIOS revision rather than the chip - a BIOS swapped between
; two cards took its fault with it - and this driver has recorded chip identity
; and no BIOS identity at all. The OEM strings would say more, but they are far
; pointers into the controller block and need the same bounded staging copy the
; mode list gets; the revision needs nothing.
V9XMINI_FN_HANDSHAKE  EQU 0000h  ; out: AX=1, EBX=magic, ECX=contract version
V9XMINI_FN_CONTROLLER EQU 0001h  ; out: AX=valid, EBX=version, ECX=64K blocks,
                                 ;      v2 also EDX=capabilities, ESI=OEM rev
V9XMINI_FN_MODE_INFO  EQU 0002h  ; in: CX=VBE mode number
V9XMINI_FN_STATUS     EQU 0003h  ; out: what the init-time collection achieved

; v2 additions.
;
; MODE_AT and MODE_MASKS take a cache index in CX and return AX=0 for an index
; at or beyond the cached count, leaving every other register untouched. They
; enumerate list-derived records only; a rescue probe is reachable only through
; MODE_INFO.
;
;   MODE_AT   out: AX=1
;                  EBX = phys_base
;                  ECX = bytes_per_scan_line | (lin_bytes_per_scan_line << 16)
;                  EDX = width | (height << 16)
;                  ESI = storage_depth | (significant_depth << 16)
;                  EDI = mode_number | (attributes << 16)
;             The memory model and the record flags come from MODE_MASKS, which
;             a caller admitting a mode has to ask for anyway.
;
;   MODE_MASKS out: AX=1
;                  EBX = red_mask_size | (red_field_position << 8) |
;                        (green_mask_size << 16) | (green_field_position << 24)
;                  ECX = blue_mask_size | (blue_field_position << 8) |
;                        (rsvd_mask_size << 16) | (rsvd_field_position << 24)
;                  EDX = memory_model | (record flags << 16)
;
;   EDID_CHUNK in: CX = chunk index below V9X_VBE_EDID_CHUNKS
;              out: AX=1 and EBX/ECX/EDX/ESI = 16 bytes of EDID block 0, low
;                   byte of EBX first. AX=0 when no valid block was collected
;                   or the index is out of range. No caller pointer crosses
;                   into the mini-VDD in either direction.
V9XMINI_FN_MODE_AT    EQU 0004h
V9XMINI_FN_MODE_MASKS EQU 0005h
V9XMINI_FN_EDID_CHUNK EQU 0006h

; The memory-type registers, reported and not interpreted.
;
; Tier-0 draws with the CPU into a framebuffer that is uncached wherever the
; BIOS left the PCI hole at the MTRR default type, and one variable range set
; to write-combining fixes that. Deciding whether it may be set is the most
; consequential decision in this driver - MTRRs are global CPU state and a
; wrong range puts WC on somebody else's memory - so ring 0 does none of it.
; It establishes what the CPU admits to, reads the registers, and reports.
; The rules live in host-tested C: src\common\mtrr.c, whose contract is
; include\velocity9x\mtrr.h.
;
; This is the same division as significant_depth in the packed record above,
; for a much larger stake.
;
;   MTRR_INFO   out: AX=1 when the flags below were established (always, on a
;                    CPU this driver can run on), 0 never.
;                    EBX = IA32_MTRRCAP, low 32 bits, or 0 if unreadable
;                    ECX = IA32_MTRR_DEF_TYPE, low 32 bits, or 0 if unreadable
;                    EDX = the V9X_MTRR_CPU_* flags
;                    ESI = variable-range pairs actually read
;
;   MTRR_RANGE  in:  CX = pair index below the count MTRR_INFO reported
;               out: AX=1, or 0 for an index at or beyond that count
;                    EBX = IA32_MTRR_PHYSBASEn, low 32 bits
;                    ECX = IA32_MTRR_PHYSMASKn, low 32 bits
;                    EDX = 1 when PHYSBASEn had a non-zero high dword - the
;                          range starts above 4 GiB and cannot reach anything
;                          a 32-bit driver maps. PHYSMASK's high dword is not
;                          reported: it is routinely non-zero on any CPU with
;                          more than 32 physical address bits.
V9XMINI_FN_MTRR_INFO  EQU 0007h
V9XMINI_FN_MTRR_RANGE EQU 0008h

; v3: Intel builds alone capture a fixed, read-only MMIO allowlist.
; I9XX_CAPTURE in: EBX = current PCI BAR0 physical base; out AX=1 on success.
; I9XX_DWORD in: CX = index; out EBX=first, ECX=second, EDX=offset,
; ESI=BAR0 provenance. Other family builds return AX=0.
V9XMINI_FN_I9XX_CAPTURE  EQU 0009h
V9XMINI_FN_I9XX_DWORD    EQU 000Ah
V9X_I9XX_SNAPSHOT_DWORDS EQU 20

; v4: map the 256-KiB Gen3 BAR3 GTT, hash two complete read passes, and expose
; four live PTEs per register-only query. The caller hashes the streamed dump
; as a third pass, so a changed table cannot be called stable. Other families
; return AX=0.
; GTT_CAPTURE in: EBX = current PCI BAR3 physical base; out AX=1 on success.
; GTT_INFO out: EBX=entry count, ECX=first FNV-1a, EDX=second FNV-1a,
;               ESI=BAR3 provenance.
; GTT_CHUNK in: CX=chunk index; out EBX/ECX/EDX/ESI=four consecutive PTEs.
V9XMINI_FN_I9XX_GTT_CAPTURE EQU 000Bh
V9XMINI_FN_I9XX_GTT_INFO    EQU 000Ch
V9XMINI_FN_I9XX_GTT_CHUNK   EQU 000Dh
V9X_I9XX_GTT_ENTRY_COUNT    EQU 65536
V9X_I9XX_GTT_CHUNK_DWORDS   EQU 4

; v5: bounded Phase 3 ownership-event journal. EVENT_CAPTURE records a
; twice-read MMIO ownership snapshot and two complete GTT hashes. DPMS calls
; use the same internal capture path at ring 0; the display driver drains all
; retained records to INTELEVT.TXT at its next lifecycle event.
V9XMINI_FN_I9XX_EVENT_CAPTURE EQU 000Eh
V9XMINI_FN_I9XX_EVENT_INFO    EQU 000Fh
V9XMINI_FN_I9XX_EVENT_DWORD   EQU 0010h
V9X_I9XX_EVENT_MAX            EQU 32
V9X_I9XX_EVENT_DWORDS         EQU 20
V9X_I9XX_EVENT_BYTES          EQU V9X_I9XX_EVENT_DWORDS * 4
V9X_I9XX_EVENT_COMPLETE       EQU 0001h
V9X_I9XX_EVENT_MMIO_STABLE    EQU 0002h
V9X_I9XX_EVENT_GTT_STABLE     EQU 0004h
V9X_I9XX_EVENT_RING_IDLE      EQU 0008h
V9X_I9XX_EVENT_RING_DISABLED  EQU 0010h
V9X_I9XX_EVENT_PGTBL_VALID    EQU 0020h
V9X_I9XX_EVENT_KIND_DPMS      EQU 0006h

; v6: the first-write sandbox. Calls exist in every family but return AX=0
; outside the Intel 27AE build. No caller pointer crosses this interface.
; RING_STAGE in: EBX=reserve physical, ECX=stream index, EDX=dword;
;            out AX=1 only after the bounded word is accepted.
; RING_MEMORY in: ECX=byte offset in the mapped 1-MiB reserve;
;             out AX=1, EBX=dword read from the physical mapping.
; RING_EXECUTE steps 20-24 are Phase 5, disjoint from Phase 4's 1-11 so a
; failure code names its phase with no cross-reference:
;   20 verify the staged stream against the generated table (ITS OWN STEP -
;      Phase 4 folded this into its programming step, which is why
;      S05Failure=12 said only "something in step 5" on 2026-09-14)
;   21 program START, HEAD and TAIL to the Phase 5 ring region and enable
;   22 submit the state block, the shader and the MI probe, and drain
;   23 submit the primitive and its vertices, and drain
;   24 tear the ring back down to zero
;
; 22 and 23 are separate submissions on purpose. A drain at 22 followed by a
; stall at 23 says the ring is alive and the state was accepted, and the
; PRIMITIVE is what hung - which one submission could not distinguish, and
; which is the difference between a diagnosis and a repeat trip.
;
; RING_EXECUTE in: EBX=full execution CRC, ECX=ordered step selector;
;              out AX=1 on a completed step, EBX=head/readback,
;              ECX=tail/readback, EDX=elapsed ms, ESI=poll count.
; RING_STAGE in: ESI = the phase being staged, 4 or 5. Pinned by the caller's
; thunk rather than passed as an argument, so a caller cannot select the wrong
; phase by getting a parameter wrong. Neither value means refusal.
;
; Two phases, TWO TABLES, TWO COUNTERS and two disjoint ring regions, so a
; Phase 4 dword cannot be staged into a Phase 5 slot and the two streams cannot
; overlap in the ring even if a counter were wrong.
;
; RING_STAGE out: EBX = refusal reason, 0 on success. 1 physical, 2 MMIO base,
; 3 no Phase 1 capture, 4 out-of-order index for THIS phase, 5 index past THIS
; phase's table, 6 value not in the reviewed stream, 7 _MapPhysToLinear refused
; the reserve, 8 no mapping at write time, 10 the store did not read back
; (non-fatal: the bytes the GPU fetches go through GMADR), 11 the phase in ESI
; was neither 4 nor 5, or Phase 5 staging is compiled out.
V9XMINI_FN_I9XX_RING_STAGE   EQU 0011h
V9XMINI_FN_I9XX_RING_MEMORY  EQU 0012h
V9XMINI_FN_I9XX_RING_EXECUTE EQU 0013h
; RING_DIAG in: CX=0..6 exact error/instruction offset index,
;               7=RING_CTL, 8=RING_START;
;           out AX=1 and EBX=value. It is read-only even in no-write builds.
V9XMINI_FN_I9XX_RING_DIAG   EQU 0014h

; RING_HASH hashes a bounded dword range of the reserve, READ-ONLY, and
; returns TWO independent passes over the same bytes. Both are returned
; rather than compared here, for the same reason the GTT capture returns
; hash A and hash B: an unstable read must be visible to the caller as two
; different numbers, not hidden behind one boolean.
;
; It maps the reserve itself and never writes it, so it works in a build
; with no executor at all. That is the point - the unarmed boot proves the
; hash path before an armed boot relies on it to report the triangle.
;
; in:  EBX = byte offset within the reserve, dword aligned
;      ECX = dword count
; out: AX  = 1 on success, 0 on refusal
;      EBX = hash pass A, ECX = hash pass B (valid only when AX=1)
;      EDX = refusal reason, 0 on success
;
; Refusals, each its own number so a capture names the bound it hit:
;   1 no Phase 1 fingerprint capture, so the device is unidentified
;   2 offset not dword aligned
;   3 dword count is zero
;   4 count above the per-call bound
;   5 offset + length wrapped (checked on carry, not a signed compare)
;   6 range extends past the reserve
;   7 _MapPhysToLinear refused the reserve
V9XMINI_FN_I9XX_RING_HASH   EQU 0015h

; What the CPU admits to, established in this order because each flag is the
; licence to look for the next: CPUID is an invalid opcode on the 386 and
; early 486 this project still targets on VLB, RDMSR is one without MSR
; support, and the MTRR registers exist only when the MTRR bit is set. No MSR
; is read unless both MSR and MTRR are present.
;
; Shared with include\velocity9x\mtrr.h, which is the C view of the same
; flags; check-tree.ps1 asserts the two agree.
V9X_MTRR_CPU_CPUID    EQU 0001h
V9X_MTRR_CPU_MSR      EQU 0002h
V9X_MTRR_CPU_MTRR     EQU 0004h
V9X_MTRR_CPU_PGE      EQU 0008h

; Variable-range pairs read and reported. Eight is what every x86 that
; implements MTRRs exposes; a CPU claiming more is read only this far, and
; because an unread pair can only make the policy decline, that direction is
; safe.
V9X_MTRR_RANGE_MAX    EQU 8

; ---------------------------------------------------------------------------
; Bounds
; ---------------------------------------------------------------------------
;
; Separate limits, so one corrupt BIOS field cannot become an unbounded boot
; delay or an overwrite. Each is checked again by the consumer of any count
; derived from it.
;
; The list and query limits are equal today and still named separately: they
; bound different risks - words read out of a BIOS pointer, versus nested-
; execution BIOS calls that have no timeout - and only one of them is likely
; to ever be lowered.
V9X_VBE_MODE_LIST_MAX     EQU 128
V9X_VBE_MODE_QUERY_MAX    EQU 128

; Records kept for indexed enumeration, and the separate reserved area for
; baseline rescue probes. 64 is well past what any BIOS this project has met
; reports as drivable, and matches V9X_MODE_TABLE_MAX on the display side.
V9X_VBE_CACHE_MAX         EQU 64
V9X_VBE_BASELINE_PROBE_MAX EQU 16

; EDID block 0 only. Extension blocks are counted for diagnostics and not
; fetched.
V9X_VBE_EDID_BYTES        EQU 128
V9X_VBE_EDID_CHUNK_BYTES  EQU 16
V9X_VBE_EDID_CHUNKS       EQU 8

; ---------------------------------------------------------------------------
; The packed cache record
; ---------------------------------------------------------------------------
;
; Replaces v1's parallel arrays. One record per cached mode, 32 bytes, which is
; the reason for the field order rather than a coincidence: 32 is a power of
; two, so indexing a record is a shift and an add exactly as indexing the
; parallel arrays was. Growing this record past 32 bytes costs that shift, so
; a new field should replace a reserved byte rather than extend the record.
;
; Word fields hold byte-wide VBE fields widened, because every consumer widens
; them anyway and a word keeps the offsets even. The four channel pairs are the
; exception: they are byte pairs, packed two per word, because MODE_MASKS
; returns them packed and nothing needs to address one alone.
;
; storage_depth is BitsPerPixel: the bits one pixel occupies in memory.
; significant_depth is the sum of the three colour channel widths, so XRGB
; 8:8:8:8 is 32 storage and 24 significant. The mini-VDD computes neither; it
; copies BitsPerPixel into storage_depth, leaves significant_depth zero, and
; the host-tested parser fills it in. Ring 0 reports facts.
V9X_VBE_REC_MODE_NUMBER   EQU 0    ; word
V9X_VBE_REC_ATTRIBUTES    EQU 2    ; word
V9X_VBE_REC_BYTES_PER_LINE EQU 4   ; word, legacy BytesPerScanLine
V9X_VBE_REC_LIN_BYTES      EQU 6   ; word, VBE 3 LinBytesPerScanLine, 0 if none
V9X_VBE_REC_WIDTH         EQU 8    ; word
V9X_VBE_REC_HEIGHT        EQU 10   ; word
V9X_VBE_REC_MEMORY_MODEL  EQU 12   ; word, byte field widened
V9X_VBE_REC_STORAGE_DEPTH EQU 14   ; word, byte field widened
V9X_VBE_REC_SIGNIF_DEPTH  EQU 16   ; word, derived by the parser, 0 from ring 0
V9X_VBE_REC_FLAGS         EQU 18   ; word, the V9X_VBE_RF_* bits below
V9X_VBE_REC_PHYS_BASE     EQU 20   ; dword
V9X_VBE_REC_RED           EQU 24   ; byte size, byte position
V9X_VBE_REC_GREEN         EQU 26   ; byte size, byte position
V9X_VBE_REC_BLUE          EQU 28   ; byte size, byte position
V9X_VBE_REC_RSVD          EQU 30   ; byte size, byte position
V9X_VBE_REC_BYTES         EQU 32
V9X_VBE_REC_SHIFT         EQU 5    ; 1 << V9X_VBE_REC_SHIFT = V9X_VBE_REC_BYTES

; Locked-data cost of the caches, stated here so it is reviewable next to the
; bounds that produce it: 64 x 32 = 2048 bytes of list-derived records, 16 x 32
; = 512 bytes of rescue probes, and 128 bytes of EDID, for 2688 bytes. The
; 512-byte V86 scratch the BIOS writes into is allocated separately and is not
; part of this.
V9X_VBE_CACHE_BYTES       EQU V9X_VBE_CACHE_MAX * V9X_VBE_REC_BYTES
V9X_VBE_PROBE_BYTES       EQU V9X_VBE_BASELINE_PROBE_MAX * V9X_VBE_REC_BYTES

; Record flags, in V9X_VBE_REC_FLAGS.
;
; ORIGIN_LIST and ORIGIN_PROBE are what keeps a rescue probe out of indexed
; enumeration: the probes exist so the static LFB path can still find its
; active mode's aperture when the BIOS list was unusable, and admitting them as
; discovered modes would let an invalid list contribute rows after all.
;
; MASKS_LINEAR and MASKS_LEGACY record which colour fields the layout came
; from. A VBE 3 record whose linear colour fields are all zero falls back to
; the legacy ones, and a driver that cannot tell the two cases apart cannot
; explain a transposed channel later.
V9X_VBE_RF_ORIGIN_LIST    EQU 0001h
V9X_VBE_RF_ORIGIN_PROBE   EQU 0002h
V9X_VBE_RF_MASKS_LINEAR   EQU 0004h
V9X_VBE_RF_MASKS_LEGACY   EQU 0008h
V9X_VBE_RF_LIN_STRIDE     EQU 0010h  ; LinBytesPerScanLine was reported non-zero
V9X_VBE_RF_ORIGIN_SWEEP   EQU 0020h  ; described only after being set

; ---------------------------------------------------------------------------
; STATUS
; ---------------------------------------------------------------------------
;
; out: AX=1
;      EBX = V86 scratch segment, or 0 if one was never obtained
;      ECX = listed count | (queried count << 16)
;      EDX = cached count | (rescue-probe count << 16)
;      ESI = the V9X_VBE_ST_* flags below
;
; Counts and flags are reported separately on purpose. "Zero cached records"
; has several causes with different fixes - no VxD, a refused 4F00h, a mode
; list that never terminated, a BIOS that refused every 4F01h - and a single
; validity bit cannot tell a field report which one happened.
V9X_VBE_ST_CTRL_VALID     EQU 0001h  ; 4F00h answered credibly
V9X_VBE_ST_LIST_VALID     EQU 0002h  ; the walk produced a trustworthy list
V9X_VBE_ST_LIST_TERM      EQU 0004h  ; FFFFh seen within the word limit
V9X_VBE_ST_LIST_OVERFLOW  EQU 0008h  ; hit V9X_VBE_MODE_LIST_MAX first
V9X_VBE_ST_LIST_FLAGGED   EQU 0010h  ; a listed value carried set-mode flag bits
V9X_VBE_ST_LIST_UNREACHED EQU 0020h  ; VideoModePtr null or not addressable
V9X_VBE_ST_CACHE_FULL     EQU 0040h  ; more admissible records than cache slots
V9X_VBE_ST_QUERY_FAILED   EQU 0080h  ; a listed 4F01h did not answer 004Fh
V9X_VBE_ST_EDID_VALID     EQU 0100h  ; a 128-byte block 0 was collected
V9X_VBE_ST_EDID_NO_DDC    EQU 0200h  ; 4F15h reported no DDC support
V9X_VBE_ST_EDID_FAILED    EQU 0400h  ; DDC claimed, read refused
V9X_VBE_ST_COLLECT_OFF    EQU 0800h  ; assembled without collection at all
V9X_VBE_ST_QUERY_LIMIT    EQU 1000h  ; first-drop BIOS-call clamp stopped queries
V9X_VBE_ST_SWEEP_RAN      EQU 2000h  ; the set-and-ask-again pass was assembled in
V9X_VBE_ST_SWEEP_UNRESTORED EQU 4000h ; the sweep could not put the entry mode back
