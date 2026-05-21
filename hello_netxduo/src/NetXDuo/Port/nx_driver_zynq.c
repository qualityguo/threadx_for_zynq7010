/*
 * nx_driver_zynq.c - NetX Duo Ethernet driver for Xilinx Zynq-7000 GEM
 *
 * All hardware operations are performed through the driver layer
 * (gem_driver) via device_find("gem0") + device_ioctl().
 * No Xilinx APIs are used directly in this file.
 *
 * Driver structure:
 *   nx_driver_zynq()           - entry point, command dispatch
 *   _nx_driver_*()             - generic NetX framework wrappers
 *   _nx_driver_hardware_*()    - hardware implementations via device_ioctl
 */

#define NX_DRIVER_SOURCE

#include "nx_driver_zynq.h"
#include "device_core.h"

/* ARM Cortex-A9 memory barriers */
#define dmb() __asm__ __volatile__ ("dmb" ::: "memory")
#define dsb() __asm__ __volatile__ ("dsb" ::: "memory")

/* ------------------------------------------------------------------ */
/*  Driver information instance                                        */
/* ------------------------------------------------------------------ */

static NX_DRIVER_INFORMATION nx_driver_information;

/* MAC address */
static UCHAR _nx_driver_hardware_address[] = {0x00, 0x11, 0x22, 0x33, 0x44, 0x56};

/* ------------------------------------------------------------------ */
/*  Device access helper                                               */
/* ------------------------------------------------------------------ */

static struct device *get_eth_dev(void)
{
    static struct device *dev;
    if (!dev)
        dev = device_find("gem0");
    return dev;
}

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
static VOID nx_driver_zynq_ethernet_error_isr(void *handle, uint8_t direction, uint32_t error_code);

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
/*  Hardware-Specific Functions (via device_ioctl)                     */
/* ================================================================== */

static UINT _nx_driver_hardware_initialize(NX_IP_DRIVER *driver_req_ptr)
{
    struct device *eth_dev = get_eth_dev();
    UINT        i;
    int         ret;

    nx_driver_information.nx_driver_information_receive_current_index  = 0;
    nx_driver_information.nx_driver_information_transmit_current_index = 0;
    nx_driver_information.nx_driver_information_transmit_release_index = 0;

    if (!eth_dev)
        return NX_DRIVER_ERROR;

    if (nx_driver_information.nx_driver_information_packet_pool_ptr == NULL)
        return NX_DRIVER_ERROR;

    /* Configure MAC address */
    {
        gem_mac_addr_t mac_arg = { .addr = _nx_driver_hardware_address };
        ret = device_ioctl(eth_dev, GEM_IOCTL_SET_MAC_ADDRESS, &mac_arg);
        if (ret != 0)
            return NX_DRIVER_ERROR;
    }

    /* Register ISR callbacks */
    {
        gem_callbacks_t cb_arg = {
            .tx_cb  = (void (*)(void *))nx_driver_zynq_ethernet_tx_isr,
            .rx_cb  = (void (*)(void *))nx_driver_zynq_ethernet_rx_isr,
            .err_cb = (void (*)(void *, uint8_t, uint32_t))nx_driver_zynq_ethernet_error_isr,
            .cb_arg = NULL,
        };
        device_ioctl(eth_dev, GEM_IOCTL_REGISTER_CALLBACKS, &cb_arg);
    }

    /* Create RX BD ring */
    {
        gem_bd_ring_create_t rx_ring = {
            .base_addr = RX_BD_LIST_START_ADDRESS,
            .count     = NX_DRIVER_RX_DESCRIPTORS,
        };
        ret = device_ioctl(eth_dev, GEM_IOCTL_RX_BD_RING_CREATE, &rx_ring);
        if (ret != 0)
            return NX_DRIVER_ERROR;
    }

    /* Allocate RX packets and assign buffer addresses */
    for (i = 0; i < NX_DRIVER_RX_DESCRIPTORS; i++) {
        gem_bd_op_t alloc_arg = { .num = 1 };
        NX_PACKET   *packet_ptr;
        nx_bd_t     *bd;
        gem_bd_op_t  hw_arg;
        gem_cache_op_t cache_arg;

        device_ioctl(eth_dev, GEM_IOCTL_BD_RING_ALLOC_RX, &alloc_arg);
        if (alloc_arg.status != 0)
            return NX_DRIVER_ERROR;

        ret = nx_packet_allocate(
            nx_driver_information.nx_driver_information_packet_pool_ptr,
            &packet_ptr, NX_RECEIVE_PACKET, NX_NO_WAIT);
        if (ret != NX_SUCCESS)
            return NX_DRIVER_ERROR;

        bd = (nx_bd_t *)alloc_arg.bd_ptr;
        NX_BD_SET_ADDR(bd, packet_ptr->nx_packet_prepend_ptr);

        hw_arg.num = 1;
        hw_arg.bd_ptr = alloc_arg.bd_ptr;
        device_ioctl(eth_dev, GEM_IOCTL_BD_RING_TO_HW_RX, &hw_arg);
        if (hw_arg.status != 0)
            return NX_DRIVER_ERROR;

        cache_arg.addr = packet_ptr->nx_packet_prepend_ptr;
        cache_arg.len  = NX_DRIVER_ETHERNET_MTU;
        device_ioctl(eth_dev, GEM_IOCTL_CACHE_INVALIDATE, &cache_arg);

        nx_driver_information.nx_driver_information_receive_packets[i] = packet_ptr;
    }

    /* Initialize TX packet pointers */
    for (i = 0; i < NX_DRIVER_TX_DESCRIPTORS; i++)
        nx_driver_information.nx_driver_information_transmit_packets[i] = NX_NULL;

    /* Set RX buffer offset (2 bytes for IP header alignment) */
    {
        uint32_t v = 2;
        device_ioctl(eth_dev, GEM_IOCTL_SET_RX_OFFSET, &v);
    }

    /* Create TX BD ring */
    {
        gem_bd_ring_create_t tx_ring = {
            .base_addr = TX_BD_LIST_START_ADDRESS,
            .count     = NX_DRIVER_TX_DESCRIPTORS,
        };
        ret = device_ioctl(eth_dev, GEM_IOCTL_TX_BD_RING_CREATE, &tx_ring);
        if (ret != 0)
            return NX_DRIVER_ERROR;
    }

    /* Register GIC interrupt handler (but don't enable yet) */
    device_ioctl(eth_dev, GEM_IOCTL_ENABLE_INTERRUPTS, NULL);

    /* Enable TX/RX checksum offload and promiscuous mode */
    {
        uint32_t opts = NX_GEM_OPT_RX_CHKSUM | NX_GEM_OPT_TX_CHKSUM |
                        NX_GEM_OPT_MULTICAST  | NX_GEM_OPT_PROMISC;
        device_ioctl(eth_dev, GEM_IOCTL_SET_OPTIONS, &opts);
    }

    /* Configure DMA */
    {
        uint32_t dma = 0x00190F10;
        device_ioctl(eth_dev, GEM_IOCTL_CONFIGURE_DMA, &dma);
    }

    driver_req_ptr->nx_ip_driver_status = NX_SUCCESS;
    return NX_SUCCESS;
}

static UINT _nx_driver_hardware_enable(NX_IP_DRIVER *driver_req_ptr)
{
    (void)driver_req_ptr;
    struct device *eth_dev = get_eth_dev();

    device_ioctl(eth_dev, GEM_IOCTL_ENABLE, NULL);
    return NX_SUCCESS;
}

static UINT _nx_driver_hardware_disable(NX_IP_DRIVER *driver_req_ptr)
{
    (void)driver_req_ptr;
    struct device *eth_dev = get_eth_dev();

    device_ioctl(eth_dev, GEM_IOCTL_DISABLE, NULL);
    return NX_SUCCESS;
}

static UINT _nx_driver_hardware_packet_send(NX_PACKET *packet_ptr)
{
    struct device *eth_dev = get_eth_dev();
    NX_PACKET  *tmp_ptr;
    ULONG       cur_idx;
    int         ret;
    TX_INTERRUPT_SAVE_AREA

    TX_DISABLE

    {
        gem_bd_op_t alloc_arg = { .num = 1 };
        device_ioctl(eth_dev, GEM_IOCTL_BD_RING_ALLOC_TX, &alloc_arg);
        if (alloc_arg.status != 0) {
            TX_RESTORE
            return NX_DRIVER_ERROR;
        }

        nx_bd_t *bd = (nx_bd_t *)alloc_arg.bd_ptr;

        for (tmp_ptr = packet_ptr; tmp_ptr != NX_NULL; tmp_ptr = tmp_ptr->nx_packet_next) {
            cur_idx = nx_driver_information.nx_driver_information_transmit_current_index;

            if (nx_driver_information.nx_driver_information_transmit_packets[cur_idx] == NX_NULL) {
                nx_driver_information.nx_driver_information_transmit_packets[cur_idx] = tmp_ptr;
            }

            nx_driver_information.nx_driver_information_transmit_current_index =
                (cur_idx + 1) & (NX_DRIVER_TX_DESCRIPTORS - 1);

            {
                gem_cache_op_t cache_arg = {
                    .addr = tmp_ptr->nx_packet_prepend_ptr,
                    .len  = tmp_ptr->nx_packet_length,
                };
                device_ioctl(eth_dev, GEM_IOCTL_CACHE_FLUSH, &cache_arg);
            }

            NX_BD_SET_ADDR(bd, tmp_ptr->nx_packet_prepend_ptr);
            NX_BD_SET_LENGTH(bd, tmp_ptr->nx_packet_length);
            NX_BD_CLEAR_LAST(bd);
            dmb();
            dsb();
        }

        NX_BD_SET_LAST(bd);
        dmb();
        dsb();

        {
            gem_bd_op_t hw_arg = { .num = 1, .bd_ptr = (uint32_t *)bd };
            device_ioctl(eth_dev, GEM_IOCTL_BD_RING_TO_HW_TX, &hw_arg);
            if (hw_arg.status != 0) {
                TX_RESTORE
                return NX_DRIVER_ERROR;
            }
        }

        NX_BD_CLEAR_TX_USED(bd);
        device_ioctl(eth_dev, GEM_IOCTL_START_TX, NULL);
        dmb();
        dsb();
    }

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
    struct device *eth_dev = get_eth_dev();
    uint32_t   num_bds;
    uint32_t  *curr_bd_mem;
    nx_bd_t   *curr_bd;
    int        i;
    ULONG      idx = nx_driver_information.nx_driver_information_transmit_release_index;
    TX_INTERRUPT_SAVE_AREA

    TX_DISABLE

    {
        gem_bd_from_hw_t from_arg = { .limit = NX_DRIVER_TX_DESCRIPTORS };
        device_ioctl(eth_dev, GEM_IOCTL_BD_RING_FROM_HW_TX, &from_arg);
        num_bds = from_arg.count;
        if (num_bds == 0) {
            TX_RESTORE
            return 0;
        }

        curr_bd_mem = from_arg.bd_ptr;
        curr_bd = (nx_bd_t *)curr_bd_mem;

        for (i = 0; i < (int)num_bds; i++) {
            NX_PACKET *packet_ptr;
            uint32_t  *value;

            packet_ptr = nx_driver_information.nx_driver_information_transmit_packets[idx];
            nx_driver_information.nx_driver_information_transmit_packets[idx] = NX_NULL;

            idx = (idx + 1) & (NX_DRIVER_TX_DESCRIPTORS - 1);
            nx_driver_information.nx_driver_information_transmit_release_index = idx;

            if (packet_ptr != NX_NULL) {
                NX_DRIVER_ETHERNET_HEADER_REMOVE(packet_ptr);
                nx_packet_transmit_release(packet_ptr);
            }

            /* Reset BD */
            value = (uint32_t *)curr_bd;
            *value = 0;
            value++;
            {
                gem_bd_nav_t nav_arg = { .bd_ptr = (uint32_t *)curr_bd };
                device_ioctl(eth_dev, GEM_IOCTL_BD_TO_INDEX_TX, &nav_arg);
                if (nav_arg.index == (NX_DRIVER_TX_DESCRIPTORS - 1))
                    *value = 0xC0000000;
                else
                    *value = 0x80000000;
            }

            {
                gem_bd_nav_t nav_arg = { .bd_ptr = (uint32_t *)curr_bd };
                device_ioctl(eth_dev, GEM_IOCTL_BD_RING_NEXT_TX, &nav_arg);
                curr_bd = (nx_bd_t *)nav_arg.next_ptr;
            }
            dmb();
            dsb();
        }

        {
            gem_bd_op_t free_arg = { .num = num_bds, .bd_ptr = curr_bd_mem };
            device_ioctl(eth_dev, GEM_IOCTL_BD_RING_FREE_TX, &free_arg);
        }
    }

    TX_RESTORE
    return 0;
}

static int _nx_driver_hardware_packet_received(VOID)
{
    struct device *eth_dev = get_eth_dev();
    NX_PACKET  *packet_ptr;
    int         length;
    ULONG       first_idx = nx_driver_information.nx_driver_information_receive_current_index;

    while (1) {
        gem_bd_from_hw_t from_arg = { .limit = 1 };
        uint32_t num_rx;
        nx_bd_t *bd;

        device_ioctl(eth_dev, GEM_IOCTL_BD_RING_FROM_HW_RX, &from_arg);
        num_rx = from_arg.count;
        if (num_rx == 0)
            return 0;

        bd = (nx_bd_t *)from_arg.bd_ptr;
        length = NX_BD_GET_LENGTH(bd);

        packet_ptr = nx_driver_information.nx_driver_information_receive_packets[first_idx];
        packet_ptr->nx_packet_length      = length;
        packet_ptr->nx_packet_prepend_ptr += 2;
        packet_ptr->nx_packet_append_ptr   =
            packet_ptr->nx_packet_prepend_ptr + length;
        packet_ptr->nx_packet_ip_interface =
            nx_driver_information.nx_driver_information_interface;

        _nx_driver_transfer_to_netx(
            nx_driver_information.nx_driver_information_ip_ptr, packet_ptr);

        {
            gem_bd_op_t free_arg = { .num = 1, .bd_ptr = (uint32_t *)bd };
            device_ioctl(eth_dev, GEM_IOCTL_BD_RING_FREE_RX, &free_arg);
        }

        /* Allocate a new packet for this BD slot */
        {
            int status = nx_packet_allocate(
                nx_driver_information.nx_driver_information_packet_pool_ptr,
                &packet_ptr, NX_RECEIVE_PACKET, NX_NO_WAIT);

            if (status == NX_SUCCESS) {
                gem_bd_op_t alloc_arg2 = { .num = 1 };
                gem_bd_nav_t nav_arg;
                nx_bd_t *new_bd;
                int bd_index;
                uint32_t *temp;

                device_ioctl(eth_dev, GEM_IOCTL_BD_RING_ALLOC_RX, &alloc_arg2);
                if (alloc_arg2.status != 0) {
                    /* Should not happen */
                }

                new_bd = (nx_bd_t *)alloc_arg2.bd_ptr;

                nav_arg.bd_ptr = (uint32_t *)new_bd;
                device_ioctl(eth_dev, GEM_IOCTL_BD_TO_INDEX_RX, &nav_arg);
                bd_index = (int)nav_arg.index;

                temp = (uint32_t *)new_bd;
                if (bd_index == (NX_DRIVER_RX_DESCRIPTORS - 1))
                    *temp = 2;
                else
                    *temp = 0;
                temp++;
                *temp = 0;

                NX_BD_SET_ADDR(new_bd, packet_ptr->nx_packet_prepend_ptr);

                {
                    gem_bd_op_t hw_arg = { .num = 1, .bd_ptr = (uint32_t *)new_bd };
                    device_ioctl(eth_dev, GEM_IOCTL_BD_RING_TO_HW_RX, &hw_arg);
                }

                nx_driver_information.nx_driver_information_receive_packets[first_idx] = packet_ptr;

                {
                    gem_cache_op_t cache_arg = {
                        .addr = packet_ptr->nx_packet_prepend_ptr,
                        .len  = NX_DRIVER_ETHERNET_MTU,
                    };
                    device_ioctl(eth_dev, GEM_IOCTL_CACHE_INVALIDATE, &cache_arg);
                }
            }
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

static VOID nx_driver_zynq_ethernet_error_isr(void *handle, uint8_t direction, uint32_t error_code)
{
    struct device *eth_dev = get_eth_dev();
    uint32_t txsr;

    (void)handle;

    nx_driver_information.nx_driver_information_deferred_events |= NX_DRIVER_DEFERRED_DRIVER_ERROR;

    device_ioctl(eth_dev, GEM_IOCTL_READ_TX_STATUS, &txsr);

    /* Some error codes are actually TX completion notifications */
    if (direction == 1 && (error_code == 0 || error_code == 8)) {
        nx_driver_zynq_ethernet_tx_isr(handle);
    }
}
