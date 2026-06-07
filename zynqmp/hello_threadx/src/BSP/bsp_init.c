#include "bsp_init.h"
#include "xtime_l.h"
#include "xpseudo_asm.h"

#define TIMER_CLK_FREQ		XPAR_CPU_CORTEXA53_0_TIMESTAMP_CLK_FREQ
#define TIMER_RELOAD		(TIMER_CLK_FREQ / 100)

XScuGic xInterruptController;
u64 timer_tick_reload = XPAR_CPU_CORTEXA53_0_TIMESTAMP_CLK_FREQ / 100;

static void Global_Timer_Init()
{
	__asm__ __volatile__(
		"MSR CNTFRQ_EL0, %0\n"
		"ISB\n"
		:: "r"((u64)XPAR_CPU_CORTEXA53_0_TIMESTAMP_CLK_FREQ)
	);

	XTime_StartTimer();
}

static void GIC_Init()
{
	XScuGic_Config *intc_cfg;
	intc_cfg = XScuGic_LookupConfig(INTC_DEVICE_ID);
	XScuGic_CfgInitialize(&xInterruptController, intc_cfg, intc_cfg->CpuBaseAddress);
}

static void HeartbeatTimer_Init()
{
	XScuGic_SetPriorityTriggerType(&xInterruptController, 29, 0xA0, 0);
	XScuGic_Enable(&xInterruptController, 29);

	mtcp(CNTPS_TVAL_EL1, (u64)TIMER_RELOAD);
	mtcp(CNTPS_CTL_EL1, (u64)1);
	__asm__ __volatile__("isb" ::: "memory");
}

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

void bsp_init(void)
{
	Global_Timer_Init();
	GIC_Init();
	HeartbeatTimer_Init();
}
