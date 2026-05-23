/*
 * uart_driver.c - UART PS driver, interrupt-driven RX/TX with ring buffers
 *
 * UART1: PS MIO, XUartPs instance 0
 *
 * RX flow:
 *   UART ISR -> write to rx_ring -> dev->notify(dev, byte_count)
 *   Application waits on semaphore (in notify callback) -> device_read()
 *   reads from rx_ring in task context.
 *
 * TX flow:
 *   device_write() -> write to tx_ring -> kick TX if idle
 *   TX complete ISR -> send next chunk from tx_ring
 *
 * Driver includes ring buffers for streaming data. Notify callback should
 * only release a semaphore -- no data processing in ISR context.
 */

#include "device_core.h"
#include "ioctl_cmd.h"
#include "xuartps.h"
#include "xscugic.h"
#include "xparameters_ps.h"
#include "cb.h"
#include <string.h>

extern XScuGic  xInterruptController;
extern XUartPs  g_uart_ps;

/* ------------------------------------------------------------------ */
/*                          UART private data                          */
/* ------------------------------------------------------------------ */
#define UART_RX_RING_SIZE	256
#define UART_TX_RING_SIZE	256
#define UART_RX_TMP_SIZE	64

struct uart_priv {
	XUartPs		 *uart_ps;
	int			  irq_id;
	/* RX: ISR writes to ring buffer, app reads via device_read */
	uint8_t		  rx_tmp[UART_RX_TMP_SIZE];
	CirCularBuffer_t rx_ring;
	uint8_t		  rx_ring_buf[UART_RX_RING_SIZE];
	/* TX: app writes via device_write, ISR sends from ring buffer */
	CirCularBuffer_t tx_ring;
	uint8_t		  tx_ring_buf[UART_TX_RING_SIZE];
};

/* ------------------------------------------------------------------ */
/*                        TX kick helper                               */
/* ------------------------------------------------------------------ */
static void uart_kick_tx(struct uart_priv *p)
{
	uint8_t tmp[UART_RX_TMP_SIZE];
	uint32_t n = cb_read(&p->tx_ring, tmp, sizeof(tmp));
	if (n > 0)
		XUartPs_Send(p->uart_ps, tmp, n);
}

/* ------------------------------------------------------------------ */
/*                     UART interrupt handler                          */
/* ------------------------------------------------------------------ */
static void uart_ps_isr(void *CallBackRef, u32 Event, u32 EventData)
{
	struct device *dev = (struct device *)CallBackRef;
	struct uart_priv *p = (struct uart_priv *)dev->priv;

	if (Event == XUARTPS_EVENT_RECV_DATA) {
		cb_write(&p->rx_ring, p->rx_tmp, EventData);
		XUartPs_Recv(p->uart_ps, p->rx_tmp, UART_RX_TMP_SIZE);
		if (dev->notify)
			dev->notify(dev, EventData);
		return;
	}

	if (Event == XUARTPS_EVENT_RECV_TOUT) {
		if (EventData > 0) {
			cb_write(&p->rx_ring, p->rx_tmp, EventData);
			if (dev->notify)
				dev->notify(dev, EventData);
		}
		XUartPs_Recv(p->uart_ps, p->rx_tmp, UART_RX_TMP_SIZE);
		return;
	}

	if (Event == XUARTPS_EVENT_SENT_DATA) {
		uart_kick_tx(p);
		return;
	}

	if (Event == XUARTPS_EVENT_RECV_ERROR ||
	    Event == XUARTPS_EVENT_PARE_FRAME_BRKE ||
	    Event == XUARTPS_EVENT_RECV_ORERR) {
		XUartPs_Recv(p->uart_ps, p->rx_tmp, UART_RX_TMP_SIZE);
	}
}

/* ------------------------------------------------------------------ */
/*                          UART operations                            */
/* ------------------------------------------------------------------ */
static int uart_init(struct device *dev)
{
	struct uart_priv *p = (struct uart_priv *)dev->priv;
	u32 IntrMask;

	cb_init(&p->rx_ring, p->rx_ring_buf, UART_RX_RING_SIZE);
	cb_init(&p->tx_ring, p->tx_ring_buf, UART_TX_RING_SIZE);

	XUartPs_SetOperMode(p->uart_ps, XUARTPS_OPER_MODE_NORMAL);

	XUartPsFormat fmt = {
		.BaudRate = 115200,
		.DataBits = XUARTPS_FORMAT_8_BITS,
		.Parity   = XUARTPS_FORMAT_NO_PARITY,
		.StopBits = XUARTPS_FORMAT_1_STOP_BIT,
	};
	XUartPs_SetDataFormat(p->uart_ps, &fmt);

	XUartPs_SetFifoThreshold(p->uart_ps, 32);
	XUartPs_SetRecvTimeout(p->uart_ps, 8);

	XUartPs_SetHandler(p->uart_ps, uart_ps_isr, dev);

	XScuGic_SetPriorityTriggerType(&xInterruptController, p->irq_id,
				       0xA0, 0x03);
	XScuGic_Connect(&xInterruptController, p->irq_id,
			(Xil_InterruptHandler)XUartPs_InterruptHandler,
			p->uart_ps);
	XScuGic_Enable(&xInterruptController, p->irq_id);

	IntrMask = XUARTPS_IXR_TOUT | XUARTPS_IXR_PARITY | XUARTPS_IXR_FRAMING |
		   XUARTPS_IXR_OVER | XUARTPS_IXR_TXEMPTY | XUARTPS_IXR_RXFULL |
		   XUARTPS_IXR_RXOVR;
	XUartPs_SetInterruptMask(p->uart_ps, IntrMask);

	XUartPs_Recv(p->uart_ps, p->rx_tmp, UART_RX_TMP_SIZE);

	return 0;
}

static int uart_read(struct device *dev, void *buf, size_t len)
{
	struct uart_priv *p = (struct uart_priv *)dev->priv;

	if (!buf || len == 0)
		return -1;

	return (int)cb_read(&p->rx_ring, buf, (uint32_t)len);
}

static int uart_write(struct device *dev, const void *buf, size_t len)
{
	struct uart_priv *p = (struct uart_priv *)dev->priv;
	uint32_t written;

	if (!buf || len == 0)
		return -1;

	written = cb_write(&p->tx_ring, buf, (uint32_t)len);
	if (!XUartPs_IsSending(p->uart_ps))
		uart_kick_tx(p);
	return (int)written;
}

static int uart_ioctl(struct device *dev, int cmd, void *arg)
{
	struct uart_priv *p = (struct uart_priv *)dev->priv;

	switch (cmd) {
	case DEV_IOCTL_RESET:
		cb_reset(&p->rx_ring);
		cb_reset(&p->tx_ring);
		break;
	case DEV_IOCTL_SET_NOTIFY:
		dev->notify = (device_notify_t)arg;
		break;
	case UART_IOCTL_SET_BAUD_RATE:
		if (!arg)
			return -1;
		XUartPs_SetBaudRate(p->uart_ps, *(u32 *)arg);
		break;
	case UART_IOCTL_SET_FORMAT:
		if (!arg)
			return -1;
		XUartPs_SetDataFormat(p->uart_ps, (XUartPsFormat *)arg);
		break;
	case UART_IOCTL_GET_RX_COUNT:
		if (!arg)
			return -1;
		*(uint32_t *)arg = cb_get_full(&p->rx_ring);
		break;
	case UART_IOCTL_GET_TX_FREE:
		if (!arg)
			return -1;
		*(uint32_t *)arg = cb_get_free(&p->tx_ring);
		break;
	default:
		return -1;
	}
	return 0;
}

/* ------------------------------------------------------------------ */
/*                          UART ops instance                          */
/* ------------------------------------------------------------------ */
static const struct device_ops uart_ops = {
	.init  = uart_init,
	.read  = uart_read,
	.write = uart_write,
	.ioctl = uart_ioctl,
};

/* ------------------------------------------------------------------ */
/*                        UART device instance                         */
/* ------------------------------------------------------------------ */
static struct uart_priv uart1_priv = {
	.uart_ps = &g_uart_ps,
	.irq_id  = XPS_UART1_INT_ID,
};
static struct device g_uart1 = {
	.name   = "uart1",
	.ops    = &uart_ops,
	.priv   = &uart1_priv,
	.notify = NULL,
};

/* ------------------------------------------------------------------ */
/*                     Driver entry for board_init                     */
/* ------------------------------------------------------------------ */
void uart_driver_init(void)
{
	device_register(&g_uart1);
	device_init(&g_uart1);
}
