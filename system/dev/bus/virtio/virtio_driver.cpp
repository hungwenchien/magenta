// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <ddk/binding.h>
#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/protocol/pci.h>
#include <mxtl/alloc_checker.h>
#include <mxtl/unique_ptr.h>
#include <magenta/compiler.h>
#include <magenta/types.h>
#include "block.h"
#include "device.h"
#include "trace.h"
#include "ethernet.h"
#include "gpu.h"

#define LOCAL_TRACE 0

// implement driver object:

extern "C" mx_status_t virtio_bind(void* ctx, mx_device_t* device, void** cookie) {
    LTRACEF("device %p\n", device);
    mx_status_t status;
    pci_protocol_t pci;

    /* grab the pci device and configuration */
    if (device_get_protocol(device, MX_PROTOCOL_PCI, &pci)) {
        TRACEF("no pci protocol\n");
        return -1;
    }

    mx_pcie_device_info_t info;
    status = pci_get_device_info(&pci, &info);
    if (status != MX_OK) {
        return status;
    }

    LTRACEF("pci %p\n", &pci);
    LTRACEF("0x%x:0x%x\n", info.vendor_id, info.device_id);

    // XXX TODO: Make symbols for these constants and reuse in the BIND protocol.
    mxtl::unique_ptr<virtio::Device> vd = nullptr;
    switch (info.device_id) {
    case 0x1000:
        LTRACEF("found net device\n");
        vd.reset(new virtio::EthernetDevice(device));
        break;
    case 0x1001:
    case 0x1042:
        LTRACEF("found block device\n");
        vd.reset(new virtio::BlockDevice(device));
        break;
    case 0x1050:
        LTRACEF("found gpu device\n");
        vd.reset(new virtio::GpuDevice(device));
        break;
    default:
        printf("unhandled device id, how did this happen?\n");
        return -1;
    }

    LTRACEF("calling Bind on driver\n");
    status = vd->Bind(&pci, info);
    if (status != MX_OK)
        return status;

    status = vd->Init();
    if (status != MX_OK)
        return status;

    // if we're here, we're successful so drop the unique ptr ref to the object and let it live on
    vd.release();

    LTRACE_EXIT;

    return MX_OK;
}
