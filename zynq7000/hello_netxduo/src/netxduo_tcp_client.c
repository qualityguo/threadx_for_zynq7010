/*
*********************************************************************************************************
*
*	模块名称 : 网络协议栈测试
*	文件名称 : demo_dm9162_netx.c
*	版    本 : V1.0
*	说    明 : 测试的功能说明
*              1. 默认IP地址192.168.28.245，在本文件配置，用户可根据需要修改。
*              2. 可以在电脑端用网络调试软件创建TCP Client连接此服务器端，端口号1001。
*              3. 实现了一个简单的回环通信，用户使用上位机发送的数据通过板子返回到上位机。
*
*	修改记录 :
*		版本号   日期         作者        说明
*		V1.0    2021-01-30   Eric2013     首发
*
*	Copyright (C), 2018-2030, 安富莱电子 www.armfly.com
*
*********************************************************************************************************
*/
#include "includes.h"



/*
*********************************************************************************************************
*	                                    IP相关
*********************************************************************************************************
*/
/* 远程服务器端口和IP */
#define TCP_SERVER_PORT                 1001
#define TCP_SERVER_ADDRESS              IP_ADDRESS(192, 168, 28, 100)

/* 本地IP地址 */
#define IP_ADDR0                        192
#define IP_ADDR1                        168
#define IP_ADDR2                        28
#define IP_ADDR3                        245

/* 本地端口号 */
#define DEFAULT_PORT                    1000

/*
*********************************************************************************************************
*	                                    NetX任务和通信组件
*********************************************************************************************************
*/
/* 上电先将其设置到低优先级，待网线插入后提升优先级到3 */
#define  APP_CFG_TASK_NETX_PRIO                           28u
#define  APP_CFG_TASK_NETX_PRIO1                           3u
#define  APP_CFG_TASK_NETX_STK_SIZE                     4096u
static   uint64_t  AppTaskNetXStk[APP_CFG_TASK_NETX_STK_SIZE/8];

/* 提升NetX应用任务优先级到6 */
#define  APP_CFG_TASK_NetXPro_PRIO1                        6u

NX_TCP_SOCKET TCPSocket;


/*
*********************************************************************************************************
*	                                    NetX任务和通信组件
*********************************************************************************************************
*/
UCHAR data_buffer1[4096];

NX_PACKET_POOL    pool_1;
NX_IP             ip_1;

/* 数据包内存池 */
#define PACKET_SIZE          1536
#define NX_PACKET_POOL_SIZE  ((PACKET_SIZE + sizeof(NX_PACKET)) * 100)

ULONG  packet_pool_area1[NX_PACKET_POOL_SIZE/4 + 4];

/* ARP缓存 */
ULONG    arp_space_area1[512 / sizeof(ULONG)];
ULONG    error_counter1;

#define PRINT_DATA(addr, port, data)        do {                                            \
                                                  printf("[%lu.%lu.%lu.%lu:%u] -> '%s' \n", \
                                                  (addr >> 24) & 0xff,                      \
                                                  (addr >> 16) & 0xff,                      \
                                                  (addr >> 8) & 0xff,                       \
                                                  (addr & 0xff), port, data);               \
                                               } while(0)

extern TX_THREAD   AppTaskNetXProTCB;
extern TX_THREAD   *netx_thread_ptr;
extern VOID  nx_driver_zynq(NX_IP_DRIVER *driver_req_ptr);


/*
*********************************************************************************************************
*	函 数 名: NetXTest
*	功能说明: TCPnet应用
*	形    参: 无
*	返 回 值: 无
*********************************************************************************************************
*/
void NetXTest1(void)
{
    UINT status;
    UINT ret;
    ULONG socket_state;
    UINT old_priority;

    NX_PACKET *RecPacket;
    NX_PACKET *TraPacket;

    ULONG bytes_read;

	ULONG peer_ip_address;
	ULONG peer_port;

    uint8_t sendbuf[20];
    uint32_t count = 0;


    /* 初始化NetX */
    nx_system_initialize();

    /* 创建内存池 */
    status =  nx_packet_pool_create(&pool_1,                                       /* 内存池控制块 */
                                     "NetX Main Packet Pool",                      /* 内存池名 */
                                     1536,                                         /* 内存池每个数据包大小，单位字节
                                                                                      此值必须至少为 40 个字节，并且还必须可以被 4 整除 */
									 (ULONG*)(((int)packet_pool_area1 + 15) & ~15) ,/* 内存池地址，此地址必须ULONG对齐 */
                                     NX_PACKET_POOL_SIZE);                         /* 内存池大小 */

    /* 检测创建是否失败 */
    if (status) error_counter1++;

    /* 例化IP */
    status = nx_ip_create(&ip_1,                                                   /* IP实例控制块 */
                            "NetX IP Instance 1",                                  /* IP实例名 */
                            IP_ADDRESS(IP_ADDR0, IP_ADDR1, IP_ADDR2, IP_ADDR3),    /* IP地址 */
                            0xFFFFFF00UL,                                          /* 子网掩码 */
                            &pool_1,                                               /* 内存池 */
							nx_driver_zynq,                                   /* 网卡驱动 */
                            (UCHAR*)AppTaskNetXStk,                                /* IP任务栈地址 */
                            sizeof(AppTaskNetXStk),                                /* IP任务栈大小，单位字节 */
                            APP_CFG_TASK_NETX_PRIO);                               /* IP任务优先级 */


    /* 检测创建是否失败 */
    if (status) error_counter1++;

    /* 使能ARP，并提供ARP缓存 */
    status =  nx_arp_enable(&ip_1,                     /* IP实例控制块 */
							(void *)arp_space_area1,    /* ARP缓存地址 */
							sizeof(arp_space_area1));   /* 每个 ARP 条目均为 52 个字节，因此，ARP 条目总数是52字节整数倍 */

    /* 使能fragment */
    status = nx_ip_fragment_enable(&ip_1);

    /* 检测使能成功 */
    if (status) error_counter1++;

    /* 使能TCP */
    status =  nx_tcp_enable(&ip_1);

    /* 检测使能成功 */
    if (status) error_counter1++;

    /* 使能UDP  */
    status =  nx_udp_enable(&ip_1);

    /* 检测使能成功 */
    if (status) error_counter1++;

    /* 使能ICMP */
    status =  nx_icmp_enable(&ip_1);

    /* 检测使能成功 */
    if (status) error_counter1++;

    /* NETX初始化完毕后，重新设置优先级 */
    tx_thread_priority_change(netx_thread_ptr, APP_CFG_TASK_NETX_PRIO1, &old_priority);
    tx_thread_priority_change(&AppTaskNetXProTCB, APP_CFG_TASK_NetXPro_PRIO1, &old_priority);

    ////////////////////////////////////////////////////////////////////////////////////////////////
    /* 创建TCP Socket */
    ret = nx_tcp_socket_create(&ip_1,                 /* IP实例控制块 */
                               &TCPSocket,            /* TCP控制块 */
                               "TCP Server Socket",   /* TCP Socket名 */
                               NX_IP_NORMAL,          /* IP服务类型 */
                               NX_FRAGMENT_OKAY,      /* 使能IP分段 */
                               NX_IP_TIME_TO_LIVE,    /* 指定一个 8 位的值，用于定义此数据包在被丢弃之前可通过的路由器数目 */
                               4320,                  /* TCP Socket接收队列中允许的最大字节数 */
                               NX_NULL,               /* 用于在接收流中检测到紧急数据时调用的回调函数 */
                               NX_NULL);              /* TCP Socket另一端发出断开连接时调用的回调函数 */
    if (ret)
    {
		app_printf("nx_tcp_socket_create error!\n");
    }

    /* 绑定端口 */
    ret =  nx_tcp_client_socket_bind(&TCPSocket, DEFAULT_PORT, NX_WAIT_FOREVER);

    if (ret != NX_SUCCESS)
    {
    	app_printf("nx_tcp_client_socket_bind error!\n");
    }

    /* 连接远程服务器 */
    ret = nx_tcp_client_socket_connect(&TCPSocket, TCP_SERVER_ADDRESS, TCP_SERVER_PORT, NX_WAIT_FOREVER);

    if (ret != NX_SUCCESS)
    {
    	app_printf("nx_tcp_client_socket_connect error!\n");
    }


	while(1)
	{
        TX_MEMSET(data_buffer1, '\0', sizeof(data_buffer1));

        /* 获取socket状态 */
        nx_tcp_socket_info_get(&TCPSocket,     /* TCP Socket控制块 */
		                       NULL,           /* 发送的TCP数据包总数目 */
		                       NULL,           /* 发送的TCP总字节数 */
		                       NULL,           /* 接收TCP数据包总数目 */
		                       NULL,           /* 接收的TCP总字节数 */
		                       NULL,           /* 重新传输的TCP数据包总数目 */
		                       NULL,           /* Socket上TCP排队的TCP数据包总数 */
		                       NULL,           /* Socket上有校验和错误的TCP数据包总数 */
		                       &socket_state,  /* Socket当前状态 */
		                       NULL,           /* 仍在排队等待ACK的发送数据包总数 */
		                       NULL,           /* 当前发送窗口大小 */
		                       NULL);          /* 当前接收窗口大小 */

        /* 如果连接还没有建立，继续接受新连接，成功的话开启接收数据 */
        if(socket_state != NX_TCP_ESTABLISHED)
        {
            /* 绑定端口 */
            ret =  nx_tcp_client_socket_bind(&TCPSocket, DEFAULT_PORT, NX_WAIT_FOREVER);

            if (ret != NX_SUCCESS)
            {
            	app_printf("nx_tcp_client_socket_bind error!\n");
            }

            /* 连接远程服务器 */
            ret = nx_tcp_client_socket_connect(&TCPSocket, TCP_SERVER_ADDRESS, TCP_SERVER_PORT, NX_WAIT_FOREVER);

            if (ret != NX_SUCCESS)
            {
            	app_printf("nx_tcp_client_socket_connect error!\n");
            }
        }

        if((socket_state == NX_TCP_ESTABLISHED)&&(ret == NX_SUCCESS))
        {

            /* 接收TCP客户端发的TCP数据包 */
            ret = nx_tcp_socket_receive(&TCPSocket,        /* TCP Socket控制块 */
			                            &RecPacket,      /* 接收到的数据包 */
			                            NX_WAIT_FOREVER);  /* 永久等待 */

            if (ret == NX_SUCCESS)
            {

				/* 获取客户端的IP地址和端口 */
				nx_tcp_socket_peer_info_get(&TCPSocket,       /* TCP Socket控制块 */
											&peer_ip_address, /* 远程IP地址 */
											&peer_port);      /* 远程端口号 */

                /* 获取客户端发来的数据 */
                nx_packet_data_retrieve(RecPacket,    /* 接收到的数据包 */
				                        data_buffer1,    /* 解析出数据 */
				                        &bytes_read);   /* 数据大小 */

                /* 打印接收到数据 */
                PRINT_DATA(peer_ip_address, (unsigned int)peer_port, data_buffer1);

                nx_packet_release(RecPacket);

                /* 申请发送数据包 */
                ret = nx_packet_allocate(&pool_1, &TraPacket, NX_TCP_PACKET, TX_WAIT_FOREVER);

                if (ret)
                {
                	app_printf("nx_packet_allocate error!\n");
                }

                sprintf((char *)sendbuf, "sendbuf = %d\r\n", (int)count++);

                /*将要发送的数据附加到TraPacket */
                ret = nx_packet_data_append(TraPacket, (VOID *)sendbuf, strlen((char *)sendbuf), &pool_1, TX_WAIT_FOREVER);

                if (ret)
                {
                	app_printf("nx_packet_data_append error!\n");
                }

                /* 发送数据，注意发送后，此函数会释放数据包 */
                ret =  nx_tcp_socket_send(&TCPSocket,
                                           TraPacket,
                                            NX_WAIT_FOREVER);


                if (ret)
                {
                	app_printf("nx_tcp_socket_send error!\n");
                }

            }
            else
            {
                /* 释放数据包 */
                nx_packet_release(RecPacket);

                /* 断开连接 */
                nx_tcp_socket_disconnect(&TCPSocket,
				                         NX_WAIT_FOREVER);

				/* disconnect the socket */
                nx_tcp_client_socket_unbind(&TCPSocket);
            }
        }
	}
}

/***************************** 安富莱电子 www.armfly.com (END OF FILE) *********************************/
