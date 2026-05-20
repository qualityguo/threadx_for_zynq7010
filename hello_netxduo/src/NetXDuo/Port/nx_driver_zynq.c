/*
 * nx_driver_zynq.c - NetX Duo Ethernet driver for Xilinx Zynq-7000 GEM
 *
 * Adapted from Express Logic reference driver for Zynq-7000.
 * Uses device_core PHY driver (rtl8211e_driver) via device_find/device_ioctl
 * instead of direct PHY register access.
 *
 * Driver structure:
 *   nx_driver_zynq()           - entry point, command dispatch
 *   _nx_driver_*()             - generic NetX framework wrappers
 *   _nx_driver_hardware_*()    - Zynq GEM specific implementations
 */

#define NX_DRIVER_SOURCE

#include "nx_driver_zynq.h"
#include "device_core.h"
#include "ioctl_cmd.h"
#include "xemacps_bdring.h"
#include "xscugic.h"
#include "xil_printf.h"

/* ------------------------------------------------------------------ */
/*  Driver information instance                                        */
/* ------------------------------------------------------------------ */

static NX_DRIVER_INFORMATION nx_driver_information;

/* MAC address */
static UCHAR _nx_driver_hardware_address[] = {0x00, 0x11, 0x22, 0x33, 0x44, 0x56};

/* Externals from BSP */
extern XEmacPs  g_emac_ps;
extern XScuGic  xInterruptController;

#define EMACPS_IRPT_INTR   XPS_GEM0_INT_ID

/* ------------------------------------------------------------------ */
/*  Forward declarations                                               */
/* ------------------------------------------------------------------ */

/* Generic driver functions */
static VOID _nx_driver_interface_attach(NX_IP_DRIVER *driver_req_ptr);
static VOID _nx_driver_initialize(NX_IP_DRIVER *driver_req_ptr);
static VOID _nx_driver_enable(NX_IP_DRIVER *driver_req_ptr);
static VOID _nx_driver_disable(NX_IP_DRIVER *driver_req_ptr);
static VOID _nx_driver_packet_send(NX_IP_DRIVER *driver_req_ptr);
static VOID _nx_driver_multicast_join(NX_IP_DRIVER *driver_req_ptr);
static VOID _nx_driver_multicast_leave(NX_IP_DRIVER *driver_req_ptr);
static VOID _nx_driver_get_status(NX_IP_DRIVER *driver_req_ptr);
static VOID _nx_driver_deferred_processing(NX_IP_DRIVER *driver_req_ptr);
static VOID _nx_driver_transfer_to_netx(NX_IP *ip_ptr, NX_PACKET *packet_ptr);

/* Hardware-specific functions */
static UINT _nx_driver_hardware_initialize(NX_IP_DRIVER *driver_req_ptr);
static UINT _nx_driver_hardware_enable(NX_IP_DRIVER *driver_req_ptr);
static UINT _nx_driver_hardware_disable(NX_IP_DRIVER *driver_req_ptr);
static UINT _nx_driver_hardware_packet_send(NX_PACKET *packet_ptr);
static UINT _nx_driver_hardware_multicast_join(NX_IP_DRIVER *driver_req_ptr);
static UINT _nx_driver_hardware_multicast_leave(NX_IP_DRIVER *driver_req_ptr);
static UINT _nx_driver_hardware_get_status(NX_IP_DRIVER *driver_req_ptr);
static int  _nx_driver_hardware_packet_transmitted(VOID);
static int  _nx_driver_hardware_packet_received(VOID);

/* ISR callbacks */
static VOID nx_driver_zynq_ethernet_rx_isr(void *handle);
static VOID nx_driver_zynq_ethernet_tx_isr(void *handle);
static VOID nx_driver_zynq_ethernet_error_isr(void *handle, UCHAR direction, ULONG error_code);

/* MDIO divisor - must be set before PHY access */
extern void XEmacPs_SetMdioDivisor(XEmacPs *InstancePtr, XEmacPs_MdcDiv Divisor);

/* ================================================================== */
/*  Driver Entry                                                       */
/* ================================================================== */

VOID nx_driver_zynq(NX_IP_DRIVER *driver_req_ptr)
{
    driver_req_ptr->nx_ip_driver_status = NX_SUCCESS;

    switch (driver_req_ptr->nx_ip_driver_command) {

    case NX_LINK_INTERFACE_ATTACH:
        _nx_driver_interface_attach(driver_req_ptr);
        break;

    case NX_LINK_INITIALIZE:
        _nx_driver_initialize(driver_req_ptr);
        break;

    case NX_LINK_ENABLE:
        _nx_driver_enable(driver_req_ptr);
        break;

    case NX_LINK_DISABLE:
        _nx_driver_disable(driver_req_ptr);
        break;

    case NX_LINK_ARP_SEND:
    case NX_LINK_ARP_RESPONSE_SEND:
    case NX_LINK_PACKET_BROADCAST:
    case NX_LINK_RARP_SEND:
    case NX_LINK_PACKET_SEND:
        _nx_driver_packet_send(driver_req_ptr);
        break;

    case NX_LINK_MULTICAST_JOIN:
        _nx_driver_multicast_join(driver_req_ptr);
        break;

    case NX_LINK_MULTICAST_LEAVE:
        _nx_driver_multicast_leave(driver_req_ptr);
        break;

    case NX_LINK_GET_STATUS:
        _nx_driver_get_status(driver_req_ptr);
        break;

    case NX_LINK_DEFERRED_PROCESSING:
        _nx_driver_deferred_processing(driver_req_ptr);
        break;

    default:
        driver_req_ptr->nx_ip_driver_status = NX_DRIVER_ERROR;
        break;
    }
}

/* ================================================================== */
/*  Generic Driver Framework Functions                                 */
/* ================================================================== */

static VOID _nx_driver_interface_attach(NX_IP_DRIVER *driver_req_ptr)
{
    nx_driver_information.nx_driver_information_interface =
        driver_req_ptr->nx_ip_driver_interface;
    driver_req_ptr->nx_ip_driver_status = NX_SUCCESS;
}

static VOID _nx_driver_initialize(NX_IP_DRIVER *driver_req_ptr)
{
    NX_IP        *ip_ptr = driver_req_ptr->nx_ip_driver_ptr;
    NX_INTERFACE *interface_ptr = driver_req_ptr->nx_ip_driver_interface;
    UINT          status;

    nx_driver_information.nx_driver_information_ip_ptr = NX_NULL;
    nx_driver_information.nx_driver_information_state  = NX_DRIVER_STATE_NOT_INITIALIZED;
    nx_driver_information.nx_driver_information_packet_pool_ptr = ip_ptr->nx_ip_default_packet_pool;
    nx_driver_information.nx_driver_information_deferred_events = 0;

    status = _nx_driver_hardware_initialize(driver_req_ptr);

    if (status == NX_SUCCESS) {
        nx_driver_information.nx_driver_information_ip_ptr = ip_ptr;
        interface_ptr->nx_interface_ip_mtu_size =
            NX_DRIVER_ETHERNET_MTU - NX_DRIVER_ETHERNET_FRAME_SIZE;
        interface_ptr->nx_interface_physical_address_msw =
            (ULONG)((_nx_driver_hardware_address[0] << 8) | _nx_driver_hardware_address[1]);
        interface_ptr->nx_interface_physical_address_lsw =
            (ULONG)((_nx_driver_hardware_address[2] << 24) | (_nx_driver_hardware_address[3] << 16) |
                    (_nx_driver_hardware_address[4] << 8)  | (_nx_driver_hardware_address[5]));
        interface_ptr->nx_interface_address_mapping_needed = NX_TRUE;
        nx_driver_information.nx_driver_information_state = NX_DRIVER_STATE_INITIALIZED;
        driver_req_ptr->nx_ip_driver_status = NX_SUCCESS;
    } else {
        driver_req_ptr->nx_ip_driver_status = NX_DRIVER_ERROR;
    }
}

static VOID _nx_driver_enable(NX_IP_DRIVER *driver_req_ptr)
{
    NX_IP *ip_ptr = driver_req_ptr->nx_ip_driver_ptr;
    UINT   status;

    if (nx_driver_information.nx_driver_information_state < NX_DRIVER_STATE_INITIALIZED) {
        driver_req_ptr->nx_ip_driver_status = NX_DRIVER_ERROR;
        return;
    }

    if (nx_driver_information.nx_driver_information_state >= NX_DRIVER_STATE_LINK_ENABLED) {
        driver_req_ptr->nx_ip_driver_status = NX_ALREADY_ENABLED;
        return;
    }

    status = _nx_driver_hardware_enable(driver_req_ptr);

    if (status == NX_SUCCESS) {
        nx_driver_information.nx_driver_information_state = NX_DRIVER_STATE_LINK_ENABLED;
        driver_req_ptr->nx_ip_driver_status = NX_SUCCESS;
        ip_ptr->nx_ip_driver_link_up = NX_TRUE;
    } else {
        driver_req_ptr->nx_ip_driver_status = NX_DRIVER_ERROR;
    }
}

static VOID _nx_driver_disable(NX_IP_DRIVER *driver_req_ptr)
{
    NX_IP *ip_ptr = driver_req_ptr->nx_ip_driver_ptr;
    UINT   status;

    if (nx_driver_information.nx_driver_information_state != NX_DRIVER_STATE_LINK_ENABLED) {
        driver_req_ptr->nx_ip_driver_status = NX_DRIVER_ERROR;
        return;
    }

    status = _nx_driver_hardware_disable(driver_req_ptr);

    if (status == NX_SUCCESS) {
        ip_ptr->nx_ip_driver_link_up = NX_FALSE;
        nx_driver_information.nx_driver_information_state = NX_DRIVER_STATE_INITIALIZED;
        driver_req_ptr->nx_ip_driver_status = NX_SUCCESS;
    } else {
        driver_req_ptr->nx_ip_driver_status = NX_DRIVER_ERROR;
    }
}

static VOID _nx_driver_packet_send(NX_IP_DRIVER *driver_req_ptr)
{
    NX_IP     *ip_ptr = driver_req_ptr->nx_ip_driver_ptr;
    NX_PACKET *packet_ptr = driver_req_ptr->nx_ip_driver_packet;
    ULONG     *ethernet_frame_ptr;
    UINT       status;

    if (nx_driver_information.nx_driver_information_state != NX_DRIVER_STATE_LINK_ENABLED) {
        NX_DRIVER_ETHERNET_HEADER_REMOVE(packet_ptr);
        driver_req_ptr->nx_ip_driver_status = NX_DRIVER_ERROR;
        nx_packet_transmit_release(packet_ptr);
        return;
    }

    /* Build ethernet header */
    packet_ptr->nx_packet_prepend_ptr -= NX_DRIVER_ETHERNET_FRAME_SIZE;
    packet_ptr->nx_packet_length      += NX_DRIVER_ETHERNET_FRAME_SIZE;

    ethernet_frame_ptr = (ULONG *)(packet_ptr->nx_packet_prepend_ptr - 2);

    /* Destination MAC */
    *ethernet_frame_ptr       = driver_req_ptr->nx_ip_driver_physical_address_msw;
    *(ethernet_frame_ptr + 1) = driver_req_ptr->nx_ip_driver_physical_address_lsw;

    /* Source MAC */
    *(ethernet_frame_ptr + 2) = (ip_ptr->nx_ip_arp_physical_address_msw << 16) |
                                 (ip_ptr->nx_ip_arp_physical_address_lsw >> 16);
    *(ethernet_frame_ptr + 3) = (ip_ptr->nx_ip_arp_physical_address_lsw << 16);

    /* EtherType */
    if ((driver_req_ptr->nx_ip_driver_command == NX_LINK_ARP_SEND) ||
        (driver_req_ptr->nx_ip_driver_command == NX_LINK_ARP_RESPONSE_SEND)) {
        *(ethernet_frame_ptr + 3) |= NX_DRIVER_ETHERNET_ARP;
    } else if (driver_req_ptr->nx_ip_driver_command == NX_LINK_RARP_SEND) {
        *(ethernet_frame_ptr + 3) |= NX_DRIVER_ETHERNET_RARP;
    } else {
        *(ethernet_frame_ptr + 3) |= NX_DRIVER_ETHERNET_IP;
    }

    /* Endian swap */
    NX_CHANGE_ULONG_ENDIAN(*(ethernet_frame_ptr));
    NX_CHANGE_ULONG_ENDIAN(*(ethernet_frame_ptr + 1));
    NX_CHANGE_ULONG_ENDIAN(*(ethernet_frame_ptr + 2));
    NX_CHANGE_ULONG_ENDIAN(*(ethernet_frame_ptr + 3));

    if (packet_ptr->nx_packet_length > NX_DRIVER_ETHERNET_MTU) {
        NX_DRIVER_ETHERNET_HEADER_REMOVE(packet_ptr);
        driver_req_ptr->nx_ip_driver_status = NX_DRIVER_ERROR;
        nx_packet_transmit_release(packet_ptr);
        return;
    }

    status = _nx_driver_hardware_packet_send(packet_ptr);

    if (status != NX_SUCCESS) {
        NX_DRIVER_ETHERNET_HEADER_REMOVE(packet_ptr);
        driver_req_ptr->nx_ip_driver_status = NX_DRIVER_ERROR;
        nx_packet_transmit_release(packet_ptr);
    } else {
        driver_req_ptr->nx_ip_driver_status = NX_SUCCESS;
    }
}

static VOID _nx_driver_multicast_join(NX_IP_DRIVER *driver_req_ptr)
{
    UINT status = _nx_driver_hardware_multicast_join(driver_req_ptr);
    driver_req_ptr->nx_ip_driver_status =
        (status == NX_SUCCESS) ? NX_SUCCESS : NX_DRIVER_ERROR;
}

static VOID _nx_driver_multicast_leave(NX_IP_DRIVER *driver_req_ptr)
{
    UINT status = _nx_driver_hardware_multicast_leave(driver_req_ptr);
    driver_req_ptr->nx_ip_driver_status =
        (status == NX_SUCCESS) ? NX_SUCCESS : NX_DRIVER_ERROR;
}

static VOID _nx_driver_get_status(NX_IP_DRIVER *driver_req_ptr)
{
    UINT status = _nx_driver_hardware_get_status(driver_req_ptr);
    driver_req_ptr->nx_ip_driver_status =
        (status == NX_SUCCESS) ? NX_SUCCESS : NX_DRIVER_ERROR;
}

static VOID _nx_driver_deferred_processing(NX_IP_DRIVER *driver_req_ptr)
{
    TX_INTERRUPT_SAVE_AREA
    ULONG deferred_events;

    TX_DISABLE
    deferred_events = nx_driver_information.nx_driver_information_deferred_events;
    nx_driver_information.nx_driver_information_deferred_events = 0;
    TX_RESTORE

    if (deferred_events & NX_DRIVER_DEFERRED_PACKET_TRANSMITTED)
        _nx_driver_hardware_packet_transmitted();

    if (deferred_events & NX_DRIVER_DEFERRED_PACKET_RECEIVED)
        _nx_driver_hardware_packet_received();

    driver_req_ptr->nx_ip_driver_status = NX_SUCCESS;
}

static VOID _nx_driver_transfer_to_netx(NX_IP *ip_ptr, NX_PACKET *packet_ptr)
{
    USHORT packet_type;

    packet_ptr->nx_packet_ip_interface =
        nx_driver_information.nx_driver_information_interface;

    packet_type = (USHORT)(((UINT)(*(packet_ptr->nx_packet_prepend_ptr + 12))) << 8) |
                          ((UINT)(*(packet_ptr->nx_packet_prepend_ptr + 13)));

    /* Strip ethernet header and route to NetX */
    packet_ptr->nx_packet_prepend_ptr += NX_DRIVER_ETHERNET_FRAME_SIZE;
    packet_ptr->nx_packet_length      -= NX_DRIVER_ETHERNET_FRAME_SIZE;

    if (packet_type == NX_DRIVER_ETHERNET_IP || packet_type == NX_DRIVER_ETHERNET_IPV6) {
        _nx_ip_packet_deferred_receive(ip_ptr, packet_ptr);
    } else if (packet_type == NX_DRIVER_ETHERNET_ARP) {
        _nx_arp_packet_deferred_receive(ip_ptr, packet_ptr);
    } else if (packet_type == NX_DRIVER_ETHERNET_RARP) {
        _nx_rarp_packet_deferred_receive(ip_ptr, packet_ptr);
    } else {
        nx_packet_release(packet_ptr);
    }
}

/* ================================================================== */
/*  Hardware-Specific Functions                                        */
/* ================================================================== */

static UINT _nx_driver_hardware_initialize(NX_IP_DRIVER *driver_req_ptr)
{
    UINT        i;
    XEmacPs_Bd  BdTemplate;
    XEmacPs_Bd *RxDesc_ptr;
    XEmacPs_Bd *TxDesc_ptr;
    int         ret;
    NX_PACKET  *packet_ptr;
    UINT        rx_offset;

    /* Setup indices */
    nx_driver_information.nx_driver_information_receive_current_index  = 0;
    nx_driver_information.nx_driver_information_transmit_current_index = 0;
    nx_driver_information.nx_driver_information_transmit_release_index = 0;

    if (nx_driver_information.nx_driver_information_packet_pool_ptr == NULL)
        return NX_DRIVER_ERROR;

    /* Configure MAC address */
    ret = XEmacPs_SetMacAddress(&g_emac_ps, _nx_driver_hardware_address, 1);
    if (ret != XST_SUCCESS) {
        xil_printf("nx_driver: SetMacAddress failed\r\n");
        return NX_DRIVER_ERROR;
    }

    /* Register ISR callbacks */
    XEmacPs_SetHandler(&g_emac_ps, XEMACPS_HANDLER_DMASEND,
                       (void *)nx_driver_zynq_ethernet_tx_isr, &g_emac_ps);
    XEmacPs_SetHandler(&g_emac_ps, XEMACPS_HANDLER_DMARECV,
                       (void *)nx_driver_zynq_ethernet_rx_isr, &g_emac_ps);
    XEmacPs_SetHandler(&g_emac_ps, XEMACPS_HANDLER_ERROR,
                       (void *)nx_driver_zynq_ethernet_error_isr, &g_emac_ps);

    /* Create RX BD ring */
    XEmacPs_BdClear(&BdTemplate);
    RxDesc_ptr = (XEmacPs_Bd *)RX_BD_LIST_START_ADDRESS;

    ret = XEmacPs_BdRingCreate(&XEmacPs_GetRxRing(&g_emac_ps),
                                (u32)RxDesc_ptr, (u32)RxDesc_ptr,
                                XEMACPS_BD_ALIGNMENT, NX_DRIVER_RX_DESCRIPTORS);
    if (ret != XST_SUCCESS) return NX_DRIVER_ERROR;

    ret = XEmacPs_BdRingClone(&XEmacPs_GetRxRing(&g_emac_ps), &BdTemplate, XEMACPS_RECV);
    if (ret != XST_SUCCESS) return NX_DRIVER_ERROR;

    /* Allocate RX packets and assign buffer addresses */
    for (i = 0; i < NX_DRIVER_RX_DESCRIPTORS; i++) {
        XEmacPs_Bd *bd_ptr;

        ret = XEmacPs_BdRingAlloc(&XEmacPs_GetRxRing(&g_emac_ps), 1, &bd_ptr);
        if (ret != XST_SUCCESS) return NX_DRIVER_ERROR;

        ret = nx_packet_allocate(
            nx_driver_information.nx_driver_information_packet_pool_ptr,
            &packet_ptr, NX_RECEIVE_PACKET, NX_NO_WAIT);
        if (ret != NX_SUCCESS) return NX_DRIVER_ERROR;

        XEmacPs_BdSetAddressRx(bd_ptr, packet_ptr->nx_packet_prepend_ptr);

        ret = XEmacPs_BdRingToHw(&XEmacPs_GetRxRing(&g_emac_ps), 1, bd_ptr);
        if (ret != XST_SUCCESS) return NX_DRIVER_ERROR;

        Xil_DCacheInvalidateRange((u32)packet_ptr->nx_packet_prepend_ptr,
                                  NX_DRIVER_ETHERNET_MTU);

        nx_driver_information.nx_driver_information_receive_packets[i] = packet_ptr;
    }

    /* Initialize TX packet pointers */
    for (i = 0; i < NX_DRIVER_TX_DESCRIPTORS; i++)
        nx_driver_information.nx_driver_information_transmit_packets[i] = NX_NULL;

    /* Configure RX buffer offset (2 bytes for IP header alignment) */
    rx_offset = XEmacPs_ReadReg(g_emac_ps.Config.BaseAddress, XEMACPS_NWCFG_OFFSET);
    rx_offset &= ~(3 << 14);
    rx_offset |= (2 << 14);
    XEmacPs_WriteReg(g_emac_ps.Config.BaseAddress, XEMACPS_NWCFG_OFFSET, rx_offset);

    /* Create TX BD ring */
    XEmacPs_BdClear(&BdTemplate);
    XEmacPs_BdSetStatus(&BdTemplate, XEMACPS_TXBUF_USED_MASK);
    TxDesc_ptr = (XEmacPs_Bd *)TX_BD_LIST_START_ADDRESS;

    ret = XEmacPs_BdRingCreate(&XEmacPs_GetTxRing(&g_emac_ps),
                                (u32)TxDesc_ptr, (u32)TxDesc_ptr,
                                XEMACPS_BD_ALIGNMENT, NX_DRIVER_TX_DESCRIPTORS);
    if (ret != XST_SUCCESS) return NX_DRIVER_ERROR;

    ret = XEmacPs_BdRingClone(&XEmacPs_GetTxRing(&g_emac_ps), &BdTemplate, XEMACPS_SEND);
    if (ret != XST_SUCCESS) return NX_DRIVER_ERROR;

    /* Set MDIO divisor (already configured by PHY driver, but ensure it's set) */
    XEmacPs_SetMdioDivisor(&g_emac_ps, MDC_DIV_224);

    /* PHY auto-negotiation is handled by rtl8211e_driver_init() in board_init.
     * Read the negotiated speed from the PHY driver. */
    {
        struct device *phy_dev = device_find("rtl8211e0");
        if (phy_dev) {
            uint32_t speed = 1000;
            device_ioctl(phy_dev, PHY_IOCTL_GET_SPEED, &speed);
            XEmacPs_SetOperatingSpeed(&g_emac_ps, speed);
            xil_printf("nx_driver: MAC speed set to %dMbps\r\n", speed);
        } else {
            xil_printf("nx_driver: WARNING - PHY driver not found, using 1000Mbps\r\n");
            XEmacPs_SetOperatingSpeed(&g_emac_ps, 1000);
        }
    }

    /* Register GIC interrupt handler (but don't enable yet - done in _enable) */
    ret = XScuGic_Connect(&xInterruptController, EMACPS_IRPT_INTR,
                           (Xil_InterruptHandler)XEmacPs_IntrHandler,
                           (void *)&g_emac_ps);
    if (ret != XST_SUCCESS) {
        xil_printf("nx_driver: GIC connect failed\r\n");
        return NX_DRIVER_ERROR;
    }

    /* Enable TX/RX checksum offload and promiscuous mode */
    XEmacPs_SetOptions(&g_emac_ps,
                       XEMACPS_RX_CHKSUM_ENABLE_OPTION |
                       XEMACPS_TX_CHKSUM_ENABLE_OPTION |
                       XEMACPS_MULTICAST_OPTION |
                       XEMACPS_PROMISC_OPTION);

    /* Configure DMA */
    XEmacPs_WriteReg(g_emac_ps.Config.BaseAddress, XEMACPS_DMACR_OFFSET, 0x00190F10);

    driver_req_ptr->nx_ip_driver_status = NX_SUCCESS;
    return NX_SUCCESS;
}

static UINT _nx_driver_hardware_enable(NX_IP_DRIVER *driver_req_ptr)
{
    (void)driver_req_ptr;

    XScuGic_Enable(&xInterruptController, XPS_GEM0_INT_ID);
    XEmacPs_Start(&g_emac_ps);

    xil_printf("nx_driver: hardware enabled\r\n");
    return NX_SUCCESS;
}

static UINT _nx_driver_hardware_disable(NX_IP_DRIVER *driver_req_ptr)
{
    (void)driver_req_ptr;
    return NX_SUCCESS;
}

static UINT _nx_driver_hardware_packet_send(NX_PACKET *packet_ptr)
{
    XEmacPs    *instance_ptr = &g_emac_ps;
    XEmacPs_Bd *bd_ptr;
    NX_PACKET  *tmp_ptr;
    ULONG       cur_idx;
    int         ret;
    TX_INTERRUPT_SAVE_AREA

    TX_DISABLE

    ret = XEmacPs_BdRingAlloc(&XEmacPs_GetTxRing(instance_ptr), 1, &bd_ptr);
    if (ret != XST_SUCCESS) {
        TX_RESTORE
        return NX_DRIVER_ERROR;
    }

    for (tmp_ptr = packet_ptr; tmp_ptr != NX_NULL; tmp_ptr = tmp_ptr->nx_packet_next) {
        cur_idx = nx_driver_information.nx_driver_information_transmit_current_index;

        if (nx_driver_information.nx_driver_information_transmit_packets[cur_idx] == NX_NULL) {
            nx_driver_information.nx_driver_information_transmit_packets[cur_idx] = tmp_ptr;
        } else {
            xil_printf("nx_driver: TX descriptor leak at index %lu\r\n", cur_idx);
        }

        nx_driver_information.nx_driver_information_transmit_current_index =
            (cur_idx + 1) & (NX_DRIVER_TX_DESCRIPTORS - 1);

        Xil_DCacheFlushRange((u32)tmp_ptr->nx_packet_prepend_ptr,
                             tmp_ptr->nx_packet_length);

        XEmacPs_BdSetAddressTx(bd_ptr, tmp_ptr->nx_packet_prepend_ptr);
        XEmacPs_BdSetLength(bd_ptr, tmp_ptr->nx_packet_length);
        XEmacPs_BdClearLast(bd_ptr);
        dmb();
        dsb();
    }

    XEmacPs_BdSetLast(bd_ptr);
    dmb();
    dsb();

    ret = XEmacPs_BdRingToHw(&XEmacPs_GetTxRing(instance_ptr), 1, bd_ptr);
    if (ret != XST_SUCCESS) {
        TX_RESTORE
        return NX_DRIVER_ERROR;
    }

    XEmacPs_BdClearTxUsed(bd_ptr);
    XEmacPs_Transmit(instance_ptr);
    dmb();
    dsb();

    TX_RESTORE
    return NX_SUCCESS;
}

static UINT _nx_driver_hardware_multicast_join(NX_IP_DRIVER *driver_req_ptr)
{
    (void)driver_req_ptr;
    return NX_SUCCESS;
}

static UINT _nx_driver_hardware_multicast_leave(NX_IP_DRIVER *driver_req_ptr)
{
    (void)driver_req_ptr;
    return NX_SUCCESS;
}

static UINT _nx_driver_hardware_get_status(NX_IP_DRIVER *driver_req_ptr)
{
    (void)driver_req_ptr;
    return NX_SUCCESS;
}

static int _nx_driver_hardware_packet_transmitted(VOID)
{
    XEmacPs_Bd *bd_ptr;
    XEmacPs_Bd *curr_bd;
    int         num_bds;
    XEmacPs    *instance_ptr = &g_emac_ps;
    ULONG      *value;
    int         i;
    UINT        ret;
    NX_PACKET  *packet_ptr;
    ULONG       idx = nx_driver_information.nx_driver_information_transmit_release_index;
    TX_INTERRUPT_SAVE_AREA

    TX_DISABLE

    num_bds = XEmacPs_BdRingFromHwTx(&XEmacPs_GetTxRing(instance_ptr),
                                       NX_DRIVER_TX_DESCRIPTORS, &bd_ptr);
    if (num_bds == 0) {
        TX_RESTORE
        return 0;
    }

    curr_bd = bd_ptr;
    for (i = 0; i < num_bds; i++) {
        value = (ULONG *)curr_bd;

        packet_ptr = nx_driver_information.nx_driver_information_transmit_packets[idx];
        nx_driver_information.nx_driver_information_transmit_packets[idx] = NX_NULL;

        idx = (idx + 1) & (NX_DRIVER_TX_DESCRIPTORS - 1);
        nx_driver_information.nx_driver_information_transmit_release_index = idx;

        if (packet_ptr != NX_NULL) {
            NX_DRIVER_ETHERNET_HEADER_REMOVE(packet_ptr);
            nx_packet_transmit_release(packet_ptr);
        }

        /* Reset BD */
        *value = 0;
        value++;
        if (XEMACPS_BD_TO_INDEX(&XEmacPs_GetTxRing(instance_ptr), curr_bd) ==
            (NX_DRIVER_TX_DESCRIPTORS - 1))
            *value = 0xC0000000;
        else
            *value = 0x80000000;

        curr_bd = XEmacPs_BdRingNext(&XEmacPs_GetTxRing(instance_ptr), curr_bd);
        dmb();
        dsb();
    }

    ret = XEmacPs_BdRingFree(&XEmacPs_GetTxRing(instance_ptr), num_bds, bd_ptr);
    Xil_AssertNonvoid(ret == XST_SUCCESS);

    TX_RESTORE
    return 0;
}

static int _nx_driver_hardware_packet_received(VOID)
{
    XEmacPs_Bd *bd_ptr;
    XEmacPs_Bd *new_bd;
    int         num_rx;
    NX_PACKET  *packet_ptr;
    int         length;
    XEmacPs    *instance_ptr = &g_emac_ps;
    ULONG       first_idx = nx_driver_information.nx_driver_information_receive_current_index;
    int         status;

    while (1) {
        num_rx = XEmacPs_BdRingFromHwRx(&XEmacPs_GetRxRing(instance_ptr), 1, &bd_ptr);
        if (num_rx == 0)
            return 0;

        length = XEmacPs_BdGetLength(bd_ptr);

        packet_ptr = nx_driver_information.nx_driver_information_receive_packets[first_idx];
        packet_ptr->nx_packet_length      = length;
        packet_ptr->nx_packet_prepend_ptr += 2;
        packet_ptr->nx_packet_append_ptr   =
            packet_ptr->nx_packet_prepend_ptr + length;
        packet_ptr->nx_packet_ip_interface =
            nx_driver_information.nx_driver_information_interface;

        _nx_driver_transfer_to_netx(
            nx_driver_information.nx_driver_information_ip_ptr, packet_ptr);

        XEmacPs_BdRingFree(&XEmacPs_GetRxRing(instance_ptr), 1, bd_ptr);

        /* Allocate a new packet for this BD slot */
        status = nx_packet_allocate(
            nx_driver_information.nx_driver_information_packet_pool_ptr,
            &packet_ptr, NX_RECEIVE_PACKET, NX_NO_WAIT);

        if (status == NX_SUCCESS) {
            ULONG *temp;
            int    bd_index;

            status = XEmacPs_BdRingAlloc(&XEmacPs_GetRxRing(instance_ptr), 1, &new_bd);
            Xil_AssertNonvoid(status == XST_SUCCESS);

            bd_index = XEMACPS_BD_TO_INDEX(&XEmacPs_GetRxRing(instance_ptr), new_bd);
            temp = (ULONG *)new_bd;
            if (bd_index == (NX_DRIVER_RX_DESCRIPTORS - 1))
                *temp = 2;
            else
                *temp = 0;
            temp++;
            *temp = 0;

            XEmacPs_BdSetAddressRx(new_bd, packet_ptr->nx_packet_prepend_ptr);

            status = XEmacPs_BdRingToHw(&XEmacPs_GetRxRing(instance_ptr), 1, new_bd);
            if (status != XST_SUCCESS)
                xil_printf("nx_driver: RX BD to HW failed: %d\r\n", status);

            nx_driver_information.nx_driver_information_receive_packets[first_idx] = packet_ptr;

            Xil_AssertNonvoid(status == XST_SUCCESS);

            Xil_DCacheInvalidateRange((u32)packet_ptr->nx_packet_prepend_ptr,
                                      NX_DRIVER_ETHERNET_MTU);
        } else {
            xil_printf("nx_driver: RX alloc fail: 0x%x\r\n", status);
        }

        first_idx = (first_idx + 1) & (NX_DRIVER_RX_DESCRIPTORS - 1);
        nx_driver_information.nx_driver_information_receive_current_index = first_idx;
    }

    return 0;
}

/* ================================================================== */
/*  ISR Callbacks                                                      */
/* ================================================================== */

static VOID nx_driver_zynq_ethernet_rx_isr(void *handle)
{
    ULONG deferred_events;

    (void)handle;

    deferred_events = nx_driver_information.nx_driver_information_deferred_events;
    nx_driver_information.nx_driver_information_deferred_events |= NX_DRIVER_DEFERRED_PACKET_RECEIVED;

    if (!deferred_events)
        _nx_ip_driver_deferred_processing(nx_driver_information.nx_driver_information_ip_ptr);
}

static VOID nx_driver_zynq_ethernet_tx_isr(void *handle)
{
    ULONG deferred_events;

    (void)handle;

    deferred_events = nx_driver_information.nx_driver_information_deferred_events;
    nx_driver_information.nx_driver_information_deferred_events |= NX_DRIVER_DEFERRED_PACKET_TRANSMITTED;

    if (!deferred_events)
        _nx_ip_driver_deferred_processing(nx_driver_information.nx_driver_information_ip_ptr);
}

static VOID nx_driver_zynq_ethernet_error_isr(void *handle, UCHAR direction, ULONG error_code)
{
    XEmacPs *instance_ptr = &g_emac_ps;
    UINT     txsr;

    (void)handle;

    nx_driver_information.nx_driver_information_deferred_events |= NX_DRIVER_DEFERRED_DRIVER_ERROR;

    txsr = XEmacPs_ReadReg(instance_ptr->Config.BaseAddress, XEMACPS_TXSR_OFFSET);

    xil_printf("nx_driver: ERROR dir=%d code=0x%lx txsr=0x%x\r\n",
               direction, error_code, txsr);

    /* Some error codes are actually TX completion notifications */
    if (direction == XEMACPS_SEND && (error_code == 0 || error_code == 8)) {
        nx_driver_zynq_ethernet_tx_isr(handle);
    }
}
