## 第一章：操作系统移植（基于版本6.4.1）

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
- `/threadx/ports_smp/cortex_a53_smp/gnu/src/*`到`ThreadX/Port`目录下（拷贝文件）
- `/threadx/ports_smp/cortex_a53_smp/gnu/inc/tx_port.h`到根目录下的`ThreadX/tx_port.h`
- `/threadx/ports_smp/cortex_a53_smp/gnu/example_build/*.h&*.S`到`ThreadX/Port`目录下（拷贝文件）
- `/threadx/utlity/execution_profile_kit/smp_version/*.s&*.c`到`ThreadX/utlity/execution_profile_kit/`目录下（拷贝文件）
- 删除无关代码
  - `GIC*`：GIC相关采用XScuGic替代（使用v2版本）
  - `sp804_timer*`：定时器采用EL3的Physical Timer替代
  - `v8_aarch64.S/h`：FVP系统寄存器封装操作
  - `v8_utils.S`：FVP工具函数
  - `v8_mmu.h`和`v8_system.h`：FVP系统定义
  - `PPM_AEM.h`：FVP电源管理
  - `timer_interrupts.c`：定时器的操作放到`tx_initialize_low_level.S`
  - `MP_Mutexes*`：多核互斥（单核版本删除）

### 三、代码修改

- 修改`vectors.S`的异常向量表的段

  - ```
    // 将EL3的异常向量表放到text段
    - .section  EL3VECTORS, "ax"
    + .section  .text, "ax"
    
    // irqFirstLevelHandler也放到text段
    - .section InterruptHandlers, "ax"
    + .section .text, "ax"
    
    // 移除irqFirstLevelHandler中设置SP的操作
    - MSR      SPSel, 0
    ```

- 修改`tx_thread_stack_build.S`

  - ```
    // 构建栈帧函数不要切换到EL0，因为只初始化了一个EL3
    - MOV x2, #0xC
    + MOV     x2, #0xD
    ```

- 修改`startup.S`

  - ```
    RVBAR_BASE          =       0xFD5C0040
    CRF_APB_BASE		=		0xFD1A0000
    RST_FPD_APU_OFF		=		0x104
    
    	.global _vector_table
    	.global	_boot
    	.global	_boot_smp
    	.global	__el3_stack
    	.global	_el3_stack_end
    	.global MMUTableL0
    	.global el3_vectors
    	.global _startup
    	.global _tx_thread_smp_initialize_wait
        .global _tx_thread_smp_release_cores_flag
    	.global enableGIC
        .global enableGICProcessorInterface
        .global setPriorityMask
        .global setIntPriority
        .global enableIntID
    
    
    .section .vectors, "ax"
    .align 11
    
    _vector_table:
    	B		_boot
    	.org	0x080
    	B		.
    	.org	0x100
    	B		.
    	.org	0x180
    	B		.
    	.org	0x800
    
    .section .boot, "ax"
    .align 2
    
    _boot:
    	MRS		x0,	MPIDR_EL1					// Read core ID
    	AND		x0, x0, #0xFF					// Extract low 8 bits
    	CBZ		x0, _boot_core0_ok				// Branch if core 0
    
    _boot_wait:
    	wfe										// Non-core0 waits for core 0 to wake up
    	b		_boot_wait
    
    _boot_core0_ok:
    	// Place cores 1-3 into reset first
    	LDR		x0, =CRF_APB_BASE
    	LDR		w1, [x0, #RST_FPD_APU_OFF]
    	ORR		w1, w1, #(7<<1)					// Set bit1-bit3, assert reset
    	STR		w1, [x0, #RST_FPD_APU_OFF]
    	BIC		w1, w1, #(7<<11)				// Clear bit11-bit13
    	STR		w1, [x0, #RST_FPD_APU_OFF]
    
    	// Set reset vector address for each core
    	LDR		x0, =RVBAR_BASE
    	LDR		x1, =_boot
    	STR		x1, [x0]						// RVBAR_CPU0 = _boot
    	LDR		x1, =_boot_smp
    	STR		x1, [x0, #8]					// RVBAR_CPU1 = _boot_smp
    	STR		x1, [x0, #16]					// RVBAR_CPU2 = _boot_smp
    	STR		x1, [x0, #24]					// RVBAR_CPU3 = _boot_smp
    
    _boot_smp:
    	// Clear all general-purpose registers
    	MOV     x0, #0
        MOV     x1, #0
        MOV     x2, #0
        MOV     x3, #0
        MOV     x4, #0
        MOV     x5, #0
        MOV     x6, #0
        MOV     x7, #0
        MOV     x8, #0
        MOV     x9, #0
        MOV     x10, #0
        MOV     x11, #0
        MOV     x12, #0
        MOV     x13, #0
        MOV     x14, #0
        MOV     x15, #0
        MOV     x16, #0
        MOV     x17, #0
        MOV     x18, #0
        MOV     x19, #0
        MOV     x20, #0
        MOV     x21, #0
        MOV     x22, #0
        MOV     x23, #0
        MOV     x24, #0
        MOV     x25, #0
        MOV     x26, #0
        MOV     x27, #0
        MOV     x28, #0
        MOV     x29, #0
        MOV     x30, #0
    
        // Confirm currently running at EL3
        MRS     x0, CurrentEL
        CMP     x0, #0xC
        BNE     error_loop
    
        // Set temporary exception vector table base (will be overwritten later)
        LDR     x1, =_vector_table
        MSR     VBAR_EL3, x1
    
        // Get core ID
        MRS		x0,	MPIDR_EL1					// Read core ID
    	AND		x0, x0, #0xFF					// Extract low 8 bits
    	MOV		x19, x0
    
    	// Allocate per-core EL3 stack
    	// sp = _el3_stack_end + (coreID+1) * (total_size/4)
    	LDR		x1, =__el3_stack
    	LDR		x2, =_el3_stack_end
    	SUB		x1, x1, x2						// Calculate total stack size
    	LSR		x1, x1, #2						// Calculate per-core stack size
    	ADD		x2, x19, #1						// coreID + 1
    	MUL		x2, x2, x1						// offset = (coreID+1) * (total_size/4)
    	LDR		x3, =_el3_stack_end
    	ADD		sp, x3, x2						// sp = _el3_stack_end + offset
    
    	// Set final exception vector table (pointing to ThreadX vectors)
    	LDR     x0, =el3_vectors
        MSR     VBAR_EL3, x0
    
        // Enable SIMD/FPU register access
        MOV     x0, #0
        MSR     CPTR_EL3, x0
        ISB
    
        // Configure SCR_EL3: EL1 as AArch64, enable IRQ/FIQ/NS
        MOV     w1, #0
        ORR     w1, w1, #(1 << 11)              // RW: EL1 -> AArch64
        ORR     w1, w1, #(1 << 10)              // ST: Secure EL1 can be trapped
        ORR     w1, w1, #(1 << 3)               // EA: SError routed to EL3
        ORR     w1, w1, #(1 << 2)               // FIQ routed to EL3
        ORR     w1, w1, #(1 << 1)               // IRQ routed to EL3
        MSR     SCR_EL3, x1
    
        // Configure CPU auxiliary control register
        LDR     x0, =0x80CA000
        MSR     S3_1_C15_C2_0, x0
    
        // Enable FMPEN (floating-point multiply-accumulate)
        MRS     x0, S3_1_c15_c2_1
        ORR     x0, x0, #(1 << 6)
        MSR     S3_1_c15_c2_1, x0
        ISB
    
    	// Invalidate TLB and caches
    	TLBI    ALLE3
        IC      IALLU
        BL      invalidate_dcaches
        DSB     SY
        ISB
    
        // Configure MMU page tables
        LDR     x1, =MMUTableL0
        MSR     TTBR0_EL3, x1
    
        LDR     x1, =0x000000BB0400FF44
        MSR     MAIR_EL3, x1
    
        LDR     x1, =0x80823518
        MSR     TCR_EL3, x1
        ISB
    
        // Enable SError interrupt
        MRS     x1, DAIF
        BIC     x1, x1, #(0x1 << 8)
        MSR     DAIF, x1
    
        // Enable MMU, ICache, DCache
        MOV     x1, #0
        ORR     x1, x1, #(1 << 12)              // ICache
        ORR     x1, x1, #(1 << 3)               // DCache
        ORR     x1, x1, #(1 << 2)               // Data cache
        ORR     x1, x1, #(1 << 0)               // MMU
        MSR     SCTLR_EL3, x1
        DSB     SY
        ISB
    
        // Dispatch by core ID: core 0 -> primaryCPUInit, others -> secondaryCPUsInit
        CBZ     x19, primaryCPUInit
        B       secondaryCPUsInit
    
    // ============================================================
    // Core 0 initialization
    // Only sets release flag and wakes up other cores, no GIC/Timer setup
    // GIC Distributor, CPU Interface, Timer, SGI configuration has been
    // moved to bsp_init() using Xilinx XScuGic API
    // ============================================================
    primaryCPUInit:
        // Clear release flag so other cores keep waiting
        LDR     x0, =_tx_thread_smp_release_cores_flag
        MOV     x1, #0
        STR     x1, [x0]
    
        // Wake up core 1/2/3 (de-assert reset)
        LDR		x2, =CRF_APB_BASE
        LDR		w3, [x2, #RST_FPD_APU_OFF]
        BIC		w3, w3, #(7<<1)					// Clear bit1-bit3, release reset
        STR		w3, [x2, #RST_FPD_APU_OFF]
        DSB		SY
    
        // Jump to C entry point _startup()
        B       _startup
    
    // ============================================================
    // Secondary core initialization (core 1/2/3)
    // Only configures per-core GIC CPU Interface and SGI 0,
    // then enters ThreadX SMP wait loop
    // GIC Distributor has been enabled by core 0 in bsp_init()
    // ============================================================
    secondaryCPUsInit:
        BL      enableGICProcessorInterface     // Enable GIC CPU Interface for this core
    
        MOV     x0, #0xF0
        BL      setPriorityMask                 // Set priority mask
    
        MOV     x0, #0
        BL      enableIntID                     // Enable SGI 0 (inter-core interrupt)
    
        B       _tx_thread_smp_initialize_wait  // Enter ThreadX SMP wait loop
    
    
    // ============================================================
    // Cache invalidation helper
    // ============================================================
    invalidate_dcaches:
        DMB     ISH
        MRS     x0, CLIDR_EL1
        UBFX    w2, w0, #24, #3
        CMP     w2, #0
        B.EQ    invalidateCaches_end
        MOV     w1, #0
    
    invalidateCaches_flush_level:
        ADD     w3, w1, w1, LSL #1
        LSR     w3, w0, w3
        UBFX    w3, w3, #0, #3
        CMP     w3, #2
        B.LT    invalidateCaches_next_level
    
        LSL     w4, w1, #1
        MSR     CSSELR_EL1, x4
        ISB
        MRS     x4, CCSIDR_EL1
    
        UBFX    w3, w4, #0, #3
        ADD     w3, w3, #2
        UBFX    w5, w4, #13, #15
        UBFX    w4, w4, #3, #10
        CLZ     w6, w4
    
    invalidateCaches_flush_set:
        MOV     w8, w4
    invalidateCaches_flush_way:
        LSL     w7, w1, #1
        LSL     w9, w5, w3
        ORR     w7, w7, w9
        LSL     w9, w8, w6
        ORR     w7, w7, w9
        DC      CISW, x7
        SUBS    w8, w8, #1
        B.GE    invalidateCaches_flush_way
        SUBS    w5, w5, #1
        B.GE    invalidateCaches_flush_set
    
    invalidateCaches_next_level:
        ADD     w1, w1, #1
        CMP     w2, w1
        B.GT    invalidateCaches_flush_level
    
    invalidateCaches_end:
        RET
    
    error_loop:
        B       error_loop
    
        .end
    ```

- 修改`tx_thread_smp_time_get.S`

  - 原函数是空函数

  - ```
        .global  _tx_thread_smp_time_get
        .type    _tx_thread_smp_time_get, @function
    _tx_thread_smp_time_get:
        MRS     x0, CNTPCT_EL0
        UBFX    x0, x0, #0, #32
        RET
    ```

- 修改`tx_thread_smp_core_preempt.S`

  - 使用GIC-V2的操作

  - ```
    /***************************************************************************
     * Copyright (c) 2024 Microsoft Corporation 
     * 
     * This program and the accompanying materials are made available under the
     * terms of the MIT License which is available at
     * https://opensource.org/licenses/MIT.
     * 
     * SPDX-License-Identifier: MIT
     **************************************************************************/
    
    
    /**************************************************************************/
    /**************************************************************************/
    /**                                                                       */
    /** ThreadX Component                                                     */
    /**                                                                       */
    /**   Thread - Low Level SMP Support                                      */
    /**                                                                       */
    /**************************************************************************/
    /**************************************************************************/
    #ifdef TX_INCLUDE_USER_DEFINE_FILE
    #include "tx_user.h"
    #endif
    
    GICD_BASE       =       0xF9010000
    GICD_SGIR_OFF   =       0xF00
    
        .text
        .align 3
    /**************************************************************************/
    /*                                                                        */
    /*  FUNCTION                                               RELEASE        */
    /*                                                                        */
    /*    _tx_thread_smp_core_preempt                        ARMv8-A-SMP      */
    /*                                                           6.3.0        */
    /*  AUTHOR                                                                */
    /*                                                                        */
    /*    William E. Lamie, Microsoft Corporation                             */
    /*                                                                        */
    /*  DESCRIPTION                                                           */
    /*                                                                        */
    /*    This function preempts the specified core in situations where the   */
    /*    thread corresponding to this core is no longer ready or when the    */
    /*    core must be used for a higher-priority thread. If the specified is */
    /*    the current core, this processing is skipped since the will give up */
    /*    control subsequently on its own.                                    */
    /*                                                                        */
    /*  INPUT                                                                 */
    /*                                                                        */
    /*    core                                  The core to preempt           */
    /*                                                                        */
    /*  OUTPUT                                                                */
    /*                                                                        */
    /*    None                                                                */
    /*                                                                        */
    /*  CALLS                                                                 */
    /*                                                                        */
    /*    None                                                                */
    /*                                                                        */
    /*  CALLED BY                                                             */
    /*                                                                        */
    /*    ThreadX Source                                                      */
    /*                                                                        */
    /*  RELEASE HISTORY                                                       */
    /*                                                                        */
    /*    DATE              NAME                      DESCRIPTION             */
    /*                                                                        */
    /*  09-30-2020     William E. Lamie         Initial Version 6.1           */
    /*  01-31-2022     Andres Mlinar            Updated comments,             */
    /*                                             added ARMv8.2-A support,   */
    /*                                            resulting in version 6.1.10 */
    /*  10-31-2023     Tiejun Zhou              Modified comment(s), added    */
    /*                                            #include tx_user.h,         */
    /*                                            resulting in version 6.3.0  */
    /*                                                                        */
    /**************************************************************************/
        .global  _tx_thread_smp_core_preempt
        .type    _tx_thread_smp_core_preempt, @function
    _tx_thread_smp_core_preempt:
        DSB     ISH
        MOV     x2, #1
        LSL     x2, x2, x0                  // 目标 CPU bitmap
        LDR     x1, =GICD_BASE
        STR     w2, [x1, #GICD_SGIR_OFF]    // 写 GICD_SGIR
        RET
    
    ```

- 修改`tx_initialize_low_level.S`

  - ```
    /***************************************************************************
     * Copyright (c) 2024 Microsoft Corporation 
     * 
     * This program and the accompanying materials are made available under the
     * terms of the MIT License which is available at
     * https://opensource.org/licenses/MIT.
     * 
     * SPDX-License-Identifier: MIT
     **************************************************************************/
    
    
    /**************************************************************************/
    /**************************************************************************/
    /**                                                                       */
    /** ThreadX Component                                                     */
    /**                                                                       */
    /**   Initialize                                                          */
    /**                                                                       */
    /**************************************************************************/
    /**************************************************************************/
    #ifdef TX_INCLUDE_USER_DEFINE_FILE
    #include "tx_user.h"
    #endif
    
        .text
        .align 3
    
    // ============================================================
    // GIC-400 (GICv2) Register Definitions for ZynqMP
    // ============================================================
    GICD_BASE           =       0xF9010000
    GICC_BASE           =       0xF9020000
    
    GICD_CTLR_OFF       =       0x000
    GICD_ISENABLER_OFF  =       0x100
    GICD_ICENABLER_OFF  =       0x180
    GICD_IPRIORITYR_OFF =       0x400
    GICD_ITARGETSR_OFF  =       0x800
    GICD_ICFGR_OFF      =       0xC00
    GICD_SGIR_OFF       =       0xF00
    
    GICC_CTLR_OFF       =       0x000
    GICC_PMR_OFF        =       0x004
    GICC_BPR_OFF        =       0x008
    GICC_IAR_OFF        =       0x00C
    GICC_EOIR_OFF       =       0x010
    GICC_RPR_OFF        =       0x014
    GICC_HPPIR_OFF      =       0x018
    
    // ============================================================
    // Timer configuration
    // ============================================================
        .global timer_tick_reload
    
    // ============================================================
    // Global Symbols
    // ============================================================
        .global  _tx_initialize_low_level
        .global  _tx_thread_system_stack_ptr
        .global  _tx_initialize_unused_memory
        .global  _tx_thread_context_save
        .global  _tx_thread_context_restore
        .global  _tx_timer_interrupt
        .global  _end
        .global  tx_irq_dispatch
        .global  el3_vectors
    
    /**************************************************************************/
    /*                                                                        */
    /*  FUNCTION                                               RELEASE        */
    /*                                                                        */
    /*    _tx_initialize_low_level                           ARMv8-A-SMP      */
    /*                                                           6.3.0        */
    /*  DESCRIPTION                                                           */
    /*                                                                        */
    /*    This function is responsible for any low-level processor            */
    /*    initialization, including saving the system stack pointer for       */
    /*    use in ISR processing later, and finding the first available        */
    /*    RAM memory address for tx_application_define.                       */
    /*                                                                        */
    /*    Note: GIC and Timer initialization has been moved to bsp_init()     */
    /*    which uses Xilinx XScuGic APIs. Core 1's GIC CPU interface is      */
    /*    still set up in startup.S (secondaryCPUsInit).                      */
    /*                                                                        */
    /*  CALLED BY                                                             */
    /*                                                                        */
    /*    _tx_initialize_kernel_enter           ThreadX entry function        */
    /*                                                                        */
    /**************************************************************************/
    // VOID   _tx_initialize_low_level(VOID)
    // {
        .type    _tx_initialize_low_level, @function
    _tx_initialize_low_level:
    
        STP     x29, x30, [sp, #-16]!              // Save frame pointer and link register
    
        MSR     DAIFSet, 0x3                        // Lockout interrupts
    
        /* Save the system stack pointer.  */
    
        LDR     x0, =_tx_thread_system_stack_ptr    // Pickup address of system stack ptr
        MOV     x1, sp                              // Pickup SP
        SUB     x1, x1, #15                         //
        BIC     x1, x1, #0xF                        // Get 16-byte alignment
        STR     x1, [x0]                            // Store system stack
    
        /* Save the first available memory address.  */
    
        LDR     x0, =_tx_initialize_unused_memory   // Pickup address of unused memory ptr
        LDR     x1, =_end                           // Pickup end of BSS
        STR     x1, [x0]                            // Store unused memory address
    
        /* Setup vector table address.  */
        LDR     x0, =el3_vectors
        MSR     VBAR_EL3, x0
        ISB
    
        /* GIC / Timer init has been moved to bsp_init() */
    
        /* Done, return to caller.  */
        LDP     x29, x30, [sp], #16                // Restore frame pointer and link register
        RET                                         // Return to caller
    // }
    
    
    // ============================================================
    // GIC Helper Functions (called from startup.S and irqHandler)
    // ============================================================
    
    // ------------------------------------------------------------
    // void enableGIC(void)
    // Global enable of the Interrupt Distributor (GICD_CTLR)
    // ------------------------------------------------------------
        .global enableGIC
        .type  enableGIC, @function
    enableGIC:
        LDR     x0, =GICD_BASE
        LDR     w1, [x0, #GICD_CTLR_OFF]           // Read GICD_CTLR
        ORR     w1, w1, #0x01                       // Set enable bit
        STR     w1, [x0, #GICD_CTLR_OFF]           // Write GICD_CTLR
        RET
    
    // ------------------------------------------------------------
    // void disableGIC(void)
    // Global disable of the Interrupt Distributor
    // ------------------------------------------------------------
        .global disableGIC
        .type  disableGIC, @function
    disableGIC:
        LDR     x0, =GICD_BASE
        LDR     w1, [x0, #GICD_CTLR_OFF]
        BIC     w1, w1, #0x01
        STR     w1, [x0, #GICD_CTLR_OFF]
        RET
    
    // ------------------------------------------------------------
    // void enableGICProcessorInterface(void)
    // Enables the processor interface (GICC_CTLR)
    // ------------------------------------------------------------
        .global enableGICProcessorInterface
        .type  enableGICProcessorInterface, @function
    enableGICProcessorInterface:
        LDR     x0, =GICC_BASE
        LDR     w1, [x0, #GICC_CTLR_OFF]           // Read GICC_CTLR
        ORR     w1, w1, #0x03                       // Enable Group0 + Group1
        STR     w1, [x0, #GICC_CTLR_OFF]           // Write GICC_CTLR
        RET
    
    // ------------------------------------------------------------
    // void setPriorityMask(uint32_t mask)
    // x0 = priority mask value
    // ------------------------------------------------------------
        .global setPriorityMask
        .type  setPriorityMask, @function
    setPriorityMask:
        LDR     x1, =GICC_BASE
        STR     w0, [x1, #GICC_PMR_OFF]            // Write GICC_PMR
        RET
    
    // ------------------------------------------------------------
    // void enableIntID(uint32_t ID)
    // x0 = interrupt ID to enable
    // ------------------------------------------------------------
        .global enableIntID
        .type  enableIntID, @function
    enableIntID:
        MOV     w1, w0                              // Backup ID
        LSR     w2, w1, #5                          // w2 = ID / 32 (register index)
        LSL     w2, w2, #2                          // w2 *= 4 (byte offset)
        AND     w1, w1, #0x1F                       // w1 = bit position within register
        MOV     w3, #1
        LSL     w3, w3, w1                          // w3 = bit mask
        LDR     x0, =GICD_BASE
        ADD     x0, x0, #GICD_ISENABLER_OFF        // x0 = ISENABLER base
        STR     w3, [x0, x2]                        // Write GICD_ISENABLERn
        RET
    
    // ------------------------------------------------------------
    // void disableIntID(uint32_t ID)
    // x0 = interrupt ID to disable
    // ------------------------------------------------------------
        .global disableIntID
        .type  disableIntID, @function
    disableIntID:
        MOV     w1, w0
        LSR     w2, w1, #5
        LSL     w2, w2, #2
        AND     w1, w1, #0x1F
        MOV     w3, #1
        LSL     w3, w3, w1
        LDR     x0, =GICD_BASE
        ADD     x0, x0, #GICD_ICENABLER_OFF
        STR     w3, [x0, x2]
        RET
    
    // ------------------------------------------------------------
    // void setIntPriority(uint32_t ID, uint32_t priority)
    // x0 = interrupt ID,  x1 = priority (5-bit, 0 = highest)
    // ------------------------------------------------------------
        .global setIntPriority
        .type  setIntPriority, @function
    setIntPriority:
        MOV     x2, x0                              // x2 = ID
        AND     w1, w1, #0x1F                       // Mask to 5-bit priority
        LSL     w1, w1, #3                          // Shift to 8-bit priority field
    
        BIC     x3, x2, #0x03                       // x3 = ID & ~3 (word-aligned byte offset)
        ADD     x3, x3, #GICD_IPRIORITYR_OFF        // x3 += priority reg base
        AND     x2, x2, #0x03                       // x2 = byte index within word
        LSL     x2, x2, #3                          // x2 = bit offset (0, 8, 16, 24)
    
        MOV     w4, #0xFF
        LSL     w4, w4, w2                          // w4 = byte mask at correct position
        LSL     w1, w1, w2                          // w1 = priority at correct position
    
        LDR     x0, =GICD_BASE
        ADD     x0, x0, x3                          // x0 = absolute address of priority reg
        LDR     w5, [x0]                            // Read current value
        BIC     w5, w5, w4                          // Clear the target byte
        ORR     w5, w5, w1                          // Set new priority value
        STR     w5, [x0]                            // Write back
        RET
    
    // ------------------------------------------------------------
    // uint32_t readIntAck(void)
    // Returns the value of the Interrupt Acknowledge Register
    // ------------------------------------------------------------
        .global readIntAck
        .type  readIntAck, @function
    readIntAck:
        LDR     x0, =GICC_BASE
        LDR     w0, [x0, #GICC_IAR_OFF]            // Read GICC_IAR
        RET
    
    // ------------------------------------------------------------
    // void writeEOI(uint32_t ID)
    // x0 = interrupt ID to signal EOI
    // ------------------------------------------------------------
        .global writeEOI
        .type  writeEOI, @function
    writeEOI:
        LDR     x1, =GICC_BASE
        STR     w0, [x1, #GICC_EOIR_OFF]           // Write GICC_EOIR
        RET
    
    
    // ============================================================
    // ARM Generic Timer Functions
    // ============================================================
    
    // ------------------------------------------------------------
    // void init_generic_timer(uint64_t reload_value)
    // x0 = countdown value for CNTPS_TVAL_EL1
    // ------------------------------------------------------------
        .global init_generic_timer
        .type  init_generic_timer, @function
    init_generic_timer:
        MSR     CNTPS_TVAL_EL1, x0                   // Set countdown value
        RET
    
    // ------------------------------------------------------------
    // void start_generic_timer(void)
    // Enables the Secure EL1 Physical Timer
    // ------------------------------------------------------------
        .global start_generic_timer
        .type  start_generic_timer, @function
    start_generic_timer:
        MOV     x0, #1                              // Enable=1, IMASK=0, ISTATUS=0
        MSR     CNTPS_CTL_EL1, x0                    // Enable timer
        ISB
        RET
    
    // ------------------------------------------------------------
    // void stop_generic_timer(void)
    // Disables the Secure EL1 Physical Timer
    // ------------------------------------------------------------
        .global stop_generic_timer
        .type  stop_generic_timer, @function
    stop_generic_timer:
        MSR     CNTPS_CTL_EL1, xzr                   // Disable timer
        RET
    
    // ------------------------------------------------------------
    // void clear_generic_timer_irq(void)
    // Re-arms the timer by reloading CNTPS_TVAL_EL1 (clears IRQ)
    // ------------------------------------------------------------
        .global clear_generic_timer_irq
        .type  clear_generic_timer_irq, @function
    clear_generic_timer_irq:
        LDR     x0, =timer_tick_reload
        LDR     x0, [x0]
        MSR     CNTPS_TVAL_EL1, x0                   // Reload + clear interrupt
        DSB     SY
        RET
    
    
    // ============================================================
    // IRQ Handler (assembly)
    // Called from irqFirstLevelHandler in vectors.S
    // Shared by all cores
    // ============================================================
    
        .global irqHandler
        .type  irqHandler, @function
    irqHandler:
    
        STP     x29, x30, [sp, #-16]!              // Save frame pointer and link register
        STP     x19, x20, [sp, #-16]!              // Save callee-saved registers
    
        BL      readIntAck
        MOV     w19, w0                             // Save interrupt ID in w19
    
        CMP     w0, #29
        BNE     __not_timer_irq
    
        BL      clear_generic_timer_irq
        BL      _tx_timer_interrupt
        B       __irq_dispatch_done
    
    __not_timer_irq:
        MOV     w0, w19
        BL      tx_irq_dispatch
    
    __irq_dispatch_done:
        MOV     w0, w19
        BL      writeEOI
    
        LDP     x19, x20, [sp], #16                // Restore callee-saved registers
        LDP     x29, x30, [sp], #16                // Restore frame pointer and link register
        RET
    
    
    // ============================================================
    // FIQ Handler (placeholder)
    // ============================================================
    
        .global fiqHandler
        .type  fiqHandler, @function
    fiqHandler:
        RET
    
    
    // ============================================================
    // Reference symbols to bring in build info
    // ============================================================
    
    BUILD_OPTIONS:
        .word  _tx_build_options                    // Reference to bring in
    VERSION_ID:
        .word  _tx_version_id                       // Reference to bring in
    
        .align 3
    ```

- 修改`tx_execution_profile.h`

  - ```
    typedef unsigned long long              EXECUTION_TIME_SOURCE_TYPE;
    #define TX_EXECUTION_MAX_TIME_SOURCE     0xFFFFFFFFFFFFFFFF
    ```

- 源码bug修改

  - ```
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

- 修改链接脚本栈大小16KB，`0x4000`

- 修改`tx_user.h`

  - ```
    #define TX_TRACE_TIME_SOURCE                    _tx_thread_smp_time_get();
    #define TX_TRACE_TIME_MASK                      0xFFFFFFFFUL
    ```

- 修改`tx_port.h`

  - ```
    #define TX_THREAD_EXTENSION_3                                           \
        unsigned long long  tx_thread_execution_time_total;                 \
        unsigned long       tx_thread_execution_time_last_start;
    ```

- 修改`tx_port.h`

  - ```
    // 如果是双核
    #define TX_THREAD_SMP_MAX_CORES                 2
    #define TX_THREAD_SMP_CORE_MASK                 0x3
    // 如果是四核
    #define TX_THREAD_SMP_MAX_CORES                 4
    #define TX_THREAD_SMP_CORE_MASK                 0xF
    ```

- 修改`bsp_init.c`

  - ```
    #include "bsp_init.h"
    #include "xtime_l.h"
    #include "xpseudo_asm.h"
    
    #define TIMER_CLK_FREQ		XPAR_CPU_CORTEXA53_0_TIMESTAMP_CLK_FREQ
    #define TIMER_RELOAD		(TIMER_CLK_FREQ / 100)
    
    XScuGic xInterruptController;
    u64 timer_tick_reload = XPAR_CPU_CORTEXA53_0_TIMESTAMP_CLK_FREQ / 100;
    
    static void Global_Timer_Init()
    {
    	__asm__ __volatile__(
    		"MSR CNTFRQ_EL0, %0\n"
    		"ISB\n"
    		:: "r"((u64)XPAR_CPU_CORTEXA53_0_TIMESTAMP_CLK_FREQ)
    	);
    
    	XTime_StartTimer();
    }
    
    static void GIC_Init()
    {
    	XScuGic_Config *intc_cfg;
    	intc_cfg = XScuGic_LookupConfig(INTC_DEVICE_ID);
    	XScuGic_CfgInitialize(&xInterruptController, intc_cfg, intc_cfg->CpuBaseAddress);
    }
    
    static void HeartbeatTimer_Init()
    {
    	XScuGic_SetPriorityTriggerType(&xInterruptController, 29, 0xA0, 0);
    	XScuGic_Enable(&xInterruptController, 29);
    
    	mtcp(CNTPS_TVAL_EL1, (u64)TIMER_RELOAD);
    	mtcp(CNTPS_CTL_EL1, (u64)1);
    	__asm__ __volatile__("isb" ::: "memory");
    }
    
    static void SGI_Init()
    {
    	XScuGic_SetPriorityTriggerType(&xInterruptController, 0, 0, 0);
    	XScuGic_Enable(&xInterruptController, 0);
    }
    
    void tx_irq_dispatch(unsigned int int_id)
    {
    	extern XScuGic xInterruptController;
    
    	if(int_id < XSCUGIC_MAX_NUM_INTR_INPUTS)
    	{
    		XScuGic_VectorTableEntry * entry =
    			&xInterruptController.Config->HandlerTable[int_id];
    		entry->Handler(entry->CallBackRef);
    	}
    }
    
    void bsp_init(void)
    {
    	Global_Timer_Init();
    	GIC_Init();
    	HeartbeatTimer_Init();
    	SGI_Init();
    }
    ```



### 四、SDK配置

- 加入头文件搜索路径：
  - `ThreadX/Source/inc`
  - `ThreadX/Port/`
  - `./`
  - `BSP/`
  - `ThreadX/utility/execution_profile_kit`
- 加入宏`TX_INCLUDE_USER_DEFINE_FILE`，加入`tx_user.h`
- 编译时加入选项`-Wno-pointer-to-int-cast`，忽略位宽不匹配的警告

