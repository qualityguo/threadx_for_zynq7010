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
