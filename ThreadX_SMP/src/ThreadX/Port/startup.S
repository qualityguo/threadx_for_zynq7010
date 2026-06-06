// Cortex-A9 MPCore SMP Startup for Zynq 7010
// Adapted for Xilinx Zynq-7010 dual-core with ThreadX SMP.

Mode_USR          =     0x10
Mode_FIQ          =     0x11
Mode_IRQ          =     0x12
Mode_SVC          =     0x13
Mode_ABT          =     0x17
Mode_UNDEF        =     0x1B
Mode_SYS          =     0x1F

I_Bit             =     0x80
F_Bit             =     0x40

SYS_MODE        =     0xDF
SVC_MODE        =     0xD3
IRQ_MODE        =     0xD2
ABT_MODE        =     0xD7
FIQ_MODE        =     0xD1

VFPEnable       =       0x40000000

// L2 Cache Controller (PL310)
L2CC_BASE                =     0xF8F02000
L2CCControl              =     (L2CC_BASE + 0x0100)
L2CCAuxControl           =     (L2CC_BASE + 0x0104)
L2CCTAGLatReg            =     (L2CC_BASE + 0x0108)
L2CCDataLatReg           =     (L2CC_BASE + 0x010C)
L2CCWay                  =     (L2CC_BASE + 0x077C)
L2CCSync                 =     (L2CC_BASE + 0x0730)
L2CCIntRaw               =     (L2CC_BASE + 0x021C)
L2CCIntClear             =     (L2CC_BASE + 0x0220)

L2CCAuxControlVal        =     0x72360000
L2CCControlVal           =     0x01
L2CCTAGLatency           =     0x0111
L2CCDataLatency          =     0x0121

// SLCR registers
SLCRBase                 =     0xF8000000
SLCRUnlockReg            =     (SLCRBase + 0x08)
SLCRUnlockKey            =     0xDF0D
SLCRlockReg              =     (SLCRBase + 0x04)
SLCRlockKey              =     0x767B
SLCRL2cRamReg            =     (SLCRBase + 0xA1C)
SLCRL2cRamConfig         =     0x00020202

CPU1_START_ADDR          =     0xFFFFFFF0

// Per-core stack offsets: total_size / num_cores (from linker script symbols)

L1_COHERENT       =     0x00014c06
L1_NONCOHERENT    =     0x00000c1e
L1_DEVICE         =     0x00000c06

.section  .vectors, "ax"
.align 3
.cfi_sections .debug_frame

// ============================================================
// Vector Table
// ============================================================
    .global _vector_table
_vector_table:
  B      Reset_Handler
  B      Undefined_Handler
  B      SVC_Handler
  B      Prefetch_Handler
  B      Abort_Handler
  B      .
  B      IRQ_Handler
  B      FIQ_Handler

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

// ============================================================
// Imports
// ============================================================
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

// ============================================================
// IRQ Handler
// ============================================================
  .section .text, "ax"
  .align 2
  .global IRQ_Handler
  .type   IRQ_Handler,function
IRQ_Handler:
  .global __tx_irq_processing_return
  B       _tx_thread_context_save
__tx_irq_processing_return:
  PUSH    {r4, r5}                    // r5 for 8-byte alignment

  BL      readIntAck                   // get interrupt ID
  MOV     r4, r0

  CMP     r0, #29                     // Private Timer?
  BNE     irq_check_sgi

  BL      clear_private_timer_irq
  DSB
  BL      _tx_timer_interrupt
  B       irq_done

irq_check_sgi:
  MOV     r0, r4                      // dispatch via XScuGic HandlerTable
  BL      tx_irq_dispatch

irq_done:
  MOV     r0, r4
  BL      writeEOI
  POP     {r4, r5}
  B       _tx_thread_context_restore


// ============================================================
// Reset Handler (all CPUs enter here)
// ============================================================
  .align 2
  .global Reset_Handler
  .type   Reset_Handler,function
Reset_Handler:

  BL      joinSMP                     // set ACTLR.SMP bit

  // Disable caches, MMU, branch prediction
  MRC     p15, 0, r0, c1, c0, 0
  BIC     r0, r0, #(0x1 << 12)
  BIC     r0, r0, #(0x1 <<  2)
  BIC     r0, r0, #0x1
  BIC     r0, r0, #(0x1 << 11)
  MCR     p15, 0, r0, c1, c0, 0

  // Read CPU ID -> r0
  MRC     p15, 0, r0, c0, c0, 5
  ANDS    r0, r0, #0x03

  // Set per-core stacks: sp = __xxx_stack - CPU_ID * (total_size / 2)

  // IRQ stack
  MOV     r1, #IRQ_MODE
  MSR     CPSR_c, r1
  LDR     r1, =__irq_stack
  LDR     r3, =_IRQ_STACK_SIZE
  LSR     r3, r3, #1                  // per_core = total / 2
  MUL     r3, r0, r3                  // offset = CPU_ID * per_core
  SUB     sp, r1, r3

  // SVC stack
  MOV     r1, #SVC_MODE
  MSR     CPSR_c, r1
  LDR     r1, =__supervisor_stack
  LDR     r3, =_SUPERVISOR_STACK_SIZE
  LSR     r3, r3, #1
  MUL     r3, r0, r3
  SUB     sp, r1, r3

  // ABT stack (shared)
  MOV     r1, #ABT_MODE
  MSR     CPSR_c, r1
  LDR     r1, =__abort_stack
  MOV     sp, r1

  // FIQ stack (shared)
  MOV     r1, #FIQ_MODE
  MSR     CPSR_c, r1
  LDR     r1, =__fiq_stack
  MOV     sp, r1

  // SYS stack
  MOV     r1, #SYS_MODE
  MSR     CPSR_c, r1
  LDR     r1, =__stack
  LDR     r3, =_STACK_SIZE
  LSR     r3, r3, #1
  MUL     r3, r0, r3
  SUB     sp, r1, r3

  // Switch to SVC mode for ThreadX
  MOV     r1, #SVC_MODE
  MSR     CPSR_c, r1

  // Set VBAR
  LDR     r0, =_vector_table
  MCR     p15, 0, r0, c12, c0, 0
  BL      disableHighVecs

  // Invalidate caches and TLB
  BL      invalidateCaches
  MOV     r0, #0x0
  MCR     p15, 0, r0, c7, c5, 6      // invalidate branch predictor
  MOV     r0, #0x0
  MCR     p15, 0, r0, c8, c7, 0      // invalidate TLB

  // Domain Access Control: all domains = client
  MRC     p15, 0, r0, c3, c0, 0
  LDR     r0, =0x55555555
  MCR     p15, 0, r0, c3, c0, 0

  // Set TTB0 to shared MMUTable
  LDR     r0, =MMUTable
  ORR     r0, r0, #0x5B
  MCR     p15, 0, r0, c2, c0, 0
  DSB

  // Enable MMU (caches still off)
  MRC     p15, 0, r0, c1, c0, 0
  BIC     r0, r0, #(0x1 << 12)
  BIC     r0, r0, #(0x1 <<  2)
  BIC     r0, r0, #0x2
  ORR     r0, r0, #0x1
  MCR     p15, 0, r0, c1, c0, 0
  DSB
  ISB

  // Enable VFP
  MRC     p15, 0, r1, c1, c0, 2
  ORR     r1, r1, #(0xf << 20)
  MCR     p15, 0, r1, c1, c0, 2
  ISB
  MOV     r0, #VFPEnable
  FMXR    FPEXC, r0

  // Clear release cores flag
  LDR     r0, =_tx_thread_smp_release_cores_flag
  MOV     r1, #0
  STR     r1, [r0]

  // Branch by CPU ID
  MRC     p15, 0, r0, c0, c0, 5
  ANDS    r0, r0, #0x03
  BEQ     primaryCPUInit
  BNE     secondaryCPUsInit



// ============================================================
// Primary CPU (core 0)
// ============================================================
  .align 2
  .global primaryCPUInit
  .type   primaryCPUInit,function
primaryCPUInit:

  // SCU init
  BL      enableSCU
  BL      enableMaintenanceBroadcast

  // Invalidate SCU tag RAMs for both cores
  MOV     r0, #0
  MOV     r1, #0x0F
  BL      secureSCUInvalidate
  MOV     r0, #1
  MOV     r1, #0x0F
  BL      secureSCUInvalidate

  // L2 Cache init (PL310)
  LDR     r0, =L2CCControl
  MOV     r1, #0
  STR     r1, [r0]                    // disable L2

  LDR     r0, =L2CCAuxControl
  LDR     r1, [r0]
  LDR     r2, =L2CCAuxControlVal
  ORR     r1, r1, r2
  STR     r1, [r0]

  LDR     r0, =L2CCTAGLatReg
  LDR     r1, =L2CCTAGLatency
  STR     r1, [r0]

  LDR     r0, =L2CCDataLatReg
  LDR     r1, =L2CCDataLatency
  STR     r1, [r0]

  LDR     r0, =L2CCWay
  LDR     r2, =0xFFFF
  STR     r2, [r0]                    // invalidate all ways

  LDR     r0, =L2CCSync
l2_sync_wait:
  LDR     r1, [r0]
  CMP     r1, #0
  BNE     l2_sync_wait

  LDR     r0, =L2CCIntRaw
  LDR     r1, [r0]
  LDR     r0, =L2CCIntClear
  STR     r1, [r0]

  // Configure SLCR L2C RAM
  LDR     r0, =SLCRUnlockReg
  LDR     r1, =SLCRUnlockKey
  STR     r1, [r0]
  LDR     r0, =SLCRL2cRamReg
  LDR     r1, =SLCRL2cRamConfig
  STR     r1, [r0]
  LDR     r0, =SLCRlockReg
  LDR     r1, =SLCRlockKey
  STR     r1, [r0]

  // Enable L2 cache
  LDR     r0, =L2CCControl
  LDR     r1, [r0]
  MOV     r2, #L2CCControlVal
  ORR     r1, r1, r2
  STR     r1, [r0]

  // Enable caches + branch prediction + prefetch
  BL      enableCaches

  MRC     p15, 0, r0, c1, c0, 0       // branch prediction
  ORR     r0, r0, #(0x1 << 11)
  MCR     p15, 0, r0, c1, c0, 0

  MRC     p15, 0, r0, c1, c0, 1       // D-side + L2 prefetch
  ORR     r0, r0, #(0x1 << 2)
  ORR     r0, r0, #(0x1 << 1)
  MCR     p15, 0, r0, c1, c0, 1

  // Start CPU1: write entry to 0xFFFFFFF0 + clean + SEV
  LDR     r0, =CPU1_START_ADDR
  LDR     r1, =_vector_table
  STR     r1, [r0]
  MCR     p15, 0, r0, c7, c10, 1      // DCCMVAC
  DSB
  SEV

  B       _start



// ============================================================
// Secondary CPUs (core 1)
// GIC config uses CP15 addressing (no C global variable dependency)
// ============================================================
  .align 2
  .global secondaryCPUsInit
  .type   secondaryCPUsInit,function
secondaryCPUsInit:

  BL      enableGICProcessorInterface

  MOV     r0, #0xF0
  BL      setPriorityMask

  MOV     r0, #0                      // SGI 0
  BL      enableIntID

  MOV     r0, #0
  MOV     r1, #0
  BL      setIntPriority

  BL      enableCaches

  // Branch prediction + prefetch (match primaryCPUInit)
  MRC     p15, 0, r0, c1, c0, 0
  ORR     r0, r0, #(0x1 << 11)
  MCR     p15, 0, r0, c1, c0, 0

  MRC     p15, 0, r0, c1, c0, 1
  ORR     r0, r0, #(0x1 << 2)
  ORR     r0, r0, #(0x1 << 1)
  MCR     p15, 0, r0, c1, c0, 1

  B       _tx_thread_smp_initialize_wait

  .end
