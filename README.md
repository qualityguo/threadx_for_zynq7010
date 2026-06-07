## ThreadX RTOS Vitis 工程

ThreadX 全家桶（v6.4.1）在 Xilinx Zynq 平台上的移植与开发工程集合，基于 Vitis IDE 开发。

### 工程目录

| 目录 | 平台 | 说明 |
|------|------|------|
| [zynq7000/](zynq7000/) | Zynq-7010（双核 Cortex-A9） | ThreadX、FileX、NetXDuo、SMP 移植 |
| [zynqmp/](zynqmp/) | Zynq UltraScale+（四核 Cortex-A53） | ThreadX、SMP 移植 |
| [jtag_tool/](jtag_tool/) | 通用 | JTAG UART 终端调试和 TraceX 导出 GUI 工具 |

### 已完成功能

| 功能 | Zynq-7000 | ZynqMP |
|------|:---------:|:------:|
| ThreadX 内核移植 | ✅ | ✅ |
| 设备驱动框架（LED/KEY/UART） | ✅ | ✅ |
| TraceX 跟踪 | ✅ | ✅ |
| FileX 文件系统（SD 卡） | ✅ | 待补充 |
| NetXDuo 网络协议栈 | ✅ | 待补充 |
| 多核 SMP 移植 | ✅ | ✅ |
| USBX | 待补充 | 待补充 |
| GUIX | 待补充 | 待补充 |
