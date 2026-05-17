---
name: zynq-device-driver
description: Use when creating new device drivers for ZYNQ peripherals following the device_core framework, or when adding LED, KEY, UART, SPI, I2C or other peripheral drivers to the BSP driver layer
---

# ZYNQ Device Driver Development

## Overview

Follow the `device_core` framework to create peripheral drivers. Each driver encapsulates Xilinx BSP API behind a unified `device_ops` interface (init/read/write/ioctl). Drivers self-register via `xxx_driver_init()`, application code accesses devices through `device_find("name")`.

## When to Use

- Adding any new peripheral driver (LED, KEY, UART, SPI, I2C, etc.)
- Extending existing driver with new device instances

## Architecture

```
Application:  device_find("led0") → device_write / device_read / device_ioctl
                           ↓
Core Layer:   device_core.h/c  →  unified API, dispatch via ops
                           ↓
Driver Layer: xxx_driver.c     →  implements device_ops, priv holds pin/state
                           ↓
Hardware:     Xilinx BSP API   →  XGpioPs, XUartPs, XSpiPs, etc.
```

## File Layout

```
src/BSP/driver/
  device_core.h        # Framework core (existing)
  device_core.c        # Framework core (existing)
  ioctl_cmd.h          # ioctl command definitions (update)
  xxx_driver.c         # New driver (create)

src/BSP/board_init.c   # Controller init + device registration (update)
```

## Development Steps

### Step 1: Define ioctl commands in `ioctl_cmd.h`

Upper 8 bits identify peripheral type, **lower byte starts from 0x01 (never 0x00)**:

```c
/* General:  0x00XX (already defined) */
#define DEV_IOCTL_RESET        0x0001
#define DEV_IOCTL_SET_NOTIFY   0x0002

/* LED:      0x01XX */
#define LED_IOCTL_SET_ON       0x0101
#define LED_IOCTL_SET_OFF      0x0102
#define LED_IOCTL_TOGGLE       0x0103

/* KEY:      0x02XX */
#define KEY_IOCTL_GET_STATE    0x0201

/* UART:     0x03XX */
/* SPI:      0x04XX */
/* ...allocate new type IDs as needed */
```

### Step 2: Create `xxx_driver.c` (4 sections)

#### Section 1: Includes and priv struct

```c
#include "device_core.h"
#include "ioctl_cmd.h"
#include "xgpiops.h"          /* or xuartps.h, xspips.h, etc. */

extern XGpioPs g_gpio_ps;     /* defined in board_init.c */

struct xxx_priv {
    XGpioPs  *gpio;           /* controller instance pointer */
    uint32_t  pin;            /* pin/channel number */
    uint8_t   state;          /* driver-specific state */
};
```

`gpio` pointer is hardcoded to `&g_gpio_ps` in the static initializer. This keeps the door open for multiple controllers (e.g., PS GPIO vs PL EMIO GPIO) without needing bind functions.

#### Section 2: device_ops functions

```c
static int xxx_init(struct device *dev)
{
    struct xxx_priv *p = (struct xxx_priv *)dev->priv;
    XGpioPs_SetDirectionPin(p->gpio, p->pin, 1U);    /* output */
    XGpioPs_SetOutputEnablePin(p->gpio, p->pin, 1U);
    return 0;
}

static int xxx_read(struct device *dev, void *buf, size_t len)  { /* ... */ }
static int xxx_write(struct device *dev, const void *buf, size_t len) { /* ... */ }
static int xxx_ioctl(struct device *dev, int cmd, void *arg) { /* ... */ }
```

Set unused ops to `NULL` (e.g., KEY driver: `.write = NULL`).

#### Section 3: device_ops and device instances

```c
/* One ops instance shared by all devices of same type */
static const struct device_ops xxx_ops = {
    .init  = xxx_init,
    .read  = xxx_read,
    .write = xxx_write,
    .ioctl = xxx_ioctl,
};

/* One priv + one device instance per physical device */
/* Device instances are static — invisible outside this file */
static struct xxx_priv xxx0_priv = { .gpio = &g_gpio_ps, .pin = 0 };
static struct device g_xxx0 = {
    .name   = "xxx0",
    .ops    = &xxx_ops,
    .priv   = &xxx0_priv,
    .notify = NULL,
};
```

#### Section 4: Driver entry function

```c
/* Called by board_init — handles register + init for all devices of this type */
void xxx_driver_init(void)
{
    device_register(&g_xxx0);
    device_init(&g_xxx0);
}
```

### Step 3: Update `board_init.c`

```c
#include "driver/device_core.h"
#include "board_init.h"
#include "xgpiops.h"

/* Shared hardware controller instance */
XGpioPs g_gpio_ps;

/* Driver entry functions */
extern void xxx_driver_init(void);

static void gpio_ps_init(void)
{
    XGpioPs_Config *cfg = XGpioPs_LookupConfig(XPAR_PS7_GPIO_0_DEVICE_ID);
    XGpioPs_CfgInitialize(&g_gpio_ps, cfg, cfg->BaseAddr);
}

void board_init()
{
    gpio_ps_init();
    xxx_driver_init();
}
```

### Application layer usage

```c
struct device *led = device_find("led0");
device_write(led, "\x01", 1);              /* turn on */
device_ioctl(led, LED_IOCTL_TOGGLE, NULL);  /* toggle */
```

## Key Principles

| Principle | Why |
|-----------|-----|
| Use Xilinx API exclusively | Never access registers directly — Xilinx API handles bank mapping, bit ops, and edge cases internally |
| `XGpioPs*` in priv, hardcoded in static init | `.gpio = &g_gpio_ps` — supports multiple controllers, no bind functions needed |
| Device instances are `static` | Invisible outside driver file, access via `device_find("name")` only |
| One `device_ops` per driver type | Same-type devices share ops, differ only in priv |
| Driver exports `xxx_driver_init()` | Self-contained register + init, board_init only calls driver init functions |
| No header file for drivers | Only `.c` file needed |
| `ioctl_cmd.h` for all ioctl commands | Centralized, upper 8 bits for peripheral type, lower byte starts from 0x01 |
| English comments in driver code | Keep code comments in English for consistency |
| Hardware init as static function in `board_init.c` | Each controller gets its own `xxx_init()` static function |

## ZYNQ 7000 GPIO Pin Mapping

| MIO Range | GPIO Bank | Pin in Bank |
|-----------|-----------|-------------|
| 0 - 31    | Bank 0    | MIO number  |
| 32 - 53   | Bank 1    | MIO - 32    |
| 54 - 85   | Bank 2 (EMIO) | Pin - 54 |
| 86 - 117  | Bank 3 (EMIO) | Pin - 86 |

XGpioPs Pin-level API (`WritePin`/`ReadPin`/`SetDirectionPin`) handles bank mapping internally — just pass the MIO number as the Pin parameter.

## Common Mistakes

| Mistake | Fix |
|---------|-----|
| Direct register access (Xil_In32/Xil_Out32) | Use `XGpioPs_WritePin`/`ReadPin` etc. — Xilinx API is the only allowed hardware access layer |
| Bind function to inject hardware handle | Not needed — hardcode `&g_gpio_ps` directly in the static initializer |
| `extern struct device` in board_init.c | Device instances are `static` in driver files, use `device_find()` to access |
| Missing `SetOutputEnablePin` | Output pins need **both** direction AND output enable |
| Active-low polarity ignored | LED: on→write 0; KEY: pressed→read 0 |
| New header file for driver | Guidelines say no `.h` file, only `.c` |
