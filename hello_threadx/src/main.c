#include "xil_printf.h"
#include "includes.h"

/*
*********************************************************************************************************
*                                 任务优先级，数值越小优先级越高
*********************************************************************************************************
*/
#define  APP_CFG_TASK_START_PRIO                     	2u
#define	 APP_CFG_TASK_EVENT_SEND_PRIO					10u					// 优先级更低的发事件标志
#define  APP_CFG_TASK_EVENT_RECV_PRIO					9u					// 优先级更高的收事件标志

/*
*********************************************************************************************************
*                                    任务栈大小，单位字节
*********************************************************************************************************
*/
#define  APP_CFG_TASK_START_STK_SIZE                    4096u
#define  APP_CFG_TASK_EVENT_SEND_STK_SIZE               4096u
#define  APP_CFG_TASK_EVENT_RECV_STK_SIZE               4096u

/*
*********************************************************************************************************
*                                       静态全局变量
*********************************************************************************************************
*/
static  TX_THREAD   AppTaskStartTCB;
static  uint64_t    AppTaskStartStk[APP_CFG_TASK_START_STK_SIZE/8];
static  TX_THREAD   AppTaskEventSendTCB;
static  uint64_t    AppTaskEventSendStk[APP_CFG_TASK_EVENT_SEND_STK_SIZE/8];
static  TX_THREAD   AppTaskEventRecvTCB;
static  uint64_t    AppTaskEventRecvStk[APP_CFG_TASK_EVENT_RECV_STK_SIZE/8];

/*
*********************************************************************************************************
*                                      函数声明
*********************************************************************************************************
*/
static  void  AppTaskStart          (ULONG thread_input);
static  void  AppTaskEventSend      (ULONG thread_input);
static  void  AppTaskEventRecv      (ULONG thread_input);
static  void  AppTaskPrint          (ULONG thread_input);
static  void  AppTaskCreate 		(void);
static  void  AppObjCreate 			(void);
static  void  App_Printf 			(const char *fmt, ...);
static 	void  DispTaskInfo			(void);

/*
*******************************************************************************************************
*                               		宏
*******************************************************************************************************
*/
#define	EVENT_FLAG_A				(1<<0)
#define	EVENT_FLAG_B				(1<<1)
#define	EVENT_FLAG_C				(1<<2)
#define EVENT_FLAG_ABC				(EVENT_FLAG_A|EVENT_FLAG_B|EVENT_FLAG_C)

/*
*******************************************************************************************************
*                               变量
*******************************************************************************************************
*/
static 	TX_MUTEX 				AppPrintfSemp;			/* 用于printf互斥 */
static 	TX_EVENT_FLAGS_GROUP 	my_event_group;			/* 用于测试事件标志 */
volatile double 				OSCPUUsage;       	   	/* CPU百分比 */
static  ULONG 					event_flags_value;		/* 事件标志暂存 */

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
	App_Printf("Hello Threadx\n\r");

	bsp_init();

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

	/* 创建任务 */
    AppTaskCreate();

	/* 创建任务间通信机制 */
	AppObjCreate();

	/* 计算CPU利用率 */
	IdleTime = _tx_execution_idle_time_total;
	TolTime = _tx_execution_thread_time_total + _tx_execution_isr_time_total + _tx_execution_idle_time_total;

    while (1)
	{
    	/* CPU利用率统计 */
    	uiCount++;
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
*	函 数 名: AppTaskPrint
*	功能说明: 启动任务。
*	形    参: thread_input 是在创建该任务时传递的形参
*	返 回 值: 无
	优 先 级: 3
*********************************************************************************************************
*/
static  void  AppTaskPrint          (ULONG thread_input)
{
	(void)thread_input;

	while(1)
	{
		DispTaskInfo();
		tx_thread_sleep(1);
	}
}

/*
*********************************************************************************************************
*	函 数 名: AppTaskEventSend
*	功能说明: 设置三个事件标志然后自己挂起
*	形    参: thread_input 是在创建该任务时传递的形参
*	返 回 值: 无
	优 先 级: 10
*********************************************************************************************************
*/
static  void  AppTaskEventSend          (ULONG thread_input)
{
	(void)thread_input;
	UINT status;
	int step = 0;

	while(1)
	{
		App_Printf("生产者步骤:%d\n",step);
		switch(step)
		{
			case 0:{
				App_Printf("设置事件标志A\n");
				status = tx_event_flags_set(&my_event_group, EVENT_FLAG_A, TX_OR);
				step = 1;
				break;
			}
			case 1:{
				App_Printf("设置事件标志B\n");
				status = tx_event_flags_set(&my_event_group, EVENT_FLAG_B, TX_OR);
				step = 2;
				break;
			}
			case 2:{
				App_Printf("设置事件标志C\n");
				status = tx_event_flags_set(&my_event_group, EVENT_FLAG_C, TX_OR);
				step = 0;
				break;
			}
		}
		/* 延时2s */
		tx_thread_sleep(200);

	}
}

/*
*********************************************************************************************************
*	函 数 名: AppTaskEventRecv
*	功能说明: 等待三个事件标志然后自己挂起
*	形    参: thread_input 是在创建该任务时传递的形参
*	返 回 值: 无
	优 先 级: 9
*********************************************************************************************************
*/
static  void  AppTaskEventRecv          (ULONG thread_input)
{
	(void)thread_input;
	UINT status;

	while(1)
	{
		App_Printf("消费者:等待ABD全部设置\n");
		status =  tx_event_flags_get(&my_event_group, EVENT_FLAG_ABC, TX_AND_CLEAR, &event_flags_value, TX_WAIT_FOREVER);
		App_Printf("消费者:ABD已经全部设置\n");
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
	/**************创建事件标志发送任务*********************/
	tx_thread_create(&AppTaskEventSendTCB,              /* 任务控制块地址 */
					  "App Task Event Send",            /* 任务名 */
					  AppTaskEventSend,                 /* 启动任务函数地址 */
					  0,                             	/* 传递给任务的参数 */
					  &AppTaskEventSendStk[0],          /* 堆栈基地址 */
					  APP_CFG_TASK_EVENT_SEND_STK_SIZE, /* 堆栈空间大小 */
					  APP_CFG_TASK_EVENT_SEND_PRIO,     /* 任务优先级*/
					  APP_CFG_TASK_EVENT_SEND_PRIO,     /* 任务抢占阀值 */
					  TX_NO_TIME_SLICE,               	/* 不开启时间片 */
					  TX_AUTO_START);                 	/* 创建后立即启动 */
	/**************创建事件标志接收任务*********************/
	tx_thread_create(&AppTaskEventRecvTCB,              /* 任务控制块地址 */
					  "App Task Event Recv",            /* 任务名 */
					  AppTaskEventRecv,                 /* 启动任务函数地址 */
					  0,                             	/* 传递给任务的参数 */
					  &AppTaskEventRecvStk[0],          /* 堆栈基地址 */
					  APP_CFG_TASK_EVENT_RECV_STK_SIZE, /* 堆栈空间大小 */
					  APP_CFG_TASK_EVENT_RECV_PRIO,     /* 任务优先级*/
					  APP_CFG_TASK_EVENT_RECV_PRIO,     /* 任务抢占阀值 */
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
	/* 创建事件标志组 */
	tx_event_flags_create(&my_event_group, "my_event_group");
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
