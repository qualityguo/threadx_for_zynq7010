#ifndef _BSP_INIT_H_
#define _BSP_INIT_H_

#include "xparameters.h"
#include "xil_types.h"
#include "xscugic.h"


#define GTC_CLK_FREQ_HZ		XPAR_CPU_CORTEXA53_0_TIMESTAMP_CLK_FREQ
#define INTC_DEVICE_ID		XPAR_SCUGIC_SINGLE_DEVICE_ID

void bsp_init();

#endif /* _BSP_INIT_H_ */
