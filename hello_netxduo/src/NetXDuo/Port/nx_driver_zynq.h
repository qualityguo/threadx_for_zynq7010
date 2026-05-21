/*
 * nx_driver_zynq.h - NetX Duo Ethernet driver for Xilinx Zynq-7000
 *
 * This header defines the driver information structure and entry function
 * for the Zynq GEM (Gigabit Ethernet MAC) NetX Duo driver.
 * All hardware operations are performed through the driver layer via
 * device_ioctl — no Xilinx API headers are included here.
 */

#ifndef NX_DRIVER_ZYNQ_H
#define NX_DRIVER_ZYNQ_H

#ifdef __cplusplus
extern "C" {
#endif

#ifndef TX_API_H
#include "tx_api.h"
#endif

#ifndef NX_API_H
#include "nx_api.h"
#endif

#include "ioctl_cmd.h"

/* Compile-time constants for all NetX Ethernet drivers */
#define NX_DRIVER_ETHERNET_IP                   0x0800
#define NX_DRIVER_ETHERNET_IPV6                 0x86dd
#define NX_DRIVER_ETHERNET_ARP                  0x0806
#define NX_DRIVER_ETHERNET_RARP                 0x8035

#define NX_DRIVER_ETHERNET_MTU                  1514
#define NX_DRIVER_ETHERNET_FRAME_SIZE           14

#define NX_DRIVER_DEFERRED_PACKET_RECEIVED      1
#define NX_DRIVER_DEFERRED_DEVICE_RESET         2
#define NX_DRIVER_DEFERRED_PACKET_TRANSMITTED   4
#define NX_DRIVER_DEFERRED_DRIVER_ERROR         8

#define NX_DRIVER_STATE_NOT_INITIALIZED         1
#define NX_DRIVER_STATE_INITIALIZE_FAILED       2
#define NX_DRIVER_STATE_INITIALIZED             3
#define NX_DRIVER_STATE_LINK_ENABLED            4

#define NX_DRIVER_ERROR                         90

#define NX_DRIVER_ETHERNET_HEADER_REMOVE(p) do { \
    (p)->nx_packet_prepend_ptr += NX_DRIVER_ETHERNET_FRAME_SIZE; \
    (p)->nx_packet_length     -= NX_DRIVER_ETHERNET_FRAME_SIZE; \
} while(0)

/* BD ring memory layout - non-cacheable region at 0x0FF00000 */
#define NX_DRIVER_TX_DESCRIPTORS    256
#define NX_DRIVER_RX_DESCRIPTORS    64

#define RX_BD_LIST_START_ADDRESS    0x0FF00000
#define TX_BD_LIST_START_ADDRESS    0x0FF10000

/* GEM option bits (matching XEMACPS_*_OPTION values) */
#define NX_GEM_OPT_RX_CHKSUM        0x00001000U
#define NX_GEM_OPT_TX_CHKSUM        0x00002000U
#define NX_GEM_OPT_MULTICAST        0x00000800U
#define NX_GEM_OPT_PROMISC          0x00000001U

/* BD type — binary compatible with XEmacPs_Bd on Zynq-7000 (2 x uint32_t) */
typedef uint32_t nx_bd_t[2];

/* BD status bit masks */
#define NX_TXBUF_USED       (1U << 31)
#define NX_TXBUF_WRAP       (1U << 30)
#define NX_TXBUF_LAST       (1U << 15)
#define NX_TXBUF_LEN_MASK   0x00003FFFU
#define NX_RXBUF_NEW        (1U << 0)
#define NX_RXBUF_WRAP       (1U << 1)
#define NX_RXBUF_EOF        (1U << 15)
#define NX_RXBUF_SOF        (1U << 14)
#define NX_RXBUF_LEN_MASK   0x00001FFFU

/* BD field access macros — bd is nx_bd_t * (pointer to uint32_t[2]) */
#define NX_BD_CLEAR(bd)             do { (*(bd))[0] = 0; (*(bd))[1] = 0; } while(0)
#define NX_BD_SET_ADDR(bd, a)       ((*(bd))[0] = (uint32_t)(a))
#define NX_BD_GET_ADDR(bd)          ((*(bd))[0])
#define NX_BD_SET_STATUS(bd, v)     ((*(bd))[1] = (v))
#define NX_BD_GET_STATUS(bd)        ((*(bd))[1])
#define NX_BD_SET_LENGTH(bd, l)     ((*(bd))[1] = ((*(bd))[1] & ~NX_TXBUF_LEN_MASK) | ((l) & NX_TXBUF_LEN_MASK))
#define NX_BD_GET_LENGTH(bd)        ((*(bd))[1] & NX_RXBUF_LEN_MASK)
#define NX_BD_SET_LAST(bd)          ((*(bd))[1] |= NX_TXBUF_LAST)
#define NX_BD_CLEAR_LAST(bd)        ((*(bd))[1] &= ~NX_TXBUF_LAST)
#define NX_BD_CLEAR_TX_USED(bd)     ((*(bd))[1] &= ~NX_TXBUF_USED)
#define NX_BD_SET_TX_USED(bd)       ((*(bd))[1] |= NX_TXBUF_USED)

/* Driver information structure - only valid when NX_DRIVER_SOURCE is defined */
#ifdef NX_DRIVER_SOURCE

typedef struct NX_DRIVER_INFORMATION_STRUCT {
    NX_IP           *nx_driver_information_ip_ptr;
    ULONG            nx_driver_information_state;
    NX_PACKET_POOL  *nx_driver_information_packet_pool_ptr;
    NX_INTERFACE    *nx_driver_information_interface;
    ULONG            nx_driver_information_deferred_events;

    UINT             nx_driver_information_receive_current_index;
    UINT             nx_driver_information_transmit_current_index;
    UINT             nx_driver_information_transmit_release_index;

    NX_PACKET       *nx_driver_information_transmit_packets[NX_DRIVER_TX_DESCRIPTORS];
    NX_PACKET       *nx_driver_information_receive_packets[NX_DRIVER_RX_DESCRIPTORS];
} NX_DRIVER_INFORMATION;

#endif /* NX_DRIVER_SOURCE */

/* Global driver entry function */
VOID nx_driver_zynq(NX_IP_DRIVER *driver_req_ptr);

#ifdef __cplusplus
}
#endif

#endif /* NX_DRIVER_ZYNQ_H */
