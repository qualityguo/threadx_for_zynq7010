/*
 * xemacpsif_physpeed.c - RTL8211E PHY speed configuration for Zynq GEM
 *
 * Uses XEmacPs_PhyRead/PhyWrite directly for MDIO access.
 * Provides Phy_Setup() and configure_IEEE_phy_speed() for nx_driver_zynq.
 */

#include "tx_api.h"
#include "xparameters_ps.h"
#include "xparameters.h"
#include "xil_types.h"
#include "xil_io.h"
#include "xemacps.h"
#include "app_print.h"

/* ------------------------------------------------------------------ */
/*  IEEE PHY register definitions                                      */
/* ------------------------------------------------------------------ */

#define IEEE_CONTROL_REG_OFFSET             0
#define IEEE_STATUS_REG_OFFSET              1
#define IEEE_AUTONEGO_ADVERTISE_REG         4
#define IEEE_PARTNER_ABILITIES_1_REG_OFFSET 5
#define IEEE_1000_ADVERTISE_REG_OFFSET      9
/* RTL8211E PHY Specific Status Register (PHYSR) - not 0x1A! */
#define RTL8211E_SPECIFIC_STATUS_REG        0x11

#define IEEE_CTRL_RESET_MASK                0x8000
#define IEEE_CTRL_AUTONEGOTIATE_ENABLE      0x1000
#define IEEE_CTRL_LINKSPEED_1000M           0x0040
#define IEEE_CTRL_LINKSPEED_100M            0x2000
#define IEEE_CTRL_LINKSPEED_10M             0x0000

#define IEEE_STAT_AUTONEGOTIATE_COMPLETE    0x0020

#define ADVERTISE_10HALF    0x0020
#define ADVERTISE_10FULL    0x0040
#define ADVERTISE_100HALF   0x0080
#define ADVERTISE_100FULL   0x0100
#define ADVERTISE_1000      0x0300

#define ADVERTISE_ALL       (ADVERTISE_10HALF | ADVERTISE_10FULL | \
                             ADVERTISE_100HALF | ADVERTISE_100FULL)

#define PHY_DETECT_REG      1
#define PHY_DETECT_MASK     0x1808

#define RTL8211E_PAGE_SELECT_REG            31

/* PHYSR register bits (Reg 0x11) */
#define RTL8211E_PHYSR_LINK_UP              (1 << 10)
#define RTL8211E_PHYSR_SPEED_MASK           (3 << 14)
#define RTL8211E_PHYSR_SPEED_1000           (2 << 14)
#define RTL8211E_PHYSR_SPEED_100            (1 << 14)

/* ------------------------------------------------------------------ */
/*  SLCR registers for GEM clock configuration                         */
/* ------------------------------------------------------------------ */

#define SLCR_LOCK_ADDR              (XPS_SYS_CTRL_BASEADDR + 0x4)
#define SLCR_UNLOCK_ADDR            (XPS_SYS_CTRL_BASEADDR + 0x8)
#define SLCR_GEM0_CLK_CTRL_ADDR     (XPS_SYS_CTRL_BASEADDR + 0x140)
#define SLCR_GEM1_CLK_CTRL_ADDR     (XPS_SYS_CTRL_BASEADDR + 0x144)

#define SLCR_LOCK_KEY_VALUE         0x767B
#define SLCR_UNLOCK_KEY_VALUE       0xDF0D
#define EMACPS_SLCR_DIV_MASK        0xFC0FC0FF

#define CONFIG_LINKSPEED_AUTODETECT

/* ------------------------------------------------------------------ */
/*  PHY debug: dump key registers (including PHY ID)                   */
/* ------------------------------------------------------------------ */

static void phy_dump_regs(XEmacPs *xemacpsp, u32 phy_addr, const char *tag)
{
    u16 r0, r1, r2, r3, r4, r5, r9, r10, r1a;

    XEmacPs_PhyRead(xemacpsp, phy_addr, 0,  &r0);
    XEmacPs_PhyRead(xemacpsp, phy_addr, 1,  &r1);
    XEmacPs_PhyRead(xemacpsp, phy_addr, 2,  &r2);
    XEmacPs_PhyRead(xemacpsp, phy_addr, 3,  &r3);
    XEmacPs_PhyRead(xemacpsp, phy_addr, 4,  &r4);
    XEmacPs_PhyRead(xemacpsp, phy_addr, 5,  &r5);
    XEmacPs_PhyRead(xemacpsp, phy_addr, 9,  &r9);
    XEmacPs_PhyRead(xemacpsp, phy_addr, 10, &r10);
    XEmacPs_PhyRead(xemacpsp, phy_addr, RTL8211E_SPECIFIC_STATUS_REG, &r1a);

    app_printf("[%s] PHY regs @ addr %d:\r\n", tag, phy_addr);
    app_printf("  R0(Ctrl)    = 0x%04X\r\n", r0);
    app_printf("  R1(Status)  = 0x%04X\r\n", r1);
    app_printf("  R2(ID_H)    = 0x%04X\r\n", r2);
    app_printf("  R3(ID_L)    = 0x%04X\r\n", r3);
    app_printf("  R4(Adv)     = 0x%04X  (our advertisement)\r\n", r4);
    app_printf("  R5(Partner) = 0x%04X  (link partner ability)\r\n", r5);
    app_printf("  R9(1000Adv) = 0x%04X  (1000BASE-T adv)\r\n", r9);
    app_printf("  R10(1000St) = 0x%04X  (1000BASE-T status)\r\n", r10);
    app_printf("  R11(PHYSR)  = 0x%04X  (specific status)\r\n", r1a);
    app_printf("  PHY ID      = 0x%04X%04X\r\n", r2, r3);
}

/* ------------------------------------------------------------------ */
/*  RTL8211E extended page register dump                               */
/* ------------------------------------------------------------------ */

static void rtl8211e_dump_extended(XEmacPs *xemacpsp, u32 phy_addr)
{
    u16 val;
    u16 saved_page;

    XEmacPs_PhyRead(xemacpsp, phy_addr, RTL8211E_PAGE_SELECT_REG, &saved_page);

    XEmacPs_PhyWrite(xemacpsp, phy_addr, RTL8211E_PAGE_SELECT_REG, 0x0A43);
    XEmacPs_PhyRead(xemacpsp, phy_addr, 0x10, &val);
    app_printf("  Pg0x0A43 R0x10 = 0x%04X\r\n", val);
    XEmacPs_PhyRead(xemacpsp, phy_addr, 0x1C, &val);
    app_printf("  Pg0x0A43 R0x1C = 0x%04X\r\n", val);

    XEmacPs_PhyWrite(xemacpsp, phy_addr, RTL8211E_PAGE_SELECT_REG, 0x0D04);
    XEmacPs_PhyRead(xemacpsp, phy_addr, 0x10, &val);
    app_printf("  Pg0x0D04 R0x10 = 0x%04X\r\n", val);
    XEmacPs_PhyRead(xemacpsp, phy_addr, 0x11, &val);
    app_printf("  Pg0x0D04 R0x11 = 0x%04X\r\n", val);

    XEmacPs_PhyWrite(xemacpsp, phy_addr, RTL8211E_PAGE_SELECT_REG, saved_page);
}

/* ------------------------------------------------------------------ */
/*  PHY detection                                                      */
/* ------------------------------------------------------------------ */

static int detect_phy(XEmacPs *xemacpsp)
{
    u16 id1, id2;
    u32 phy_addr;

    for (phy_addr = 0; phy_addr < 32; phy_addr++) {
        XEmacPs_PhyRead(xemacpsp, phy_addr, 2, &id1);
        XEmacPs_PhyRead(xemacpsp, phy_addr, 3, &id2);

        if (id1 == 0x001C && (id2 & 0xFFF0) == 0xC910) {
            app_printf("RTL8211E: PHY detected at address %d, ID=0x%04X%04X\r\n",
                       phy_addr, id1, id2);
            return phy_addr;
        }
    }

    app_printf("RTL8211E: No PHY detected, assuming address 0\r\n");
    return 0;
}

/* ------------------------------------------------------------------ */
/*  PHY reset                                                          */
/* ------------------------------------------------------------------ */

static int phy_reset(XEmacPs *xemacpsp, u32 phy_addr)
{
    u16 control;
    int timeout = 10000;

    XEmacPs_PhyRead(xemacpsp, phy_addr, IEEE_CONTROL_REG_OFFSET, &control);
    control |= IEEE_CTRL_RESET_MASK;
    XEmacPs_PhyWrite(xemacpsp, phy_addr, IEEE_CONTROL_REG_OFFSET, control);

    do {
        XEmacPs_PhyRead(xemacpsp, phy_addr, IEEE_CONTROL_REG_OFFSET, &control);
        if (timeout-- <= 0) {
            app_printf("RTL8211E: PHY reset timeout\r\n");
            return -1;
        }
    } while (control & IEEE_CTRL_RESET_MASK);

    return 0;
}

/* ------------------------------------------------------------------ */
/*  RTL8211E RGMII delay configuration - try multiple pages            */
/* ------------------------------------------------------------------ */

static void rtl8211e_rgmii_config(XEmacPs *xemacpsp, u32 phy_addr)
{
    u16 val;
    u16 saved_page;

    XEmacPs_PhyRead(xemacpsp, phy_addr, 31, &saved_page);

    /* Page 0xA43, Reg 0x1C: RGMII TX/RX delay (Linux: RTL8211E_PHYCR2)
     *   Bit 13: CTRL_DELAY
     *   Bit 12: TX_DELAY   (adds ~2ns TX clock delay)
     *   Bit 11: RX_DELAY   (adds ~2ns RX clock delay)
     */
    XEmacPs_PhyWrite(xemacpsp, phy_addr, 31, 0x0A43);
    XEmacPs_PhyRead(xemacpsp, phy_addr, 0x1C, &val);
    app_printf("RTL8211E: RGMII delay (Pg0xA43 R0x1C) before = 0x%04X\r\n", val);

    val |= (1 << 13);  /* CTRL_DELAY */
    val |= (1 << 12);  /* TX_DELAY */
    val |= (1 << 11);  /* RX_DELAY */
    XEmacPs_PhyWrite(xemacpsp, phy_addr, 0x1C, val);

    XEmacPs_PhyRead(xemacpsp, phy_addr, 0x1C, &val);
    app_printf("RTL8211E: RGMII delay (Pg0xA43 R0x1C) after  = 0x%04X\r\n", val);

    XEmacPs_PhyWrite(xemacpsp, phy_addr, 31, saved_page);
}

/* ------------------------------------------------------------------ */
/*  Wait for link up via SSS register                                  */
/*  Returns speed on success, 0 on timeout.                            */
/* ------------------------------------------------------------------ */

static unsigned wait_for_link(XEmacPs *xemacpsp, u32 phy_addr, int timeout_ticks)
{
    u16 status;

    do {
        tx_thread_sleep(1);
        XEmacPs_PhyRead(xemacpsp, phy_addr, RTL8211E_SPECIFIC_STATUS_REG, &status);
        if (status & RTL8211E_PHYSR_LINK_UP) {
            if ((status & RTL8211E_PHYSR_SPEED_MASK) == RTL8211E_PHYSR_SPEED_1000)
                return 1000;
            else if ((status & RTL8211E_PHYSR_SPEED_MASK) == RTL8211E_PHYSR_SPEED_100)
                return 100;
            else
                return 10;
        }
    } while (timeout_ticks-- > 0);

    return 0;
}

/* ------------------------------------------------------------------ */
/*  Do auto-negotiation with specified advertisement                   */
/* ------------------------------------------------------------------ */

static unsigned do_autoneg(XEmacPs *xemacpsp, u32 phy_addr,
                           u16 adv_1000, u16 adv_10_100, const char *label)
{
    u16 control, status;
    int timeout;

    XEmacPs_PhyWrite(xemacpsp, phy_addr, IEEE_1000_ADVERTISE_REG_OFFSET, adv_1000);
    XEmacPs_PhyWrite(xemacpsp, phy_addr, IEEE_AUTONEGO_ADVERTISE_REG, adv_10_100);

//    phy_dump_regs(xemacpsp, phy_addr, label);

    /* Restart auto-negotiation */
    XEmacPs_PhyRead(xemacpsp, phy_addr, IEEE_CONTROL_REG_OFFSET, &control);
    control |= IEEE_CTRL_AUTONEGOTIATE_ENABLE | 0x0200;
    XEmacPs_PhyWrite(xemacpsp, phy_addr, IEEE_CONTROL_REG_OFFSET, control);

    app_printf("RTL8211E: [%s] Waiting for AN...\r\n", label);
    timeout = 50000;
    do {
        XEmacPs_PhyRead(xemacpsp, phy_addr, IEEE_STATUS_REG_OFFSET, &status);
        if (timeout-- <= 0) {
            app_printf("RTL8211E: [%s] AN timeout\r\n", label);
            return 0;
        }
    } while (!(status & IEEE_STAT_AUTONEGOTIATE_COMPLETE));

    app_printf("RTL8211E: [%s] AN complete, waiting for link...\r\n", label);
    return wait_for_link(xemacpsp, phy_addr, 500);
}

/* ------------------------------------------------------------------ */
/*  PHY auto-negotiation: 1000M only                                   */
/* ------------------------------------------------------------------ */

static unsigned get_IEEE_phy_speed(XEmacPs *xemacpsp)
{
    u32 phy_addr = detect_phy(xemacpsp);
    unsigned speed;

    if (phy_reset(xemacpsp, phy_addr) != 0)
        return 10;

    rtl8211e_rgmii_config(xemacpsp, phy_addr);
    tx_thread_sleep(50);

//    phy_dump_regs(xemacpsp, phy_addr, "After reset+RGMII cfg");

    app_printf("RTL8211E: === Trying 1000Mbps ===\r\n");
    speed = do_autoneg(xemacpsp, phy_addr, ADVERTISE_1000,
                       ADVERTISE_ALL, "1000M");
    if (speed == 1000) {
        app_printf("RTL8211E: 1000Mbps link UP!\r\n");
    } else {
        app_printf("RTL8211E: 1000Mbps failed, forcing 10Mbps fallback\r\n");
        speed = 10;
    }

    return speed;
}

/* ------------------------------------------------------------------ */
/*  Configure PHY speed manually                                       */
/* ------------------------------------------------------------------ */

unsigned configure_IEEE_phy_speed(XEmacPs *xemacpsp, unsigned speed)
{
    u16 control;
    u32 phy_addr = detect_phy(xemacpsp);

    XEmacPs_PhyRead(xemacpsp, phy_addr, IEEE_CONTROL_REG_OFFSET, &control);
    control &= ~IEEE_CTRL_LINKSPEED_1000M;
    control &= ~IEEE_CTRL_LINKSPEED_100M;
    control &= ~IEEE_CTRL_LINKSPEED_10M;

    if (speed == 1000) {
        control |= IEEE_CTRL_LINKSPEED_1000M;
    } else if (speed == 100) {
        control |= IEEE_CTRL_LINKSPEED_100M;
        XEmacPs_PhyWrite(xemacpsp, phy_addr, IEEE_1000_ADVERTISE_REG_OFFSET, 0);
        XEmacPs_PhyWrite(xemacpsp, phy_addr, IEEE_AUTONEGO_ADVERTISE_REG,
                         ADVERTISE_100FULL | ADVERTISE_100HALF);
    } else if (speed == 10) {
        control |= IEEE_CTRL_LINKSPEED_10M;
        XEmacPs_PhyWrite(xemacpsp, phy_addr, IEEE_1000_ADVERTISE_REG_OFFSET, 0);
        XEmacPs_PhyWrite(xemacpsp, phy_addr, IEEE_AUTONEGO_ADVERTISE_REG,
                         ADVERTISE_10FULL | ADVERTISE_10HALF);
    }

    XEmacPs_PhyWrite(xemacpsp, phy_addr, IEEE_CONTROL_REG_OFFSET,
                     control | IEEE_CTRL_RESET_MASK);
    {
        volatile int wait;
        for (wait = 0; wait < 100000; wait++);
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/*  Phy_Setup - main entry point called from nx_driver_zynq            */
/* ------------------------------------------------------------------ */

unsigned Phy_Setup(XEmacPs *xemacpsp)
{
    unsigned link_speed = 1000;
    u32 SlcrTxClkCntrl;

#ifdef CONFIG_LINKSPEED_AUTODETECT
    link_speed = get_IEEE_phy_speed(xemacpsp);
    app_printf("RTL8211E: auto-negotiated link speed: %d\r\n", link_speed);

    if (link_speed == 1000) {
        *(volatile unsigned int *)(SLCR_UNLOCK_ADDR) = SLCR_UNLOCK_KEY_VALUE;
        SlcrTxClkCntrl = *(volatile unsigned int *)(SLCR_GEM0_CLK_CTRL_ADDR);
        SlcrTxClkCntrl &= EMACPS_SLCR_DIV_MASK;
        SlcrTxClkCntrl |= (XPAR_PS7_ETHERNET_0_ENET_SLCR_1000MBPS_DIV1 << 20);
        SlcrTxClkCntrl |= (XPAR_PS7_ETHERNET_0_ENET_SLCR_1000MBPS_DIV0 << 8);
        *(volatile unsigned int *)(SLCR_GEM0_CLK_CTRL_ADDR) = SlcrTxClkCntrl;
        *(volatile unsigned int *)(SLCR_LOCK_ADDR) = SLCR_LOCK_KEY_VALUE;
        tx_thread_sleep(100);

    } else if (link_speed == 100) {
        *(volatile unsigned int *)(SLCR_UNLOCK_ADDR) = SLCR_UNLOCK_KEY_VALUE;
        SlcrTxClkCntrl = *(volatile unsigned int *)(SLCR_GEM0_CLK_CTRL_ADDR);
        SlcrTxClkCntrl &= EMACPS_SLCR_DIV_MASK;
        SlcrTxClkCntrl |= (XPAR_PS7_ETHERNET_0_ENET_SLCR_100MBPS_DIV1 << 20);
        SlcrTxClkCntrl |= (XPAR_PS7_ETHERNET_0_ENET_SLCR_100MBPS_DIV0 << 8);
        *(volatile unsigned int *)(SLCR_GEM0_CLK_CTRL_ADDR) = SlcrTxClkCntrl;
        *(volatile unsigned int *)(SLCR_LOCK_ADDR) = SLCR_LOCK_KEY_VALUE;
        tx_thread_sleep(100);

    } else if (link_speed == 10) {
        *(volatile unsigned int *)(SLCR_UNLOCK_ADDR) = SLCR_UNLOCK_KEY_VALUE;
        SlcrTxClkCntrl = *(volatile unsigned int *)(SLCR_GEM0_CLK_CTRL_ADDR);
        SlcrTxClkCntrl &= EMACPS_SLCR_DIV_MASK;
        SlcrTxClkCntrl |= (XPAR_PS7_ETHERNET_0_ENET_SLCR_10MBPS_DIV1 << 20);
        SlcrTxClkCntrl |= (XPAR_PS7_ETHERNET_0_ENET_SLCR_10MBPS_DIV0 << 8);
        *(volatile unsigned int *)(SLCR_GEM0_CLK_CTRL_ADDR) = SlcrTxClkCntrl;
        *(volatile unsigned int *)(SLCR_LOCK_ADDR) = SLCR_LOCK_KEY_VALUE;
        tx_thread_sleep(100);
    }
#endif

    return link_speed;
}
