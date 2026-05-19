---
name: zynq-device-driver
description: Use when creating new device drivers for ZYNQ peripherals following the device_core framework, adding LED, KEY, UART, SPI, I2C drivers, or implementing interrupt-driven drivers using the notify callback mechanism
---

# ZYNQ Device Driver Development

Reference implementations: `led_driver.c` (polling output), `key_driver.c` (interrupt input with notify), `uart_driver.c` (interrupt streaming with ring buffer and notify).

## Architecture

```
board_init.c:  Controller globals (XGpioPs, XUartPs, ...)  ← LookupConfig + CfgInitialize
                            ↓ pointer
Driver Layer:  xxx_driver.c  →  implements device_ops, priv holds pointer + device-specific state
                            ↓
Core Layer:   device_core.h/c  →  unified API, dispatch via ops
                            ↓
Application:  device_find("name") → device_read / device_write / device_ioctl
```

## Two-Level Initialization

**Controller level** (board_init.c): owns the global controller object, does `LookupConfig` + `CfgInitialize`.

```c
/* board_init.c */
XGpioPs g_gpio_ps;
XUartPs g_uart_ps;

static void gpio_ps_init(void) {
    XGpioPs_Config *cfg = XGpioPs_LookupConfig(XPAR_PS7_GPIO_0_DEVICE_ID);
    XGpioPs_CfgInitialize(&g_gpio_ps, cfg, cfg->BaseAddr);
}
static void uart_ps_init(void) {
    XUartPs_Config *cfg = XUartPs_LookupConfig(XPAR_XUARTPS_0_DEVICE_ID);
    XUartPs_CfgInitialize(&g_uart_ps, cfg, cfg->BaseAddress);
}
void board_init() {
    gpio_ps_init();          /* controller init first */
    uart_ps_init();
    led_driver_init();       /* then driver inits */
    key_driver_init();
    uart_driver_init();
}
```

**Device level** (driver init): pin/UART config, interrupt setup, FIFO settings — anything specific to one device instance.

**Why separate?** Same controller can serve multiple devices (one XGpioPs for many LEDs and KEYs). Controller lifecycle is board-level concern, not driver concern.

## Driver Template (5 sections in one .c file)

### Section 1: Includes and priv struct

**Static-state driver (GPIO / KEY):**

```c
#include "device_core.h"
#include "ioctl_cmd.h"
#include "xgpiops.h"
#include "xscugic.h"      /* interrupt drivers only */
#include "xparameters_ps.h"

extern XGpioPs  g_gpio_ps;
extern XScuGic  xInterruptController;

struct xxx_priv {
    XGpioPs  *gpio;       /* pointer to controller, NEVER the object itself */
    uint32_t  pin;
    uint8_t   state;
    /* NO RTOS primitives — driver is OS-agnostic */
};
```

**Streaming driver (UART / SPI):**

```c
#include "device_core.h"
#include "ioctl_cmd.h"
#include "xuartps.h"
#include "xscugic.h"
#include "xparameters_ps.h"
#include "cb.h"           /* ring buffer from utils/buffer/ */
#include <string.h>

extern XScuGic  xInterruptController;
extern XUartPs  g_uart_ps;

#define XXX_RX_RING_SIZE    256
#define XXX_TX_RING_SIZE    256
#define XXX_RX_TMP_SIZE     64

struct xxx_priv {
    XUartPs         *uart_ps;
    int              irq_id;
    /* RX: ISR writes to ring buffer, app reads via device_read */
    uint8_t          rx_tmp[XXX_RX_TMP_SIZE];
    CirCularBuffer_t rx_ring;
    uint8_t          rx_ring_buf[XXX_RX_RING_SIZE];
    /* TX: app writes via device_write, ISR sends from ring buffer */
    CirCularBuffer_t tx_ring;
    uint8_t          tx_ring_buf[XXX_TX_RING_SIZE];
};
```

**Priv holds pointers only.** The controller object lives in `board_init.c`. This matches how `led_priv.gpio = &g_gpio_ps` and `uart_priv.uart_ps = &g_uart_ps` work.

### Section 2: device_ops functions

```c
static int xxx_init(struct device *dev)
{
    struct xxx_priv *p = (struct xxx_priv *)dev->priv;
    /* Device-level config only — controller already initialized in board_init */
    XGpioPs_SetDirectionPin(p->gpio, p->pin, 1U);
    XGpioPs_SetOutputEnablePin(p->gpio, p->pin, 1U);
    return 0;
}

static int xxx_ioctl(struct device *dev, int cmd, void *arg)
{
    switch (cmd) {
    case DEV_IOCTL_SET_NOTIFY:
        dev->notify = (device_notify_t)arg;
        break;
    default:
        return -1;
    }
    return 0;
}
```

### Section 3: ops and device instances

```c
static const struct device_ops xxx_ops = {
    .init  = xxx_init,
    .read  = xxx_read,       /* or NULL if output-only */
    .write = xxx_write,      /* or NULL if input-only */
    .ioctl = xxx_ioctl,
};

static struct xxx_priv xxx0_priv = { .gpio = &g_gpio_ps, .pin = 0 };
static struct device g_xxx0 = {
    .name = "xxx0", .ops = &xxx_ops, .priv = &xxx0_priv, .notify = NULL,
};
```

### Section 4: ISR (interrupt drivers only)

**Static-state ISR (GPIO / KEY):**

```c
static void xxx_bank_isr(void *CallBackRef, u32 Bank, u32 Status)
{
    (void)CallBackRef;
    if (Bank != XGPIOPS_BANK1) return;

    if (Status & (1U << (50 - 32))) {
        XGpioPs_IntrClearPin(&g_gpio_ps, 50);  /* clear BEFORE notify */
        if (g_xxx0.notify)
            g_xxx0.notify(&g_xxx0, 50);         /* event = pin number */
    }
}

static void xxx_intr_setup(void)
{
    XGpioPs_SetCallbackHandler(&g_gpio_ps, &g_gpio_ps, xxx_bank_isr);
    XScuGic_Connect(&xInterruptController, XPS_GPIO_INT_ID,
                    (Xil_InterruptHandler)XGpioPs_IntrHandler, &g_gpio_ps);
    XScuGic_Enable(&xInterruptController, XPS_GPIO_INT_ID);
}
```

**Streaming ISR (UART / SPI) — ring buffer in driver, notify only releases semaphore:**

```c
static void xxx_kick_tx(struct xxx_priv *p)
{
    uint8_t tmp[XXX_RX_TMP_SIZE];
    uint32_t n = cb_read(&p->tx_ring, tmp, sizeof(tmp));
    if (n > 0)
        XUartPs_Send(p->uart_ps, tmp, n);
}

static void uart_ps_isr(void *CallBackRef, u32 Event, u32 EventData)
{
    struct device *dev = (struct device *)CallBackRef;
    struct xxx_priv *p = (struct xxx_priv *)dev->priv;

    /* RX: write to ring buffer, then notify */
    if (Event == XUARTPS_EVENT_RECV_DATA) {
        cb_write(&p->rx_ring, p->rx_tmp, EventData);
        XUartPs_Recv(p->uart_ps, p->rx_tmp, XXX_RX_TMP_SIZE);
        if (dev->notify)
            dev->notify(dev, EventData);
        return;
    }
    if (Event == XUARTPS_EVENT_RECV_TOUT) {
        if (EventData > 0) {
            cb_write(&p->rx_ring, p->rx_tmp, EventData);
            if (dev->notify)
                dev->notify(dev, EventData);
        }
        XUartPs_Recv(p->uart_ps, p->rx_tmp, XXX_RX_TMP_SIZE);
        return;
    }
    /* TX complete: send next chunk from ring buffer */
    if (Event == XUARTPS_EVENT_SENT_DATA) {
        xxx_kick_tx(p);
        return;
    }
    /* Error: restart reception */
    if (Event == XUARTPS_EVENT_RECV_ERROR ||
        Event == XUARTPS_EVENT_PARE_FRAME_BRKE ||
        Event == XUARTPS_EVENT_RECV_ORERR) {
        XUartPs_Recv(p->uart_ps, p->rx_tmp, XXX_RX_TMP_SIZE);
    }
}
```

### Section 5: Driver entry

```c
void xxx_driver_init(void)
{
    device_register(&g_xxx0);
    device_init(&g_xxx0);
    xxx_intr_setup();       /* interrupt drivers only, must be AFTER device_init */
}
```

## Two ISR Data Patterns

### Pattern A: Static state (GPIO / KEY)

State doesn't change between ISR and task read. Application reads in **task context**.

```
ISR → dev->notify(dev, pin) → app callback (set semaphore/event flag)
                                        ↓
                              task wakes → device_read() → stable state
```

### Pattern B: Streaming with ring buffer (UART / SPI)

Driver owns ring buffers for RX and TX. ISR writes received data to RX ring buffer and calls notify. Notify callback should **only release a semaphore** — no data processing in ISR context. Application reads from ring buffer via `device_read` in task context.

```
RX:
  ISR → cb_write(&p->rx_ring, p->rx_tmp, EventData)
      → dev->notify(dev, byte_count)   ← app callback: only semaphore put
      → XUartPs_Recv(restart)
                                        ↓
                              task wakes → device_read() → reads from rx_ring

TX:
  device_write() → cb_write(&p->tx_ring, data, len)
                 → kick_tx() if UART idle
      ISR (SENT_DATA) → cb_read(&p->tx_ring) → XUartPs_Send(next chunk)
```

**Key difference:** Pattern A reads stable hardware state in task context. Pattern B reads from a driver-managed ring buffer in task context — ISR handles all buffering internally, notify only signals.

## Streaming driver read/write implementation

```c
static int uart_read(struct device *dev, void *buf, size_t len)
{
    struct xxx_priv *p = (struct xxx_priv *)dev->priv;
    if (!buf || len == 0)
        return -1;
    return (int)cb_read(&p->rx_ring, buf, (uint32_t)len);
}

static int uart_write(struct device *dev, const void *buf, size_t len)
{
    struct xxx_priv *p = (struct xxx_priv *)dev->priv;
    if (!buf || len == 0)
        return -1;
    uint32_t written = cb_write(&p->tx_ring, buf, (uint32_t)len);
    if (!XUartPs_IsSending(p->uart_ps))
        xxx_kick_tx(p);
    return (int)written;
}
```

## New driver checklist

1. Add controller global + init function in `board_init.c` (LookupConfig + CfgInitialize)
2. Define ioctl commands in `ioctl_cmd.h` (upper 8 bits = peripheral type, lower byte from 0x01)
3. Create `xxx_driver.c` following the 5-section template above
4. Add `extern void xxx_driver_init(void)` and call it in `board_init()` after controller init
5. Verify priv struct holds only pointers to controllers, never controller objects
6. For streaming drivers: include `cb.h`, add ring buffers to priv, init with `cb_init` in `xxx_init`

## Key Rules

| Rule | Detail |
|------|--------|
| Controller init in board_init.c | `LookupConfig` + `CfgInitialize` at board level. Driver init does device-level config only |
| Priv holds pointers, not objects | `XGpioPs *gpio`, `XUartPs *uart_ps` — never embed the controller struct in priv |
| Xilinx API only | No direct register access. Use pin-level API (`WritePin`/`ReadPin`) |
| `static` device instances | Invisible outside driver file, access via `device_find("name")` only |
| One `device_ops` per driver type | Same-type devices share ops, differ only in `priv` |
| No `.h` file for drivers | Only `.c`, export `xxx_driver_init()` as sole entry point |
| No RTOS headers in driver | Driver calls only `dev->notify()` for ISR→app notification |
| Streaming drivers use ring buffers | UART/SPI-type drivers include `cb.h` and own CirCularBuffer_t for RX and TX in priv struct |
| Notify only releases semaphore | Application notify callback must only do `tx_semaphore_put()` — no data processing in ISR context |
| `notify` is the only ISR bridge | Application registers callback via `DEV_IOCTL_SET_NOTIFY`, owns synchronization primitives |
| Clear interrupt before notify | `XGpioPs_IntrClearPin` before `dev->notify()`, otherwise ISR re-triggers immediately |
| Streaming ISR: write ring then notify | `cb_write(&rx_ring, rx_tmp, EventData)` → `dev->notify()` → restart reception. App reads from ring buffer in task context |
| Check `notify != NULL` | Application may not have registered a callback when first interrupt fires |
| English comments only | Keep all code comments in English |
| Active-low polarity | LED: on→write 0; KEY: pressed→read 0 |
| Output pins need both dir + enable | `SetDirectionPin` AND `SetOutputEnablePin` |

## Quick Reference

### ZYNQ 7000 GPIO banks

| MIO Range | Bank | Bit in bank |
|-----------|------|-------------|
| 0 - 31    | 0    | MIO number  |
| 32 - 53   | 1    | MIO - 32 |
| 54 - 85   | 2 (EMIO) | Pin - 54 |
| 86 - 117  | 3 (EMIO) | Pin - 86 |

### XGpioPs interrupt types

| Constant | Trigger | Use case |
|----------|---------|----------|
| `IRQ_TYPE_EDGE_FALLING` | High→Low | Key press (active-low) |
| `IRQ_TYPE_EDGE_RISING` | Low→High | Release detection |
| `IRQ_TYPE_EDGE_BOTH` | Both edges | Rotary encoder |
| `IRQ_TYPE_LEVEL_HIGH` | High level | Sensor alert |
| `IRQ_TYPE_LEVEL_LOW` | Low level | Sensor alert |

### Application usage patterns

**Static state (KEY) — read in task context:**
```c
static void key_cb(struct device *dev, uint32_t event) {
    tx_semaphore_put(&key_sem);    /* ISR-safe, just signal */
}
device_ioctl(key, DEV_IOCTL_SET_NOTIFY, (void *)key_cb);
tx_semaphore_get(&key_sem, TX_WAIT_FOREVER);
device_read(key, &val, 1);        /* task context, state is stable */
```

**Streaming (UART) — notify only releases semaphore, read in task context:**
```c
static void uart_cb(struct device *dev, uint32_t event) {
    tx_semaphore_put(&uart_sem);   /* only release semaphore, no data processing */
}
device_ioctl(uart, DEV_IOCTL_SET_NOTIFY, (void *)uart_cb);

/* In task context: */
while (1) {
    tx_semaphore_get(&uart_sem, TX_WAIT_FOREVER);
    uint8_t buf[64];
    int n = device_read(uart, buf, sizeof(buf));  /* reads from driver's rx_ring */
    /* process buf[0..n-1] */
}
```
