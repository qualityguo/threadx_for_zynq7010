@; ------------------------------------------------------------
@; Cortex-A9 MPCore SMP Startup for Zynq 7010
@;
@; Based on: ARM Cortex-A15 MPCore SMP Prime Number Generator Example
@; Copyright (c) 2011-2012 ARM Ltd.  All rights reserved.
@; Adapted for Xilinx Zynq-7010 (Cortex-A9 dual-core) with ThreadX SMP.
@; ------------------------------------------------------------
@
@    PRESERVE8
@
@  AREA  StartUp,CODE,READONLY
@
@; Standard definitions of mode bits and interrupt (I&F) flags in PSRs
@
Mode_USR          =     0x10
Mode_FIQ          =     0x11
Mode_IRQ          =     0x12
Mode_SVC          =     0x13
Mode_ABT          =     0x17
Mode_UNDEF        =     0x1B
Mode_SYS          =     0x1F

I_Bit             =     0x80 @ when I bit is set, IRQ is disabled
F_Bit             =     0x40 @ when F bit is set, FIQ is disabled

SYS_MODE        =     0xDF
SVC_MODE        =     0xD3
IRQ_MODE        =     0xD2
ABT_MODE        =     0xD7
FIQ_MODE        =     0xD1

@; ------------------------------------------------------------
@; Zynq 7010 specific constants
@; ------------------------------------------------------------
@
VFPEnable       =       0x40000000              @ VFP enable value

@; L2 Cache Controller (PL310) offsets from 0xF8F02000
L2CC_BASE                =     0xF8F02000
L2CCControl              =     (L2CC_BASE + 0x0100)
L2CCAuxControl           =     (L2CC_BASE + 0x0104)
L2CCTAGLatReg            =     (L2CC_BASE + 0x0108)
L2CCDataLatReg           =     (L2CC_BASE + 0x010C)
L2CCWay                  =     (L2CC_BASE + 0x077C)
L2CCSync                 =     (L2CC_BASE + 0x0730)
L2CCIntRaw               =     (L2CC_BASE + 0x021C)
L2CCIntClear             =     (L2CC_BASE + 0x0220)

@; L2 configuration values (from Xilinx BSP)
L2CCAuxControlVal        =     0x72360000
L2CCControlVal           =     0x01
L2CCTAGLatency           =     0x0111
L2CCDataLatency          =     0x0121

@; SLCR registers
SLCRBase                 =     0xF8000000
SLCRUnlockReg            =     (SLCRBase + 0x08)
SLCRUnlockKey            =     0xDF0D
SLCRlockReg              =     (SLCRBase + 0x04)
SLCRlockKey              =     0x767B
SLCRL2cRamReg            =     (SLCRBase + 0xA1C)
SLCRL2cRamConfig         =     0x00020202

@; CPU1 start address (OCM location per Zynq TRM)
CPU1_START_ADDR          =     0xFFFFFFF0

@; Per-core stack slice sizes (must match linker script lscript.ld)
@;   SYS:  0x2000 total (only used during early boot mode-switching)
@;   IRQ:  0x1000 total = 2 * 0x800    SVC:  0x2000 total = 2 * 0x1000
@;   ABORT/FIQ/UNDEF: single-core sizes (exception debug only)
SYS_STACK_PER_CORE       =     0x1000
IRQ_STACK_PER_CORE       =     0x0800
SVC_STACK_PER_CORE       =     0x1000

@; ------------------------------------------------------------
@; Porting defines - L1 page table descriptor templates
@; ------------------------------------------------------------
@
L1_COHERENT       =     0x00014c06  @ Template descriptor for coherent memory
L1_NONCOHERENT    =     0x00000c1e  @ Template descriptor for non-coherent memory
L1_DEVICE         =     0x00000c06  @ Template descriptor for device memory

.section  .vectors, "ax"
.align 3
.cfi_sections .debug_frame

@; ------------------------------------------------------------
@; Vector Table
@; ------------------------------------------------------------
@
@  ENTRY
@
    .global _vector_table
_vector_table:
  B      Reset_Handler
  B      Undefined_Handler
  B      SVC_Handler
  B      Prefetch_Handler
  B      Abort_Handler
  B      .                         @ Reserved (no Hypervisor on Cortex-A9)
  B      IRQ_Handler
  B      FIQ_Handler

@; ------------------------------------------------------------
@; Handlers for unused exceptions
@; ------------------------------------------------------------
@
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

@; ------------------------------------------------------------
@; Imports
@; ------------------------------------------------------------
  .global   readIntAck
  .global   writeEOI
  .global   enableGIC
  .global   enableGICProcessorInterface
  .global   setPriorityMask
  .global   enableIntID
  .global   setIntPriority
  .global   joinSMP

  .global   invalidateCaches
  .global   disableHighVecs
  .global   _start

  .global   _tx_thread_smp_initialize_wait
  .global   _tx_thread_smp_release_cores_flag
  .global   _tx_thread_context_save
  .global   _tx_thread_context_restore
  .global   _tx_timer_interrupt
  .global   _tx_thread_smp_inter_core_interrupts

  .global   enableBranchPrediction
  .global   enableCaches

  .global   enableSCU
  .global   enableMaintenanceBroadcast
  .global   secureSCUInvalidate

  .global   clear_private_timer_irq
  .global   tx_irq_dispatch

  .global   MMUTable

@; ------------------------------------------------------------
@; IRQ Handler (ThreadX-aware)
@; ------------------------------------------------------------
@
  .section .text, "ax"
  .align 2
  .global IRQ_Handler
  .type   IRQ_Handler,function
IRQ_Handler:
  .global __tx_irq_processing_return
  B       _tx_thread_context_save
__tx_irq_processing_return:
  PUSH    {r4, r5}            @ Save preserved registers (r5 for 8-byte alignment)

  @ Acknowledge the interrupt
  BL      readIntAck
  MOV     r4, r0

  @ Check if Private Timer interrupt (ID 29)
  CMP     r0, #29                      @ Private Timer PPI ID
  BNE     irq_check_sgi

  @ Clear timer interrupt and call ThreadX timer ISR
  BL      clear_private_timer_irq
  DSB
  BL      _tx_timer_interrupt
  B       irq_done

irq_check_sgi:
  @ Dispatch other interrupts (SGI, shared peripherals) via Xilinx GIC
  MOV     r0, r4
  BL      tx_irq_dispatch

irq_done:
  @ Write end of interrupt reg
  MOV     r0, r4
  BL      writeEOI

  @ Jump to context restore to restore system context
  POP     {r4, r5}
  B       _tx_thread_context_restore


@; ------------------------------------------------------------
@; Reset Handler - Generic initialization, run by all CPUs
@; ------------------------------------------------------------
@
  .align 2
  .global Reset_Handler
  .type   Reset_Handler,function
Reset_Handler:

@  ;
@  ; Set ACTLR.SMP bit
@  ; ------------------
  BL      joinSMP

@;
@; Disable caches, MMU and branch prediction in case they were left enabled from an earlier run
@; This does not need to be done from a cold reset
@; ------------------------------------------------------------
  MRC     p15, 0, r0, c1, c0, 0       @ Read CP15 System Control register
  BIC     r0, r0, #(0x1 << 12)        @ Clear I bit 12 to disable I Cache
  BIC     r0, r0, #(0x1 <<  2)        @ Clear C bit  2 to disable D Cache
  BIC     r0, r0, #0x1                @ Clear M bit  0 to disable MMU
  BIC     r0, r0, #(0x1 << 11)        @ Clear Z bit 11 to disable branch prediction
  MCR     p15, 0, r0, c1, c0, 0       @ Write CP15 System Control register

@; The MMU is enabled later, before calling main().  Caches and branch prediction are enabled inside main(),
@; after the MMU has been enabled and scatterloading has been performed.
@
@  ;
@  ; Setup stacks
@  ;---------------

  MRC     p15, 0, r0, c0, c0, 5       @ Read CPU ID register
  ANDS    r0, r0, #0x03               @ Mask off, leaving the CPU ID field

@; Stack symbols point to the TOP of the total stack area (defined in lscript.ld).
@; Each core offsets downward by CPU_ID * per_core_slice.

  MOV     r1, #IRQ_MODE                         @ Build IRQ mode CPSR
  MSR     CPSR_c, r1                            @ Enter IRQ mode
  LDR     r1, =__irq_stack                      @ IRQ stacks for CPU 0,1
  SUB     r1, r1, r0, LSL #11                   @ CPU_ID * 0x800 (IRQ_STACK_PER_CORE)
  MOV     sp, r1

  MOV     r1, #SVC_MODE                         @ Build SVC mode CPSR
  MSR     CPSR_c, r1                            @ Enter SVC mode
  LDR     r1, =__supervisor_stack               @ SVC stacks for CPU 0,1
  SUB     r1, r1, r0, LSL #12                   @ CPU_ID * 0x1000 (SVC_STACK_PER_CORE)
  MOV     sp, r1

  MOV     r1, #ABT_MODE                         @ Build ABT mode CPSR
  MSR     CPSR_c, r1                            @ Enter ABT mode
  LDR     r1, =__abort_stack                    @ ABORT stack (shared, debug only)
  MOV     sp, r1

  MOV     r1, #FIQ_MODE                         @ Build FIQ mode CPSR
  MSR     CPSR_c, r1                            @ Enter FIQ mode
  LDR     r1, =__fiq_stack                      @ FIQ stack (shared, debug only)
  MOV     sp, r1

  MOV     r1, #SYS_MODE                         @ Build SYS mode CPSR
  MSR     CPSR_c, r1                            @ Enter SYS mode
  LDR     r1, =__stack                          @ App stacks for all CPUs
  SUB     r1, r1, r0, LSL #12                   @ CPU_ID * 0x1000 (SYS_STACK_PER_CORE)
  MOV     sp, r1

@; Switch back to SVC mode for ThreadX kernel (SYS mode has no SPSR,
@; which breaks the interrupt-return dispatch path in _tx_thread_schedule)
  MOV     r1, #SVC_MODE
  MSR     CPSR_c, r1

@  ;
@  ; Set vector base address
@  ; ------------------------
  LDR     r0, =_vector_table
  MCR     p15, 0, r0, c12, c0, 0 @ Write Secure or Non-secure Vector Base Address
  BL      disableHighVecs @ Ensure that V-bit is cleared

@  ;
@  ; Invalidate caches
@  ; ------------------
  BL      invalidateCaches

@  ;
@  ; Clear Branch Prediction Array
@  ; ------------------------------
  MOV     r0, #0x0
  MCR     p15, 0, r0, c7, c5, 6     @ BPIALL - Invalidate entire branch predictor array

@
@  ; Invalidate TLBs
@  ;------------------
  MOV     r0, #0x0
  MCR     p15, 0, r0, c8, c7, 0     @ TLBIALL - Invalidate entire Unified TLB

@  ;
@  ; Set up Domain Access Control Reg
@  ; ----------------------------------
@  ; b00 - No Access (abort)
@  ; b01 - Client (respect table entry)
@  ; b10 - RESERVED
@  ; b11 - Manager (ignore access permissions)

  MRC     p15, 0, r0, c3, c0, 0      @ Read Domain Access Control Register
  LDR     r0, =0x55555555            @ Initialize every domain entry to b01 (client)
  MCR     p15, 0, r0, c3, c0, 0      @ Write Domain Access Control Register

@  ;
@  ; Set TTB0 to shared MMUTable (from BSP translation_table.S)
@  ; -----------------------------------------------------------
@  ; All cores share the same page table for Zynq 7010 SMP.
@  ; The BSP translation_table.S pre-populates all Zynq address mappings.
@  ;
  LDR     r0, =MMUTable
  ORR     r0, r0, #0x5B              @ Outer write-back cacheable, inner non-cacheable
  MCR     p15, 0, r0, c2, c0, 0

@  ;
@  ; Barrier
@  ; --------
  DSB

@  ;
@  ; Enable MMU
@  ;-------------
@  ; Leaving the caches disabled until later.
  MRC     p15, 0, r0, c1, c0, 0       @ Read CP15 System Control register
  BIC     r0, r0, #(0x1 << 12)        @ Clear I bit 12 to disable I Cache
  BIC     r0, r0, #(0x1 <<  2)        @ Clear C bit  2 to disable D Cache
  BIC     r0, r0, #0x2                @ Clear A bit  1 to disable strict alignment fault checking
  ORR     r0, r0, #0x1                @ Set M bit 0 to enable MMU before scatter loading
  MCR     p15, 0, r0, c1, c0, 0       @ Write CP15 System Control register
  DSB
  ISB

@  ;
@  ; MMU now enabled - Virtual address system now active
@  ;

@  ;
@  ; Enable VFP
@  ; ------------
  MRC     p15, 0, r1, c1, c0, 2               @ r1 = Access Control Register
  ORR     r1, r1, #(0xf << 20)                @ Enable full access for p10,11
  MCR     p15, 0, r1, c1, c0, 2               @ Access Control Register = r1
  ISB
  MOV     r0, #VFPEnable                      @ Enable VFP itself
  FMXR    FPEXC, r0                           @ FPEXC = r0

  LDR     r0, =_tx_thread_smp_release_cores_flag         @ Build address of release cores flag
  MOV     r1, #0
  STR     r1, [r0]

@  ;
@  ; SMP initialization
@  ; -------------------
  MRC     p15, 0, r0, c0, c0, 5     @ Read CPU ID register
  ANDS    r0, r0, #0x03             @ Mask off, leaving the CPU ID field
  BEQ     primaryCPUInit
  BNE     secondaryCPUsInit



@; ------------------------------------------------------------
@; Initialization for PRIMARY CPU
@; ------------------------------------------------------------
@
  .align 2
  .global primaryCPUInit
  .type   primaryCPUInit,function
primaryCPUInit:

@  ;
@  ; SCU Initialization
@  ; --------------------
  BL      enableSCU
  BL      enableMaintenanceBroadcast

@  ;
@  ; Invalidate SCU tag RAMs for both cores
@  ; -----------------------------------------
  MOV     r0, #0                       @ CPU 0
  MOV     r1, #0x0F                    @ All ways
  BL      secureSCUInvalidate
  MOV     r0, #1                       @ CPU 1
  MOV     r1, #0x0F                    @ All ways
  BL      secureSCUInvalidate

@  ;
@  ; L2 Cache Initialization (PL310)
@  ; ---------------------------------
@
@  ; Disable L2
  LDR     r0, =L2CCControl
  MOV     r1, #0
  STR     r1, [r0]

@  ; Configure auxiliary control register
  LDR     r0, =L2CCAuxControl
  LDR     r1, [r0]
  LDR     r2, =L2CCAuxControlVal
  ORR     r1, r1, r2
  STR     r1, [r0]

@  ; Configure TAG latency
  LDR     r0, =L2CCTAGLatReg
  LDR     r1, =L2CCTAGLatency
  STR     r1, [r0]

@  ; Configure DATA latency
  LDR     r0, =L2CCDataLatReg
  LDR     r1, =L2CCDataLatency
  STR     r1, [r0]

@  ; Invalidate all L2 ways
  LDR     r0, =L2CCWay
  LDR     r2, =0xFFFF
  STR     r2, [r0]

@  ; Wait for invalidate to complete
  LDR     r0, =L2CCSync
l2_sync_wait:
  LDR     r1, [r0]
  CMP     r1, #0
  BNE     l2_sync_wait

@  ; Clear pending L2 interrupts
  LDR     r0, =L2CCIntRaw
  LDR     r1, [r0]
  LDR     r0, =L2CCIntClear
  STR     r1, [r0]

@  ; Configure SLCR L2C RAM
  LDR     r0, =SLCRUnlockReg
  LDR     r1, =SLCRUnlockKey
  STR     r1, [r0]
  LDR     r0, =SLCRL2cRamReg
  LDR     r1, =SLCRL2cRamConfig
  STR     r1, [r0]
  LDR     r0, =SLCRlockReg
  LDR     r1, =SLCRlockKey
  STR     r1, [r0]

@  ; Enable L2 cache
  LDR     r0, =L2CCControl
  LDR     r1, [r0]
  MOV     r2, #L2CCControlVal
  ORR     r1, r1, r2
  STR     r1, [r0]

@  ;
@  ; Enable caches and branch prediction
@  ; --------------------------------------
  BL      enableCaches

@  ; Enable branch prediction
  MRC     p15, 0, r0, c1, c0, 0
  ORR     r0, r0, #(0x1 << 11)
  MCR     p15, 0, r0, c1, c0, 0

@  ; Enable D-side prefetch and L2 prefetch hint
  MRC     p15, 0, r0, c1, c0, 1
  ORR     r0, r0, #(0x1 << 2)         @ D-side prefetch
  ORR     r0, r0, #(0x1 << 1)         @ L2 prefetch hint
  MCR     p15, 0, r0, c1, c0, 1

@  ;
@  ; Start CPU1
@  ; ------------
@  ; Write entry point to CPU1 start address (Zynq TRM: 0xFFFFFFF0)
@  ; 0xFFFFFFF0 is in OCM mapped as cacheable. Must push the write
@  ; to physical memory so CPU1 (caches off, boot ROM state) can see it.
  LDR     r0, =CPU1_START_ADDR
  LDR     r1, =_vector_table
  STR     r1, [r0]
  MCR     p15, 0, r0, c7, c10, 1       @ DCCMVAC: clean cache line to PoC
  DSB                                  @ Wait for clean to complete
  SEV                                  @ Wake CPU1 from WFE

@  ;
@  ; Branch to C lib code
@  ; ----------------------
  B       _start



@; ------------------------------------------------------------
@; Initialization for SECONDARY CPUs
@; ------------------------------------------------------------
@
  .align 2
  .global secondaryCPUsInit
  .type   secondaryCPUsInit,function
secondaryCPUsInit:

@  ;
@  ; GIC Init
@  ; ---------
  BL      enableGICProcessorInterface

  MOV     r0, #0xF0                 @ Priority mask (0xF0 = Xilinx default)
  BL      setPriorityMask

@  ;
@  ; Enable SGI for inter-core interrupts
@  ; --------------------------------------
  MOV     r0, #0                     @ SGI ID 0 (inter-core)
  BL      enableIntID

  MOV     r0, #0                     @ SGI ID 0 (inter-core)
  MOV     r1, #0                    @ Priority
  BL      setIntPriority

@  ;
@  ; Enable caches
@  ; ---------------
  BL      enableCaches

@  ;
@  ; Branch to ThreadX SMP wait loop
@  ; ---------------------------------
  B       _tx_thread_smp_initialize_wait


@; ------------------------------------------------------------
@; End of code
@; ------------------------------------------------------------

@; ------------------------------------------------------------
@; End of startup.S
@; ------------------------------------------------------------
  .end
