// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/driver.h>
#include <ddk/protocol/gpio.h>
#include <hw/reg.h>
#include <stdio.h>

#include "hi3660-bus.h"
#include "hi3660-regs.h"


static mx_status_t hi3660_usb_set_host(hi3660_bus_t* bus, bool host) {
    gpio_protocol_t gpio;
    if (pdev_get_protocol(&bus->pdev, MX_PROTOCOL_GPIO, &gpio) != MX_OK) {
        printf("hi3360_usb_init: could not get GPIO protocol!\n");
        return MX_ERR_INTERNAL;
    }

    gpio_config(&gpio, 46, GPIO_DIR_OUT);
    gpio_config(&gpio, 202, GPIO_DIR_OUT);
    gpio_config(&gpio, 206, GPIO_DIR_OUT);
    gpio_config(&gpio, 206, GPIO_DIR_OUT);

unsigned gpio_46, gpio_202, gpio_206, gpio_150;
gpio_read(&gpio, 46, &gpio_46);
gpio_read(&gpio, 202, &gpio_202);
gpio_read(&gpio, 206, &gpio_206);
gpio_read(&gpio, 150, &gpio_150);
printf("GPIO 46: %d\n", gpio_46);
printf("GPIO 202: %d\n", gpio_202);
printf("GPIO 206: %d\n", gpio_206);
printf("GPIO 150: %d\n", gpio_150);

    gpio_write(&gpio, 46, 1);
    gpio_write(&gpio, 202, 1); // off
    gpio_write(&gpio, 206, 1); // hub on

gpio_read(&gpio, 46, &gpio_46);
gpio_read(&gpio, 202, &gpio_202);
gpio_read(&gpio, 206, &gpio_206);
gpio_read(&gpio, 150, &gpio_150);
printf("GPIO 46: %d\n", gpio_46);
printf("GPIO 202: %d\n", gpio_202);
printf("GPIO 206: %d\n", gpio_206);
printf("GPIO 150: %d\n", gpio_150);

/*
    if (host) {
        // disable type-c vbus
        gpio_write(&gpio, 202, 0);
        // enable host vbus
        gpio_write(&gpio, 46, 1);
        gpio_write(&gpio, 206, 1);
    } else {
        // disable host vbus
        gpio_write(&gpio, 46, 0);
        // enable type-c vbus
        gpio_write(&gpio, 202, 1);
    }
*/
    return MX_OK;
}


mx_status_t hi3360_usb_init(hi3660_bus_t* bus) {
    volatile void* usb3otg_bc = bus->usb3otg_bc.vaddr;
    volatile void* peri_crg = bus->peri_crg.vaddr;
    volatile void* pctrl = bus->pctrl.vaddr;
    uint32_t temp;

    hi3660_usb_set_host(bus, true);

    writel(PERI_CRG_ISODIS_REFCLK_ISO_EN, peri_crg + PERI_CRG_ISODIS);
    writel(PCTRL_CTRL3_USB_TCXO_EN | (PCTRL_CTRL3_USB_TCXO_EN << PCTRL_CTRL3_MSK_START),
           pctrl + PCTRL_CTRL3);

    temp = readl(pctrl + PCTRL_CTRL24);
    temp &= ~PCTRL_CTRL24_SC_CLK_USB3PHY_3MUX1_SEL;
    writel(temp, pctrl + PCTRL_CTRL24);

    writel(PERI_CRG_GT_CLK_USB3OTG_REF | PERI_CRG_GT_ACLK_USB3OTG, peri_crg + PERI_CRG_CLK_EN4);
    writel(PERI_CRG_IP_RST_USB3OTG_MUX | PERI_CRG_IP_RST_USB3OTG_AHBIF
           | PERI_CRG_IP_RST_USB3OTG_32K,  peri_crg + PERI_CRG_RSTDIS4);

    writel(PERI_CRG_IP_RST_USB3OTGPHY_POR | PERI_CRG_IP_RST_USB3OTG, peri_crg + PERI_CRG_RSTEN4);

    // enable PHY REF CLK
    temp = readl(usb3otg_bc + USB3OTG_CTRL0);
    temp |= USB3OTG_CTRL0_ABB_GT_EN;
    writel(temp, usb3otg_bc + USB3OTG_CTRL0);

    temp = readl(usb3otg_bc + USB3OTG_CTRL7);
    temp |= USB3OTG_CTRL7_REF_SSP_EN;
    writel(temp, usb3otg_bc + USB3OTG_CTRL7);

    // exit from IDDQ mode
    temp = readl(usb3otg_bc + USB3OTG_CTRL2);
    temp &= ~(USB3OTG_CTRL2_POWERDOWN_HSP | USB3OTG_CTRL2_POWERDOWN_SSP);
    writel(temp, usb3otg_bc + USB3OTG_CTRL2);
    mx_nanosleep(mx_deadline_after(MX_USEC(100)));

    writel(PERI_CRG_IP_RST_USB3OTGPHY_POR, peri_crg + PERI_CRG_RSTDIS4);
    writel(PERI_CRG_IP_RST_USB3OTG, peri_crg + PERI_CRG_RSTDIS4);
    mx_nanosleep(mx_deadline_after(MX_MSEC(20)));

    temp = readl(usb3otg_bc + USB3OTG_CTRL3);
    temp |= (USB3OTG_CTRL3_VBUSVLDEXT | USB3OTG_CTRL3_VBUSVLDEXTSEL);
    writel(temp, usb3otg_bc + USB3OTG_CTRL3);
    mx_nanosleep(mx_deadline_after(MX_USEC(100)));

    return MX_OK;
}
