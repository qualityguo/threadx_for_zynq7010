#include "device_core.h"
#include <string.h>

#define		MAX_DEVICE_NUM		16

static struct device *g_devices[MAX_DEVICE_NUM];
static int g_dev_count = 0;

int device_register(struct device *dev)
{
	if(!dev || g_dev_count >= MAX_DEVICE_NUM)
		return -1;
	g_devices[g_dev_count++] = dev;
	return 0;
}

struct device* device_find(const char *name)
{
	for(int i = 0; i < g_dev_count; i++)
	{
		if(strcmp(name, g_devices[i]->name) == 0)
			return g_devices[i];
	}
	return NULL;
}

int	device_init(struct device *dev)
{
	if(!dev || !dev->ops || !dev->ops->init)
		return -1;
	return dev->ops->init(dev);

}
int device_read(struct device *dev, void *buf, size_t len)
{
	if(!dev || !dev->ops || !dev->ops->read)
		return -1;
	return dev->ops->read(dev, buf, len);
}
int	device_write(struct device *dev, const void *buf, size_t len)
{
	if(!dev || !dev->ops || !dev->ops->write)
		return -1;
	return dev->ops->write(dev, buf, len);
}

int	device_ioctl(struct device *dev, int cmd, void *arg)
{
	if(!dev || !dev->ops || !dev->ops->ioctl)
		return -1;
	return dev->ops->ioctl(dev, cmd, arg);
}
