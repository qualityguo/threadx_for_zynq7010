/*
 * key_driver.c - KEY driver, pressed = active low
 *
 * KEY0: BANK501 MIO50
 * KEY1: BANK501 MIO51
 *
 * Xilinx Pin-level API: SetDirectionPin / ReadPin
 */

#include "device_core.h"
#include "ioctl_cmd.h"
#include "xgpiops.h"

extern XGpioPs g_gpio_ps;

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
	/* Active low: pressed=0, invert to 1=pressed, 0=released */
	return XGpioPs_ReadPin(p->gpio, p->pin) ? 0U : 1U;
}

static int key_init(struct device *dev)
{
	struct key_priv *p = (struct key_priv *)dev->priv;
	XGpioPs_SetDirectionPin(p->gpio, p->pin, 0U);
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
/*                          KEY device instances                       */
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
/*                     Driver entry for board_init                     */
/* ------------------------------------------------------------------ */
void key_driver_init(void)
{
	device_register(&g_key0);
	device_register(&g_key1);
	device_init(&g_key0);
	device_init(&g_key1);
}
