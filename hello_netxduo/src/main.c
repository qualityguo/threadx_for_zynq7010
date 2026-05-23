#include "xil_printf.h"
#include "includes.h"

/*
*********************************************************************************************************
*                                 任务优先级，数值越小优先级越高
*********************************************************************************************************
*/
#define  APP_CFG_TASK_START_PRIO                     	2u
#define	 APP_CFG_TASK_LED_PRIO							20u					// LED闪烁任务
#define  APP_CFG_TASK_KEY_PRIO							21u					// KEY任务
#define  APP_CFG_TASK_NetXPro_PRIO                      29u					// 未插入网线前
	/* 在netxduo_udp.c定义，网线插入后从29提升到6 */
	//#define  APP_CFG_TASK_NetXPro_PRIO                      29u
	//#define  APP_CFG_TASK_NetXPro_PRIO1                     6u


/*
*********************************************************************************************************
*                                    任务栈大小，单位字节
*********************************************************************************************************
*/
#define  APP_CFG_TASK_START_STK_SIZE                    4096u
#define	 APP_CFG_TASK_LED_STK_SIZE						1024u
#define	 APP_CFG_TASK_KEY_STK_SIZE						4096u
#define  APP_CFG_TASK_NETXPRO_STK_SIZE                  4096u

/*
*********************************************************************************************************
*                                       静态全局变量
*********************************************************************************************************
*/
static  TX_THREAD   AppTaskStartTCB;
static  uint64_t    AppTaskStartStk[APP_CFG_TASK_START_STK_SIZE/8];
static  TX_THREAD   AppTaskLEDTCB;
static  uint64_t    AppTaskLEDStk[APP_CFG_TASK_LED_STK_SIZE/8];
static  TX_THREAD   AppTaskKEYTCB;
static  uint64_t    AppTaskKEYStk[APP_CFG_TASK_KEY_STK_SIZE/8];
		TX_THREAD   AppTaskNetXProTCB;
static  uint64_t    AppTaskNetXProStk[APP_CFG_TASK_NETXPRO_STK_SIZE/8];
/*
*********************************************************************************************************
*                                      函数声明
*********************************************************************************************************
*/
static  void  AppTaskStart          (ULONG thread_input);
static	void  AppTaskLED			(ULONG thread_input);
static	void  AppTaskKEY			(ULONG thread_input);
static  void  AppTaskNetXPro		(ULONG thread_input);
static  void  AppTaskCreate 		(void);
static  void  AppObjCreate 			(void);
static  void  App_Printf 			(const char *fmt, ...);
static 	void  DispTaskInfo			(void);
	uint32_t  GetRunTime			(void);

/*
*******************************************************************************************************
*                               		宏
*******************************************************************************************************
*/
#define EVENT_KEY0					(1<<0)
#define EVENT_KEY1					(1<<1)
#define TRACE_BUFFER_SIZE 			(500 * 32)  		/* 500个事件的存储空间 */
#define TRACE_MAX_OBJECTS  			20         			/* 最多跟踪的ThreadX对象数量 */

/*
*******************************************************************************************************
*                               变量
*******************************************************************************************************
*/
static 	TX_MUTEX 				AppPrintfSemp;			/* 用于printf互斥 */
static  TX_EVENT_FLAGS_GROUP	key_event_group;		/* key中断通知任务事件标志组 */
volatile double 				OSCPUUsage;       	   	/* CPU百分比 */
static  volatile uint32_t		base_time;				/* 低精度时间计数-10ms */

UCHAR 	g_trace_buffer[TRACE_BUFFER_SIZE];				/* 缓冲区对象 */


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
	xil_printf("Hello NetXDuo!\r\n");

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
	EXECUTION_TIME TolTime, IdleTime, deltaTolTime, deltaIdleTime;
	uint32_t uiCount = 0;
	(void)thread_input;

	/* 先挂起定时器组 */
#ifndef TX_NO_TIMER
	tx_thread_suspend(&_tx_timer_thread);
#endif

	/* 恢复定时器组 */
#ifndef TX_NO_TIMER
	tx_thread_resume(&_tx_timer_thread);
#endif

	base_time = 0;				/* 用作低精度的时间计数-500ms单位 */

	/* 创建任务 */
    AppTaskCreate();

	/* 创建任务间通信机制 */
	AppObjCreate();

	/* 计算CPU利用率 */
	IdleTime = _tx_execution_idle_time_total;
	TolTime = _tx_execution_thread_time_total + _tx_execution_isr_time_total + _tx_execution_idle_time_total;

	/* 启动任务跟踪 */
	tx_trace_enable(&g_trace_buffer, 					/* 存储地址 */
					TRACE_BUFFER_SIZE,					/* buffer大小 */
					TRACE_MAX_OBJECTS);					/* 对象数量 */

    while (1)
	{
    	/* CPU利用率统计 */
    	uiCount++;
    	base_time += 10;
    	if(uiCount == 200)
    	{
    		uiCount = 0;
			deltaIdleTime = _tx_execution_idle_time_total - IdleTime;
			deltaTolTime = _tx_execution_thread_time_total + _tx_execution_isr_time_total + _tx_execution_idle_time_total - TolTime;
			OSCPUUsage = (double)deltaIdleTime/deltaTolTime;
			OSCPUUsage = 100- OSCPUUsage*100;
			IdleTime = _tx_execution_idle_time_total;
			TolTime = _tx_execution_thread_time_total + _tx_execution_isr_time_total + _tx_execution_idle_time_total;
    	}

        tx_thread_sleep(1);
    }
}

/*
*********************************************************************************************************
*	函 数 名: AppTaskLED
*	功能说明: LED闪烁
*	形    参: thread_input 是在创建该任务时传递的形参
*	返 回 值: 无
	优 先 级: 20
*********************************************************************************************************
*/
static  void  AppTaskLED          (ULONG thread_input)
{
	(void)thread_input;
//	UINT status;
	struct device *pled0 = device_find("led0");
//	struct device *pled1 = device_find("led1");
	uint8_t val = 1;


	while(1)
	{
		device_write(pled0, &val, 1);
//		device_write(pled1, &val, 1);
		val = (val==1)? 0 : 1;
		/* 延时2s */
		tx_thread_sleep(200);
	}
}

/*
*********************************************************************************************************
*	函 数 名: AppTaskKEY
*	功能说明: 按键打印信息+发送消息
*	形    参: thread_input 是在创建该任务时传递的形参
*	返 回 值: 无
	优 先 级: 9
*********************************************************************************************************
*/
void key0_cb(struct device *dev, uint32_t event)
{
	tx_event_flags_set(&key_event_group, EVENT_KEY0, TX_OR);
}
void key1_cb(struct device *dev, uint32_t event)
{
	tx_event_flags_set(&key_event_group, EVENT_KEY1, TX_OR);
}


static  void  AppTaskKEY          (ULONG thread_input)
{
	(void)thread_input;
	UINT status;
	struct device *pkey0 = device_find("key0");
	struct device *pkey1 = device_find("key1");
	uint8_t val0 = 0;
//	uint8_t val1 = 0;
	static  ULONG 					event_flags_value;		/* 事件标志暂存 */

	device_ioctl(pkey0, DEV_IOCTL_SET_NOTIFY, key0_cb);
	device_ioctl(pkey1, DEV_IOCTL_SET_NOTIFY, key1_cb);

	while(1)
	{
		// 等待事件标志更新
		status = tx_event_flags_get(&key_event_group,
									EVENT_KEY0|EVENT_KEY1,
									TX_OR_CLEAR, 					// OR+CLEAR
									&event_flags_value,
									TX_WAIT_FOREVER);				// 无线等待

		if(status == TX_SUCCESS)
		{
			if(event_flags_value & EVENT_KEY0)			// key0按下打印信息
			{
				tx_thread_sleep(2);				// 延时20ms-消抖
				device_read(pkey0, &val0, 1);
				if(val0 == 1)
				{
					DispTaskInfo();
				}
			}
			if(event_flags_value & EVENT_KEY1)			// key1按下发送消息
			{
				// do nothing
			}

		}
	}
}

/*
*********************************************************************************************************
*	函 数 名: AppTaskNetXPro
*	功能说明: 消息处理，这里用作NetX网络任务处理
*	形    参: thread_input 是在创建该任务时传递的形参
*	返 回 值: 无
	优 先 级: 上电是29，网线插入后提升至6
*********************************************************************************************************
*/
extern void NetXTest(void);
static void AppTaskNetXPro(ULONG thread_input)
{
    (void)thread_input;

    tx_thread_sleep(1000);
	while(1)
	{
        NetXTest();
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
	/**************创建LED闪烁任务*********************/
	tx_thread_create(&AppTaskLEDTCB,	              	/* 任务控制块地址 */
					  "App Task LED",		            /* 任务名 */
					  AppTaskLED,                 		/* 启动任务函数地址 */
					  0,                             	/* 传递给任务的参数 */
					  &AppTaskLEDStk[0],		        /* 堆栈基地址 */
					  APP_CFG_TASK_LED_STK_SIZE, 		/* 堆栈空间大小 */
					  APP_CFG_TASK_LED_PRIO,     		/* 任务优先级*/
					  APP_CFG_TASK_LED_PRIO,    	 	/* 任务抢占阀值 */
					  TX_NO_TIME_SLICE,               	/* 不开启时间片 */
					  TX_AUTO_START);                 	/* 创建后立即启动 */
	/**************创建KEY任务*********************/
	tx_thread_create(&AppTaskKEYTCB,	              	/* 任务控制块地址 */
					  "App Task KEY",		            /* 任务名 */
					  AppTaskKEY,                 		/* 启动任务函数地址 */
					  0,                             	/* 传递给任务的参数 */
					  &AppTaskKEYStk[0],		        /* 堆栈基地址 */
					  APP_CFG_TASK_KEY_STK_SIZE, 		/* 堆栈空间大小 */
					  APP_CFG_TASK_KEY_PRIO,     		/* 任务优先级*/
					  APP_CFG_TASK_KEY_PRIO,    	 	/* 任务抢占阀值 */
					  TX_NO_TIME_SLICE,               	/* 不开启时间片 */
					  TX_AUTO_START);                 	/* 创建后立即启动 */
	/**************创建NetX处理任务*********************/
    tx_thread_create(&AppTaskNetXProTCB,               	/* 任务控制块地址 */
                      "App NETX Pro",                   /* 任务名 */
                       AppTaskNetXPro,                  /* 启动任务函数地址 */
                       0,                           	/* 传递给任务的参数 */
                       &AppTaskNetXProStk[0],           /* 堆栈基地址 */
                       APP_CFG_TASK_NETXPRO_STK_SIZE,   /* 堆栈空间大小 */
                       APP_CFG_TASK_NetXPro_PRIO,    	/* 任务优先级*/
                       APP_CFG_TASK_NetXPro_PRIO,    	/* 任务抢占阀值 */
                       TX_NO_TIME_SLICE,             	/* 不开启时间片 */
                       TX_AUTO_START);               	/* 创建后立即启动 */
}

/*
*********************************************************************************************************
*	函 数 名: GetRunTime
*	功能说明: 获得运行时间
*	形    参: void
*	返 回 值: uint32_t
*********************************************************************************************************
*/
uint32_t  GetRunTime			(void)
{
	return base_time;
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
	/* 创建事件标志组 */
	tx_event_flags_create(&key_event_group, "key_event_group");

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

    app_printf("%s", buf_str);

    tx_mutex_put(&AppPrintfSemp);
}

/*
*********************************************************************************************************
*	函 数 名: DispTaskInfo
*	功能说明: 将ThreadX任务信息通过串口打印出来
*	形    参：无
*	返 回 值: 无
*********************************************************************************************************
*/
static void DispTaskInfo(void)
{
	TX_THREAD      *p_tcb;	        /* 定义一个任务控制块指针 */

    p_tcb = &AppTaskStartTCB;

	/* 打印标题 */
    App_Printf("===============================================================\r\n");
    App_Printf("CPU利用率 = %5.2f%%\r\n", OSCPUUsage);
    App_Printf("任务执行时间 = %.9fs\r\n", (double)_tx_execution_thread_time_total/GTC_CLK_FREQ_HZ);
    App_Printf("空闲执行时间 = %.9fs\r\n", (double)_tx_execution_idle_time_total/GTC_CLK_FREQ_HZ);
    App_Printf("中断执行时间 = %.9fs\r\n", (double)_tx_execution_isr_time_total/GTC_CLK_FREQ_HZ);
    App_Printf("系统总执行时间 = %.9fs\r\n", (double)(_tx_execution_thread_time_total + \
		                                               _tx_execution_idle_time_total +  \
	                                                   _tx_execution_isr_time_total)/GTC_CLK_FREQ_HZ);
    App_Printf("===============================================================\r\n");
    App_Printf(" 任务优先级 任务栈大小 当前使用栈  最大栈使用   任务名\r\n");
    App_Printf("   Prio     StackSize   CurStack    MaxStack   Taskname\r\n");

	/* 遍历任务控制列表TCB list)，打印所有的任务的优先级和名称 */
	while (p_tcb != (TX_THREAD *)0)
	{

		App_Printf("   %2d        %5d      %5d       %5d      %s\r\n",
                    p_tcb->tx_thread_priority,
                    p_tcb->tx_thread_stack_size,
                    (int)p_tcb->tx_thread_stack_end - (int)p_tcb->tx_thread_stack_ptr,
                    (int)p_tcb->tx_thread_stack_end - (int)p_tcb->tx_thread_stack_highest_ptr,
                    p_tcb->tx_thread_name);


        p_tcb = p_tcb->tx_thread_created_next;

        if(p_tcb == &AppTaskStartTCB) break;
	}
}
