#include "driver/device_core.h"
#include "board_init.h"
#include "xgpiops.h"

/* Shared GPIO PS instance, referenced by led_driver.c and key_driver.c */
XGpioPs g_gpio_ps;

/* Driver entry functions */
extern void led_driver_init(void);
extern void key_driver_init(void);

static void gpio_ps_init(void)
{
	XGpioPs_Config *cfg = XGpioPs_LookupConfig(XPAR_PS7_GPIO_0_DEVICE_ID);
	XGpioPs_CfgInitialize(&g_gpio_ps, cfg, cfg->BaseAddr);
}

void board_init()
{
	gpio_ps_init();
	led_driver_init();
	key_driver_init();
}
