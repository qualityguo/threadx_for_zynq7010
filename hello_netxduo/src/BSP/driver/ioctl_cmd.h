#ifndef __IOCTL_CMD_H_
#define __IOCTL_CMD_H_

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

#endif /* __IOCTL_CMD_H_ */
