/*
 * sd_driver.c - SD card block device driver
 *
 * SD0: PS SDIO controller, XSdPs instance 0
 *
 * Block device accessed via ioctl (sector read/write).
 * device_read / device_write are not applicable.
 */

#include "device_core.h"
#include "ioctl_cmd.h"
#include "xsdps.h"

extern XSdPs g_sd_ps;

/* ------------------------------------------------------------------ */
/*                          SD private data                            */
/* ------------------------------------------------------------------ */
struct sd_priv {
	XSdPs *sd_ps;
};

/* ------------------------------------------------------------------ */
/*                          SD operations                              */
/* ------------------------------------------------------------------ */
static int sd_init(struct device *dev)
{
	(void)dev;
	/* Controller already initialized in board_init.c */
	return 0;
}

static int sd_ioctl(struct device *dev, int cmd, void *arg)
{
	struct sd_priv *p = (struct sd_priv *)dev->priv;
	struct sd_rw_args *rw;
	s32 ret;

	switch (cmd) {
	case SD_IOCTL_READ_SECTORS:
		if (!arg)
			return -1;
		rw = (struct sd_rw_args *)arg;
		ret = XSdPs_ReadPolled(p->sd_ps, rw->sector, rw->count, (u8 *)rw->buf);
		return (ret == XST_SUCCESS) ? 0 : -1;

	case SD_IOCTL_WRITE_SECTORS:
		if (!arg)
			return -1;
		rw = (struct sd_rw_args *)arg;
		ret = XSdPs_WritePolled(p->sd_ps, rw->sector, rw->count, (u8 *)rw->buf);
		return (ret == XST_SUCCESS) ? 0 : -1;

	case SD_IOCTL_GET_SECTOR_COUNT:
		if (!arg)
			return -1;
		*(uint32_t *)arg = p->sd_ps->SectorCount;
		return 0;

	default:
		return -1;
	}
}

/* ------------------------------------------------------------------ */
/*                          SD ops instance                            */
/* ------------------------------------------------------------------ */
static const struct device_ops sd_ops = {
	.init  = sd_init,
	.read  = NULL,
	.write = NULL,
	.ioctl = sd_ioctl,
};

/* ------------------------------------------------------------------ */
/*                        SD device instance                           */
/* ------------------------------------------------------------------ */
static struct sd_priv sd0_priv = { .sd_ps = &g_sd_ps };
static struct device g_sd0 = {
	.name   = "sd0",
	.ops    = &sd_ops,
	.priv   = &sd0_priv,
	.notify = NULL,
};

/* ------------------------------------------------------------------ */
/*                     Driver entry for board_init                     */
/* ------------------------------------------------------------------ */
void sd_driver_init(void)
{
	device_register(&g_sd0);
	device_init(&g_sd0);
}
