/*
 * fx_zynq_sdio_driver.c - FileX port layer for ZYNQ SD card
 *
 * Thin adapter between FileX media driver interface and the
 * board-level SD block device driver (sd_driver.c / "sd0").
 *
 * Controller init is done in board_init.c; this layer only
 * dispatches FileX requests to device_ioctl.
 */

#include "fx_zynq_sdio_driver.h"
#include "device_core.h"
#include "ioctl_cmd.h"
#include "xil_printf.h"

extern UINT _fx_partition_offset_calculate(void *partition_sector, UINT partition,
                                           ULONG *partition_start, ULONG *partition_size);

static struct device *sd_dev;

static int sd_read_sectors(ULONG sector, UINT count, void *buf)
{
	struct sd_rw_args args = { .sector = sector, .count = count, .buf = buf };
	int ret = device_ioctl(sd_dev, SD_IOCTL_READ_SECTORS, &args);
	if (ret != 0)
		xil_printf("SD read fail: sector=%lu count=%u\r\n", sector, count);
	return ret;
}

static int sd_write_sectors(ULONG sector, UINT count, void *buf)
{
	struct sd_rw_args args = { .sector = sector, .count = count, .buf = buf };
	int ret = device_ioctl(sd_dev, SD_IOCTL_WRITE_SECTORS, &args);
	if (ret != 0)
		xil_printf("SD write fail: sector=%lu count=%u\r\n", sector, count);
	return ret;
}

VOID fx_zynq_sd_driver(FX_MEDIA *media_ptr)
{
	ULONG partition_start;
	ULONG partition_size;
	UINT  status;

	switch (media_ptr->fx_media_driver_request)
	{
	case FX_DRIVER_READ:
		if (sd_read_sectors(media_ptr->fx_media_driver_logical_sector +
				media_ptr->fx_media_hidden_sectors,
				media_ptr->fx_media_driver_sectors,
				media_ptr->fx_media_driver_buffer) != 0) {
			media_ptr->fx_media_driver_status = FX_IO_ERROR;
			break;
		}
		media_ptr->fx_media_driver_status = FX_SUCCESS;
		break;

	case FX_DRIVER_WRITE:
		if (sd_write_sectors(media_ptr->fx_media_driver_logical_sector +
				 media_ptr->fx_media_hidden_sectors,
				 media_ptr->fx_media_driver_sectors,
				 media_ptr->fx_media_driver_buffer) != 0) {
			media_ptr->fx_media_driver_status = FX_IO_ERROR;
			break;
		}
		media_ptr->fx_media_driver_status = FX_SUCCESS;
		break;

	case FX_DRIVER_FLUSH:
	case FX_DRIVER_ABORT:
	case FX_DRIVER_UNINIT:
		media_ptr->fx_media_driver_status = FX_SUCCESS;
		break;

	case FX_DRIVER_INIT:
		sd_dev = device_find("sd0");
		xil_printf("FX INIT: sd_dev=%s\r\n", sd_dev ? "found" : "NOT FOUND");
		media_ptr->fx_media_driver_status =
			sd_dev ? FX_SUCCESS : FX_IO_ERROR;
		break;

	case FX_DRIVER_BOOT_READ:
		if (sd_read_sectors(0, 1, media_ptr->fx_media_driver_buffer) != 0) {
			xil_printf("FX BOOT_READ: sector 0 read failed\r\n");
			media_ptr->fx_media_driver_status = FX_IO_ERROR;
			break;
		}
		media_ptr->fx_media_driver_status = FX_SUCCESS;

		partition_start = 0;
		status = _fx_partition_offset_calculate(
				media_ptr->fx_media_driver_buffer, 0,
				&partition_start, &partition_size);
		xil_printf("FX BOOT_READ: partition calc status=%u, start=%lu\r\n",
			   status, partition_start);
		if (status) {
			media_ptr->fx_media_driver_status = FX_IO_ERROR;
			return;
		}

		if (partition_start) {
			if (sd_read_sectors(partition_start, 1,
					media_ptr->fx_media_driver_buffer) != 0) {
				media_ptr->fx_media_driver_status = FX_IO_ERROR;
				break;
			}
			media_ptr->fx_media_driver_status = FX_SUCCESS;
		}
		break;

	case FX_DRIVER_BOOT_WRITE:
		if (sd_write_sectors(media_ptr->fx_media_hidden_sectors, 1,
					media_ptr->fx_media_driver_buffer) != 0) {
			media_ptr->fx_media_driver_status = FX_IO_ERROR;
			break;
		}
		media_ptr->fx_media_driver_status = FX_SUCCESS;
		break;

	default:
		media_ptr->fx_media_driver_status = FX_IO_ERROR;
		break;
	}
}
