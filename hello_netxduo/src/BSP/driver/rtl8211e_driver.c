/*
 * rtl8211e_driver.c - RTL8211E Gigabit Ethernet PHY driver
 *
 * Architecture:
 *   board_init.c:  XEmacPs g_emac_ps  (LookupConfig + CfgInitialize)
 *                         |
 *   gem_driver.c:       "gem0" — GEM MAC + DMA + MDIO bus controller
 *                         |
 *   rtl8211e_driver.c:  "rtl8211e0" — PHY-only, uses gem0 MDIO ioctl
 *                         |
 *   nx_driver_zynq.c:   device_find("gem0") -> GEM ioctl for TX/RX
 *
 * This driver has zero Xilinx API dependencies. All PHY register access
 * is performed through the GEM driver's MDIO ioctl interface.
 */

#include "device_core.h"
#include "ioctl_cmd.h"
#include "xil_printf.h"

/* ------------------------------------------------------------------ */
/*  Section 1: Includes and priv struct                                */
/* ------------------------------------------------------------------ */

struct rtl8211e_priv {
    struct device *gem_dev;     /* gem0 device for MDIO access */
    uint32_t       phy_addr;
    uint32_t       speed;      /* 10 / 100 / 1000 */
    uint32_t       duplex;     /* 0=half, 1=full */
    uint32_t       link_up;    /* 0=down, 1=up */
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

/* PHY detection */
#define PHY_DETECT_REG          1
#define PHY_DETECT_MASK         0x1808

/* ------------------------------------------------------------------ */
/*  Section 2: MDIO wrapper functions                                  */
/* ------------------------------------------------------------------ */

static int phy_mdio_read(struct rtl8211e_priv *p, uint32_t phy_addr,
                         uint32_t reg, uint16_t *val)
{
    gem_mdio_xfer_t xfer = { .phy_addr = phy_addr, .reg_addr = reg, .value = 0 };
    int ret = device_ioctl(p->gem_dev, GEM_IOCTL_MDIO_READ, &xfer);
    *val = xfer.value;
    return ret;
}

static int phy_mdio_write(struct rtl8211e_priv *p, uint32_t phy_addr,
                          uint32_t reg, uint16_t val)
{
    gem_mdio_xfer_t xfer = { .phy_addr = phy_addr, .reg_addr = reg, .value = val };
    return device_ioctl(p->gem_dev, GEM_IOCTL_MDIO_WRITE, &xfer);
}

/* ------------------------------------------------------------------ */
/*  Section 2 (cont): PHY helper functions                             */
/* ------------------------------------------------------------------ */

/* Scan MDIO addresses 31->1 to find a valid PHY */
static uint32_t phy_detect_addr(struct rtl8211e_priv *p)
{
    uint16_t phy_reg;
    uint32_t addr;

    for (addr = 31; addr > 0; addr--) {
        phy_mdio_read(p, addr, PHY_DETECT_REG, &phy_reg);
        if ((phy_reg != 0xFFFF) &&
            ((phy_reg & PHY_DETECT_MASK) == PHY_DETECT_MASK)) {
            xil_printf("RTL8211E: PHY detected at address %d\r\n", addr);
            return addr;
        }
    }

    xil_printf("RTL8211E: No PHY detected, assuming address 0\r\n");
    return 0;
}

/* Software reset via control register */
static int phy_reset(struct rtl8211e_priv *p)
{
    uint16_t control;
    int timeout = 10000;

    phy_mdio_read(p, p->phy_addr, IEEE_CONTROL_REG, &control);
    control |= CTRL_RESET_MASK;
    phy_mdio_write(p, p->phy_addr, IEEE_CONTROL_REG, control);

    do {
        phy_mdio_read(p, p->phy_addr, IEEE_CONTROL_REG, &control);
        if (timeout-- <= 0) {
            xil_printf("RTL8211E: PHY reset timeout\r\n");
            return -1;
        }
    } while (control & CTRL_RESET_MASK);

    return 0;
}

/* Run auto-negotiation using standard IEEE registers */
static int phy_autoneg(struct rtl8211e_priv *p)
{
    uint16_t control;
    uint16_t status;
    int timeout;

    /* Advertise 1000BASE-T capabilities */
    phy_mdio_read(p, p->phy_addr, IEEE_1000_ADVERTISE_REG, &control);
    control |= ADVERTISE_1000;
    phy_mdio_write(p, p->phy_addr, IEEE_1000_ADVERTISE_REG, control);

    /* Advertise 10/100 Mbps capabilities */
    phy_mdio_read(p, p->phy_addr, IEEE_AUTONEGO_ADVERTISE_REG, &control);
    control |= ADVERTISE_ALL;
    phy_mdio_write(p, p->phy_addr, IEEE_AUTONEGO_ADVERTISE_REG, control);

    /* Enable auto-negotiation and restart */
    phy_mdio_read(p, p->phy_addr, IEEE_CONTROL_REG, &control);
    control |= CTRL_AUTONEG_ENABLE;
    control |= CTRL_RESTART_AUTONEG;
    phy_mdio_write(p, p->phy_addr, IEEE_CONTROL_REG, control);

    /* Wait for auto-negotiation to complete */
    xil_printf("RTL8211E: Waiting for auto-negotiation...\r\n");
    timeout = 50000;
    do {
        phy_mdio_read(p, p->phy_addr, IEEE_STATUS_REG, &status);
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
    uint16_t status;

    phy_mdio_read(p, p->phy_addr, RTL8211E_SPECIFIC_STATUS_REG, &status);

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
/*  Section 3: device_ops functions                                    */
/* ------------------------------------------------------------------ */

static int rtl8211e_init(struct device *dev)
{
    struct rtl8211e_priv *p = (struct rtl8211e_priv *)dev->priv;

    p->gem_dev = device_find("gem0");
    if (!p->gem_dev) {
        xil_printf("RTL8211E: gem0 device not found\r\n");
        return -1;
    }

    p->speed = 0;
    p->duplex = 0;
    p->link_up = 0;

    /* Detect PHY address on MDIO bus (MDIO already initialized by gem_driver) */
    p->phy_addr = phy_detect_addr(p);

    /* Software reset PHY */
    if (phy_reset(p) != 0)
        return -1;

    /* Run auto-negotiation */
    if (phy_autoneg(p) != 0)
        return -1;

    /* Read negotiated result */
    phy_read_status(p);

    /* Set MAC speed via gem0 (handles both SLCR clock + MAC speed) */
    device_ioctl(p->gem_dev, GEM_IOCTL_SET_SPEED, &p->speed);

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
        if (phy_autoneg(p) != 0)
            return -1;
        phy_read_status(p);
        device_ioctl(p->gem_dev, GEM_IOCTL_SET_SPEED, &p->speed);
        xil_printf("RTL8211E: re-negotiated speed=%d\r\n", p->speed);
        break;

    default:
        return -1;
    }

    return 0;
}

/* ------------------------------------------------------------------ */
/*  Section 4: Ops and device instance                                 */
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
