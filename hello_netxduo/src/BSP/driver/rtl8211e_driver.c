/*
 * rtl8211e_driver.c - RTL8211E Gigabit Ethernet PHY driver
 *
 * Architecture:
 *   board_init.c:  XEmacPs g_emac_ps  (LookupConfig + CfgInitialize)
 *                         |
 *   rtl8211e_driver.c:   priv holds XEmacPs* + phy_addr + speed/duplex
 *                         |
 *   nx_driver_zynq.c:    device_find("rtl8211e0") -> device_ioctl()
 *
 * PHY is accessed via MDIO through XEmacPs MAC controller.
 * Pattern A (static state) - no ISR, polled via MDIO.
 */

#include "device_core.h"
#include "ioctl_cmd.h"
#include "xemacps.h"
#include "xparameters_ps.h"
#include "xparameters.h"
#include "xil_printf.h"
#include "xil_io.h"

/* ------------------------------------------------------------------ */
/*  Section 1: Includes and priv struct                                */
/* ------------------------------------------------------------------ */

extern XEmacPs  g_emac_ps;

struct rtl8211e_priv {
    XEmacPs   *emac;
    uint32_t   phy_addr;
    uint32_t   speed;      /* 10 / 100 / 1000 */
    uint32_t   duplex;     /* 0=half, 1=full */
    uint32_t   link_up;    /* 0=down, 1=up */
};

/* IEEE standard register offsets */
#define IEEE_CONTROL_REG                0
#define IEEE_STATUS_REG                 1
#define IEEE_AUTONEGO_ADVERTISE_REG     4
#define IEEE_PARTNER_ABILITIES_REG      5
#define IEEE_1000_ADVERTISE_REG         9

/* RTL8211E specific status register */
#define RTL8211E_SPECIFIC_STATUS_REG    0x1A

/* Control register bits */
#define CTRL_RESET_MASK                 0x8000
#define CTRL_AUTONEG_ENABLE             0x1000
#define CTRL_RESTART_AUTONEG            0x0200

/* Status register bits */
#define STAT_AUTONEG_COMPLETE           0x0020
#define STAT_LINK_UP                    0x0004

/* Auto-negotiation advertisement */
#define ADVERTISE_10HALF                0x0020
#define ADVERTISE_10FULL                0x0040
#define ADVERTISE_100HALF               0x0080
#define ADVERTISE_100FULL               0x0100
#define ADVERTISE_1000                  0x0300

#define ADVERTISE_ALL   (ADVERTISE_10HALF | ADVERTISE_10FULL | \
                         ADVERTISE_100HALF | ADVERTISE_100FULL)

/* SLCR register definitions for GEM0 clock configuration */
#define SLCR_LOCK_ADDR          (XPS_SYS_CTRL_BASEADDR + 0x4)
#define SLCR_UNLOCK_ADDR        (XPS_SYS_CTRL_BASEADDR + 0x8)
#define SLCR_GEM0_CLK_CTRL_ADDR (XPS_SYS_CTRL_BASEADDR + 0x140)
#define SLCR_LOCK_KEY_VALUE     0x767B
#define SLCR_UNLOCK_KEY_VALUE   0xDF0D
#define EMACPS_SLCR_DIV_MASK    0xFC0FC0FF

/* PHY detection */
#define PHY_DETECT_REG          1
#define PHY_DETECT_MASK         0x1808

/* ------------------------------------------------------------------ */
/*  Section 2: Helper functions (internal)                             */
/* ------------------------------------------------------------------ */

/* Scan MDIO addresses 31->1 to find a valid PHY */
static uint32_t phy_detect_addr(XEmacPs *emac)
{
    u16 phy_reg;
    u32 phy_addr;

    for (phy_addr = 31; phy_addr > 0; phy_addr--) {
        XEmacPs_PhyRead(emac, phy_addr, PHY_DETECT_REG, &phy_reg);
        if ((phy_reg != 0xFFFF) &&
            ((phy_reg & PHY_DETECT_MASK) == PHY_DETECT_MASK)) {
            xil_printf("RTL8211E: PHY detected at address %d\r\n", phy_addr);
            return phy_addr;
        }
    }

    xil_printf("RTL8211E: No PHY detected, assuming address 0\r\n");
    return 0;
}

/* Software reset via control register */
static int phy_reset(XEmacPs *emac, uint32_t phy_addr)
{
    u16 control;
    int timeout = 10000;

    XEmacPs_PhyRead(emac, phy_addr, IEEE_CONTROL_REG, &control);
    control |= CTRL_RESET_MASK;
    XEmacPs_PhyWrite(emac, phy_addr, IEEE_CONTROL_REG, control);

    /* Wait for reset to complete */
    do {
        XEmacPs_PhyRead(emac, phy_addr, IEEE_CONTROL_REG, &control);
        if (timeout-- <= 0) {
            xil_printf("RTL8211E: PHY reset timeout\r\n");
            return -1;
        }
    } while (control & CTRL_RESET_MASK);

    return 0;
}

/* Configure SLCR GEM0 TX clock dividers for given link speed */
static void slcr_set_gem0_clock(uint32_t speed)
{
    u32 slcr_val;

    *(volatile u32 *)SLCR_UNLOCK_ADDR = SLCR_UNLOCK_KEY_VALUE;

    slcr_val = *(volatile u32 *)SLCR_GEM0_CLK_CTRL_ADDR;
    slcr_val &= EMACPS_SLCR_DIV_MASK;

    if (speed == 1000) {
        slcr_val |= (XPAR_PS7_ETHERNET_0_ENET_SLCR_1000MBPS_DIV1 << 20);
        slcr_val |= (XPAR_PS7_ETHERNET_0_ENET_SLCR_1000MBPS_DIV0 << 8);
    } else if (speed == 100) {
        slcr_val |= (XPAR_PS7_ETHERNET_0_ENET_SLCR_100MBPS_DIV1 << 20);
        slcr_val |= (XPAR_PS7_ETHERNET_0_ENET_SLCR_100MBPS_DIV0 << 8);
    } else {
        slcr_val |= (XPAR_PS7_ETHERNET_0_ENET_SLCR_10MBPS_DIV1 << 20);
        slcr_val |= (XPAR_PS7_ETHERNET_0_ENET_SLCR_10MBPS_DIV0 << 8);
    }

    *(volatile u32 *)SLCR_GEM0_CLK_CTRL_ADDR = slcr_val;
    *(volatile u32 *)SLCR_LOCK_ADDR = SLCR_LOCK_KEY_VALUE;
}

/* Run auto-negotiation using standard IEEE registers */
static int phy_autoneg(XEmacPs *emac, uint32_t phy_addr)
{
    u16 control;
    u16 status;
    int timeout;

    /* Advertise 1000BASE-T capabilities */
    XEmacPs_PhyRead(emac, phy_addr, IEEE_1000_ADVERTISE_REG, &control);
    control |= ADVERTISE_1000;
    XEmacPs_PhyWrite(emac, phy_addr, IEEE_1000_ADVERTISE_REG, control);

    /* Advertise 10/100 Mbps capabilities */
    XEmacPs_PhyRead(emac, phy_addr, IEEE_AUTONEGO_ADVERTISE_REG, &control);
    control |= ADVERTISE_ALL;
    XEmacPs_PhyWrite(emac, phy_addr, IEEE_AUTONEGO_ADVERTISE_REG, control);

    /* Enable auto-negotiation and restart */
    XEmacPs_PhyRead(emac, phy_addr, IEEE_CONTROL_REG, &control);
    control |= CTRL_AUTONEG_ENABLE;
    control |= CTRL_RESTART_AUTONEG;
    XEmacPs_PhyWrite(emac, phy_addr, IEEE_CONTROL_REG, control);

    /* Wait for auto-negotiation to complete */
    xil_printf("RTL8211E: Waiting for auto-negotiation...\r\n");
    timeout = 50000;
    do {
        XEmacPs_PhyRead(emac, phy_addr, IEEE_STATUS_REG, &status);
        if (timeout-- <= 0) {
            xil_printf("RTL8211E: Auto-negotiation timeout\r\n");
            return -1;
        }
    } while (!(status & STAT_AUTONEG_COMPLETE));

    xil_printf("RTL8211E: Auto-negotiation complete\r\n");
    return 0;
}

/* Read speed/duplex/link from RTL8211E specific status register 0x1A */
static void phy_read_status(struct rtl8211e_priv *p)
{
    u16 status;

    XEmacPs_PhyRead(p->emac, p->phy_addr, RTL8211E_SPECIFIC_STATUS_REG, &status);

    /* Speed: bits [15:14] - 00=10M, 01=100M, 10=1000M */
    switch ((status >> 14) & 3) {
    case 2:  p->speed = 1000; break;
    case 1:  p->speed = 100;  break;
    default: p->speed = 10;   break;
    }

    /* Duplex: bit 13 - 1=full, 0=half */
    p->duplex = (status & (1 << 13)) ? 1 : 0;

    /* Link: bit 2 - 1=up, 0=down */
    p->link_up = (status & (1 << 2)) ? 1 : 0;
}

/* ------------------------------------------------------------------ */
/*  Section 2 (cont): device_ops functions                             */
/* ------------------------------------------------------------------ */

static int rtl8211e_init(struct device *dev)
{
    struct rtl8211e_priv *p = (struct rtl8211e_priv *)dev->priv;

    p->emac = &g_emac_ps;
    p->speed = 0;
    p->duplex = 0;
    p->link_up = 0;

    /* Detect PHY address on MDIO bus */
    p->phy_addr = phy_detect_addr(p->emac);

    /* Set SLCR to 1G clock as default before auto-negotiation */
    slcr_set_gem0_clock(1000);

    /* Software reset PHY */
    if (phy_reset(p->emac, p->phy_addr) != 0)
        return -1;

    /* Run auto-negotiation */
    if (phy_autoneg(p->emac, p->phy_addr) != 0)
        return -1;

    /* Read negotiated result */
    phy_read_status(p);

    /* Reconfigure SLCR clock and MAC speed based on result */
    slcr_set_gem0_clock(p->speed);
    XEmacPs_SetOperatingSpeed(p->emac, p->speed);

    xil_printf("RTL8211E: link=%s speed=%dMbps duplex=%s\r\n",
               p->link_up ? "UP" : "DOWN",
               p->speed,
               p->duplex ? "Full" : "Half");

    return 0;
}

static int rtl8211e_read(struct device *dev, void *buf, size_t len)
{
    struct rtl8211e_priv *p = (struct rtl8211e_priv *)dev->priv;

    if (!buf || len < 1)
        return -1;

    /* Refresh status from PHY */
    phy_read_status(p);

    *((uint8_t *)buf) = (uint8_t)p->link_up;
    return 1;
}

static int rtl8211e_ioctl(struct device *dev, int cmd, void *arg)
{
    struct rtl8211e_priv *p = (struct rtl8211e_priv *)dev->priv;

    switch (cmd) {
    case DEV_IOCTL_SET_NOTIFY:
        dev->notify = (device_notify_t)arg;
        break;

    case PHY_IOCTL_GET_SPEED:
        if (!arg) return -1;
        phy_read_status(p);
        *((uint32_t *)arg) = p->speed;
        break;

    case PHY_IOCTL_GET_DUPLEX:
        if (!arg) return -1;
        phy_read_status(p);
        *((uint32_t *)arg) = p->duplex;
        break;

    case PHY_IOCTL_GET_LINK:
        if (!arg) return -1;
        phy_read_status(p);
        *((uint32_t *)arg) = p->link_up;
        break;

    case PHY_IOCTL_RESTART_AUTONEG:
        if (phy_autoneg(p->emac, p->phy_addr) != 0)
            return -1;
        phy_read_status(p);
        slcr_set_gem0_clock(p->speed);
        XEmacPs_SetOperatingSpeed(p->emac, p->speed);
        xil_printf("RTL8211E: re-negotiated speed=%d\r\n", p->speed);
        break;

    default:
        return -1;
    }

    return 0;
}

/* ------------------------------------------------------------------ */
/*  Section 3: Ops and device instance                                 */
/* ------------------------------------------------------------------ */

static const struct device_ops rtl8211e_ops = {
    .init  = rtl8211e_init,
    .read  = rtl8211e_read,
    .write = NULL,
    .ioctl = rtl8211e_ioctl,
};

static struct rtl8211e_priv rtl8211e0_priv;
static struct device g_rtl8211e0 = {
    .name   = "rtl8211e0",
    .ops    = &rtl8211e_ops,
    .priv   = &rtl8211e0_priv,
    .notify = NULL,
};

/* ------------------------------------------------------------------ */
/*  Section 5: Driver entry                                            */
/* ------------------------------------------------------------------ */

void rtl8211e_driver_init(void)
{
    device_register(&g_rtl8211e0);
    device_init(&g_rtl8211e0);
}
