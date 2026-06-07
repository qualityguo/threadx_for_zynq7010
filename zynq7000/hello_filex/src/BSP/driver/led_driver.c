/*
 * led_driver.c - LED driver, active low
 *
 * LED0: BANK500 MIO0
 * LED1: BANK500 MIO13
 *
 * Xilinx Pin-level API: SetDirectionPin / SetOutputEnablePin / WritePin
 */

#include "device_core.h"
#include "ioctl_cmd.h"
#include "xgpiops.h"

extern XGpioPs g_gpio_ps;

/* ------------------------------------------------------------------ */
/*                           LED private data                          */
/* ------------------------------------------------------------------ */
struct led_priv {
	XGpioPs	*gpio;
	uint32_t pin;
	uint8_t  state;
};

/* ------------------------------------------------------------------ */
/*                          LED operations                             */
/* ------------------------------------------------------------------ */
static void led_hw_set(struct led_priv *p, uint8_t on)
{
	/* Active low: on=1 -> write 0, on=0 -> write 1 */
	XGpioPs_WritePin(p->gpio, p->pin, on ? 0U : 1U);
	p->state = on;
}

static int led_init(struct device *dev)
{
	struct led_priv *p = (struct led_priv *)dev->priv;

	XGpioPs_SetDirectionPin(p->gpio, p->pin, 1U);
	XGpioPs_SetOutputEnablePin(p->gpio, p->pin, 1U);
	led_hw_set(p, 0);
	return 0;
}

static int led_read(struct device *dev, void *buf, size_t len)
{
	if (!buf || len < 1)
		return -1;
	*(uint8_t *)buf = ((struct led_priv *)dev->priv)->state;
	return 1;
}

static int led_write(struct device *dev, const void *buf, size_t len)
{
	if (!buf || len < 1)
		return -1;
	led_hw_set((struct led_priv *)dev->priv, *(const uint8_t *)buf ? 1 : 0);
	return 0;
}

static int led_ioctl(struct device *dev, int cmd, void *arg)
{
	struct led_priv *p = (struct led_priv *)dev->priv;

	switch (cmd) {
	case DEV_IOCTL_RESET:
		led_hw_set(p, 0);
		break;
	case LED_IOCTL_SET_ON:
		led_hw_set(p, 1);
		break;
	case LED_IOCTL_SET_OFF:
		led_hw_set(p, 0);
		break;
	case LED_IOCTL_TOGGLE:
		led_hw_set(p, !p->state);
		break;
	default:
		return -1;
	}
	return 0;
}

/* ------------------------------------------------------------------ */
/*                          LED ops instance                           */
/* ------------------------------------------------------------------ */
static const struct device_ops led_ops = {
	.init  = led_init,
	.read  = led_read,
	.write = led_write,
	.ioctl = led_ioctl,
};

/* ------------------------------------------------------------------ */
/*                          LED device instances                       */
/* ------------------------------------------------------------------ */
static struct led_priv led0_priv = { .gpio = &g_gpio_ps, .pin = 0,  .state = 0 };
static struct device g_led0 = {
	.name   = "led0",
	.ops    = &led_ops,
	.priv   = &led0_priv,
	.notify = NULL,
};

static struct led_priv led1_priv = { .gpio = &g_gpio_ps, .pin = 13, .state = 0 };
static struct device g_led1 = {
	.name   = "led1",
	.ops    = &led_ops,
	.priv   = &led1_priv,
	.notify = NULL,
};

/* ------------------------------------------------------------------ */
/*                     Driver entry for board_init                     */
/* ------------------------------------------------------------------ */
void led_driver_init(void)
{
	device_register(&g_led0);
	device_register(&g_led1);
	device_init(&g_led0);
	device_init(&g_led1);
}
