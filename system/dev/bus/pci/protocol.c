// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/protocol/pci.h>

#include <assert.h>
#include <limits.h>
#include <magenta/assert.h>
#include <magenta/process.h>

#include "kpci-private.h"

static mx_status_t kpci_enable_bus_master(void* ctx, bool enable) {
    kpci_device_t* device = ctx;
    return mx_pci_enable_bus_master(device->handle, enable);
}

static mx_status_t kpci_enable_pio(void* ctx, bool enable) {
    kpci_device_t* device = ctx;
    return mx_pci_enable_pio(device->handle, enable);
}

static mx_status_t kpci_reset_device(void* ctx) {
    kpci_device_t* device = ctx;
    return mx_pci_reset_device(device->handle);
}

// TODO(cja): Figure out how to handle passing PIO privileges to other
// processes in the future when PCI is moved out of the kernel into
// userspace.
static mx_status_t do_resource_bookkeeping(mx_pci_resource_t* res) {
    mx_status_t status;

    if (!res) {
        return MX_ERR_INVALID_ARGS;
    }

    switch(res->type) {
    case PCI_RESOURCE_TYPE_PIO:
#if __x86_64__
            // x86 PIO space access requires permission in the I/O bitmap
        status = mx_mmap_device_io(get_root_resource(), res->pio_addr, res->size);
#else
        status = MX_ERR_NOT_SUPPORTED;
#endif
        break;
    default:
        status = MX_OK;
    }

    return status;
}


// These reads are proxied directly over to the device's PciConfig object so the validity of the
// widths and offsets will be validated on that end and then trickle back to this level of the
// protocol.
//
// In the case of config and capability reads/writes, failure is a catastrophic occurrence
// along the lines of hardware failure or a device being removed from the bus. Due to this,
// those statuses will be asserted upon rather than forcing callers to add additional checks
// every time they wish to do a config read / write.
static uint32_t kpci_config_read(void* ctx, uint8_t offset, size_t width) {
    MX_DEBUG_ASSERT(ctx);
    kpci_device_t* device = ctx;
    uint32_t val;

    mx_status_t status = mx_pci_config_read(device->handle, offset, width, &val);
    MX_DEBUG_ASSERT_MSG(status == MX_OK, "pci_config_read: %d\n", status);
    return val;
}

static uint8_t kpci_get_next_capability(void* ctx, uint8_t offset, uint8_t type) {
    uint8_t cap_offset = offset;
    uint8_t limit = 64;

    // Walk the capability list looking for the type requested, starting at the offset
    // passed in. limit acts as a barrier in case of an invalid capability pointer list
    // that causes us to iterate forever otherwise.
    while (cap_offset != 0 && limit--) {
        uint8_t type_id = (uint8_t)kpci_config_read(ctx, cap_offset, 8);
        if (type_id == type) {
            return cap_offset;
        }

        // We didn't find the right type, move on
        cap_offset = (uint8_t)kpci_config_read(ctx, cap_offset + 1, 8);
    }

    // No more entries are in the list
    return 0;
}

static mx_status_t pci_get_resource(void* ctx, uint32_t res_id, mx_pci_resource_t* out_res) {
    mx_status_t status = MX_OK;

    if (!out_res || res_id >= PCI_RESOURCE_COUNT) {
        return MX_ERR_INVALID_ARGS;
    }

    kpci_device_t* device = ctx;

    switch (res_id) {
        case PCI_RESOURCE_BAR_0:
        case PCI_RESOURCE_BAR_1:
        case PCI_RESOURCE_BAR_2:
        case PCI_RESOURCE_BAR_3:
        case PCI_RESOURCE_BAR_4:
        case PCI_RESOURCE_BAR_5:
            status = mx_pci_get_bar(device->handle, res_id, out_res);
            break;
        case PCI_RESOURCE_CONFIG:
            status = mx_pci_get_config(device->handle, out_res);
            break;
    }

    if (status != MX_OK) {
        return status;
    }

    return do_resource_bookkeeping(out_res);
}

// Sanity check the resource enum
static_assert(PCI_RESOURCE_BAR_0 == 0, "BAR 0's value is not 0");
static_assert(PCI_RESOURCE_BAR_5 == 5, "BAR 5's value is not 5");
static_assert(PCI_RESOURCE_CONFIG > PCI_RESOURCE_BAR_5, "resource order in the enum is wrong");

/* Get a resource from the pci bus driver and map for the driver. */
static mx_status_t kpci_map_resource(void* ctx,
                                    uint32_t res_id,
                                    mx_cache_policy_t cache_policy,
                                    void** vaddr,
                                    size_t* size,
                                    mx_handle_t* out_handle) {
    if (!vaddr || !size || !out_handle) {
        return MX_ERR_INVALID_ARGS;
    }

    mx_pci_resource_t resource;
    mx_status_t status = pci_get_resource(ctx, res_id, &resource);
    if (status != MX_OK) {
        return status;
    }

    // TODO(cja): PIO may be mappable on non-x86 architectures
    if (resource.type == PCI_RESOURCE_TYPE_PIO) {
        return MX_ERR_WRONG_TYPE;
    }

    uint32_t map_flags = MX_VM_FLAG_PERM_READ | MX_VM_FLAG_MAP_RANGE;
    if (res_id <= PCI_RESOURCE_BAR_5) {
        // Writes to bar resources are allowed.
        map_flags |= MX_VM_FLAG_PERM_WRITE;

        // Bar cache policy can be controlled by the driver.
        status = mx_vmo_set_cache_policy(resource.mmio_handle, cache_policy);
        if (status != MX_OK) {
            mx_handle_close(resource.mmio_handle);
            return status;
        }
    }

    // Map the config/bar passed in. Mappings require PAGE_SIZE alignment for
    // both base and size
    void* vaddr_tmp;
    status = mx_vmar_map(mx_vmar_root_self(), 0, resource.mmio_handle, 0,
                            ROUNDUP(resource.size, PAGE_SIZE),
                            map_flags, (uintptr_t*)&vaddr_tmp);

    if (status != MX_OK) {
        mx_handle_close(resource.mmio_handle);
        return status;
    }

    *size = resource.size;
    *out_handle = resource.mmio_handle;
    *vaddr = vaddr_tmp;

    return status;
}

static mx_status_t kpci_map_interrupt(void* ctx, int which_irq, mx_handle_t* out_handle) {
    mx_status_t status = MX_OK;

    if (!out_handle) {
        return MX_ERR_INVALID_ARGS;
    }

    kpci_device_t* device = ctx;
    if (device->handle == MX_HANDLE_INVALID) {
        return MX_ERR_BAD_HANDLE;
    }

    status = mx_pci_map_interrupt(device->handle, which_irq, out_handle);
    if (status != MX_OK) {
        *out_handle = MX_HANDLE_INVALID;
        return status;
    }

    return MX_OK;
}

static mx_status_t kpci_query_irq_mode_caps(void* ctx,
                                           mx_pci_irq_mode_t mode,
                                           uint32_t* out_max_irqs) {
    kpci_device_t* device = ctx;
    return mx_pci_query_irq_mode_caps(device->handle, mode, out_max_irqs);
}

static mx_status_t kpci_set_irq_mode(void* ctx, mx_pci_irq_mode_t mode,
                                    uint32_t requested_irq_count) {
    kpci_device_t* device = ctx;
    return mx_pci_set_irq_mode(device->handle, mode, requested_irq_count);
}

static mx_status_t kpci_get_device_info(void* ctx, mx_pcie_device_info_t* out_info) {
    if (out_info == NULL) {
        return MX_ERR_INVALID_ARGS;
    }

    kpci_device_t* device = ctx;
    *out_info = device->info;
    return MX_OK;
}

static pci_protocol_ops_t _pci_protocol = {
    .enable_bus_master = kpci_enable_bus_master,
    .enable_pio = kpci_enable_pio,
    .reset_device = kpci_reset_device,
    .map_resource = kpci_map_resource,
    .map_interrupt = kpci_map_interrupt,
    .query_irq_mode_caps = kpci_query_irq_mode_caps,
    .set_irq_mode = kpci_set_irq_mode,
    .get_device_info = kpci_get_device_info,
    .config_read = kpci_config_read,
    .get_next_capability = kpci_get_next_capability,
};
