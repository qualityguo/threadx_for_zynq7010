# ThreadX Cortex-A9 移植实施方案（基于 Vitis 裸机）

## 1. 总体策略

保留 Vitis 裸机 BSP 的启动链和硬件初始化，在 `main()` 中进入 ThreadX 内核。

**启动链**：

```
FSBL
  └─ _vector_table (asm_vectors.S)      ← 裸机向量表，Reset → _boot
       └─ _boot (boot.S)                ← CPU/缓存/MMU/L2/栈/VFP 初始化
            └─ _start (xil-crt0.S)      ← BSS 清零 → main()
                 └─ main()              ← 【用户入口】
                      └─ tx_kernel_enter()
                           └─ _tx_initialize_kernel_enter()
                                ├─ _tx_initialize_low_level()   ← ThreadX 接管
                                ├─ _tx_initialize_high_level()
                                ├─ tx_application_define()
                                └─ _tx_thread_schedule()        ← 调度器启动，永不返回
```

**分工**：

| 阶段 | 负责者 | 内容 |
|------|--------|------|
| 上电 → main() | 裸机 BSP（不修改） | VBAR、缓存、TLB、MMU、L2、各模式栈、VFP、SCU |
| main() → 调度器 | ThreadX Ports（不修改） | 重设栈、重设 VBAR、GIC、私有定时器 |

---

## 2. 需要修改的文件（共 2 个）

### 2.1 修改 `src/lscript.ld`

在 `.stack` 段之后、`_end = .;` 之前，添加两个 ThreadX 符号别名。

**修改位置**：第 288-290 行

**修改前**：
```ld
} > ps7_ddr_0

_end = .;
}
```

**修改后**：
```ld
} > ps7_ddr_0

/* ThreadX stack symbols - reuse entire bare-metal stack region */
_sp = __undef_stack;
_stack_bottom = _stack_end;

_end = .;
}
```

**原理**：

裸机 `.stack` 段共分配 14KB 连续内存（从低地址到高地址）：

```
_stack_end          ← 最低地址 = _stack_bottom
  │ SYS      8KB   │
  │ IRQ      1KB   │
  │ SVC      2KB   │
  │ Abort    1KB   │
  │ FIQ      1KB   │
  │ Undef    1KB   │
__undef_stack       ← 最高地址 = _sp
```

ThreadX `_tx_initialize_low_level` 从 `_sp`（最高点）往下分配：

```
__undef_stack = _sp
  │ ThreadX FIQ 栈   512B   │
  │ ThreadX IRQ 栈  1024B   │
  ├─ _tx_thread_system_stack_ptr（~_sp - 1544）
  │ ThreadX SVC 栈  ~12.5KB │   ← 复用裸机 SVC + IRQ + SYS 区域
_stack_end = _stack_bottom
```

**关键优势**：
- 零额外内存、零浪费
- `boot.S` 先用裸机栈完成硬件初始化
- `_tx_initialize_low_level` 从同一区域另一端重新分配，覆盖旧设置
- 不修改任何库代码

### 2.2 修改 `src/main.c`

**修改前**：
```c
#include "xil_printf.h"

int main()
{
	xil_printf("Hello Threadx\n\r");
    return 0;
}
```

**修改后**：
```c
#include "tx_api.h"
#include "xil_printf.h"

#define DEMO_STACK_SIZE 1024
#define BYTE_POOL_SIZE  4096

TX_THREAD   thread_0;
TX_BYTE_POOL byte_pool_0;

void thread_0_entry(ULONG thread_input)
{
    while (1)
    {
        xil_printf("Hello ThreadX\r\n");
        tx_thread_sleep(100);
    }
}

int main()
{
    tx_kernel_enter();
}

void tx_application_define(void *first_unused_memory)
{
    CHAR *pointer = TX_NULL;

    tx_byte_pool_create(&byte_pool_0, "byte pool 0",
                        first_unused_memory, BYTE_POOL_SIZE);

    tx_byte_allocate(&byte_pool_0, (VOID **)&pointer,
                     DEMO_STACK_SIZE, TX_NO_WAIT);

    tx_thread_create(&thread_0, "thread 0", thread_0_entry, 0,
                     pointer, DEMO_STACK_SIZE,
                     1, 1, TX_NO_TIME_SLICE, TX_AUTO_START);
}
```

---

## 3. 需要编译的源文件清单

### 3.1 ThreadX 内核（`src/ThreadX/src/` 目录）

编译该目录下所有 `.c` 文件，包括：

- `tx_*.c` — ThreadX 内核核心
- `txe_*.c` — 错误检查版 API

### 3.2 ThreadX 移植文件（`src/ThreadX-Ports/` 目录）

| 文件 | 说明 |
|------|------|
| **`reset.S`** | **必须编译** — 提供 `__vectors` 符号，被 `_tx_initialize_low_level` 设置到 VBAR |
| **`tx_initialize_low_level.S`** | 底层初始化：栈、VBAR、GIC、定时器 |
| **`MP_GIC.S`** | GIC 中断控制器驱动 |
| **`MP_PrivateTimer.S`** | Cortex-A9 私有定时器驱动 |
| **`v7.s`** | ARMv7-A 辅助函数（缓存、TLB、HighVecs 等） |
| `tx_thread_schedule.S` | 线程调度器 |
| `tx_thread_context_save.S` | IRQ 上下文保存 |
| `tx_thread_context_restore.S` | IRQ 上下文恢复 |
| `tx_thread_stack_build.S` | 线程栈构建 |
| `tx_thread_system_return.S` | 线程系统返回 |
| `tx_thread_vectored_context_save.S` | 向量化上下文保存 |
| `tx_thread_irq_nesting_start.S` | IRQ 嵌套启动 |
| `tx_thread_irq_nesting_end.S` | IRQ 嵌套结束 |
| `tx_thread_interrupt_disable.S` | 中断禁用 |
| `tx_thread_interrupt_restore.S` | 中断恢复 |
| `tx_thread_interrupt_control.S` | 中断控制 |
| `tx_thread_fiq_context_save.S` | FIQ 上下文保存 |
| `tx_thread_fiq_context_restore.S` | FIQ 上下文恢复 |
| `tx_thread_fiq_nesting_start.S` | FIQ 嵌套启动 |
| `tx_thread_fiq_nesting_end.S` | FIQ 嵌套结束 |
| `tx_timer_interrupt.S` | 定时器中断处理 |

### 3.3 不编译的文件

| 文件 | 原因 |
|------|------|
| `src/ThreadX-Ports/crt0.S` | 裸机 `xil-crt0.S`（libxil.a）已替代此功能 |

### 3.4 头文件包含路径

确保编译器包含路径设置：

```
-I src/
-I src/ThreadX/inc
-I src/ThreadX-Ports
-I <BSP_INCLUDE_PATH>/bspinclude/include
```

其中 `<BSP_INCLUDE_PATH>` 为 Vitis BSP 头文件目录。

---

## 4. 启动流程详解

### 阶段 1：裸机启动（FSBL → main）

| 步骤 | 执行者 | 操作 |
|------|--------|------|
| 1 | FSBL | 加载裸机应用到 DDR，跳转到 `_vector_table` |
| 2 | `asm_vectors.S` | 向量表：Reset → `_boot` |
| 3 | `boot.S` | CPU ID 检查、ARM Errata、VBAR 设置、SCU、缓存/TLB 无效化 |
| 4 | `boot.S` | 各模式栈设置（SYS/IRQ/SVC/Abort/FIQ/Undef） |
| 5 | `boot.S` | MMU 配置（页表基地址、域访问）、启用 MMU + I/D Cache |
| 6 | `boot.S` | ACTLR SMP 位、L2 缓存配置与启用 |
| 7 | `boot.S` | VFP 启用、分支预测启用、异步 abort 启用 |
| 8 | `boot.S` | 跳转到 `_start` |
| 9 | `xil-crt0.S` | `__cpu_init`、SBSS/BSS 清零、全局定时器、构造函数 → `main()` |

### 阶段 2：ThreadX 接管（main → 调度器）

| 步骤 | 执行者 | 操作 |
|------|--------|------|
| 10 | `main()` | 调用 `tx_kernel_enter()` |
| 11 | `_tx_initialize_kernel_enter()` | 设置系统状态，调用底层/高层初始化 |
| 12 | `_tx_initialize_low_level()` | 从 `_sp` 分配 FIQ(512B)/IRQ(1024B) 栈，保存 SVC 栈指针 |
| 13 | `_tx_initialize_low_level()` | 栈溢出检测（`_stack_bottom`） |
| 14 | `_tx_initialize_low_level()` | 计算可用内存：`_end + 8` → `_tx_initialize_unused_memory` |
| 15 | `_tx_initialize_low_level()` | **重设 VBAR** → `__vectors`（ThreadX 向量表接管中断） |
| 16 | `_tx_initialize_low_level()` | 禁用高向量（`disableHighVecs`） |
| 17 | `_tx_initialize_low_level()` | GIC 初始化（`enableGIC` + `enableGICProcessorInterface`） |
| 18 | `_tx_initialize_low_level()` | 中断优先级掩码设为 0x1F |
| 19 | `_tx_initialize_low_level()` | 启用私有定时器中断（ID 29，优先级 0） |
| 20 | `_tx_initialize_low_level()` | 配置并启动私有定时器（加载值 0xF0000，自动重载） |
| 21 | `_tx_initialize_low_level()` | 启用 SGI 0（软件中断，ID 0，优先级 0） |
| 22 | `_tx_initialize_high_level()` | ThreadX 内核各模块初始化 |
| 23 | `tx_application_define()` | 用户应用定义（创建线程、队列等） |
| 24 | `_tx_thread_schedule()` | 启动调度器，永不返回 |

---

## 5. 向量表切换机制

### 切换前（裸机阶段）

```
VBAR → _vector_table (asm_vectors.S)
         IRQ → IRQHandler → IRQInterrupt() (C函数，裸机ISR)
```

### 切换后（ThreadX 阶段，步骤 15）

```
VBAR → __vectors (reset.S)
         IRQ → __tx_irq_handler → _tx_thread_context_save
              → IRQ处理 → _tx_timer_interrupt (定时器tick)
              → _tx_thread_context_restore → _tx_thread_schedule
```

`_tx_initialize_low_level` 第 170-172 行执行切换：

```asm
LDR     r0, =__vectors
MCR     p15, 0, r0, c12, c0, 0    // 写入 VBAR 寄存器
BL      disableHighVecs            // 禁用高向量
```

---

## 6. 内存布局

### 链接脚本 `lscript.ld` 定义的布局

```
0x100000 (DDR 起始)
  ├─ .vectors          ← asm_vectors.S（KEEP，裸机向量表）
  ├─ .text             ← 代码段（含 reset.S 的 __vectors）
  ├─ .init / .fini
  ├─ .rodata
  ├─ .data
  ├─ .bss              ← BSS 段
  ├─ .heap             ← 堆（4KB）
  └─ .stack            ← 栈区域（14KB，各模式独立分配）
       ├─ SYS      8KB
       ├─ IRQ      1KB
       ├─ SVC      2KB
       ├─ Abort    1KB
       ├─ FIQ      1KB
       └─ Undef    1KB
                          _sp = __undef_stack（栈区最高地址）
                          _stack_bottom = _stack_end（栈区最低地址）
_end                     ← ThreadX 可用内存起始点
```

### ThreadX 内存使用

```
_tx_initialize_unused_memory = _end + 8
                                ↓
                         ThreadX 可用内存池
                         （由 tx_application_define 使用）
```

---

## 7. 注意事项与潜在风险

### 7.1 必须保留的裸机初始化

| 初始化项 | 不能省略的原因 |
|----------|---------------|
| **MMU** | DDR 地址 0x100000 不等于物理 0x0，页表提供正确映射；外设需 Device 属性 |
| **L2 缓存** | Zynq 7000 有 512KB L2，性能影响巨大 |
| **I/D Cache** | Cortex-A9 无缓存时 DDR 访问 ~100ns，性能极差 |
| **VFP** | 编译选项 `-mfpu=vfpv3 -mfloat-abi=hard`，不启用则任何浮点操作触发 Undefined 异常 |
| **SCU** | 多核缓存一致性 |

### 7.2 GIC 重复初始化

- 裸机 BSP 的 `Xil_ExceptionInit()` 可能初始化了 GIC CPU Interface
- ThreadX 的 `_tx_initialize_low_level()` 会再次初始化 GIC
- **不冲突**：GIC 寄存器是幂等的，重复写入相同的使能位无副作用

### 7.3 私有定时器

- ThreadX 使用 Cortex-A9 私有定时器（PPI ID 29）作为系统 tick
- 加载值 `0xF0000`（983040），CPU 私有定时器默认 1/2 CPU 主频
- Zynq 7010 CPU 主频 667MHz 时，tick 周期 ≈ 983040 / 333.5MHz ≈ 2.95ms
- 可根据需要修改 `tx_initialize_low_level.S` 第 201 行的加载值调整 tick 频率

### 7.4 IRQ 中断未在 `_tx_initialize_low_level` 中全局启用

- 第 186-188 行 `CPSIE i` 被注释掉
- IRQ 在调度器启动后通过 ThreadX 线程调度机制启用
- 这是正确的设计，不应修改

### 7.5 `__vectors` 与 `_vector_table` 共存

- 两者都在 `.text` 段中编译
- `_vector_table` 在 `.vectors` section（有 `KEEP`），启动时由 `boot.S` 设置 VBAR
- `__vectors` 在 `.text` section，运行时由 `_tx_initialize_low_level` 重设 VBAR
- 切换后裸机向量表不再使用，但不影响功能

---

## 8. 在 Vitis IDE 中配置编译

### 8.1 添加源文件

在 Vitis IDE 中右键项目 → Properties → C/C++ Build → Settings：

1. **添加 ThreadX 源文件**：将 `src/ThreadX/src/*.c` 和 `src/ThreadX-Ports/*.S` 添加到源文件列表
2. **排除 `src/ThreadX-Ports/crt0.S`**：不编译此文件

### 8.2 添加头文件路径

在 Compiler → Include Paths 中添加：

```
src/ThreadX/inc
src/ThreadX-Ports
```

### 8.3 添加预定义宏（可选）

如需 FIQ 支持或 IRQ 嵌套，在 Compiler → Preprocessor Defines 中添加：

```
TX_ENABLE_FIQ_SUPPORT
TX_ENABLE_IRQ_NESTING
```

---

## 9. 文件依赖关系图

```
main.c
  └─ tx_api.h (ThreadX/inc/)
       └─ tx_port.h (src/)
            └─ tx_user.h (可选)

tx_initialize_low_level.S
  ├─ reset.S          → 提供 __vectors 符号
  ├─ MP_GIC.S         → 提供 enableGIC, enableIntID, setIntPriority, setPriorityMask, ...
  ├─ MP_PrivateTimer.S → 提供 init_private_timer, start_private_timer, clear_private_timer_irq
  ├─ v7.s             → 提供 disableHighVecs, enableCaches, ...
  └─ lscript.ld       → 提供 _sp, _stack_bottom, _end 符号

tx_thread_context_save.S ─┐
tx_thread_context_restore.S ┤
tx_thread_schedule.S       ├── 互相引用，共同实现线程调度
tx_thread_stack_build.S    ┤
tx_thread_system_return.S  ┘
tx_timer_interrupt.S       → 被 tx_initialize_low_level.S 中的 IRQ handler 调用
```
