---
name: zynq-device-driver
description: Use when creating new device drivers for ZYNQ peripherals following the device_core framework, adding LED, KEY, UART, SPI, I2C drivers, or implementing interrupt-driven drivers using the notify callback mechanism
---

# ZYNQ Device Driver Development

Reference implementations: `led_driver.c` (polling output), `key_driver.c` (interrupt input with notify).

## Architecture

```
Application:  device_find("name") → device_read / device_write / device_ioctl
                            ↓
Core Layer:   device_core.h/c  →  unified API, dispatch via ops
                            ↓
Driver Layer: xxx_driver.c     →  implements device_ops, priv holds pin/state
                            ↓
Hardware:     Xilinx BSP API   →  XGpioPs, XUartPs, XSpiPs, etc.
```

## Driver Template (5 sections in one .c file)

### Section 1: Includes and priv struct

```c
#include "device_core.h"
#include "ioctl_cmd.h"
#include "xgpiops.h"
/* Interrupt driver additionally needs: */
#include "xscugic.h"
#include "xparameters_ps.h"

extern XGpioPs  g_gpio_ps;                    /* board_init.c */
extern XScuGic  xInterruptController;         /* bsp_init.c, interrupt only */

struct xxx_priv {
    XGpioPs  *gpio;
    uint32_t  pin;
    uint8_t   state;                           /* driver-specific, optional */
    /* NO RTOS primitives — driver is OS-agnostic */
};
```

### Section 2: device_ops functions

```c
static int xxx_init(struct device *dev)
{
    struct xxx_priv *p = (struct xxx_priv *)dev->priv;
    /* Output device (LED etc.): */
    XGpioPs_SetDirectionPin(p->gpio, p->pin, 1U);
    XGpioPs_SetOutputEnablePin(p->gpio, p->pin, 1U);

    /* Input device with interrupt (KEY etc.) — replace above with: */
    XGpioPs_SetDirectionPin(p->gpio, p->pin, 0U);           /* input */
    XGpioPs_SetIntrTypePin(p->gpio, p->pin, XGPIOPS_IRQ_TYPE_EDGE_FALLING);
    XGpioPs_IntrClearPin(p->gpio, p->pin);
    XGpioPs_IntrEnablePin(p->gpio, p->pin);
    return 0;
}

static int xxx_ioctl(struct device *dev, int cmd, void *arg)
{
    switch (cmd) {
    case DEV_IOCTL_SET_NOTIFY:                /* interrupt drivers must handle this */
        dev->notify = (device_notify_t)arg;
        break;
    /* ... peripheral-specific commands ... */
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

```c
static void xxx_bank_isr(void *CallBackRef, u32 Bank, u32 Status)
{
    (void)CallBackRef;
    if (Bank != XGPIOPS_BANK1) return;

    if (Status & (1U << (50 - 32))) {          /* pin 50 → bank1 bit 18 */
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

### Section 5: Driver entry

```c
void xxx_driver_init(void)
{
    device_register(&g_xxx0);
    device_init(&g_xxx0);
    xxx_intr_setup();       /* interrupt drivers only, must be AFTER device_init */
}
```

## New driver checklist

1. Define ioctl commands in `ioctl_cmd.h` (upper 8 bits = peripheral type, lower byte from 0x01)
2. Create `xxx_driver.c` following the 5-section template above
3. Add `extern void xxx_driver_init(void)` and call it in `board_init()` after controller init

## Key Rules

| Rule | Detail |
|------|--------|
| Xilinx API only | No direct register access. Pin-level API (`WritePin`/`ReadPin`) handles bank mapping internally |
| `static` device instances | Invisible outside driver file, access via `device_find("name")` only |
| One `device_ops` per driver type | Same-type devices share ops, differ only in `priv` |
| No `.h` file for drivers | Only `.c`, export `xxx_driver_init()` as sole entry point |
| No RTOS headers in driver | Driver calls only `dev->notify()` for ISR→app notification |
| `notify` is the only ISR bridge | Application registers callback via `DEV_IOCTL_SET_NOTIFY`, owns synchronization primitives |
| Clear interrupt before notify | `XGpioPs_IntrClearPin` before `dev->notify()`, otherwise ISR re-triggers immediately |
| Check `notify != NULL` | Application may not have registered a callback when first interrupt fires |
| English comments only | Keep all code comments in English |
| Active-low polarity | LED: on→write 0; KEY: pressed→read 0 |
| Output pins need both dir + enable | `SetDirectionPin` AND `SetOutputEnablePin` |

## Quick Reference

### ZYNQ 7000 GPIO banks

| MIO Range | Bank | Bit in bank |
|-----------|------|-------------|
| 0 - 31    | 0    | MIO number  |
| 32 - 53   | 1    | MIO - 32    |
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

### Application usage pattern (interrupt)

```c
static TX_SEMAPHORE key_sem;
static void key_cb(struct device *dev, uint32_t event) {
    tx_semaphore_put(&key_sem);    /* ISR-safe */
}
/* In task: */
device_ioctl(key, DEV_IOCTL_SET_NOTIFY, (void *)key_cb);
tx_semaphore_get(&key_sem, TX_WAIT_FOREVER);
```
