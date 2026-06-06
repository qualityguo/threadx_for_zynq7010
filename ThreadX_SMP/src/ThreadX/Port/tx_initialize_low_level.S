// ThreadX SMP Low-Level Init for Zynq 7010
// Called from tx_kernel_enter() on core 0 only.
// GIC / Timer / SGI init has been moved to bsp_init().

    .global      _tx_thread_system_stack_ptr
    .global      _tx_initialize_unused_memory
    .global      _tx_version_id
    .global      _tx_build_options
    .global      _end

    .arm
    .text
    .align 2

    .global _tx_initialize_low_level
    .type _tx_initialize_low_level,function
_tx_initialize_low_level:
    PUSH    {lr}

    // Sanity check: must be core 0
    MRC     p15, 0, r0, c0, c0, 5
    AND     r0, r0, #0x03
    CMP     r0, #0
    BNE     _low_level_panic

    // Save unused memory pointer (_end + 8)
    LDR     r0, =_end
    LDR     r2, =_tx_initialize_unused_memory
    ADD     r0, r0, #8
    STR     r0, [r2]
    B       _low_level_done

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
