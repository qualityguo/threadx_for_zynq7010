#include "xil_printf.h"
#include "includes.h"


/*
*********************************************************************************************************
*                                 任务优先级，数值越小优先级越高
*********************************************************************************************************
*/
#define  APP_CFG_TASK_START_PRIO                     	2u
#define	 APP_CFG_TASK_CAL0_PRIO							20u					// 计算任务0
#define  APP_CFG_TASK_CAL1_PRIO							20u					// 计算任务1
#define	 APP_CFG_TASK_PRINT_PRIO						10u					// 打印任务

/*
*********************************************************************************************************
*                                    任务栈大小，单位字节
*********************************************************************************************************
*/
#define  APP_CFG_TASK_START_STK_SIZE                    4096u
#define	 APP_CFG_TASK_CAL0_STK_SIZE						4096u
#define	 APP_CFG_TASK_CAL1_STK_SIZE						4096u
#define  APP_CFG_TASK_PRINT_STK_SIZE					1024u


/*
*********************************************************************************************************
*                                       静态全局变量
*********************************************************************************************************
*/
static  TX_THREAD   AppTaskStartTCB;
static  uint64_t    AppTaskStartStk[APP_CFG_TASK_START_STK_SIZE/8];
static  TX_THREAD   AppTaskCal0TCB;
static  uint64_t    AppTaskCal0Stk[APP_CFG_TASK_CAL0_STK_SIZE/8];
static  TX_THREAD   AppTaskCal1TCB;
static  uint64_t    AppTaskCal1Stk[APP_CFG_TASK_CAL1_STK_SIZE/8];
static  TX_THREAD   AppTaskPrintTCB;
static  uint64_t    AppTaskPrintStk[APP_CFG_TASK_PRINT_STK_SIZE/8];
static  volatile unsigned long   tm_basic_processing_counter0;
static  volatile unsigned long   tm_basic_processing_counter1;
static 	volatile unsigned long   tm_basic_processing_array0[1024];
static	volatile unsigned long   tm_basic_processing_array1[1024];
/*
*********************************************************************************************************
*                                      函数声明
*********************************************************************************************************
*/
static  void  AppTaskStart          (ULONG thread_input);
static	void  AppTaskCAL0			(ULONG thread_input);
static	void  AppTaskCAL1			(ULONG thread_input);
static  void  AppTaskPrint          (ULONG thread_input);
static  void  AppTaskCreate 		(void);
static  void  AppObjCreate 			(void);
static 	void  AppTaskSmpCfg			(void);
static  void  App_Printf 			(const char *fmt, ...);

/*
*******************************************************************************************************
*                               		宏
*******************************************************************************************************
*/
#define TASK_EXC_CORE0				(1<<0)
#define TASK_EXC_CORE1				(1<<1)


/*
*******************************************************************************************************
*                               变量
*******************************************************************************************************
*/
static 	TX_MUTEX 				AppPrintfSemp;			/* 用于printf互斥 */

/*
*********************************************************************************************************
*	函 数 名: main
*	功能说明: 标准c程序入口。
*	形    参: 无
*	返 回 值: 无
*********************************************************************************************************
*/
int main()
{
	xil_printf("Hello Threadx Benchmark for one core!\r\n");

	bsp_init();
	board_init();

	tx_kernel_enter();

	while(1);
}

/*
*********************************************************************************************************
*	函 数 名: tx_application_define
*	功能说明: ThreadX专用的任务创建，通信组件创建函数
*	形    参: first_unused_memory  未使用的地址空间
*	返 回 值: 无
*********************************************************************************************************
*/
void tx_application_define(void *first_unused_memory)
{
	/**************创建启动任务*********************/
	tx_thread_create(&AppTaskStartTCB,              /* 任务控制块地址 */
					   "App Task Start",              /* 任务名 */
					   AppTaskStart,                  /* 启动任务函数地址 */
					   0,                             /* 传递给任务的参数 */
					   &AppTaskStartStk[0],            /* 堆栈基地址 */
					   APP_CFG_TASK_START_STK_SIZE,    /* 堆栈空间大小 */
					   APP_CFG_TASK_START_PRIO,        /* 任务优先级*/
					   APP_CFG_TASK_START_PRIO,        /* 任务抢占阀值 */
					   TX_NO_TIME_SLICE,               /* 不开启时间片 */
					   TX_AUTO_START);                 /* 创建后立即启动 */
}


/*
*********************************************************************************************************
*	函 数 名: AppTaskStart
*	功能说明: 启动任务。
*	形    参: thread_input 是在创建该任务时传递的形参
*	返 回 值: 无
	优 先 级: 2
*********************************************************************************************************
*/

static  void  AppTaskStart (ULONG thread_input)
{
	(void)thread_input;

	/* 创建任务 */
	AppTaskCreate();

	/* 创建任务间通信机制 */
	AppObjCreate();

	/* 创建任务多核特性 */
	AppTaskSmpCfg();

	while(1)
	{
		tx_thread_sleep(1);			// do nothing
	}
}


/*
*********************************************************************************************************
*	函 数 名: AppTaskCAL0
*	功能说明: 计算任务0
*	形    参: thread_input 是在创建该任务时传递的形参
*	返 回 值: 无
	优 先 级: 20
*********************************************************************************************************
*/
static  void  AppTaskCAL0          (ULONG thread_input)
{
	(void)thread_input;

	for(int i = 0; i < 1024; i++)
	{
		tm_basic_processing_array0[i] = 0;
	}
	tm_basic_processing_counter0 = 0;

	while(1)
	{
		for (int i = 0; i < 1024; i++)
		{
			tm_basic_processing_array0[i] =  (tm_basic_processing_array0[i] + tm_basic_processing_counter0) ^ tm_basic_processing_array0[i];
		}
		tm_basic_processing_counter0++;
	}
}

/*
*********************************************************************************************************
*	函 数 名: AppTaskCAL1
*	功能说明: 计算任务1
*	形    参: thread_input 是在创建该任务时传递的形参
*	返 回 值: 无
	优 先 级: 20
*********************************************************************************************************
*/
static  void  AppTaskCAL1          (ULONG thread_input)
{
	(void)thread_input;

	for(int i = 0; i < 1024; i++)
	{
		tm_basic_processing_array1[i] = 0;
	}
	tm_basic_processing_counter1 = 0;

	while(1)
	{
		for (int i = 0; i < 1024; i++)
		{
			tm_basic_processing_array1[i] =  (tm_basic_processing_array1[i] + tm_basic_processing_counter1) ^ tm_basic_processing_array1[i];
		}
		tm_basic_processing_counter1++;
	}
}

/*
*********************************************************************************************************
*	函 数 名: AppTaskPrint
*	功能说明: 打印任务
*	形    参: thread_input 是在创建该任务时传递的形参
*	返 回 值: 无
	优 先 级: 10
*********************************************************************************************************
*/
static  void  AppTaskPrint          (ULONG thread_input)
{
	(void)thread_input;
	unsigned long   last_counter0 = 0;
	unsigned long   last_counter1 = 0;
	unsigned long   relative_time = 0;

	while(1)
	{
		tx_thread_sleep(100*20);			// 20s
		relative_time =  relative_time + 20;
		App_Printf("Relative Time: %lu s\n", relative_time);
		if (tm_basic_processing_counter0 == last_counter0)
		{
			App_Printf("ERROR: Invalid counter value(s). Basic processing thread died!\n");
		}
		App_Printf("Task0 cal count:  %lu\n\n", tm_basic_processing_counter0 - last_counter0);
		if (tm_basic_processing_counter0 == last_counter0)
		{
			App_Printf("ERROR: Invalid counter value(s). Basic processing thread died!\n");
		}
		App_Printf("Task1 cal count:  %lu\n\n", tm_basic_processing_counter1 - last_counter1);
		last_counter0 =  tm_basic_processing_counter0;
		last_counter1 =  tm_basic_processing_counter1;
	}
}


/*
*********************************************************************************************************
*	函 数 名: AppTaskCreate
*	功能说明: 创建应用任务
*	形    参: 无
*	返 回 值: 无
*********************************************************************************************************
*/
static  void  AppTaskCreate (void)
{
	/**************创建CAL0任务*********************/
	tx_thread_create(&AppTaskCal0TCB,	              	/* 任务控制块地址 */
					  "App Task CAL0",		            /* 任务名 */
					  AppTaskCAL0,                 		/* 启动任务函数地址 */
					  0,                             	/* 传递给任务的参数 */
					  &AppTaskCal0Stk[0],		        /* 堆栈基地址 */
					  APP_CFG_TASK_CAL0_STK_SIZE, 		/* 堆栈空间大小 */
					  APP_CFG_TASK_CAL0_PRIO,     		/* 任务优先级*/
					  APP_CFG_TASK_CAL0_PRIO,    	 	/* 任务抢占阀值 */
					  50,               				/* 时间片=50ticks */
					  TX_AUTO_START);                 	/* 创建后立即启动 */
	/**************创建CAL1任务*********************/
	tx_thread_create(&AppTaskCal1TCB,	              	/* 任务控制块地址 */
					  "App Task CAL1",		            /* 任务名 */
					  AppTaskCAL1,                 		/* 启动任务函数地址 */
					  0,                             	/* 传递给任务的参数 */
					  &AppTaskCal1Stk[0],		        /* 堆栈基地址 */
					  APP_CFG_TASK_CAL1_STK_SIZE, 		/* 堆栈空间大小 */
					  APP_CFG_TASK_CAL1_PRIO,     		/* 任务优先级*/
					  APP_CFG_TASK_CAL1_PRIO,    	 	/* 任务抢占阀值 */
					  50,               				/* 时间片=50ticks */
					  TX_AUTO_START);                 	/* 创建后立即启动 */
	/**************创建Print任务*********************/
	tx_thread_create(&AppTaskPrintTCB,              	/* 任务控制块地址 */
					  "App Task Print",		            /* 任务名 */
					  AppTaskPrint,              		/* 启动任务函数地址 */
					  0,                             	/* 传递给任务的参数 */
					  &AppTaskPrintStk[0],	        	/* 堆栈基地址 */
					  APP_CFG_TASK_PRINT_STK_SIZE,		/* 堆栈空间大小 */
					  APP_CFG_TASK_PRINT_PRIO,  		/* 任务优先级*/
					  APP_CFG_TASK_PRINT_PRIO,  	 	/* 任务抢占阀值 */
					  TX_NO_TIME_SLICE,               	/* 不开启时间片 */
					  TX_AUTO_START);                 	/* 创建后立即启动 */
}

/*
*********************************************************************************************************
*	函 数 名: AppObjCreate
*	功能说明: 创建任务通讯
*	形    参: 无
*	返 回 值: 无
*********************************************************************************************************
*/
static  void  AppObjCreate (void)
{
	/* 创建互斥信号量 */
	tx_mutex_create(&AppPrintfSemp,"AppPrintfSemp",TX_NO_INHERIT);
}


/*
*********************************************************************************************************
*	函 数 名: AppTaskSmpCfg
*	功能说明: 配置任务多核依赖
*	形    参: 无
*	返 回 值: 无
*********************************************************************************************************
*/
static  void  AppTaskSmpCfg (void)
{

}


/*
*********************************************************************************************************
*	函 数 名: App_Printf
*	功能说明: 线程安全的printf方式
*	形    参: 同printf的参数。
*             在C中，当无法列出传递函数的所有实参的类型和数目时,可以用省略号指定参数表
*	返 回 值: 无
*********************************************************************************************************
*/
static  void  App_Printf(const char *fmt, ...)
{
    char  buf_str[200 + 1]; /* 特别注意，如果printf的变量较多，注意此局部变量的大小是否够用 */
    va_list   v_args;


    va_start(v_args, fmt);
   (void)vsnprintf((char       *)&buf_str[0],
                   (size_t      ) sizeof(buf_str),
                   (char const *) fmt,
                                  v_args);
    va_end(v_args);

	/* 互斥操作 */
    tx_mutex_get(&AppPrintfSemp, TX_WAIT_FOREVER);

    xil_printf("%s", buf_str);

    tx_mutex_put(&AppPrintfSemp);
}


