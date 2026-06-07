#include "bsp_init.h"
#include "xparameters.h"
#include "xscutimer.h"

#define GTIMER_COUNTER_LO   (*(volatile u32 *)0xF8F00200)
#define GTIMER_COUNTER_HI   (*(volatile u32 *)0xF8F00204)
#define GTIMER_CONTROL      (*(volatile u32 *)0xF8F00208)
#define SCU_RELOAD_CNT		(XPAR_CPU_CORTEXA9_0_CPU_CLK_FREQ_HZ/2/100)			//10ms

XScuGic xInterruptController;
static XScuTimer xScuTimer;

// Global Timer init (free-running, used by execution profile)
static void Global_Timer_Init()
{
	GTIMER_CONTROL    = 0x00;
	GTIMER_COUNTER_LO = 0;
	GTIMER_COUNTER_HI = 0;
	GTIMER_CONTROL    = 0x01;
}

// GIC init (also enables Distributor + CPU Interface + priority mask)
static void GIC_Init()
{
	XScuGic_Config *intc_cfg;
	intc_cfg = XScuGic_LookupConfig(INTC_DEVICE_ID);
	XScuGic_CfgInitialize(&xInterruptController, intc_cfg, intc_cfg->CpuBaseAddress);
}

// Dispatch non-timer IRQs via XScuGic HandlerTable
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

// Private Timer: 3333333 ticks @ 333 MHz = 10 ms period
static void PrivateTimer_Init(void)
{
	XScuTimer_Config *cfg = XScuTimer_LookupConfig(XPAR_XSCUTIMER_0_DEVICE_ID);
	XScuTimer_CfgInitialize(&xScuTimer, cfg, cfg->BaseAddr);

	XScuTimer_LoadTimer(&xScuTimer, SCU_RELOAD_CNT);
	XScuTimer_EnableAutoReload(&xScuTimer);
	XScuTimer_EnableInterrupt(&xScuTimer);
	XScuTimer_Start(&xScuTimer);
}

void bsp_init()
{
	Global_Timer_Init();
	GIC_Init();

	// Enable Private Timer interrupt (PPI 29)
	XScuGic_SetPriorityTriggerType(&xInterruptController, 29, 0, 0);
	XScuGic_Enable(&xInterruptController, 29);

	PrivateTimer_Init();
}
