#include "driver/device_core.h"

extern struct device g_gpio0;


void board_init()
{
	/* ×¢²á */
	device_register(&g_gpio0);


	/* ³õÊ¼»¯ */
	device_init(&g_gpio0);
}
