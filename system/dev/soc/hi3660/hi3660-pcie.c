// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.


#include <ddk/debug.h>
#include <magenta/process.h>
#include <magenta/syscalls.h>
#include <magenta/assert.h>

#define PERIPHERAL_BASE_PHYS (0)

#define PCIE_MMIO_BASE_PHYS ((mx_paddr_t)(PERIPHERAL_BASE_PHYS + 0x10000000))
#define PCIE_MMIO_SIZE      (0x2eff0000)
//#define PCIE_PIO_BASE_PHYS  ((mx_paddr_t)(PERIPHERAL_BASE_PHYS + 0x3eff0000))
//#define PCIE_PIO_SIZE       (0x00010000)

#define PCIE_ECAM_BASE_PHYS ((mx_paddr_t)(PERIPHERAL_BASE_PHYS + 0x3f000000))
#define PCIE_ECAM_SIZE      (0x01000000)

#define PCIE_INT_BASE   314

mx_status_t hi3360_pcie_init(void) {
    mx_status_t status;

    mx_pci_init_arg_t* arg;
    size_t arg_size = sizeof(*arg) + sizeof(arg->addr_windows[0]);   // room for one addr window
    arg = calloc(1, arg_size);
    if (!arg) return MX_ERR_NO_MEMORY;

    status = mx_pci_add_subtract_io_range(get_root_resource(), true /* mmio */,
                                          PCIE_MMIO_BASE_PHYS, PCIE_MMIO_SIZE, true /* add */);
    if (status != MX_OK) {
        goto fail;
    }
/*
    status = mx_pci_add_subtract_io_range(get_root_resource(), false /* pio */,
                                          PCIE_PIO_BASE_PHYS, PCIE_PIO_SIZE, true /* add */);
    if (status != MX_OK) {
        goto fail;
    }
*/
    // initialize our swizzle table
    mx_pci_irq_swizzle_lut_t* lut = &arg->dev_pin_to_global_irq;
    for (unsigned dev_id = 0; dev_id < MX_PCI_MAX_DEVICES_PER_BUS; dev_id++) {
        for (unsigned func_id = 0; func_id < MX_PCI_MAX_FUNCTIONS_PER_DEVICE; func_id++) {
            for (unsigned pin = 0; pin < MX_PCI_MAX_LEGACY_IRQ_PINS; pin++) {
                (*lut)[dev_id][func_id][pin] = PCIE_INT_BASE +
                                               (pin + dev_id) % MX_PCI_MAX_LEGACY_IRQ_PINS;
            }
        }
    }
    arg->num_irqs = 0;
    arg->addr_window_count = 1;
    arg->addr_windows[0].is_mmio = true;
    arg->addr_windows[0].has_ecam = true;
    arg->addr_windows[0].base = PCIE_ECAM_BASE_PHYS;
    arg->addr_windows[0].size = PCIE_ECAM_SIZE;
    arg->addr_windows[0].bus_start = 0;
    arg->addr_windows[0].bus_end = (PCIE_ECAM_SIZE / MX_PCI_ECAM_BYTE_PER_BUS) - 1;

    status = mx_pci_init(get_root_resource(), arg, arg_size);
    free(arg);
    if (status != MX_OK) {
        dprintf(ERROR, "%S: error %d in mx_pci_init\n", __FUNCTION__, status);
        goto fail;
    }

fail:
    free(arg);
    return status;
}
