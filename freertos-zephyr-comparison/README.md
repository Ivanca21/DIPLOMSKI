# FreeRTOS vs Zephyr Latency Comparison on STM32 NUCLEO-F401RE

This repository contains the source code used as supporting material for the diploma thesis:

> **Postupak portovanja operativnog sistema Zephyr na ciljnu platformu i poređenje performansi sa operativnim sistemom FreeRTOS**

The project compares two implementations of the same interrupt-to-task/thread latency test on the **STM32 NUCLEO-F401RE** development board:

1. a **FreeRTOS** implementation with direct register access, and
2. a **Zephyr** implementation using an out-of-tree custom board port.

The goal is not to benchmark only the kernel scheduler in isolation, but to compare the complete practical signal path used in each implementation.

---

## Platform

Hardware platform:

- **Development board:** STM32 NUCLEO-F401RE
- **Microcontroller:** STM32F401RE
- **CPU core:** ARM Cortex-M4
- **System clock:** 84 MHz
- **Measurement tool:** USB logic analyzer, 24 MHz

Used signals:

| Function | Pin | Description |
|---|---:|---|
| USER button B1 | PC13 | Input signal that generates the interrupt |
| USER LED LD2 | PA5 | Visual indication that the task/thread has executed |
| Latency marker | PA6 | Digital signal measured by the logic analyzer |
| Ground | GND | Common ground with the logic analyzer |

---

## Measurement Principle

The latency measurement is based on the width of a HIGH pulse generated on the marker pin **PA6**.

Measurement flow:

```text
USER button press on PC13
    ↓
Interrupt / GPIO callback context
    ↓
PA6 = HIGH
    ↓
Semaphore give
    ↓
Scheduler and context switch
    ↓
Task/thread resumes execution
    ↓
PA6 = LOW
```

The measured pulse width represents the time from the point where the event is identified in the interrupt/callback context to the point where the corresponding task/thread continues execution after the semaphore wait function returns.

The marker pin is controlled by direct access to the STM32 `GPIOA_BSRR` register in both implementations. This avoids adding GPIO driver overhead to the marker signal itself.

---

## Repository Structure

```text
DIPLOMSKI/
├── FREERTOS/
│   ├── Inc/
│   ├── Src/
│   ├── Startup/
│   ├── FreeRTOS_source/
│   ├── STM32F401RETX_FLASH.ld
│   ├── STM32F401RETX_RAM.ld
│   ├── .project
│   ├── .cproject
│   └── .settings/
│
├── button_latency/
│   ├── boards/
│   │   └── st/
│   │       └── my_nucleo_f401re/
│   │           ├── board.yml
│   │           ├── board.cmake
│   │           ├── Kconfig.my_nucleo_f401re
│   │           ├── my_nucleo_f401re.dts
│   │           └── my_nucleo_f401re_defconfig
│   ├── src/
│   │   └── main.c
│   ├── CMakeLists.txt
│   └── prj.conf
│
└── README.md
```

Build output folders such as `Debug/`, `Release/` and `build/` are not part of the source code and should not be committed to the repository.

---

## FreeRTOS Implementation

The FreeRTOS test is implemented as a minimal bare-metal STM32 project in **STM32CubeIDE**.

The code directly configures:

- RCC clock configuration
- GPIOA and GPIOC
- SYSCFG
- EXTI13 interrupt line
- NVIC interrupt priority and enable control
- PA6 marker pin
- PA5 LED output

FreeRTOS synchronization is implemented with a binary semaphore:

- `xSemaphoreCreateBinary()`
- `xSemaphoreGiveFromISR()`
- `xSemaphoreTake()`
- `portYIELD_FROM_ISR()`

FreeRTOS signal path:

```text
PC13 button press
    ↓
EXTI13 interrupt
    ↓
EXTI15_10_IRQHandler()
    ↓
PA6 = HIGH
    ↓
xSemaphoreGiveFromISR()
    ↓
FreeRTOS scheduler / context switch
    ↓
button_task resumes after xSemaphoreTake()
    ↓
PA6 = LOW
```

### FreeRTOS Clock Configuration

The FreeRTOS project configures the STM32F401RE system clock to **84 MHz**:

```text
HSE = 8 MHz
PLLM = 8
PLLN = 336
PLLP = 4

SYSCLK = (8 MHz / 8) × 336 / 4 = 84 MHz
```

The same value must be used in `FreeRTOSConfig.h`:

```c
#define configCPU_CLOCK_HZ 84000000UL
```

---

## Zephyr Implementation

The Zephyr project is implemented as an **out-of-tree custom board port** named:

```text
my_nucleo_f401re
```

The custom board description is located inside the application project, under:

```text
button_latency/boards/st/my_nucleo_f401re/
```

The Zephyr implementation uses:

- Devicetree hardware description
- Kconfig configuration
- GPIO driver API
- GPIO callback mechanism
- Zephyr semaphore
- `k_sem_give()`
- `k_sem_take()`

Zephyr signal path:

```text
PC13 button press
    ↓
Zephyr GPIO interrupt handling
    ↓
GPIO callback
    ↓
PA6 = HIGH
    ↓
k_sem_give()
    ↓
Zephyr scheduler
    ↓
main thread resumes after k_sem_take()
    ↓
PA6 = LOW
```

The Zephyr implementation demonstrates the porting process at board level. Since the STM32F401xE SoC is already supported by Zephyr, the port mainly consists of the board description, Devicetree file, Kconfig connection to the SoC, default configuration and flashing integration.

### Zephyr Build Example

Example build command from the Zephyr workspace root, assuming `button_latency` is located inside the workspace:

```bat
west build -p always -b my_nucleo_f401re button_latency -- -DBOARD_ROOT=%cd%\button_latency
```

Example flash command:

```bat
west flash
```

If the application is located elsewhere, replace `button_latency` and `BOARD_ROOT` with the actual path to the application folder.

---

## Tested Optimizations

Two compiler optimization modes were tested:

| RTOS | Size optimization | Speed optimization |
|---|---|---|
| FreeRTOS | `-Os` | `-O2` |
| Zephyr | `CONFIG_SIZE_OPTIMIZATIONS=y` | `CONFIG_SPEED_OPTIMIZATIONS=y` |

All measurements were performed with the CPU running at **84 MHz**.

---

## Latency Results

Each configuration was measured using 30 logic analyzer samples.

### Size Optimized Build

| RTOS | Optimization | Min [µs] | Max [µs] | Average [µs] | Std. deviation [µs] |
|---|---:|---:|---:|---:|---:|
| Zephyr | size optimization | 15.125 | 15.208 | 15.192 | 0.023 |
| FreeRTOS | `-Os` | 5.542 | 5.583 | 5.549 | 0.016 |

Ratio of average latencies:

```text
Zephyr / FreeRTOS = 15.192 / 5.549 ≈ 2.74
```

### Speed Optimized Build

| RTOS | Optimization | Min [µs] | Max [µs] | Average [µs] | Std. deviation [µs] |
|---|---:|---:|---:|---:|---:|
| Zephyr | `CONFIG_SPEED_OPTIMIZATIONS=y` | 12.833 | 12.917 | 12.857 | 0.024 |
| FreeRTOS | `-O2` | 5.458 | 5.500 | 5.463 | 0.013 |

Ratio of average latencies:

```text
Zephyr / FreeRTOS = 12.857 / 5.463 ≈ 2.35
```

---

## Program Size Results

Flash usage is calculated as:

```text
Flash = text + data
```

RAM usage is calculated as:

```text
RAM = data + bss
```

| Configuration | text [B] | data [B] | bss [B] | Flash [B] | RAM [B] |
|---|---:|---:|---:|---:|---:|
| Zephyr, size optimization | 15000 | 60 | 4387 | 15060 | 4447 |
| FreeRTOS, `-Os` | 6396 | 4 | 12084 | 6400 | 12088 |
| Zephyr, speed optimization | 18552 | 60 | 4390 | 18612 | 4450 |
| FreeRTOS, `-O2` | 6824 | 4 | 12084 | 6828 | 12088 |

FreeRTOS RAM usage includes a statically reserved heap:

```c
#define configTOTAL_HEAP_SIZE 10240
```

If this reserved heap is subtracted from the total RAM value, the remaining static RAM usage of the FreeRTOS image is approximately:

```text
12088 B - 10240 B = 1848 B
```

This comparison is not fully equivalent to the Zephyr RAM value, because FreeRTOS task stacks and kernel objects are allocated from the reserved heap, while Zephyr stacks are included directly in the reported `bss` section.

---

## Discussion

The measured results show that the implemented FreeRTOS signal path has lower average latency than the implemented Zephyr signal path on the same hardware platform and at the same CPU frequency.

This result should be interpreted as a comparison of the complete implemented signal paths, not as an isolated comparison of the two kernel schedulers.

The FreeRTOS implementation uses a direct EXTI interrupt handler and direct STM32 register access. The Zephyr implementation uses the GPIO driver layer, interrupt dispatching through the driver, a registered callback mechanism and the Zephyr kernel semaphore API.

This additional abstraction in Zephyr improves portability and separates application code from board-specific hardware details, but it also increases code size and contributes to a longer measured signal path.

---

## Conclusion

For this simple interrupt-to-task/thread latency experiment on STM32F401RE at 84 MHz:

- FreeRTOS achieved lower average latency.
- FreeRTOS used less Flash memory.
- Zephyr provided a more portable and structured hardware abstraction model.
- FreeRTOS total RAM usage was dominated by the statically reserved 10 KB heap.

The results show the practical trade-off between low-level efficiency and higher-level portability.

---

## Note

This repository is intended as supporting material for a diploma thesis. Results depend on the exact project configuration, compiler optimization level, CPU clock configuration, measurement method and logic analyzer resolution.
