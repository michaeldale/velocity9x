#include "velocity9x/backend_registry.h"
#include "velocity9x/matrox_millennium2.h"
#include "velocity9x/s3_virge.h"
#include "velocity9x/vbe_generic.h"

const struct v9x_backend_ops *v9x_backend_for_pci(
    const struct v9x_pci_identity *pci)
{
    if (pci == 0) {
        return 0;
    }
    if (pci->vendor_id == V9X_PCI_VENDOR_S3 &&
        (pci->device_id == V9X_PCI_DEVICE_VIRGE_DX ||
         pci->device_id == V9X_PCI_DEVICE_TRIO64)) {
        return v9x_s3_virge_backend();
    }
    if (pci->vendor_id == V9X_PCI_VENDOR_MATROX &&
        pci->device_id == V9X_PCI_DEVICE_MILLENNIUM_II) {
        return v9x_matrox_millennium2_backend();
    }
    /*
     * Tier-0 is an allowlist like every other arm, not a fallback. It would be
     * easy to return it for anything unmatched, and wrong: this function's
     * refusal is what the driver and the family-matrix tests rely on to mean
     * "this card is not claimed", and a generic catch-all would silently claim
     * hardware nobody has run. Unlisted cards reach tier-0 by Have-Disk.
     */
    if (pci->vendor_id == V9X_PCI_VENDOR_QEMU_BOCHS &&
        pci->device_id == V9X_PCI_DEVICE_STDVGA) {
        return v9x_vbe_generic_backend();
    }
    return 0;
}
