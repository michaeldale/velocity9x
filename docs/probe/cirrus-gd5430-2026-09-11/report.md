# Card baseline

## Identity

- Card label: `cirrus-logic-gd5430-alpine` (operator statement)
- PCI identity: `1013:00a0`
- BDF: `0000:04:06.0`

## Findings

- PCI BAR 0 size: 16777216 bytes
- Standard VGA trust: `hardware`
- Expansion ROM: 32768 bytes, `full-image`, sha256 `0020fc81418f82a3f34b4269fbb2d0366a6932e2a19fae788062ba5b64714609`
- Attribute controller: 21 registers, entry index byte 0x20
- DAC palette: 256 entries, 63 non-zero
- Register sweep: index 0x00-0xFF of the CRTC, sequencer and graphics banks, measured and unattributed

## Uncertainty

- `vbe.controller`: unsupported (no-real-mode-vbe)
- `vbe.modes`: unsupported (no-real-mode-vbe)
- `edid`: unsupported (ddc-outside-baseline-contract)
- `runtime.driver`: unsupported (no-velocity9x-runtime)
- `survey.platform`: unsupported (no-dos-survey)
- `survey.bios_data`: unsupported (no-dos-survey)
- `aperture.probe`: unsupported (no-aperture-probe-in-baseline)

## Capture limits

Current-state Linux capture. No mode set, VRAM access or vendor unlock was performed.

The ROM read enabled memory decode and restored it, cleared the declared bridge's master-abort latch **irreversibly**, and left the Expansion ROM BAR programmed with decode disabled.

## Side effects

- `attribute-index-written`
- `bridge-master-abort-latch-cleared`
- `command-memory-enable-written-and-restored`
- `dac-mask-read-advances-hidden-register-counter`
- `dac-read-pointer-written`
- `display-blanked-while-palette-readable`
- `index-port-writes`
- `input-status-1-read-resets-atc-flipflop`
- `rom-bar-programmed-by-kernel`

## Sections

| Section | Status | Reason |
| --- | --- | --- |
| `inventory.host` | captured |  |
| `inventory.pci` | captured |  |
| `inventory.topology` | captured |  |
| `inventory.console` | captured |  |
| `inventory.arbiter` | captured |  |
| `vga.misc_output` | captured |  |
| `vga.input_status` | captured |  |
| `vga.feature_control` | captured |  |
| `vga.crtc` | captured |  |
| `vga.sequencer` | captured |  |
| `vga.graphics` | captured |  |
| `vga.attribute` | captured |  |
| `vga.dac_mask_state` | captured |  |
| `vga.dac_palette` | captured |  |
| `rom.image` | captured |  |
| `vendor.extended` | captured |  |
| `vbe.controller` | unsupported | no-real-mode-vbe |
| `vbe.modes` | unsupported | no-real-mode-vbe |
| `edid` | unsupported | ddc-outside-baseline-contract |
| `runtime.driver` | unsupported | no-velocity9x-runtime |
| `survey.platform` | unsupported | no-dos-survey |
| `survey.bios_data` | unsupported | no-dos-survey |
| `aperture.probe` | unsupported | no-aperture-probe-in-baseline |

## Attachments

- `attachments/rom.bin` (rom-image, 32768 bytes, sha256 `0020fc81418f82a3f34b4269fbb2d0366a6932e2a19fae788062ba5b64714609`)
