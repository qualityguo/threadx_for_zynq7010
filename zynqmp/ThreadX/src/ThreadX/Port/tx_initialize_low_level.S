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

// ------------------------------------------------------------
// GIC-400 (GICv2) Register Definitions for ZynqMP
// ------------------------------------------------------------
GICD_BASE           =       0xF9010000
GICC_BASE           =       0xF9020000

// GIC Distributor Register Offsets
GICD_CTLR_OFF       =       0x000
GICD_ISENABLER_OFF  =       0x100
GICD_ICENABLER_OFF  =       0x180
GICD_IPRIORITYR_OFF =       0x400
GICD_ITARGETSR_OFF  =       0x800
GICD_ICFGR_OFF      =       0xC00
GICD_SGIR_OFF       =       0xF00

// GIC CPU Interface Register Offsets
GICC_CTLR_OFF       =       0x000
GICC_PMR_OFF        =       0x004
GICC_BPR_OFF        =       0x008
GICC_IAR_OFF        =       0x00C
GICC_EOIR_OFF       =       0x010
GICC_RPR_OFF        =       0x014
GICC_HPPIR_OFF      =       0x018


// GTC
    .global timer_tick_reload

// ------------------------------------------------------------
// Global Symbols
// ------------------------------------------------------------
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
/*    _tx_initialize_low_level                             ARMv8-A        */
/*                                                           6.3.0        */
/*  AUTHOR                                                                */
/*                                                                        */
/*    William E. Lamie, Microsoft Corporation                             */
/*                                                                        */
/*  DESCRIPTION                                                           */
/*                                                                        */
/*    This function is responsible for any low-level processor            */
/*    initialization, including setting up interrupt vectors, setting     */
/*    up a periodic timer interrupt source, saving the system stack       */
/*    pointer for use in ISR processing later, and finding the first      */
/*    available RAM memory address for tx_application_define.             */
/*                                                                        */
/*  INPUT                                                                 */
/*                                                                        */
/*    None                                                                */
/*                                                                        */
/*  OUTPUT                                                                */
/*                                                                        */
/*    None                                                                */
/*                                                                        */
/*  CALLS                                                                 */
/*                                                                        */
/*    enableGIC                     Enable GIC Distributor                */
/*    enableGICProcessorInterface   Enable GIC CPU Interface              */
/*    setPriorityMask               Set CPU priority mask                 */
/*    enableIntID                   Enable an interrupt source            */
/*    setIntPriority                Set interrupt priority                */
/*    init_generic_timer            Initialize ARM Generic Timer          */
/*    start_generic_timer           Start ARM Generic Timer               */
/*                                                                        */
/*  CALLED BY                                                             */
/*                                                                        */
/*    _tx_initialize_kernel_enter           ThreadX entry function        */
/*                                                                        */
/*  RELEASE HISTORY                                                       */
/*                                                                        */
/*    DATE              NAME                      DESCRIPTION             */
/*                                                                        */
/*  09-30-2020     William E. Lamie         Initial Version 6.1           */
/*  01-31-2022     Andres Mlinar            Updated comments,             */
/*                                            resulting in version 6.1.10 */
/*  10-31-2023     Tiejun Zhou              Modified comment(s), added    */
/*                                            #include tx_user.h,         */
/*                                            resulting in version 6.3.0  */
/*                                                                        */
/**************************************************************************/
// VOID   _tx_initialize_low_level(VOID)
// {
    .type    _tx_initialize_low_level, @function
_tx_initialize_low_level:

    STP     x29, x30, [sp, #-16]!              // Save frame pointer and link register

    MSR     DAIFSet, 0x3                        // Lockout interrupts

    /* Save the system stack pointer.  */
    // _tx_thread_system_stack_ptr = (VOID_PTR) (sp);

    LDR     x0, =_tx_thread_system_stack_ptr    // Pickup address of system stack ptr
    MOV     x1, sp                              // Pickup SP
    SUB     x1, x1, #15                         //
    BIC     x1, x1, #0xF                        // Get 16-byte alignment
    STR     x1, [x0]                            // Store system stack

    /* Save the first available memory address.  */
    // _tx_initialize_unused_memory =  (VOID_PTR) _end;

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
// GIC Helper Functions
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
// Replaces the C version in timer_interrupts.c
// Called from irqFirstLevelHandler in vectors.S
// ============================================================

    .global irqHandler
    .type  irqHandler, @function
irqHandler:

    STP     x29, x30, [sp, #-16]!              // Save frame pointer and link register
    STP     x19, x20, [sp, #-16]!              // Save callee-saved registers

    /* Read Interrupt Acknowledge Register to get interrupt ID.  */
    BL      readIntAck
    MOV     w19, w0                             // Save interrupt ID in w19

    /* Check if this is the timer interrupt.  */
    CMP     w0, #29
    BNE     __not_timer_irq

    /* Timer interrupt: clear and re-arm, then call ThreadX timer handler.  */
    BL      clear_generic_timer_irq
    BL      _tx_timer_interrupt
    B       __irq_dispatch_done

__not_timer_irq:
    /* Not timer: dispatch to user interrupt handler (bsp_init.c).  */
    MOV     w0, w19
    BL      tx_irq_dispatch

__irq_dispatch_done:
    /* Signal End of Interrupt.  */
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
