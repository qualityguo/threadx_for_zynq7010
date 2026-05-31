## 第一章：SMP操作系统移植（基于版本6.4.1）

### 一、项目工程结构

- `BSP`：外设驱动代码
- `ThreadX/Source`：存储操作系统源码
- `ThreadX/Port`：操作系统的移植层
- `ThreadX/utility`：相关组件
- `main.c`：任务管理文件
- `includes.h`：头文件汇总
- `ThreadX`
  - `tx_port.h`：移植头文件
  - `tx_user.h`：ThreadX配置文件

### 二、代码拷贝

- `/threadx/common_smp/inc/`到`ThreadX/Source`目录下
- `/threadx/common_smp/src/`到`ThreadX/Source`目录下
- `/threadx/ports_smp/cortex_a9_smp/gnu/src/*`到`ThreadX/Port`目录下（拷贝文件）
- `/threadx/ports_smp/cortex_a9_smp/gnu/inc/tx_port.h`到根目录下的`ThreadX/tx_port.h`
- `/threadx/ports_smp/cortex_a9_smp/gnu/example_build/*.h&*.S`到`ThreadX/Port`目录下（拷贝文件）
- `/threadx/utlity/execution_profile_kit/smp_version/*.s&*.c`到`ThreadX/utlity/execution_profile_kit/`目录下（拷贝文件）
- IRQ嵌套用的汇编（不需要删除，也不配置）
  - `tx_thread_irq_nesting_start.S`（IRQ嵌套开始）
  - `tx_thread_irq_nesting_end.S`（IRQ嵌套结束）

### 三、代码修改

- 修改SMP的时钟源获取计数值（此处应该获取定增的计数器）

  - ```c
    // tx_thread_smp_time_get.S
    - LDR     r0, [r0, #0x604]                    @ Read count register	
    + LDR     r0, [r0, #0x200]                    @ Read GTC counter lower 32 bits (CBAR+0x200)
    ```

- 修改`tx_port.h`用作匹配zynq7010

  - ```c
    // 双核
    #define TX_THREAD_SMP_MAX_CORES                 2
    #define TX_THREAD_SMP_CORE_MASK                 0x3
    // 用作性能测试
    #define TX_THREAD_EXTENSION_3           unsigned long long  tx_thread_execution_time_total; \
                                            unsigned long       tx_thread_execution_time_last_start;
    ```

- 大改`startup.S`（Xilinx的BSP的boot.S完全无法使用）

  - ```c
    // 定义一个中断向量表-同时处理Reset_Handler和IRQ_Handler都是死循环
        .section .vectors, "ax"
        .align 3
    
        .global _vector_table
    _vector_table:
        B       Reset_Handler
        B       Undefined_Handler
        B       SVC_Handler
        B       Prefetch_Handler
        B       Abort_Handler
        B       .                         @; Reserved
        B       IRQ_Handler
        B       FIQ_Handler
    
    Undefined_Handler:
        B       Undefined_Handler
    SVC_Handler:
        B       SVC_Handler
    Prefetch_Handler:
        B       Prefetch_Handler
    Abort_Handler:
        B       Abort_Handler
    FIQ_Handler:
        B       FIQ_Handler
    ```

  - ```c
    // Reset_Handler
    1. 设置SCTLR.SMP的bit	调用joinSMP进行设置
    2. 关闭I-Cache、D-Cache、MMU、分支预测	设置SCTLR相关bit
    3. 读取CPU-ID
    4. 设置每个核的独立栈(IRQ的栈、SVC的栈、SYS的栈)CPU0和1走的相同的路但是设置不同的地址，	ABT和FIQ的栈为了和裸机统一随便设置一下
    5. 切换到SVC模式，ThreadX运行的时候必须是SVC模式
    6. 设置VBAR
    7. 失效Cache、清空分支预测数组、无效TLB
    8. 设置域访问权限、全部Client(0x55555555)
    9. 设置TTB0 	所有核共享一个TTB
    10. 使能MMU
    11. 使能VFP
    12. 按照CPU的ID进行分流
        CPU0 -> primaryCPUInit
        CPU1 -> secondaryCPUsInit
    ```

  - ```c
    // 栈分布-递减栈
    高字节		CPU0的SYS栈(4KB)
    		  CPU1的SYS栈(4KB)
              CPU0的SVC栈(4KB)
    		  CPU1的SVC栈(4KB)
    		  CPU0的IRQ栈(2KB)
    		  CPU1的IRQ栈(2KB)
    ```

  - ```c
    // primaryCPUInit-CPU0专用
    1. 使能SCU+维护广播
    2. 失效 SCU 标签 RAM (CPU0 + CPU1 全部 way)
    3. L2Cache的初始化、配置、使能L2Cache
    4. 使能Cache+分支预测
    5. 启动CPU1
       - 写 _vector_table 地址到 0xFFFFFFF0
       - 清洁 D-cache 行 + DSB + SEV
    6. 跳转到 _start
    ```

  - ```c
    // secondaryCPUsInit-CPU1专属
    1. 使能 GIC CPU Interface + 优先级掩码
    2. 使能 SGI 0 (核间中断)
    3. 使能 caches
    4. 跳转 _tx_thread_smp_initialize_wait (自旋等待 CPU0 释放)
    ```

  - ```c
    // IRQ_Handler
    1. 保存上下文_tx_thread_context_save
    2. PUSH r4 r5寄存器
    3. 读取中断ID  readIntAck，同时将中断ID保存到r4
    4. 如果是29号，私有定时器ID，clear_private_timer_irq()+_tx_timer_interrupt
    5. 如果是非29号，分发tx_irq_dispatch(都交给GIC0)
    6. 中断结束 writeEOI
    7. 恢复上下文 _tx_thread_context_restore
    ```

- 大改`tx_initialize_low_level.S`

  - ```c
    1. 将lr压栈
    2. 读取CPU-ID，CPU1不可能进入此函数，CPU0进行一些额外的初始化
    3. CPU0的初始化
        使能GIC
        使能GIC-Interface
        设置优先级掩码
        配置SCU、初始化并启动
        使能SGI0
        保存_tx_initialize_unused_memory指针
    4. 将lr出栈并跳转执行
    ```

- 修改链接脚本`lscript.ld`

  - ```
    1. 扩充SVC的栈为0x2000
    2. 扩充IRQ的栈为0x1000
    ```

### 四、SDK配置

- 加入头文件搜索路径：
  - `ThreadX/Source/inc`
  - `ThreadX/Port/`
  - `./`
  - `BSP/`
  - `ThreadX/utility/execution_profile_kit`
- 加入宏`TX_INCLUDE_USER_DEFINE_FILE`，加入`tx_user.h`
- 加入宏`TX_ENABLE_EXECUTION_CHANGE_NOTIFY`，使能调试

### 一、SMP和单核比较

- 中断的处理：中断需要路由到特定核
- 心跳定时器：每个核都需要一个定时器， 每个核都需要独立设置自己的时钟中断时间
- SGI核间中断：（唤醒另一个核干活）
  - 线程的移动，需要SGI通知
  - 互斥量的唤醒，可能需要SGI通知
- SMP的保护
  - 原子操作：防止数据竞争
  - 公平等待队列：使用环形FIFO管理获取互斥量的核，不会饿死
  - 低功耗等待：WFE
- 启动同步
  - 核0等到核1被启动
- 各自自己的栈

### 二、参考示例（飞腾）

- 调度器实体

  - 单核环境下

    - ```c
      // 一个单独的执行线程指针和当前线程指针
      _tx_thread_execute_ptr
      _tx_thread_current_ptr
      ```

  - SMP环境下

    - ```c
      // 通过id的偏移访问
      MRS    x20, TPIDR_EL1 						// ID
      LDR     x1, =_tx_thread_execute_ptr 		// 基址
      LDR     x0, [x1, x20, LSL #3]				// 偏移
      ```

- 启动过程

  - 核0启动并进入main函数，然后进入函数`tx_kernel_init`
  - 调用函数`_tx_thread_smp_low_level_initialize()`
    - 遍历每个核
      - 获得物理核ID
      - 设置`tx_core_info`
      - 唤醒从核
      - WFE等待，`tx_core_info.flag == 1`
      - 从核开始配置，设置向量表、栈空间、MMU初始化、GIC初始化等
  - 内核初始化完成之后，所有从核进入调度

- 核间通信

  - SGI0中断

- SMP自旋锁-基于LDAXR/STXR独占访问的自旋锁

- SMP调度器的线程就绪位-解决多核同时竞争同一线程的问题

- 从核同步机制，实现了从核与主核的同步

- 栈空间-每个核都有自己独立的系统栈

### 三、

### 三、zynq7010应该如何启动双核

- 首先需要确认的一点是，SMP版本其实是运行的一个程序，不会有两个文件

- 但是Xilinx的BSP启动代码，根据一个宏对CPU0和CPU1产生两个文件，因此关键是需要重新设计汇编启动文件

- 汇编启动文件介绍

  - 中断向量表和链接脚本中的配置

    - ```
      // 链接脚本
      ENTRY(_vector_table)				// 定义了中断向量表的符号
      
      // 汇编文件
          .section .vectors, "ax"
          .align 3
      
          .global _vector_table
      _vector_table:
          B       Reset_Handler
          B       Undefined_Handler
          B       SVC_Handler
          B       Prefetch_Handler
          B       Abort_Handler
          B       .                         @; Reserved
          B       IRQ_Handler
          B       FIQ_Handler
      ```

  - 复位函数`Reset_Handler`做了什么（对于所有核的初始化-运行时判断）

    - ```
      // 第一步：进入SMP的一致域	joinSMP函数-	设置ACTLR寄存器的bit6置1，使能SMP
      // 第二步：关闭Cache、MMU、分支预测	 设置SCTLR寄存器的bit12、bit2、bit0、bit11清零(I-Cache禁止、D-Cache禁止、MMU禁止、分支预测禁止)
      // 第三步：读取CPU的ID到r0中
      // 第四步：设置栈(IRQ的栈、SVC的栈、SYS的栈)
      // 第五步：设置VBAR为中断向量表的地址
      // 第六步：无效化Cache和TLB	invalidateCaches+清空分支预测+TLB
      // 第七步：设置所有域为Client模式
      // 第八步：设置页表的基址，写入TTBR0
      // 第九步：使能MMU
      // 第十步：使能VFP
      // 第十一步：清除释放从核标志位为0，_tx_thread_smp_release_cores_flag
      // 第十一步：启动跳转，CPU0跳转到primaryCPUInit，CPU1跳转到secondaryCPUsInit
      ```

    - ```
      // 栈分布-慢递减栈
      高字节		CPU0的SYS栈(16KB)
      		  CPU1的SYS栈(16KB)
                CPU0的SVC栈(4KB)
      		  CPU1的SVC栈(4KB)
      		  CPU0的IRQ栈(2KB)
      		  CPU1的IRQ栈(2KB)
      ```

  - CPU0启动函数

    - 初始化SCU
    - 初始化L2Cache
    - 初始化GIC
    - 初始化私有定时器
    - 初始化SGI
    - 配置Cache和分支预测
    - 启动CPU1
    - 跳转`start`函数

  - CPU1的启动函数

    - 使能GIC-CPU接口
    - 设置优先级掩码
    - 使能SGI
    - 使能Cache
    - 跳转`_tx_thread_smp_initialize_wait`函数

### 四、相关修改

- 修改`startup.S`
- 修改`lscript.ld`
- SDK配置
  - 加入符号`TX_INCLUDE_USER_DEFINE_FILE`

- 源代码bug修改

  - ```c
    // tx_execution_profile.c
    // 移除如下代码
    *total_time =  _tx_execution_thread_time_total[core];
    *total_time =  _tx_execution_isr_time_total[core];
    *total_time =  _tx_execution_idle_time_total[core];
    // 加入如下代码
    total_time[core] =  _tx_execution_thread_time_total[core];
    total_time[core] =  _tx_execution_isr_time_total[core];
    total_time[core] =  _tx_execution_idle_time_total[core];
    ```

  - 

## 第二章：SMP-API使用

- 关于任务、定时器、信号量、互斥量、消息队列、事件标志组等API是通用的

- 加入了核心查询类API、核心亲和性设置API、高级系统控制API（应用层一般不使用）

- | API                            | 描述                               |
  | ------------------------------ | ---------------------------------- |
  | tx_thread_smp_core_get         | 获得当前运行的核ID                 |
  | tx_thread_smp_core_exclude     | 设置指定线程不能在哪些核上运行     |
  | tx_thread_smp_core_exclude_get | 获取指定线程的核心排除掩码         |
  | tx_timer_smp_core_exclude      | 设置系统定时器不能在哪些核心上触发 |
  | tx_timer_smp_core_exclude_get  | 获得系统定时器的核心排除掩码       |

  