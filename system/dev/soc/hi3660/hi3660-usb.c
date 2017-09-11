// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/driver.h>
#include <ddk/protocol/gpio.h>
#include <hw/reg.h>
#include <stdio.h>

#include "hi3660-bus.h"
#include "hi3660-regs.h"

#define USB3OTG_PHY_CR_ACK                      (1 << 0)
#define USB3OTG_PHY_CR_CAP_DATA                 (1 << 1)
#define USB3OTG_PHY_CR_CAP_ADDR                 (1 << 0)
#define USB3OTG_PHY_CR_DATA_OUT(x)              (((x) & 0xFFFF) << 1)
#define USB3OTG_PHY_CR_DATA_IN(x)               (((x) & 0xFFFF) << 4)
#define USB3OTG_PHY_CR_READ                     (1 << 2)
#define USB3OTG_PHY_CR_WRITE                   (1 << 3)

static void phy_cr_wait_ack(volatile void *otg_bc_base)
{
	int i = 1000;

	while (1) {
		if ((readl(otg_bc_base + USB3OTG_PHY_CR_STS) & USB3OTG_PHY_CR_ACK) == 1)
			break;
        mx_nanosleep(mx_deadline_after(MX_USEC(50)));
		if (i-- < 0) {
			printf("wait phy_cr_ack timeout!\n");
			break;
		}
	}
}

static void phy_cr_set_addr(volatile void *otg_bc_base, uint32_t addr)
{
	uint32_t reg;

	/* set addr */
	reg = USB3OTG_PHY_CR_DATA_IN(addr);
	writel(reg, otg_bc_base + USB3OTG_PHY_CR_CTRL);

    mx_nanosleep(mx_deadline_after(MX_USEC(100)));

	/* cap addr */
	reg = readl(otg_bc_base + USB3OTG_PHY_CR_CTRL);
	reg |= USB3OTG_PHY_CR_CAP_ADDR;
	writel(reg, otg_bc_base + USB3OTG_PHY_CR_CTRL);

	phy_cr_wait_ack(otg_bc_base);

	/* clear ctrl reg */
	writel(0, otg_bc_base + USB3OTG_PHY_CR_CTRL);
}

static uint16_t phy_cr_read(volatile void *otg_bc_base, uint32_t addr)
{
	uint32_t reg;
	int i = 1000;

	phy_cr_set_addr(otg_bc_base, addr);

	/* read cap */
	writel(USB3OTG_PHY_CR_READ, otg_bc_base + USB3OTG_PHY_CR_CTRL);

    mx_nanosleep(mx_deadline_after(MX_USEC(100)));

	while (1) {
		reg = readl(otg_bc_base + USB3OTG_PHY_CR_STS);
		if ((reg & USB3OTG_PHY_CR_ACK) == 1) {
			break;
		}
    mx_nanosleep(mx_deadline_after(MX_USEC(50)));
		if (i-- < 0) {
			printf("wait phy_cr_ack timeout!\n");
			break;
		}
	}

	/* clear ctrl reg */
	writel(0, otg_bc_base + USB3OTG_PHY_CR_CTRL);

	return (uint16_t)USB3OTG_PHY_CR_DATA_OUT(reg);
}

static void phy_cr_write(volatile void *otg_bc_base, uint32_t addr, uint32_t value)
{
	uint32_t reg;

	phy_cr_set_addr(otg_bc_base, addr);

	reg = USB3OTG_PHY_CR_DATA_IN(value);
	writel(reg, otg_bc_base + USB3OTG_PHY_CR_CTRL);

	/* cap data */
	reg = readl(otg_bc_base + USB3OTG_PHY_CR_CTRL);
	reg |= USB3OTG_PHY_CR_CAP_DATA;
	writel(reg, otg_bc_base + USB3OTG_PHY_CR_CTRL);

	/* wait ack */
	phy_cr_wait_ack(otg_bc_base);

	/* clear ctrl reg */
	writel(0, otg_bc_base + USB3OTG_PHY_CR_CTRL);

	reg = USB3OTG_PHY_CR_WRITE;
	writel(reg, otg_bc_base + USB3OTG_PHY_CR_CTRL);

	/* wait ack */
	phy_cr_wait_ack(otg_bc_base);
}

#define DWC3_PHY_RX_OVRD_IN_HI	0x1006
#define DWC3_PHY_RX_SCOPE_VDCC	0x1026
#define RX_SCOPE_LFPS_EN	(1 << 0)
#define TX_VBOOST_LVL_MASK             7
#define TX_VBOOST_LVL(x)               ((x) & TX_VBOOST_LVL_MASK)

void config_femtophy_param(volatile void* otg_bc_base, bool host)
{
	uint32_t reg;

	/* set high speed phy parameter */
	if (host) {
		writel(0x1c466e3, otg_bc_base + USB3OTG_CTRL4);
		printf("set hs phy param 0x%x for host\n",
				readl(otg_bc_base + USB3OTG_CTRL4));
	} else {
		writel(0x01c466e3, otg_bc_base + USB3OTG_CTRL4);
		printf("set hs phy param 0x%x for device\n",
				readl(otg_bc_base + USB3OTG_CTRL4));
	}

	/* set usb3 phy cr config for usb3.0 */

	if (host) {
		phy_cr_write(otg_bc_base, DWC3_PHY_RX_OVRD_IN_HI,
				0x980 /* hisi_dwc->usb3_phy_host_cr_param */);
	} else {
		phy_cr_write(otg_bc_base, DWC3_PHY_RX_OVRD_IN_HI,
				0xb80 /*hisi_dwc->usb3_phy_cr_param*/);
	}

	printf("set ss phy rx equalization 0x%x\n",
			phy_cr_read(otg_bc_base, DWC3_PHY_RX_OVRD_IN_HI));

	/* enable RX_SCOPE_LFPS_EN for usb3.0 */
	reg = phy_cr_read(otg_bc_base, DWC3_PHY_RX_SCOPE_VDCC);
	reg |= RX_SCOPE_LFPS_EN;
	phy_cr_write(otg_bc_base, DWC3_PHY_RX_SCOPE_VDCC, reg);

	printf("set ss RX_SCOPE_VDCC 0x%x\n",
			phy_cr_read(otg_bc_base, DWC3_PHY_RX_SCOPE_VDCC));

	reg = readl(otg_bc_base + USB3OTG_CTRL6);
	reg &= ~TX_VBOOST_LVL_MASK;
	reg |= TX_VBOOST_LVL(0x5 /*hisi_dwc->usb3_phy_tx_vboost_lvl*/);
	writel(reg, otg_bc_base + USB3OTG_CTRL6);
	printf("set ss phy tx vboost lvl 0x%x\n", readl(otg_bc_base + USB3OTG_CTRL6));
}



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

unsigned gpio_46, gpio_202, gpio_206;
gpio_read(&gpio, 46, &gpio_46);
gpio_read(&gpio, 202, &gpio_202);
gpio_read(&gpio, 206, &gpio_206);
printf("GPIO 46: %d\n", gpio_46);
printf("GPIO 202: %d\n", gpio_202);
printf("GPIO 206: %d\n", gpio_206);

    gpio_write(&gpio, 46, 1);
    gpio_write(&gpio, 202, 0); // 1 - typec, 0 USB-A
    gpio_write(&gpio, 206, 1); // hub on

gpio_read(&gpio, 46, &gpio_46);
gpio_read(&gpio, 202, &gpio_202);
gpio_read(&gpio, 206, &gpio_206);
printf("GPIO 46: %d\n", gpio_46);
printf("GPIO 202: %d\n", gpio_202);
printf("GPIO 206: %d\n", gpio_206);

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

    config_femtophy_param(usb3otg_bc, false);
printf("did config_femtophy_param\n");

    return MX_OK;
}
