## threadx_for_zynq7010

这是ThreadX全家桶在zynq7010上的实现。

> **编译方式**：每个组件（ThreadX、FileX、NetXDuo）都编译为独立的静态库工程（`*_system`），应用工程（`hello_*`）链接这些静态库。工程结构如下：
>
> | 静态库工程 | 源码目录 | 应用工程 |
> |-----------|---------|---------|
> | `ThreadX_system` | `ThreadX/` | `hello_threadx`, `hello_filex`, `hello_netxduo` |
> | `FileX_system` | `FileX/` | `hello_filex` |
> | `NetXDuo_system` | `NetXDuo/` | `hello_netxduo` |

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

### 第三章：FileX

将 FileX 文件系统（v6.4.1）移植到 Zynq7010 平台，基于 SD 卡实现 FatFS 文件操作。静态库工程为 `FileX_system`，应用工程为 `hello_filex`。

目录结构：

```
hello_filex/src/
├── BSP/                    # 板级支持包（同 ThreadX）
│   └── driver/
│       └── sd_driver.c     # SD 卡底层驱动
├── ThreadX/                # ThreadX 内核头文件
├── FileX/
│   ├── fx_api.h            # FileX API 头文件
│   ├── fx_port.h           # 移植配置头文件
│   ├── fx_user_sample.h    # 用户配置
│   ├── fx_zynq_sdio_driver.c/h  # SDIO 移植层驱动
│   └── fx_*.h              # 其他 FileX 头文件
├── utils/                  # 通用工具
├── main.c                  # 任务创建与管理
├── demo_sd_file.c          # SD 卡文件操作示例
└── includes.h              # 头文件汇总
```

移植层实现：

- `fx_zynq_sdio_driver.c/h`：实现 `fx_zynq_sd_driver(FX_MEDIA *media_ptr)` 函数，处理 FileX 对 SD 卡的各种操作请求（读扇区、写扇区、读引导扇区、写引导扇区等）

功能演示（通过串口发送数字 1-6 选择）：

1. 显示根目录下的文件列表
2. 创建新文件 armfly.txt 并写入字符串
3. 读取 armfly.txt 文件内容
4. 创建目录（Dir1、Dir2、Dir1/Dir1_1）
5. 删除文件和目录
6. 读写文件速度测试（2MB 文件，32KB 缓冲区，显示读写速度）

任务介绍：

- LED0 任务控制闪烁，周期 2s
- KEY0 按下显示任务信息（含 CPU 使用率、栈使用情况）
- KEY1 按下发送消息 + 释放信号量
- 消息队列接收任务：打印收到的消息内容
- 信号量接收任务：打印同步信号量通知
- UART1 接收任务：收到串口数据后调用 `DemoFileX()` 执行对应的文件操作

### 第四章：NetXDuo

将 NetXDuo 网络协议栈（v6.4.1）移植到 Zynq7010 平台，基于 GEM（Gigabit Ethernet MAC）和 RTL8211E PHY 芯片实现以太网通信。静态库工程为 `NetXDuo_system`，应用工程为 `hello_netxduo`。

目录结构：

```
hello_netxduo/src/
├── BSP/                      # 板级支持包（同 ThreadX）
├── ThreadX/                  # ThreadX 内核头文件
├── NetXDuo/
│   ├── nx_api.h              # NetXDuo API 头文件
│   ├── nx_port.h             # 移植配置头文件
│   ├── nx_user_sample.h      # 用户配置
│   ├── nx_*.h                # 协议头文件（ARP、ICMP、TCP、UDP 等）
│   └── Port/
│       ├── nx_driver_zynq.c/h    # 网卡驱动（GEM + DMA）
│       └── rtl8211e_phy.c        # RTL8211E PHY 驱动
├── utils/                    # 通用工具
├── main.c                    # 任务创建与管理
├── netxduo_udp.c             # UDP 回环示例
├── netxduo_tcp_server.c      # TCP 服务器回环示例
├── netxduo_tcp_client.c      # TCP 客户端回环示例
└── includes.h                # 头文件汇总
```

移植层实现：

- `nx_driver_zynq.c/h`：实现 `nx_driver_zynq(NX_IP_DRIVER *driver_req_ptr)` 网卡驱动函数，基于 Xilinx EmacPs（GEM）外设，包含：
  - GEM 初始化（MAC 地址、BD 环、MDIO 分频）
  - PHY 芯片配置（RTL8211E：检测 ID、复位、自动协商、设置 SLCR 时钟）
  - DMA 收发（64 个 RX BD、256 个 TX BD）
  - 中断回调（接收、发送完成、错误处理）
  - 延迟处理（释放已发送 BD、将接收数据传递给协议栈）
- `rtl8211e_phy.c`：RTL8211E PHY 芯片专用驱动，替代原 Xilinx 通用 PHY 驱动

网络示例：

| 示例 | 文件 | 默认 IP | 端口 | 说明 |
|------|------|---------|------|------|
| UDP 回环 | `netxduo_udp.c` | 192.168.28.245 | 1000 | 接收 UDP 数据并原样返回 |
| TCP 服务器 | `netxduo_tcp_server.c` | 192.168.28.245 | 1001 | 监听端口，接收数据并原样返回 |
| TCP 客户端 | `netxduo_tcp_client.c` | 192.168.28.245 → 192.168.28.100 | 1000→1001 | 连接远端服务器，收发回环 |

> 切换示例：修改 `main.c` 中 `AppTaskNetXPro` 函数调用的 `NetXTest0()`（UDP）、`NetXTest1()`（TCP 客户端）、`NetXTest2()`（TCP 服务器）。

任务介绍：

- LED0 任务控制闪烁，周期 2s
- KEY0 按下显示任务信息（含 CPU 使用率、栈使用情况）
- NetX 网络任务：初始化协议栈（ARP、TCP、UDP、ICMP），运行网络示例

### 第五章：USBX（待补充）
