// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/debug.h>
#include <hw/arch_ops.h>

#include "dwc3.h"
#include "dwc3-regs.h"

#include <stdio.h>
#include <unistd.h>

static int dwc3_otg_irq_thread(void* arg) {
    dwc3_t* dwc = arg;
//    volatile void* mmio = dwc3_mmio(dwc);

    dprintf(TRACE, "dwc3_otg_irq_thread start\n");

    while (1) {
        mx_status_t status = mx_interrupt_wait(dwc->otg_irq_handle);
        mx_interrupt_complete(dwc->otg_irq_handle);
        if (status != MX_OK) {
            dprintf(ERROR, "dwc3_otg_irq_thread: mx_interrupt_wait returned %d\n", status);
            break;
        }
printf("got OTG IRQ\n");
    }

    dprintf(TRACE, "dwc3_otg_irq_thread done\n");
    return 0;
}

void dwc3_otg_start(dwc3_t* dwc) {
    volatile void* mmio = dwc3_mmio(dwc);

    // enable events
    uint32_t event_mask = BCEVTEN_CHNG_EVNT_ENA;
    DWC3_WRITE32(mmio + BCEVTEN, event_mask);

    DWC3_WRITE32(mmio + OEVTEN, 0);
    DWC3_WRITE32(mmio + OEVT, 0x0ffffff0);
    DWC3_WRITE32(mmio + OEVTEN, 0);

    event_mask =  OEVT_OTGBDEVVBUSCHNGEVNT | OEVT_OTGCONIDSTSCHNGEVNT;
    DWC3_WRITE32(mmio + OEVTEN, event_mask);

    thrd_create_with_name(&dwc->otg_irq_thread, dwc3_otg_irq_thread, dwc, "dwc3_otg_irq_thread");
}
