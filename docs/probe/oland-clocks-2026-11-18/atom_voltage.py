"""Boot voltages (FirmwareInfo v2.2) and VoltageObjectInfo of an AtomBIOS.

Offsets from Linux atombios.h: ATOM_FIRMWARE_INFO_V2_2 usBootUpVDDCVoltage
at 46, usBootUpVDDCIVoltage at 78, usBootUpMVDDCVoltage at 92; the master
data list's VoltageObjectInfo is entry 32; ATOM_VOLTAGE_OBJECT_HEADER_V3 is
ucVoltageType, ucVoltageMode, usSize.
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
master = u16(hdr + 0x20)
fw = u16(master + 4 + 2 * 4)
print('boot VDDC %d mV, VDDCI %d mV, MVDDC %d mV'
      % (u16(fw + 46), u16(fw + 78), u16(fw + 92)))

vo = u16(master + 4 + 2 * 32)
print('VoltageObjectInfo at %04X, rev %d.%d, size %d'
      % (vo, u8(vo + 2), u8(vo + 3), u16(vo)))
types = {1: 'VDDC', 2: 'MVDDC', 3: 'MVDDQ', 4: 'VDDCI', 5: 'VDDGFX'}
modes = {0: 'GPIO_LUT', 1: 'VR_I2C_INIT_SEQ', 2: 'PHASE_LUT', 3: 'SVID2',
         4: 'EVV', 0x10: 'PWRBOOST_LEAKAGE_LUT', 0x11: 'HIGH_STATE_LEAKAGE_LUT',
         0x12: 'HIGH1_STATE_LEAKAGE_LUT'}
at = vo + 4
end = vo + u16(vo)
while at + 4 <= end:
    vtype, mode, size = u8(at), u8(at + 1), u16(at + 2)
    print('  object at %04X: type %s, mode %s, size %d: %s'
          % (at, types.get(vtype, vtype), modes.get(mode, mode), size,
             rom[at:at + min(size, 32)].hex()))
    if size == 0:
        break
    at += size

