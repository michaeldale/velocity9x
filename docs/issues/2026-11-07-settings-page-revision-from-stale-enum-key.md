# The settings page may report a removed card's PCI revision

Date: 2026-11-07. Status: open, found by reading the code. Not
reproduced.

Seen in a field report: three S3 cards, two of them `5333:8811`,
probably fitted in turn in one machine
([record](../decisions/2026-11-07-s3-trio-0.39.0-field-report.md)).
The Trio64V+'s settings report says `Revision: 00`, the same as the
Trio64 before it.

`v9x_collect_revision` (`tools/diag/settings_status.c`) reads
`HKLM\Enum\PCI` and takes `REV_rr` from the **first** subkey whose name
starts with `VEN_vvvv&DEV_dddd`. Windows keeps the Enum key of a card
that has been removed, or moved to another slot. So on a machine that has
had more than one card of one PCI id, the page can show another card's
revision. The value is display only: the driver does not use it, and
`V9XHW.INI`'s `PciRevision` on the S3 path is CR2F, read from the chip.

The fix is to take the key of the device that is actually present: the
subkey whose instance has a `Driver` value naming the Velocity9x display
class entry, or whose devnode is started. It belongs in the same file and
the host-testable part of it, if there is one. Check it on a machine with
two Enum keys for one id before claiming it.
