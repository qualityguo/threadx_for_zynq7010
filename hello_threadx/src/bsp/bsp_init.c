#include "bsp_init.h"
#include "xparameters.h"

#define GTIMER_COUNTER_LO   (*(volatile u32 *)0xF8F00200)
#define GTIMER_COUNTER_HI   (*(volatile u32 *)0xF8F00204)
#define GTIMER_CONTROL      (*(volatile u32 *)0xF8F00208)

/* 只使用1个32bit的值用作性能测试 */
static void Global_Timer_Init()
{
	GTIMER_CONTROL    = 0x00;
	GTIMER_COUNTER_LO = 0;
	GTIMER_COUNTER_HI = 0;
	GTIMER_CONTROL    = 0x01;
}

void bsp_init()
{
	Global_Timer_Init();
}
