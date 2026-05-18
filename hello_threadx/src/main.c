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
#define	 APP_CFG_TASK_MSG_RECV_PRIO						19u					// 消息队列接收任务
#define  APP_CFG_TASK_SEM_RECV_PRIO						18u					// 信号量接收任务

/*
*********************************************************************************************************
*                                    任务栈大小，单位字节
*********************************************************************************************************
*/
#define  APP_CFG_TASK_START_STK_SIZE                    4096u
#define	 APP_CFG_TASK_LED_STK_SIZE						1024u
#define	 APP_CFG_TASK_KEY_STK_SIZE						4096u
#define  APP_CFG_TASK_MSG_RECV_STK_SIZE					1024u
#define  APP_CFG_TASK_SEM_RECV_STK_SIZE					1024u

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
static  TX_THREAD   AppTaskMsgRecvTCB;
static  uint64_t    AppTaskMsgRecvStk[APP_CFG_TASK_MSG_RECV_STK_SIZE/8];
static  TX_THREAD   AppTaskSemRecvTCB;
static  uint64_t    AppTaskSemRecvStk[APP_CFG_TASK_SEM_RECV_STK_SIZE/8];

/*
*********************************************************************************************************
*                                      函数声明
*********************************************************************************************************
*/
static  void  AppTaskStart          (ULONG thread_input);
static	void  AppTaskLED			(ULONG thread_input);
static	void  AppTaskKEY			(ULONG thread_input);
static  void  AppTaskMsgRecv		(ULONG thread_input);
static  void  AppTaskSemRecv		(ULONG thread_input);
static  void  AppTaskCreate 		(void);
static  void  AppObjCreate 			(void);
static  void  App_Printf 			(const char *fmt, ...);
static 	void  DispTaskInfo			(void);

/*
*******************************************************************************************************
*                               		宏
*******************************************************************************************************
*/
#define EVENT_KEY0					(1<<0)
#define EVENT_KEY1					(1<<1)


/*
*******************************************************************************************************
*                               变量
*******************************************************************************************************
*/
static 	TX_MUTEX 				AppPrintfSemp;			/* 用于printf互斥 */
static  TX_SEMAPHORE			my_semaphore;			/* 任务通知的信号量 */
static  TX_EVENT_FLAGS_GROUP	key_event_group;		/* key中断通知任务事件标志组 */
static  TX_QUEUE				my_msg_queue;			/* 任务通信消息队列 */
volatile double 				OSCPUUsage;       	   	/* CPU百分比 */
static  ULONG 					event_flags_value;		/* 事件标志暂存 */

typedef struct Msg
{
	uint8_t  ucMessageID;
	uint16_t usData[2];
	uint32_t ulData[2];
}MSG_T;

uint32_t MessageQueuesBuf1[10]; /* 定义消息队列缓冲1 */
MSG_T   g_tMsg;                 /* 定义一个结构体用于消息队列数据传递 */


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
	struct device *pled1 = device_find("led1");
	uint8_t val = 1;


	while(1)
	{
		device_write(pled0, &val, 1);
		device_write(pled1, &val, 1);
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
//	tx_semaphore_put(&my_key0_semaphore);
	tx_event_flags_set(&key_event_group, EVENT_KEY0, TX_OR);
}
void key1_cb(struct device *dev, uint32_t event)
{
//	tx_semaphore_put(&my_key1_semaphore);
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
	MSG_T   *ptMsg;

	device_ioctl(pkey0, DEV_IOCTL_SET_NOTIFY, key0_cb);
	device_ioctl(pkey1, DEV_IOCTL_SET_NOTIFY, key1_cb);

	// 初始化消息结构体
	ptMsg = &g_tMsg;
	/* 初始化数组 */
	ptMsg->ucMessageID = 0;
	ptMsg->ulData[0] = 0;
	ptMsg->usData[0] = 0;


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
				ptMsg->ucMessageID++;
				ptMsg->ulData[0]++;;
				ptMsg->usData[0]++;
				// 发送消息
				status = tx_queue_send(&my_msg_queue, &ptMsg, 1000);
				if(status == TX_SUCCESS)
				{
					App_Printf("K2键按下，向MessageQueues发送数据成功\r\n");
				}
				// 释放信号量
				status = tx_semaphore_put(&my_semaphore);
				if(status == TX_SUCCESS)
				{
					App_Printf("K2键按下，释放信号量成功\r\n");
				}
			}

		}

	}
}

/*
*********************************************************************************************************
*	函 数 名: AppTaskMsgRecv
*	功能说明: 接收消息
*	形    参: thread_input 是在创建该任务时传递的形参
*	返 回 值: 无
	优 先 级: 19
*********************************************************************************************************
*/
static  void  AppTaskMsgRecv		(ULONG thread_input)
{
	(void)thread_input;
	MSG_T *ptMsg;
	UINT status;

	while(1)
	{
		status = tx_queue_receive(&my_msg_queue, &ptMsg, TX_WAIT_FOREVER);
		if(status == TX_SUCCESS)
		{
			/* 成功接收，并通过串口将数据打印出来 */
			App_Printf("接收到消息队列数据ptMsg->ucMessageID = %d\r\n", ptMsg->ucMessageID);
			App_Printf("接收到消息队列数据ptMsg->ulData[0] = %d\r\n", ptMsg->ulData[0]);
			App_Printf("接收到消息队列数据ptMsg->usData[0] = %d\r\n", ptMsg->usData[0]);
		}

	}
}

/*
*********************************************************************************************************
*	函 数 名: AppTaskSemRecv
*	功能说明: 接收信号量
*	形    参: thread_input 是在创建该任务时传递的形参
*	返 回 值: 无
	优 先 级: 18
*********************************************************************************************************
*/
static  void  AppTaskSemRecv		(ULONG thread_input)
{
	(void)thread_input;
	UINT status;

	while(1)
	{
		status = tx_semaphore_get(&my_semaphore, TX_WAIT_FOREVER);
		if(status == TX_SUCCESS)
		{
			/* 成功接收，打印消息 */
			App_Printf("接收到同步信号量\r\n");
		}

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
	/**************创建MSG-Recv任务*********************/
	tx_thread_create(&AppTaskMsgRecvTCB,              	/* 任务控制块地址 */
					  "App Task MSGRecv",	            /* 任务名 */
					  AppTaskMsgRecv,              		/* 启动任务函数地址 */
					  0,                             	/* 传递给任务的参数 */
					  &AppTaskMsgRecvStk[0],	        /* 堆栈基地址 */
					  APP_CFG_TASK_MSG_RECV_STK_SIZE,	/* 堆栈空间大小 */
					  APP_CFG_TASK_MSG_RECV_PRIO,  		/* 任务优先级*/
					  APP_CFG_TASK_MSG_RECV_PRIO,  	 	/* 任务抢占阀值 */
					  TX_NO_TIME_SLICE,               	/* 不开启时间片 */
					  TX_AUTO_START);                 	/* 创建后立即启动 */
	/**************创建Sem-Recv任务*********************/
	tx_thread_create(&AppTaskSemRecvTCB,              	/* 任务控制块地址 */
					  "App Task SEMRecv",	            /* 任务名 */
					  AppTaskSemRecv,              		/* 启动任务函数地址 */
					  0,                             	/* 传递给任务的参数 */
					  &AppTaskSemRecvStk[0],	        /* 堆栈基地址 */
					  APP_CFG_TASK_SEM_RECV_STK_SIZE,	/* 堆栈空间大小 */
					  APP_CFG_TASK_SEM_RECV_PRIO,  		/* 任务优先级*/
					  APP_CFG_TASK_SEM_RECV_PRIO,  	 	/* 任务抢占阀值 */
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
	tx_event_flags_create(&key_event_group, "key_event_group");
	/* 创建消息队列 */
	tx_queue_create(&my_msg_queue, "my_msg_queue", 1, MessageQueuesBuf1, sizeof(MessageQueuesBuf1));
	/* 创建信号量 */
	tx_semaphore_create(&my_semaphore, "mysem", 0);		// 初始0
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
