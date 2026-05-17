#include "bsp_init.h"
#include "xparameters.h"

#define GTIMER_COUNTER_LO   (*(volatile u32 *)0xF8F00200)
#define GTIMER_COUNTER_HI   (*(volatile u32 *)0xF8F00204)
#define GTIMER_CONTROL      (*(volatile u32 *)0xF8F00208)

XScuGic xInterruptController;

/* 只使用1个32bit的值用作性能测试 */
static void Global_Timer_Init()
{
	GTIMER_CONTROL    = 0x00;
	GTIMER_COUNTER_LO = 0;
	GTIMER_COUNTER_HI = 0;
	GTIMER_CONTROL    = 0x01;
}


/*
 * GIC初始化
 * 主要是需要一个vector的table
 * 其他的操作会和tx_initialize_low_level中的部分重合
 */
static void GIC_Init()
{
	XScuGic_Config *intc_cfg;
	intc_cfg = XScuGic_LookupConfig(INTC_DEVICE_ID);
	XScuGic_CfgInitialize(&xInterruptController, intc_cfg, intc_cfg->CpuBaseAddress);
}

/*
 * 中断分发函数(汇编by_pass_timer_interrupt调用)
 */
void tx_irq_dispatch(unsigned int int_id)
{
	extern XScuGic xInterruptController;

	if(int_id < XSCUGIC_MAX_NUM_INTR_INPUTS)
	{
		XScuGic_VectorTableEntry * entry =
			&xInterruptController.Config->HandlerTable[int_id];
		entry->Handler(entry->CallBackRef);
	}
}



void bsp_init()
{
	Global_Timer_Init();
	GIC_Init();
}
