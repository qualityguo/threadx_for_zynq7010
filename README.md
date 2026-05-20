## threadx_for_zynq7010

这是ThreadX全家桶在zynq7010上的实现。

### 第一章：ThreadX 的移植和开发

将 ThreadX RTOS（v6.4.1）移植到 Zynq7010（Cortex-A9）平台，基于 Vitis IDE 开发，并在此基础上构建了外设驱动框架和多个应用示例。

- **内核移植**：适配 Cortex-A9 SMP、GIC 中断控制器、私有定时器
- **设备驱动框架**：统一的注册/查找/读/写/ioctl 接口，支持中断通知回调
- **外设驱动**：LED、按键（GPIO 中断）、UART
- **RTOS 示例**：消息队列、同步信号量、软件定时器、事件标志
- **低功耗**：调度空闲时进入 WFI（Tickless）

目录结构：

```
hello_threadx/src/
├── BSP/                  # 板级支持包
│   ├── board_init.c/h    # 硬件板级初始化
│   ├── bsp_init.c/h      # BSP 初始化入口
│   └── driver/           # 设备驱动框架及外设驱动
│       ├── device_core   # 统一设备驱动框架（注册/读/写/ioctl）
│       ├── led_driver    # LED 驱动
│       ├── key_driver    # 按键驱动（中断模式）
│       └── uart_driver   # UART 串口驱动
├── ThreadX/
│   ├── Source/           # ThreadX 内核源码
│   ├── Port/             # Cortex-A9 移植层（GIC、定时器、上下文切换）
│   ├── utility/          # 辅助工具（执行性能分析等）
│   ├── tx_port.h         # 移植配置头文件
│   └── tx_user.h         # ThreadX 用户配置
├── utils/                # 通用工具（打印、环形缓冲区等）
├── main.c                # 任务创建与管理
└── includes.h            # 头文件汇总
```

详细文档：

- [移植与开发](docs/移植与开发.md) — ThreadX 移植步骤、工程结构与配置说明
- [设备驱动开发准则](docs/设备驱动开发准则.md) — 驱动框架设计、API 说明与开发规范
- [中断处理流程](docs/中断处理流程.md) — GIC 中断、Tick 处理、上下文保存恢复流程

任务介绍：

- LED0任务控制闪烁，周期2s
- LED1软件定时器控制闪烁，周期1s
- KEY0按下显示任务信息
- KEY1按下发送消息+释放信号量
- 两个任务接收消息或者信号量进行显示
- UART1中断释放信号量，任务读取数据并回显

### 第二章：TraceX

使用TraceX跟踪任务实现。

- `tx_user.h`中加入宏
- 使用`run_xsct.bat`的脚本的选项3保存trx文件，并使用TraceX软件打开

### 第三章：FileX（待补充）

### 第三章：NetDuo（待补充）

### 第四章：USBX（待补充）
