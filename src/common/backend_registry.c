#include "velocity9x/backend_registry.h"

/*
 * PCI dispatch is data generated from the family manifests, not code: each
 * manifest's Chips list is the single statement of which ids its family
 * claims, and backend_registry_table.inc restates exactly that, one row per
 * chip. scripts\update-backend-registry.ps1 regenerates it and check-tree.ps1
 * fails when the two disagree, so adding a family never edits this file.
 */
struct v9x_backend_registry_row {
    unsigned short vendor_id;
    unsigned short device_id;
    const struct v9x_backend_ops *(*backend)(void);
};

#include "backend_registry_table.inc"

/*
 * The table is an allowlist like the if-chain it replaced, not a fallback
 * ladder. It would be easy to return the tier-0 backend for anything
 * unmatched, and wrong: this function's refusal is what the driver and the
 * family-matrix tests rely on to mean "this card is not claimed", and a
 * generic catch-all would silently claim hardware nobody has run. The vbe
 * family's row is therefore the QEMU std-vga id and nothing else.
 *
 * Unlisted cards reach tier-0 by a Have-Disk install of the vbe package,
 * which is a person choosing it rather than this function guessing. Note that
 * route is governed by the 16-bit family table's pci_match_optional, not by
 * anything here: this registry is the host-testable policy layer and is not
 * on the driver's enable path at all.
 */
const struct v9x_backend_ops *v9x_backend_for_pci(
    const struct v9x_pci_identity *pci)
{
    unsigned int row;

    if (pci == 0) {
        return 0;
    }
    for (row = 0u;
         row < sizeof(v9x_backend_registry_rows) /
               sizeof(v9x_backend_registry_rows[0]);
         ++row) {
        if (v9x_backend_registry_rows[row].vendor_id == pci->vendor_id &&
            v9x_backend_registry_rows[row].device_id == pci->device_id) {
            return v9x_backend_registry_rows[row].backend();
        }
    }
    return 0;
}
