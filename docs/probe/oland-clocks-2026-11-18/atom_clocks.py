"""Print an AtomBIOS image's FirmwareInfo clocks and SI PowerPlay states.

Layouts from Linux atombios.h / pptable.h (ATOM_ROM_HEADER,
ATOM_MASTER_LIST_OF_DATA_TABLES, ATOM_FIRMWARE_INFO_V2_x,
ATOM_PPLIB_POWERPLAYTABLE, ATOM_PPLIB_STATE_V2, ATOM_PPLIB_SI_CLOCK_INFO).
Clocks are in 10 kHz units.
"""
import struct
import sys

rom = open(sys.argv[1], 'rb').read()


def u8(o):
    return rom[o]


def u16(o):
    return struct.unpack_from('<H', rom, o)[0]


def u32(o):
    return struct.unpack_from('<I', rom, o)[0]


hdr = u16(0x48)
print('ATOM_ROM_HEADER at %04X, signature %r' % (hdr, rom[hdr + 4:hdr + 8]))
master_data = u16(hdr + 0x20)
names = ['UtilityPipeLine', 'MultimediaCapabilityInfo', 'MultimediaConfigInfo',
         'StandardVESA_Timing', 'FirmwareInfo', 'PaletteData', 'LCD_Info',
         'DIGTransmitterInfo', 'SMU_Info', 'SupportedDevicesInfo',
         'GPIO_I2C_Info', 'VRAM_UsageByFirmware', 'GPIO_Pin_LUT',
         'VESA_ToInternalModeLUT', 'ComponentVideoInfo', 'PowerPlayInfo']
tables = {}
for i, name in enumerate(names):
    tables[name] = u16(master_data + 4 + 2 * i)

fw = tables['FirmwareInfo']
print('FirmwareInfo at %04X, rev %d.%d, size %d' % (fw, u8(fw + 2), u8(fw + 3), u16(fw)))
print('  ulDefaultEngineClock %.2f MHz' % (u32(fw + 8) / 100.0))
print('  ulDefaultMemoryClock %.2f MHz' % (u32(fw + 12) / 100.0))
# V2.1/2.2: usReferenceClock at 0x52 (v1.4 layout keeps it there too).
print('  usReferenceClock     %.2f MHz' % (u16(fw + 0x52) / 100.0))

pp = tables['PowerPlayInfo']
print('PowerPlayInfo at %04X, rev %d.%d, size %d' % (pp, u8(pp + 2), u8(pp + 3), u16(pp)))
data_rev = u8(pp + 4)
num_states = u8(pp + 5)
state_off = pp + u16(pp + 9)
clock_off = pp + u16(pp + 11)
nonclock_off = pp + u16(pp + 13)
print('  data rev %d, states %d, state array %04X, clock info %04X, nonclock %04X'
      % (data_rev, num_states, state_off, clock_off, nonclock_off))

# Version-2 arrays (table revision 6 and later, as si_parse_power_table reads).
clock_entries = u8(clock_off)
clock_size = u8(clock_off + 1)
print('  clock info: %d entries of %d bytes' % (clock_entries, clock_size))
for i in range(clock_entries):
    e = clock_off + 2 + i * clock_size
    sclk = u16(e) | (u8(e + 2) << 16)
    mclk = u16(e + 3) | (u8(e + 5) << 16)
    vddc = u16(e + 6)
    vddci = u16(e + 8)
    pcie = u8(e + 10)
    print('    clock %d: sclk %.2f MHz, mclk %.2f MHz, vddc %d mV, vddci %d, pcie gen %d'
          % (i, sclk / 100.0, mclk / 100.0, vddc, vddci, pcie))

nonclock_entries = u8(nonclock_off)
nonclock_size = u8(nonclock_off + 1)
state_entries = u8(state_off)
at = state_off + 1
for s in range(state_entries):
    levels = u8(at)
    nonclock_index = u8(at + 1)
    indices = list(rom[at + 2:at + 2 + levels])
    n = nonclock_off + 2 + nonclock_index * nonclock_size
    classification = u16(n)
    print('  state %d: classification %04X, levels -> clock entries %s'
          % (s, classification, indices))
    at += 2 + levels
