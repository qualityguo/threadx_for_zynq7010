#ifndef __IOCTL_CMD_H_
#define __IOCTL_CMD_H_

#include <stdint.h>

/* ------------------------------------------------------------------ */
/*                              Generic                                */
/* ------------------------------------------------------------------ */
#define DEV_IOCTL_RESET					0x0001
#define DEV_IOCTL_SET_NOTIFY			0x0002

/* ------------------------------------------------------------------ */
/*                              LED (0x01XX)                           */
/* ------------------------------------------------------------------ */
#define LED_IOCTL_SET_ON				0x0101
#define LED_IOCTL_SET_OFF				0x0102
#define LED_IOCTL_TOGGLE				0x0103

/* ------------------------------------------------------------------ */
/*                              KEY (0x02XX)                           */
/* ------------------------------------------------------------------ */
#define KEY_IOCTL_GET_STATE				0x0201

/* ------------------------------------------------------------------ */
/*                              UART (0x03XX)                          */
/* ------------------------------------------------------------------ */
#define UART_IOCTL_SET_BAUD_RATE		0x0301
#define UART_IOCTL_SET_FORMAT			0x0302
#define UART_IOCTL_GET_RX_COUNT			0x0303
#define UART_IOCTL_GET_TX_FREE			0x0304

/* ------------------------------------------------------------------ */
/*                            SD card (0x04XX)                         */
/* ------------------------------------------------------------------ */
struct sd_rw_args {
	uint32_t sector;
	uint32_t count;
	void    *buf;
};

#define SD_IOCTL_READ_SECTORS			0x0401
#define SD_IOCTL_WRITE_SECTORS			0x0402
#define SD_IOCTL_GET_SECTOR_COUNT		0x0403

/* ------------------------------------------------------------------ */
/*                          TTC timer (0x05XX)                         */
/* ------------------------------------------------------------------ */
#define TTC_IOCTL_START					0x0501
#define TTC_IOCTL_STOP					0x0502
#define TTC_IOCTL_SET_INTERVAL			0x0503
#define TTC_IOCTL_SET_PRESCALER			0x0504
#define TTC_IOCTL_GET_COUNTER			0x0505

/* ------------------------------------------------------------------ */
/*                           PHY (0x06XX)                              */
/* ------------------------------------------------------------------ */
#define PHY_IOCTL_GET_SPEED				0x0601
#define PHY_IOCTL_GET_DUPLEX				0x0602
#define PHY_IOCTL_GET_LINK				0x0603
#define PHY_IOCTL_RESTART_AUTONEG			0x0604

/* ------------------------------------------------------------------ */
/*                      GEM Ethernet MAC (0x07XX)                      */
/* ------------------------------------------------------------------ */

/* GEM configuration */
#define GEM_IOCTL_SET_MAC_ADDRESS			0x0701
#define GEM_IOCTL_SET_SPEED				0x0702
#define GEM_IOCTL_SET_MDIO_DIVISOR			0x0703
#define GEM_IOCTL_SET_RX_OFFSET				0x0704
#define GEM_IOCTL_SET_OPTIONS				0x0705
#define GEM_IOCTL_CONFIGURE_DMA				0x0706

/* BD Ring management */
#define GEM_IOCTL_RX_BD_RING_CREATE			0x0710
#define GEM_IOCTL_TX_BD_RING_CREATE			0x0711
#define GEM_IOCTL_BD_RING_ALLOC_RX			0x0712
#define GEM_IOCTL_BD_RING_ALLOC_TX			0x0713
#define GEM_IOCTL_BD_RING_TO_HW_RX			0x0714
#define GEM_IOCTL_BD_RING_TO_HW_TX			0x0715
#define GEM_IOCTL_BD_RING_FROM_HW_TX			0x0716
#define GEM_IOCTL_BD_RING_FROM_HW_RX			0x0717
#define GEM_IOCTL_BD_RING_FREE_RX			0x0718
#define GEM_IOCTL_BD_RING_FREE_TX			0x0719
#define GEM_IOCTL_BD_RING_NEXT_TX			0x071A
#define GEM_IOCTL_BD_RING_NEXT_RX			0x071B
#define GEM_IOCTL_BD_TO_INDEX_TX			0x071C
#define GEM_IOCTL_BD_TO_INDEX_RX			0x071D

/* GEM control */
#define GEM_IOCTL_REGISTER_CALLBACKS			0x0720
#define GEM_IOCTL_ENABLE_INTERRUPTS			0x0721
#define GEM_IOCTL_ENABLE				0x0722
#define GEM_IOCTL_DISABLE				0x0723
#define GEM_IOCTL_START_TX				0x0724

/* Status read */
#define GEM_IOCTL_READ_TX_STATUS			0x0730

/* Cache operations */
#define GEM_IOCTL_CACHE_FLUSH				0x0740
#define GEM_IOCTL_CACHE_INVALIDATE			0x0741

/* MDIO bus access */
#define GEM_IOCTL_MDIO_READ				0x0750
#define GEM_IOCTL_MDIO_WRITE				0x0751

/* ---- GEM ioctl argument structures ---- */

typedef struct {
	uint32_t base_addr;
	uint32_t count;
} gem_bd_ring_create_t;

typedef struct {
	uint32_t  num;
	uint32_t *bd_ptr;
	int       status;
} gem_bd_op_t;

typedef struct {
	uint32_t  limit;
	uint32_t *bd_ptr;
	uint32_t  count;
} gem_bd_from_hw_t;

typedef struct {
	uint32_t *bd_ptr;
	uint32_t *next_ptr;
	uint32_t  index;
} gem_bd_nav_t;

typedef struct {
	const uint8_t *addr;
} gem_mac_addr_t;

typedef struct {
	void (*tx_cb)(void *arg);
	void (*rx_cb)(void *arg);
	void (*err_cb)(void *arg, uint8_t direction, uint32_t error);
	void *cb_arg;
} gem_callbacks_t;

typedef struct {
	void    *addr;
	uint32_t len;
} gem_cache_op_t;

typedef struct {
	uint32_t phy_addr;
	uint32_t reg_addr;
	uint16_t value;
} gem_mdio_xfer_t;

#endif /* __IOCTL_CMD_H_ */
