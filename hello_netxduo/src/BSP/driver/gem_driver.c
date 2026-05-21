/*
 * gem_driver.c - Zynq-7000 GEM (Gigabit Ethernet MAC) driver
 *
 * Architecture:
 *   board_init.c:  XEmacPs g_emac_ps  (LookupConfig + CfgInitialize)
 *                         |
 *   gem_driver.c:       priv holds XEmacPs*, exposes all GEM operations
 *                         |       via ioctl (BD ring, DMA, interrupts, MDIO, SLCR)
 *                         |
 *   rtl8211e_driver.c:  device_find("gem0") -> MDIO ioctl for PHY access
 *   nx_driver_zynq.c:   device_find("gem0") -> GEM ioctl for TX/RX
 */

#include "device_core.h"
#include "ioctl_cmd.h"
#include "xemacps.h"
#include "xemacps_bdring.h"
#include "xscugic.h"
#include "xparameters_ps.h"
#include "xparameters.h"
#include "xil_printf.h"
#include "xil_io.h"
#include "xil_cache.h"

static inline u32 XEMACPS_BD_TO_INDEX(XEmacPs_BdRing *RingPtr,
                                       XEmacPs_Bd *BdPtr)
{
    return ((u32)BdPtr - RingPtr->BaseBdAddr) / RingPtr->Separation;
}

/* ------------------------------------------------------------------ */
/*  Section 1: Includes and priv struct                                */
/* ------------------------------------------------------------------ */

extern XEmacPs  g_emac_ps;
extern XScuGic  xInterruptController;

/* SLCR register definitions for GEM0 clock configuration */
#define SLCR_LOCK_ADDR          (XPS_SYS_CTRL_BASEADDR + 0x4)
#define SLCR_UNLOCK_ADDR        (XPS_SYS_CTRL_BASEADDR + 0x8)
#define SLCR_GEM0_CLK_CTRL_ADDR (XPS_SYS_CTRL_BASEADDR + 0x140)
#define SLCR_LOCK_KEY_VALUE     0x767B
#define SLCR_UNLOCK_KEY_VALUE   0xDF0D
#define EMACPS_SLCR_DIV_MASK    0xFC0FC0FF

struct gem_priv {
    XEmacPs *emac;
};

/* ------------------------------------------------------------------ */
/*  Section 2: Helper functions (internal)                             */
/* ------------------------------------------------------------------ */

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

static void gem_do_set_mac(struct gem_priv *p, const uint8_t *addr)
{
    XEmacPs_SetMacAddress(p->emac, (void *)addr, 1);
}

static void gem_do_set_speed(struct gem_priv *p, uint32_t speed)
{
    slcr_set_gem0_clock(speed);
    XEmacPs_SetOperatingSpeed(p->emac, speed);
}

static void gem_do_set_mdio_divisor(struct gem_priv *p, uint32_t divisor)
{
    XEmacPs_SetMdioDivisor(p->emac, (XEmacPs_MdcDiv)divisor);
}

static void gem_do_set_rx_offset(struct gem_priv *p, uint32_t bytes)
{
    uint32_t reg = XEmacPs_ReadReg(p->emac->Config.BaseAddress,
                                    XEMACPS_NWCFG_OFFSET);
    reg &= ~(3U << 14);
    reg |= (bytes << 14);
    XEmacPs_WriteReg(p->emac->Config.BaseAddress,
                      XEMACPS_NWCFG_OFFSET, reg);
}

static void gem_do_set_options(struct gem_priv *p, uint32_t options)
{
    XEmacPs_SetOptions(p->emac, options);
}

static void gem_do_configure_dma(struct gem_priv *p, uint32_t dmacr_val)
{
    XEmacPs_WriteReg(p->emac->Config.BaseAddress,
                      XEMACPS_DMACR_OFFSET, dmacr_val);
}

static void gem_do_enable(struct gem_priv *p)
{
    XEmacPs_SetQueuePtr(p->emac,
        XEmacPs_GetRxRing(p->emac).BaseBdAddr, 0, XEMACPS_RECV);
    XEmacPs_SetQueuePtr(p->emac,
        XEmacPs_GetTxRing(p->emac).BaseBdAddr, 0, XEMACPS_SEND);
    XEmacPs_Start(p->emac);
}

/* ------------------------------------------------------------------ */
/*  Section 3: device_ops functions                                    */
/* ------------------------------------------------------------------ */

static int gem_init(struct device *dev)
{
    struct gem_priv *p = (struct gem_priv *)dev->priv;

    p->emac = &g_emac_ps;

    /* Set MDIO divisor early so PHY drivers can use MDIO immediately */
    XEmacPs_SetMdioDivisor(p->emac, (XEmacPs_MdcDiv)7);

    /* Set SLCR GEM0 clock to 1G as default before PHY auto-negotiation */
    slcr_set_gem0_clock(1000);

    return 0;
}

static int gem_ioctl(struct device *dev, int cmd, void *arg)
{
    struct gem_priv *p = (struct gem_priv *)dev->priv;

    switch (cmd) {
    case DEV_IOCTL_SET_NOTIFY:
        dev->notify = (device_notify_t)arg;
        break;

    /* ---- GEM configuration ---- */
    case GEM_IOCTL_SET_MAC_ADDRESS: {
        gem_mac_addr_t *a = (gem_mac_addr_t *)arg;
        if (!a || !a->addr) return -1;
        gem_do_set_mac(p, a->addr);
        break;
    }
    case GEM_IOCTL_SET_SPEED: {
        if (!arg) return -1;
        gem_do_set_speed(p, *((uint32_t *)arg));
        break;
    }
    case GEM_IOCTL_SET_MDIO_DIVISOR: {
        if (!arg) return -1;
        gem_do_set_mdio_divisor(p, *((uint32_t *)arg));
        break;
    }
    case GEM_IOCTL_SET_RX_OFFSET: {
        if (!arg) return -1;
        gem_do_set_rx_offset(p, *((uint32_t *)arg));
        break;
    }
    case GEM_IOCTL_SET_OPTIONS: {
        if (!arg) return -1;
        gem_do_set_options(p, *((uint32_t *)arg));
        break;
    }
    case GEM_IOCTL_CONFIGURE_DMA: {
        if (!arg) return -1;
        gem_do_configure_dma(p, *((uint32_t *)arg));
        break;
    }

    /* ---- BD Ring management ---- */
    case GEM_IOCTL_RX_BD_RING_CREATE: {
        gem_bd_ring_create_t *a = (gem_bd_ring_create_t *)arg;
        if (!a) return -1;
        XEmacPs_Bd bd_template;
        int r;
        XEmacPs_BdClear(&bd_template);
        r = XEmacPs_BdRingCreate(&XEmacPs_GetRxRing(p->emac),
                                  a->base_addr, a->base_addr,
                                  XEMACPS_BD_ALIGNMENT, a->count);
        if (r != XST_SUCCESS) return -1;
        r = XEmacPs_BdRingClone(&XEmacPs_GetRxRing(p->emac),
                                 &bd_template, XEMACPS_RECV);
        if (r != XST_SUCCESS) return -1;
        break;
    }
    case GEM_IOCTL_TX_BD_RING_CREATE: {
        gem_bd_ring_create_t *a = (gem_bd_ring_create_t *)arg;
        if (!a) return -1;
        XEmacPs_Bd bd_template;
        int r;
        XEmacPs_BdClear(&bd_template);
        XEmacPs_BdSetStatus(&bd_template, XEMACPS_TXBUF_USED_MASK);
        r = XEmacPs_BdRingCreate(&XEmacPs_GetTxRing(p->emac),
                                  a->base_addr, a->base_addr,
                                  XEMACPS_BD_ALIGNMENT, a->count);
        if (r != XST_SUCCESS) return -1;
        r = XEmacPs_BdRingClone(&XEmacPs_GetTxRing(p->emac),
                                 &bd_template, XEMACPS_SEND);
        if (r != XST_SUCCESS) return -1;
        break;
    }
    case GEM_IOCTL_BD_RING_ALLOC_RX: {
        gem_bd_op_t *a = (gem_bd_op_t *)arg;
        if (!a) return -1;
        a->status = XEmacPs_BdRingAlloc(&XEmacPs_GetRxRing(p->emac),
                                          a->num, (XEmacPs_Bd **)&a->bd_ptr);
        break;
    }
    case GEM_IOCTL_BD_RING_ALLOC_TX: {
        gem_bd_op_t *a = (gem_bd_op_t *)arg;
        if (!a) return -1;
        a->status = XEmacPs_BdRingAlloc(&XEmacPs_GetTxRing(p->emac),
                                          a->num, (XEmacPs_Bd **)&a->bd_ptr);
        break;
    }
    case GEM_IOCTL_BD_RING_TO_HW_RX: {
        gem_bd_op_t *a = (gem_bd_op_t *)arg;
        if (!a) return -1;
        a->status = XEmacPs_BdRingToHw(&XEmacPs_GetRxRing(p->emac),
                                          a->num, (XEmacPs_Bd *)a->bd_ptr);
        break;
    }
    case GEM_IOCTL_BD_RING_TO_HW_TX: {
        gem_bd_op_t *a = (gem_bd_op_t *)arg;
        if (!a) return -1;
        a->status = XEmacPs_BdRingToHw(&XEmacPs_GetTxRing(p->emac),
                                          a->num, (XEmacPs_Bd *)a->bd_ptr);
        break;
    }
    case GEM_IOCTL_BD_RING_FROM_HW_TX: {
        gem_bd_from_hw_t *a = (gem_bd_from_hw_t *)arg;
        if (!a) return -1;
        a->count = XEmacPs_BdRingFromHwTx(&XEmacPs_GetTxRing(p->emac),
                                            a->limit, (XEmacPs_Bd **)&a->bd_ptr);
        break;
    }
    case GEM_IOCTL_BD_RING_FROM_HW_RX: {
        gem_bd_from_hw_t *a = (gem_bd_from_hw_t *)arg;
        if (!a) return -1;
        a->count = XEmacPs_BdRingFromHwRx(&XEmacPs_GetRxRing(p->emac),
                                            a->limit, (XEmacPs_Bd **)&a->bd_ptr);
        break;
    }
    case GEM_IOCTL_BD_RING_FREE_RX: {
        gem_bd_op_t *a = (gem_bd_op_t *)arg;
        if (!a) return -1;
        a->status = XEmacPs_BdRingFree(&XEmacPs_GetRxRing(p->emac),
                                          a->num, (XEmacPs_Bd *)a->bd_ptr);
        break;
    }
    case GEM_IOCTL_BD_RING_FREE_TX: {
        gem_bd_op_t *a = (gem_bd_op_t *)arg;
        if (!a) return -1;
        a->status = XEmacPs_BdRingFree(&XEmacPs_GetTxRing(p->emac),
                                          a->num, (XEmacPs_Bd *)a->bd_ptr);
        break;
    }
    case GEM_IOCTL_BD_RING_NEXT_TX: {
        gem_bd_nav_t *a = (gem_bd_nav_t *)arg;
        if (!a) return -1;
        a->next_ptr = (uint32_t *)XEmacPs_BdRingNext(
            &XEmacPs_GetTxRing(p->emac), (XEmacPs_Bd *)a->bd_ptr);
        break;
    }
    case GEM_IOCTL_BD_RING_NEXT_RX: {
        gem_bd_nav_t *a = (gem_bd_nav_t *)arg;
        if (!a) return -1;
        a->next_ptr = (uint32_t *)XEmacPs_BdRingNext(
            &XEmacPs_GetRxRing(p->emac), (XEmacPs_Bd *)a->bd_ptr);
        break;
    }
    case GEM_IOCTL_BD_TO_INDEX_TX: {
        gem_bd_nav_t *a = (gem_bd_nav_t *)arg;
        if (!a) return -1;
        a->index = XEMACPS_BD_TO_INDEX(&XEmacPs_GetTxRing(p->emac),
                                         (XEmacPs_Bd *)a->bd_ptr);
        break;
    }
    case GEM_IOCTL_BD_TO_INDEX_RX: {
        gem_bd_nav_t *a = (gem_bd_nav_t *)arg;
        if (!a) return -1;
        a->index = XEMACPS_BD_TO_INDEX(&XEmacPs_GetRxRing(p->emac),
                                         (XEmacPs_Bd *)a->bd_ptr);
        break;
    }

    /* ---- GEM control ---- */
    case GEM_IOCTL_REGISTER_CALLBACKS: {
        gem_callbacks_t *a = (gem_callbacks_t *)arg;
        if (!a) return -1;
        XEmacPs_SetHandler(p->emac, XEMACPS_HANDLER_DMASEND,
                           (void *)a->tx_cb, a->cb_arg);
        XEmacPs_SetHandler(p->emac, XEMACPS_HANDLER_DMARECV,
                           (void *)a->rx_cb, a->cb_arg);
        XEmacPs_SetHandler(p->emac, XEMACPS_HANDLER_ERROR,
                           (void *)a->err_cb, a->cb_arg);
        break;
    }
    case GEM_IOCTL_ENABLE_INTERRUPTS: {
        XScuGic_Connect(&xInterruptController, XPS_GEM0_INT_ID,
                        (Xil_InterruptHandler)XEmacPs_IntrHandler,
                        (void *)p->emac);
        break;
    }
    case GEM_IOCTL_ENABLE: {
        XScuGic_Enable(&xInterruptController, XPS_GEM0_INT_ID);
        gem_do_enable(p);
        break;
    }
    case GEM_IOCTL_DISABLE: {
        XEmacPs_Stop(p->emac);
        break;
    }
    case GEM_IOCTL_START_TX: {
        XEmacPs_Transmit(p->emac);
        break;
    }

    /* ---- Status read ---- */
    case GEM_IOCTL_READ_TX_STATUS: {
        if (!arg) return -1;
        *((uint32_t *)arg) = XEmacPs_ReadReg(
            p->emac->Config.BaseAddress, XEMACPS_TXSR_OFFSET);
        break;
    }

    /* ---- Cache operations ---- */
    case GEM_IOCTL_CACHE_FLUSH: {
        gem_cache_op_t *a = (gem_cache_op_t *)arg;
        if (!a) return -1;
        Xil_DCacheFlushRange((UINTPTR)a->addr, a->len);
        break;
    }
    case GEM_IOCTL_CACHE_INVALIDATE: {
        gem_cache_op_t *a = (gem_cache_op_t *)arg;
        if (!a) return -1;
        Xil_DCacheInvalidateRange((UINTPTR)a->addr, a->len);
        break;
    }

    /* ---- MDIO bus access ---- */
    case GEM_IOCTL_MDIO_READ: {
        gem_mdio_xfer_t *x = (gem_mdio_xfer_t *)arg;
        if (!x) return -1;
        if (XEmacPs_PhyRead(p->emac, x->phy_addr, x->reg_addr, &x->value)
            != XST_SUCCESS)
            return -1;
        break;
    }
    case GEM_IOCTL_MDIO_WRITE: {
        gem_mdio_xfer_t *x = (gem_mdio_xfer_t *)arg;
        if (!x) return -1;
        if (XEmacPs_PhyWrite(p->emac, x->phy_addr, x->reg_addr, x->value)
            != XST_SUCCESS)
            return -1;
        break;
    }

    default:
        return -1;
    }

    return 0;
}

/* ------------------------------------------------------------------ */
/*  Section 4: Ops and device instance                                 */
/* ------------------------------------------------------------------ */

static const struct device_ops gem_ops = {
    .init  = gem_init,
    .read  = NULL,
    .write = NULL,
    .ioctl = gem_ioctl,
};

static struct gem_priv gem0_priv;
static struct device g_gem0 = {
    .name   = "gem0",
    .ops    = &gem_ops,
    .priv   = &gem0_priv,
    .notify = NULL,
};

/* ------------------------------------------------------------------ */
/*  Section 5: Driver entry                                            */
/* ------------------------------------------------------------------ */

void gem_driver_init(void)
{
    device_register(&g_gem0);
    device_init(&g_gem0);
}
