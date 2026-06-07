/*
 * key_driver.c - KEY driver, interrupt mode with notify callback
 *
 * KEY0: BANK501 MIO50  (Bank 1, bit 18)
 * KEY1: BANK501 MIO51  (Bank 1, bit 19)
 *
 * Interrupt flow:
 *   GPIO Bank1 ISR -> key_bank_isr() -> dev->notify(dev, pin)
 *   Application registers notify callback via DEV_IOCTL_SET_NOTIFY
 *
 * Driver has NO OS dependency — synchronization is owned by application.
 */

#include "device_core.h"
#include "ioctl_cmd.h"
#include "xgpiops.h"
#include "xscugic.h"
#include "xparameters_ps.h"

extern XGpioPs  g_gpio_ps;
extern XScuGic  xInterruptController;

/* ------------------------------------------------------------------ */
/*                           KEY private data                          */
/* ------------------------------------------------------------------ */
struct key_priv {
	XGpioPs	*gpio;
	uint32_t pin;
};

/* ------------------------------------------------------------------ */
/*                          KEY operations                             */
/* ------------------------------------------------------------------ */
static uint8_t key_hw_read(struct key_priv *p)
{
	return XGpioPs_ReadPin(p->gpio, p->pin) ? 0U : 1U;
}

static int key_init(struct device *dev)
{
	struct key_priv *p = (struct key_priv *)dev->priv;

	XGpioPs_SetDirectionPin(p->gpio, p->pin, 0U);
	XGpioPs_SetIntrTypePin(p->gpio, p->pin, XGPIOPS_IRQ_TYPE_EDGE_FALLING);
	XGpioPs_IntrClearPin(p->gpio, p->pin);
	XGpioPs_IntrEnablePin(p->gpio, p->pin);
	return 0;
}

static int key_read(struct device *dev, void *buf, size_t len)
{
	if (!buf || len < 1)
		return -1;
	*(uint8_t *)buf = key_hw_read((struct key_priv *)dev->priv);
	return 1;
}

static int key_ioctl(struct device *dev, int cmd, void *arg)
{
	switch (cmd) {
	case DEV_IOCTL_RESET:
		break;
	case KEY_IOCTL_GET_STATE:
		if (!arg)
			return -1;
		*(uint8_t *)arg = key_hw_read((struct key_priv *)dev->priv);
		break;
	case DEV_IOCTL_SET_NOTIFY:
		dev->notify = (device_notify_t)arg;
		break;
	default:
		return -1;
	}
	return 0;
}

/* ------------------------------------------------------------------ */
/*                          KEY ops instance                           */
/* ------------------------------------------------------------------ */
static const struct device_ops key_ops = {
	.init  = key_init,
	.read  = key_read,
	.write = NULL,
	.ioctl = key_ioctl,
};

/* ------------------------------------------------------------------ */
/*                      KEY device instances                           */
/* ------------------------------------------------------------------ */
static struct key_priv key0_priv = { .gpio = &g_gpio_ps, .pin = 50 };
static struct device g_key0 = {
	.name   = "key0",
	.ops    = &key_ops,
	.priv   = &key0_priv,
	.notify = NULL,
};

static struct key_priv key1_priv = { .gpio = &g_gpio_ps, .pin = 51 };
static struct device g_key1 = {
	.name   = "key1",
	.ops    = &key_ops,
	.priv   = &key1_priv,
	.notify = NULL,
};

/* ------------------------------------------------------------------ */
/*                     GPIO Bank interrupt handler                     */
/*                                                                      */
/*  XGpioPs_IntrHandler -> key_bank_isr(CallBackRef, Bank, Status)      */
/*  Bank 1 covers MIO 32-53; pin N -> bit (N - 32) in Status mask.     */
/* ------------------------------------------------------------------ */
static void key_bank_isr(void *CallBackRef, u32 Bank, u32 Status)
{
	(void)CallBackRef;

	if (Bank != XGPIOPS_BANK1)
		return;

	if (Status & (1U << (50 - 32))) {
		XGpioPs_IntrClearPin(&g_gpio_ps, 50);
		if (g_key0.notify)
			g_key0.notify(&g_key0, 50);
	}
	if (Status & (1U << (51 - 32))) {
		XGpioPs_IntrClearPin(&g_gpio_ps, 51);
		if (g_key1.notify)
			g_key1.notify(&g_key1, 51);
	}
}

/* ------------------------------------------------------------------ */
/*       Connect GPIO interrupt to GIC (call after devices init)       */
/* ------------------------------------------------------------------ */
static void key_intr_setup(void)
{
	XGpioPs_SetCallbackHandler(&g_gpio_ps, &g_gpio_ps, key_bank_isr);

	XScuGic_Connect(&xInterruptController, XPS_GPIO_INT_ID,
			(Xil_InterruptHandler)XGpioPs_IntrHandler, &g_gpio_ps);

	XScuGic_Enable(&xInterruptController, XPS_GPIO_INT_ID);
}

/* ------------------------------------------------------------------ */
/*                     Driver entry for board_init                     */
/* ------------------------------------------------------------------ */
void key_driver_init(void)
{
	device_register(&g_key0);
	device_register(&g_key1);
	device_init(&g_key0);
	device_init(&g_key1);
	key_intr_setup();
}
