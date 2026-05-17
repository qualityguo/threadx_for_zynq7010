#include "gpio_driver.h"

struct gpio_priv{
	void *handle;				/* ¾ä±ú */
};

static int gpio_init(struct deivce *dev)
{

}

static int gpio_read(struct device *dev, void *buf, size_t len)
{

}

static int gpio_write(struct device *dev, const void *buf, size_t len)
{

}

static int gpio_ioctl(struct device *dev, int cmd, void *arg)
{

}


static struct device_ops gpio_ops = {
	.init 	= gpio_init,
	.read 	= gpio_read,
	.write 	= gpio_write,
	.ioctl 	= gpio_ioctl,
};

static struct gpio_priv gpio0_priv = {
	.handle = NULL,
};

struct device g_gpio0 = {
	.name = "led0",
	.ops = &gpio_ops,
	.priv = &gpio0_priv,
	.notify = NULL,
};
