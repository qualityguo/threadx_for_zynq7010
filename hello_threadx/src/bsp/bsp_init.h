#ifndef __BSP_INIT_H_
#define __BSP_INIT_H_

#include "xparameters.h"
#include "xil_types.h"

#define GTC_CLK_FREQ_HZ		(XPAR_CPU_CORTEXA9_0_CPU_CLK_FREQ_HZ / 2)


void bsp_init();


#endif /* __BSP_INIT_H_ */
