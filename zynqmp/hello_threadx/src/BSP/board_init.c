#include "driver/device_core.h"
#include "board_init.h"
#include "xgpiops.h"
#include "xuartps.h"
#include "xsdps.h"
#include "xparameters.h"
#include "xil_printf.h"

/* Shared GPIO PS instance, referenced by led_driver.c and key_driver.c */
XGpioPs g_gpio_ps;

/* Shared UART PS instance, referenced by uart_driver.c */
XUartPs g_uart_ps;

/* Shared SD PS instance, referenced by sd_driver.c */
XSdPs   g_sd_ps;

/* Driver entry functions */
extern void led_driver_init(void);
extern void key_driver_init(void);
extern void uart_driver_init(void);
extern void sd_driver_init(void);

static void gpio_ps_init(void)
{
	XGpioPs_Config *cfg = XGpioPs_LookupConfig(XPAR_PSU_GPIO_0_DEVICE_ID);
	XGpioPs_CfgInitialize(&g_gpio_ps, cfg, cfg->BaseAddr);
}

static void uart_ps_init(void)
{
	XUartPs_Config *cfg = XUartPs_LookupConfig(XPAR_XUARTPS_0_DEVICE_ID);
	XUartPs_CfgInitialize(&g_uart_ps, cfg, cfg->BaseAddress);
}

static void sd_ps_init(void)
{
	s32 ret;
	XSdPs_Config *cfg = XSdPs_LookupConfig(XPAR_PSU_SD_1_DEVICE_ID);
	XSdPs_CfgInitialize(&g_sd_ps, cfg, cfg->BaseAddress);
	ret = XSdPs_CardInitialize(&g_sd_ps);
	xil_printf("SD card init: %s (sectors=%lu)\r\n",
		   ret == XST_SUCCESS ? "OK" : "FAIL", g_sd_ps.SectorCount);
}

void board_init()
{
	gpio_ps_init();
	uart_ps_init();
	sd_ps_init();
	led_driver_init();
	key_driver_init();
	uart_driver_init();
	sd_driver_init();
}
