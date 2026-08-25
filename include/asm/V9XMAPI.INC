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
; advertise; Stage 1 makes v2 an exact-match package-pair contract.
V9XMINI_API_V1      EQU 0001h
V9XMINI_API_V2      EQU 0002h
V9XMINI_API_VERSION EQU V9XMINI_API_V2

; ---------------------------------------------------------------------------
; Functions, in the client's AX
; ---------------------------------------------------------------------------
;
; Every function is a bounds-checked read of locked data. None calls the BIOS,
; allocates, or touches a register of the card: the caller is a display driver
; part-way through GDI initialisation. An unknown function returns AX=0.
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
