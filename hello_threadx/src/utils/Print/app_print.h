#ifndef __APP_PRINT_H_
#define __APP_PRINT_H_

#define USE_CORESIGHTPS_PRINT
//#define USE_UARTPS0_PRINT
//#define USE_UARTPS1_PRINT

#if (defined(USE_CORESIGHTPS_PRINT) + defined(USE_UARTPS0_PRINT) + defined(USE_UARTPS1_PRINT)) != 1
#error "Exactly one of USE_CORESIGHTPS_PRINT, USE_UARTPS0_PRINT, or USE_UARTPS1_PRINT must be defined"
#endif

/* 线程安全的 */
void app_printf(const char *ctrl1, ...);


#endif /* __APP_PRINT_H_ */
