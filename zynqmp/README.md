## threadx_for_zynqmp

这是ThreadX全家桶在Zynq UltraScale+（ZynqMP，Cortex-A53）上的实现。

> **编译方式**：每个组件（ThreadX、ThreadX SMP）都编译为独立的静态库工程（`*_system`），应用工程（`hello_*`）链接这些静态库。工程结构如下：
>
> | 静态库工程 | 源码目录 | 应用工程 |
> |-----------|---------|---------|
> | `ThreadX_system` | `ThreadX/` | `hello_threadx` |
> | `ThreadX_SMP_system` | `ThreadX_SMP/` | `hello_threadx_smp` |

### 第一章：ThreadX 的移植和开发

将 ThreadX RTOS（v6.4.1）移植到 ZynqMP（Cortex-A53，AArch64）平台，基于 Vitis IDE 开发，运行于 EL3，并在此基础上构建了外设驱动框架和多个应用示例。

- **内核移植**：适配 Cortex-A53（AArch64）、GIC-400（GICv2）中断控制器、ARM Generic Timer（Secure EL1 Physical Timer）
- **设备驱动框架**：统一的注册/查找/读/写/ioctl 接口，支持中断通知回调
- **外设驱动**：LED、按键（GPIO 中断）、UART、SD 卡
- **RTOS 示例**：消息队列、同步信号量、软件定时器、事件标志
- **低功耗**：调度空闲时进入 WFI（Tickless）

目录结构：

```
hello_threadx/src/
├── BSP/                  # 板级支持包
│   ├── board_init.c/h    # 硬件板级初始化
│   ├── bsp_init.c/h      # BSP 初始化入口（GIC、Timer、Global Timer）
│   └── driver/           # 设备驱动框架及外设驱动
│       ├── device_core   # 统一设备驱动框架（注册/读/写/ioctl）
│       ├── led_driver    # LED 驱动
│       ├── key_driver    # 按键驱动（中断模式）
│       ├── uart_driver   # UART 串口驱动
│       └── sd_driver     # SD 卡驱动
├── ThreadX/
│   ├── Source/           # ThreadX 内核源码
│   ├── Port/             # Cortex-A53 移植层（GIC、Generic Timer、上下文切换）
│   ├── utility/          # 辅助工具（执行性能分析等）
│   ├── tx_port.h         # 移植配置头文件
│   └── tx_user.h         # ThreadX 用户配置
├── utils/                # 通用工具（打印、环形缓冲区等）
├── main.c                # 任务创建与管理
└── includes.h            # 头文件汇总
```

移植要点：

- **运行级别**：运行于 EL3（AArch64），使用 SP_EL3 作为系统栈
- **时钟源**：使用 ARM Secure EL1 Physical Timer（CNTPS_TVAL_EL1）替代私有定时器
- **中断控制器**：GIC-400（GICv2），地址为 GICD 0xF9010000 / GICC 0xF9020000
- **中断处理**（`irqHandler`）：通过 GIC IAR 读取中断 ID，ID 29 为定时器中断，其余走 `tx_irq_dispatch`
- **异常向量表**：修改 `vectors.S`，将 EL3 异常向量表放入 `.text` 段
- **栈构建**：修改 `tx_thread_stack_build.S`，初始 SPSR 为 EL3（0xD）
- **全局定时器**：通过 `CNTPCT_EL0` 读取计数值，用于性能分析和 TraceX 时间戳
- **宏冲突**：`tx_port.h` 中 `LONG`/`ULONG` 宏与 Xilinx BSP 冲突，需在包含 `xil_type.h` 前包含 `tx_api.h`

详细文档：

- [移植操作与说明](docs/移植操作与说明.md) — ThreadX 移植步骤、代码修改、工程结构与配置说明

任务介绍：

- LED0任务控制闪烁，周期2s
- KEY0按下显示任务信息
- KEY1按下发送消息+释放信号量
- 两个任务接收消息或者信号量进行显示
- UART1中断释放信号量，任务读取数据并回显

### 第二章：TraceX

使用TraceX跟踪任务实现。

- `tx_user.h`中加入宏（`TX_ENABLE_EVENT_TRACE`、`TX_TRACE_TIME_SOURCE`）
- 使用`jtag_tool`工具导出 trx 文件，并用 TraceX 软件打开

### 第三章：多核SMP移植

将 ThreadX SMP（v6.4.1）多核对称多处理移植到 ZynqMP 四核 Cortex-A53 平台，实现多核并行任务调度。应用工程为 `hello_threadx_smp`。

目录结构：

```
hello_threadx_smp/src/
├── BSP/                  # 板级支持包（同单核版本）
│   └── driver/           # 设备驱动框架及外设驱动
│       ├── device_core   # 统一设备驱动框架
│       ├── led_driver    # LED 驱动
│       ├── key_driver    # 按键驱动（中断模式）
│       ├── uart_driver   # UART 串口驱动
│       └── sd_driver     # SD 卡驱动
├── ThreadX/
│   ├── Source/           # ThreadX SMP 内核源码（common_smp）
│   ├── Port/             # Cortex-A53 SMP 移植层
│   │   ├── startup.S            # 启动汇编（多核引导、MMU、Cache、GIC 初始化）
│   │   ├── tx_initialize_low_level.S  # 底层初始化（GIC、Timer）
│   │   └── ...                   # 上下文切换、中断处理等
│   ├── utility/          # 辅助工具（SMP 版性能分析）
│   ├── tx_port.h         # SMP 移植配置（多核、Generic Timer 时钟源）
│   └── tx_user.h         # ThreadX 用户配置
├── utils/                # 通用工具（打印、环形缓冲区等）
├── main.c                # 任务创建与多核亲和性配置
├── lscript.ld            # 链接脚本（扩充 EL3 栈 16KB）
└── includes.h            # 头文件汇总
```

移植要点：

- **SMP 内核源码**：使用 `common_smp` 替代单核 `common`，移植层使用 `ports_smp/cortex_a53_smp/gnu`
- **启动流程**（`startup.S`）：
  - CPU0 通过 CRF_APB 寄存器将 CPU1-3 置于复位状态
  - 设置 RVBAR 寄存器指定各核的复位向量地址
  - 释放复位后，CPU1-3 执行 `_boot_smp`：初始化 EL3 栈、配置 MMU/Cache、使能 GIC CPU Interface、使能 SGI 0
  - CPU0 跳转 `_startup` 进入 ThreadX，通过 `_tx_thread_smp_release_cores_flag` 唤醒其他核
- **时钟源**：使用 ARM Generic Timer（CNTPCT_EL0）作为 SMP 时间基准
- **核间中断**：通过 GIC SGI（Software Generated Interrupt）ID 0 实现核间通信
- **栈布局**：每个核独立的 EL3 栈（总计 16KB，按核数均分）
- **核心抢占**：`tx_thread_smp_core_preempt.S` 使用 GICv2 SGIR 寄存器发送核间中断
- **源码修正**：`tx_execution_profile.c` 中数组赋值语法修正

SMP 专属 API：

| API | 描述 |
|-----|------|
| `tx_thread_smp_core_get` | 获得当前运行的核 ID |
| `tx_thread_smp_core_exclude` | 设置指定线程不能在哪些核上运行 |
| `tx_thread_smp_core_exclude_get` | 获取指定线程的核心排除掩码 |
| `tx_timer_smp_core_exclude` | 设置系统定时器不能在哪些核心上触发 |
| `tx_timer_smp_core_exclude_get` | 获得系统定时器的核心排除掩码 |

任务介绍：

- CPU0 运行：LED0 闪烁（周期 2s）、消息队列接收、信号量接收、启动任务（CPU 使用率统计）
- CPU1 运行：KEY 按键处理（事件标志组）、UART1 接收任务（中断回显）
- 软件定时器控制 LED1 闪烁（周期 1s）
- KEY0 按下显示多核各任务信息（含每核 CPU 使用率、执行时间、栈使用情况）

详细文档：

- [SMP移植说明](docs/SMP移植说明.md) — SMP 移植步骤、启动流程、代码修改说明、API 使用

### 第四章：FileX（待补充）

### 第五章：NetXDuo（待补充）

### 第六章：USBX（待补充）

### 第七章：GUIX（待补充）
