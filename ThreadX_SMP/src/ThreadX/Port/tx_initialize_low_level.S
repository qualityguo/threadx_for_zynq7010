@/***************************************************************************
@ * ThreadX SMP Low-Level Initialization for Zynq 7010 (Cortex-A9 MPCore)
@ *
@ * Called from tx_kernel_enter() on core 0 only.
@ * Core 1's GIC CPU interface is set up in startup.S secondaryCPUsInit.
@ **************************************************************************/

@; ------------------------------------------------------------
@; Constants
@; ------------------------------------------------------------
@; Private Timer PPI ID: 29
@; SGI for inter-core:   0
@; Timer load value:      3333333 (333MHz / 100Hz = 10ms tick)

@; ------------------------------------------------------------
@; Imports
@; ------------------------------------------------------------
    .global      _tx_thread_system_stack_ptr
    .global      _tx_initialize_unused_memory
    .global      _tx_version_id
    .global      _tx_build_options
    .global      _end

    .global      enableGIC
    .global      enableGICProcessorInterface
    .global      setPriorityMask
    .global      enableIntID
    .global      setIntPriority
    .global      init_private_timer
    .global      start_private_timer

    .arm
    .text
    .align 2

@/**************************************************************************/
@/*  _tx_initialize_low_level                                             */
@/*                                                                        */
@/*  Called by tx_kernel_enter() on core 0.                                */
@/*  Sets up GIC, private timer, SGI, and unused memory pointer.          */
@/**************************************************************************/
    .global _tx_initialize_low_level
    .type _tx_initialize_low_level,function
_tx_initialize_low_level:

    @; ---- Save return address ----
    PUSH    {lr}

    @; ---- Sanity check: this function must only be called on core 0 ----
    MRC     p15, 0, r0, c0, c0, 5
    AND     r0, r0, #0x03
    CMP     r0, #0
    BEQ     _low_level_core0
    B       _low_level_panic

@; ------------------------------------------------------------
@; Core 0: Full GIC + Timer + SGI + unused memory
@; ------------------------------------------------------------
_low_level_core0:

    @; Enable GIC distributor (global)
    BL      enableGIC

    @; Enable GIC CPU interface (per-core, core 0)
    BL      enableGICProcessorInterface

    @; Set priority mask (0xF0 = Xilinx default)
    MOV     r0, #0xF0
    BL      setPriorityMask

    @; ---- Private Timer (PPI ID 29) ----

    MOV     r0, #29
    MOV     r1, #0
    BL      enableIntID

    MOV     r0, #29
    MOV     r1, #0
    BL      setIntPriority

    LDR     r0, =3333333
    MOV     r1, #0                       @; 0 = auto-reload
    BL      init_private_timer
    BL      start_private_timer

    @; ---- SGI for inter-core interrupts (ID 0) ----

    MOV     r0, #0                     @; SGI ID 0 (inter-core)
    BL      enableIntID

    MOV     r0, #0                     @; SGI ID 0 (inter-core)
    MOV     r1, #0
    BL      setIntPriority

    @; ---- Save unused memory pointer ----
    LDR     r0, =_end
    LDR     r2, =_tx_initialize_unused_memory
    ADD     r0, r0, #8
    STR     r0, [r2, #0]

    B       _low_level_done

@; ------------------------------------------------------------
@; Panic: non-core-0 reached _tx_initialize_low_level (should never happen)
@; ------------------------------------------------------------
_low_level_panic:
    WFE
    B       _low_level_panic

_low_level_done:
    POP     {lr}
    BX      lr

BUILD_OPTIONS:
    .word  _tx_build_options
VERSION_ID:
    .word  _tx_version_id
