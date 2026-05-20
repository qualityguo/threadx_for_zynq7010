/*
 * nx_driver_zynq.h - NetX Duo Ethernet driver for Xilinx Zynq-7000
 *
 * This header defines the driver information structure and entry function
 * for the Zynq GEM (Gigabit Ethernet MAC) NetX Duo driver.
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

/* Xilinx Standalone BSP headers */
#include "xemacps.h"
#include "xparameters.h"
#include "xparameters_ps.h"
#include "xil_types.h"
#include "xil_cache.h"

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

#define XEMACPS_BD_TO_INDEX(ringptr, bdptr) \
    (((UINT)(bdptr) - (UINT)(ringptr)->BaseBdAddr) / (ringptr)->Separation)

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
