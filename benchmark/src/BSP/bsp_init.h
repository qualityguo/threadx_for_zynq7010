#ifndef __BSP_INIT_H_
#define __BSP_INIT_H_

#include "xparameters.h"
#include "xil_types.h"
#include "xscugic.h"

#define GTC_CLK_FREQ_HZ		(XPAR_CPU_CORTEXA9_0_CPU_CLK_FREQ_HZ / 2)
#define INTC_DEVICE_ID      XPAR_SCUGIC_SINGLE_DEVICE_ID
extern XScuGic xInterruptController;


void bsp_init();


#endif /* __BSP_INIT_H_ */
