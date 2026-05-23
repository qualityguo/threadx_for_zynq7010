/*
*********************************************************************************************************
*
*	模块名称 : 网络协议栈测试
*	文件名称 : demo_dm9162_netx.c
*	版    本 : V1.0
*	说    明 : 测试的功能说明
*              1. 默认IP地址192.168.245.120，在本文件配置，用户可根据需要修改。
*              2. 可以在电脑端用网络调试软件创建UDP连接此服务器端，端口号1000。
*              3. 实现了一个简单的回环通信，用户使用上位机发送的数据通过板子返回到上位机。
*
*	修改记录 :
*		版本号   日期         作者        说明
*		V1.0    2021-12-30   Eric2013     首发
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

/* 本地IP地址 */
#define IP_ADDR0                        192
#define IP_ADDR1                        168
#define IP_ADDR2                        28
#define IP_ADDR3                        120

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
NX_UDP_SOCKET UDPSocket;
TX_SEMAPHORE  Semaphore;


/*
*********************************************************************************************************
*	                                    NetX任务和通信组件
*********************************************************************************************************
*/
UCHAR data_buffer[4096];

NX_PACKET_POOL    pool_0;
NX_IP             ip_0;

/* 数据包内存池 */
#define PACKET_SIZE          1536
#define NX_PACKET_POOL_SIZE  ((PACKET_SIZE + sizeof(NX_PACKET)) * 100)

ULONG  packet_pool_area[NX_PACKET_POOL_SIZE/4 + 4];

/* ARP缓存 */
ULONG    arp_space_area[512 / sizeof(ULONG)];
ULONG    error_counter;

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
*	功能说明: NetXDUO应用
*	形    参: 无
*	返 回 值: 无
*********************************************************************************************************
*/
void NetXTest(void)
{
    UINT status;
    UINT ret;
    UINT old_priority;

    NX_PACKET *RecPacket;
    NX_PACKET* TraPacket;

    ULONG bytes_read;

    uint8_t sendbuf[20];
    uint32_t count = 0;

	UINT source_port;
    ULONG source_ip_address;


    /* 初始化NetX */
    nx_system_initialize();

    /* 创建内存池 */
    status =  nx_packet_pool_create(&pool_0,                                       /* 内存池控制块 */
                                     "NetX Main Packet Pool",                      /* 内存池名 */
                                     1536,                                         /* 内存池每个数据包大小，单位字节
                                                                                      此值必须至少为 40 个字节，并且还必须可以被 4 整除 */
									 (ULONG*)(((int)packet_pool_area + 15) & ~15) ,/* 内存池地址，此地址必须ULONG对齐 */
                                     NX_PACKET_POOL_SIZE);                         /* 内存池大小 */

    /* 检测创建是否失败 */
    if (status) error_counter++;

    /* 例化IP */
    status = nx_ip_create(&ip_0,                                                   /* IP实例控制块 */
                            "NetX IP Instance 0",                                  /* IP实例名 */
                            IP_ADDRESS(IP_ADDR0, IP_ADDR1, IP_ADDR2, IP_ADDR3),    /* IP地址 */
                            0xFFFFFF00UL,                                          /* 子网掩码 */
                            &pool_0,                                               /* 内存池 */
							nx_driver_zynq,                                   	   /* 网卡驱动 */
                            (UCHAR*)AppTaskNetXStk,                                /* IP任务栈地址 */
                            sizeof(AppTaskNetXStk),                                /* IP任务栈大小，单位字节 */
                            APP_CFG_TASK_NETX_PRIO);                               /* IP任务优先级 */


    /* 检测创建是否失败 */
    if (status) error_counter++;

    /* 使能ARP，并提供ARP缓存 */
    status =  nx_arp_enable(&ip_0,                     /* IP实例控制块 */
							(void *)arp_space_area,    /* ARP缓存地址 */
							sizeof(arp_space_area));   /* 每个 ARP 条目均为 52 个字节，因此，ARP 条目总数是52字节整数倍 */

    /* 使能fragment */
    status = nx_ip_fragment_enable(&ip_0);

    /* 检测使能成功 */
    if (status) error_counter++;

    /* 使能TCP */
    status =  nx_tcp_enable(&ip_0);

    /* 检测使能成功 */
    if (status) error_counter++;

    /* 使能UDP  */
    status =  nx_udp_enable(&ip_0);

    /* 检测使能成功 */
    if (status) error_counter++;

    /* 使能ICMP */
    status =  nx_icmp_enable(&ip_0);

    /* 检测使能成功 */
    if (status) error_counter++;

    /* NETX初始化完毕后，重新设置优先级 */
    tx_thread_priority_change(netx_thread_ptr, APP_CFG_TASK_NETX_PRIO1, &old_priority);
    tx_thread_priority_change(&AppTaskNetXProTCB, APP_CFG_TASK_NetXPro_PRIO1, &old_priority);

    ////////////////////////////////////////////////////////////////////////////////////////////////
    /* 创建UDP socket */
    ret = nx_udp_socket_create(&ip_0,                 /* IP实例控制块 */
                                &UDPSocket,           /* UDP控制块 */
                                "UDP Server Socket",  /* UDP名 */
                                NX_IP_NORMAL,         /* IP服务类型 */
                                NX_FRAGMENT_OKAY,     /* 使能IP分段 */
                                NX_IP_TIME_TO_LIVE,   /* 指定一个 8 位的值，用于定义此数据包在被丢弃之前可通过的路由器数目 */
                                512);                 /* 支持的报文数 */

    if (ret != NX_SUCCESS)
    {
    	app_printf("nx_udp_socket_create error!\r\n");
    }

    /* UDP Socket绑定端口 */
    ret = nx_udp_socket_bind(&UDPSocket, DEFAULT_PORT, TX_WAIT_FOREVER);

    if (ret != NX_SUCCESS)
    {
    	app_printf("nx_udp_socket_bind error!\r\n");
    }


	while(1)
	{
        /* 接收数据 */
        ret = nx_udp_socket_receive(&UDPSocket, &RecPacket, TX_WAIT_FOREVER);

        if (ret == NX_SUCCESS)
        {
            /* 将UDP数据包中的数据复制到缓冲data_buffer */
            nx_packet_data_extract_offset(RecPacket,            /* 数据包 */
                                          0,                    /* 数据包地址偏移 */
                                          data_buffer,          /* 目标缓冲 */
                                          sizeof(data_buffer),  /* 目标缓冲大小 */
                                          &bytes_read);         /* 数据复制的字节数 */

            /* 获取远程端口和IP  */
            nx_udp_source_extract(RecPacket, &source_ip_address, &source_port);

            /* 打印接收到数据 */
            PRINT_DATA(source_ip_address, source_port, data_buffer);

            /* 释放数据包 */
            nx_packet_release(RecPacket);

            /* 申请发送数据包 */
            ret = nx_packet_allocate(&pool_0, &TraPacket, NX_UDP_PACKET, TX_WAIT_FOREVER);

            if (ret)
            {
                app_printf("nx_packet_allocate error!\r\n");
            }

            sprintf((char *)sendbuf, "sendbuf = %d\r\n", (int)count++);

            /*将要发送的数据附加到TraPacket */
            ret = nx_packet_data_append(TraPacket, (VOID *)sendbuf, strlen((char *)sendbuf), &pool_0, TX_WAIT_FOREVER);

            if (ret)
            {
            	app_printf("nx_packet_data_append error!\r\n");
            }

            /* 发送数据包到UDP发送端 */
            ret =  nx_udp_socket_send(&UDPSocket, TraPacket, source_ip_address, source_port);

        }
	}
}

/***************************** 安富莱电子 www.armfly.com (END OF FILE) *********************************/
