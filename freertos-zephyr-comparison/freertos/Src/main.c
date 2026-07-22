#include <stdint.h>

#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"

/*
 * FreeRTOS latency measurement example for STM32 NUCLEO-F401RE.
 *
 * Measurement idea:
 * - PC13 USER button generates EXTI13 interrupt.
 * - ISR sets PA6 marker HIGH and gives a binary semaphore.
 * - FreeRTOS task wakes up, clears PA6 marker LOW and toggles PA5 LED.
 * - Logic analyzer measures HIGH pulse width on PA6.
 */

/* ===================== Register base addresses ===================== */

/*
 * Peripheral memory base.
 *
 * STM32F401xE datasheet, Memory mapping, pp. 51-54:
 * - peripheral region starts at 0x40000000
 * - GPIOA  = 0x40020000
 * - GPIOC  = 0x40020800
 * - RCC    = 0x40023800
 * - FLASH interface registers = 0x40023C00
 * - PWR    = 0x40007000
 * - SYSCFG = 0x40013800
 * - EXTI   = 0x40013C00
 */
#define PERIPH_BASE             0x40000000UL

/*
 * Bus base addresses derived from the STM32 memory map.
 *
 * APB1 peripherals start at PERIPH_BASE.
 * APB2 peripherals are at PERIPH_BASE + 0x00010000.
 * AHB1 peripherals are at PERIPH_BASE + 0x00020000.
 */
#define AHB1PERIPH_BASE         (PERIPH_BASE + 0x00020000UL)
#define APB2PERIPH_BASE         (PERIPH_BASE + 0x00010000UL)
#define APB1PERIPH_BASE         PERIPH_BASE

/*
 * Peripheral base addresses.
 */
#define GPIOA_BASE              (AHB1PERIPH_BASE + 0x0000UL)   /* 0x40020000 */
#define GPIOC_BASE              (AHB1PERIPH_BASE + 0x0800UL)   /* 0x40020800 */

#define RCC_BASE                (AHB1PERIPH_BASE + 0x3800UL)   /* 0x40023800 */
#define SYSCFG_BASE             (APB2PERIPH_BASE + 0x3800UL)   /* 0x40013800 */
#define EXTI_BASE               (APB2PERIPH_BASE + 0x3C00UL)   /* 0x40013C00 */
#define FLASH_BASE              0x40023C00UL
#define PWR_BASE                (APB1PERIPH_BASE + 0x7000UL)   /* 0x40007000 */

/*
 * RM0368:
 * - FLASH_ACR, p. 60, address offset 0x00
 * - PWR_CR,    p. 87, address offset 0x00
 */
#define FLASH_ACR               (*(volatile uint32_t *)(FLASH_BASE + 0x00UL))
#define PWR_CR                  (*(volatile uint32_t *)(PWR_BASE + 0x00UL))

/* ===================== RCC ===================== */

/*
 * RCC register offsets.
 *
 * RM0368:
 * - RCC_CR      offset 0x00, p. 103
 * - RCC_PLLCFGR offset 0x04, p. 105
 * - RCC_CFGR    offset 0x08, p. 107
 * - RCC_AHB1ENR offset 0x30, p. 118
 * - RCC_APB1ENR offset 0x40, p. 119
 * - RCC_APB2ENR offset 0x44, p. 122
 */
#define RCC_CR                  (*(volatile uint32_t *)(RCC_BASE + 0x00UL))
#define RCC_PLLCFGR             (*(volatile uint32_t *)(RCC_BASE + 0x04UL))
#define RCC_CFGR                (*(volatile uint32_t *)(RCC_BASE + 0x08UL))
#define RCC_AHB1ENR             (*(volatile uint32_t *)(RCC_BASE + 0x30UL))
#define RCC_APB1ENR             (*(volatile uint32_t *)(RCC_BASE + 0x40UL))
#define RCC_APB2ENR             (*(volatile uint32_t *)(RCC_BASE + 0x44UL))

/*
 * RCC_AHB1ENR bits.
 *
 * RM0368, p. 118-119:
 * - bit 0: GPIOAEN, IO port A clock enable
 * - bit 2: GPIOCEN, IO port C clock enable
 */
#define RCC_AHB1ENR_GPIOAEN     (1UL << 0)
#define RCC_AHB1ENR_GPIOCEN     (1UL << 2)

/*
 * RCC_APB2ENR bit.
 *
 * RM0368, p. 122:
 * - bit 14: SYSCFGEN, System configuration controller clock enable
 */
#define RCC_APB2ENR_SYSCFGEN    (1UL << 14)

/*
 * RCC_CR bits.
 *
 * RM0368, p. 103-104:
 * - bit 16: HSEON,  HSE clock enable
 * - bit 17: HSERDY, HSE clock ready flag
 * - bit 18: HSEBYP, HSE clock bypass
 * - bit 24: PLLON,  main PLL enable
 * - bit 25: PLLRDY, main PLL ready flag
 */
#define RCC_CR_HSEON            (1UL << 16)
#define RCC_CR_HSERDY           (1UL << 17)
#define RCC_CR_HSEBYP           (1UL << 18)
#define RCC_CR_PLLON            (1UL << 24)
#define RCC_CR_PLLRDY           (1UL << 25)

/*
 * RCC_PLLCFGR bit.
 *
 * RM0368, p. 105:
 * - bit 22: PLLSRC
 *   0 = HSI selected as PLL input
 *   1 = HSE selected as PLL input
 */
#define RCC_PLLCFGR_PLLSRC_HSE  (1UL << 22)

/*
 * RCC_APB1ENR bit.
 *
 * RM0368, p. 119-120:
 * - bit 28: PWREN, Power interface clock enable
 */
#define RCC_APB1ENR_PWREN       (1UL << 28)

/* ===================== FLASH ===================== */

/*
 * FLASH_ACR fields.
 *
 * RM0368, p. 60:
 * - LATENCY[3:0] controls Flash wait states
 * - 0010 = two wait states
 * - bit 8  PRFTEN = prefetch enable
 * - bit 9  ICEN   = instruction cache enable
 * - bit 10 DCEN   = data cache enable
 *
 * STM32F401xE datasheet, Flash wait-state table:
 * for VDD = 2.7 V to 3.6 V and 84 MHz operation, two wait states are required.
 */
#define FLASH_ACR_LATENCY_MASK  0xFUL
#define FLASH_ACR_LATENCY_2WS   0x2UL
#define FLASH_ACR_PRFTEN        (1UL << 8)
#define FLASH_ACR_ICEN          (1UL << 9)
#define FLASH_ACR_DCEN          (1UL << 10)

/* ===================== GPIO ===================== */

/*
 * GPIO register offsets.
 *
 * RM0368:
 * - GPIOx_MODER offset 0x00, p. 158
 * - GPIOx_PUPDR offset 0x0C, p. 159
 * - GPIOx_ODR   offset 0x14, p. 160
 * - GPIOx_BSRR  offset 0x18, p. 161
 *
 * RM0368, p. 146:
 * GPIO ports contain MODER, PUPDR, ODR and BSRR registers.
 */
#define GPIOA_MODER             (*(volatile uint32_t *)(GPIOA_BASE + 0x00UL))
#define GPIOA_ODR               (*(volatile uint32_t *)(GPIOA_BASE + 0x14UL))
#define GPIOA_BSRR              (*(volatile uint32_t *)(GPIOA_BASE + 0x18UL))

#define GPIOC_MODER             (*(volatile uint32_t *)(GPIOC_BASE + 0x00UL))
#define GPIOC_PUPDR             (*(volatile uint32_t *)(GPIOC_BASE + 0x0CUL))

/*
 * Board pin usage.
 *
 * UM1724:
 * - p. 24: LD2 user LED is connected to PA5.
 *          LD2 is ON when PA5 is HIGH and OFF when PA5 is LOW.
 * - p. 45: Arduino D12 maps to PA6, used here as latency marker pin.
 * - p. 24: B1 USER button is connected to PC13.
 */
#define LED_PIN                 5UL
#define MARKER_PIN              6UL
#define BUTTON_PIN              13UL

#define LED_MASK                (1UL << LED_PIN)
#define MARKER_MASK             (1UL << MARKER_PIN)
#define BUTTON_MASK             (1UL << BUTTON_PIN)

/*
 * Atomic GPIO output control through GPIOA_BSRR.
 *
 * RM0368, p. 152 and p. 161:
 * - writing 1 to BSRR bit y sets ODRy
 * - writing 1 to BSRR bit y+16 resets ODRy
 * - BSRR provides atomic bitwise handling, so no read-modify-write sequence
 *   is needed for setting/clearing one GPIO output.
 */
#define LED_ON()                (GPIOA_BSRR = LED_MASK)
#define LED_OFF()               (GPIOA_BSRR = (LED_MASK << 16U))

#define MARKER_HIGH()           (GPIOA_BSRR = MARKER_MASK)
#define MARKER_LOW()            (GPIOA_BSRR = (MARKER_MASK << 16U))

/*
 * LED_TOGGLE reads GPIOA_ODR and then updates PA5 through BSRR.
 *
 * RM0368, p. 160:
 * - GPIOx_ODR bits can be read and written by software.
 * - For atomic set/reset, use GPIOx_BSRR.
 */
#define LED_TOGGLE()                                                    \
    do {                                                                \
        if (GPIOA_ODR & LED_MASK) {                                     \
            LED_OFF();                                                  \
        } else {                                                        \
            LED_ON();                                                   \
        }                                                               \
    } while (0)

/* ===================== SYSCFG / EXTI ===================== */

/*
 * SYSCFG_EXTICR4.
 *
 * RM0368, p. 143:
 * - SYSCFG_EXTICR4 offset = 0x14
 * - controls EXTI12, EXTI13, EXTI14 and EXTI15 source selection
 * - EXTI13[3:0] selects which GPIO port is connected to EXTI13
 */
#define SYSCFG_EXTICR4          (*(volatile uint32_t *)(SYSCFG_BASE + 0x14UL))

/*
 * EXTI register offsets.
 *
 * RM0368:
 * - EXTI_IMR  offset 0x00, p. 209
 * - EXTI_RTSR offset 0x08, p. 210
 * - EXTI_FTSR offset 0x0C, p. 210
 * - EXTI_PR   offset 0x14, p. 211
 */
#define EXTI_IMR                (*(volatile uint32_t *)(EXTI_BASE + 0x00UL))
#define EXTI_RTSR               (*(volatile uint32_t *)(EXTI_BASE + 0x08UL))
#define EXTI_FTSR               (*(volatile uint32_t *)(EXTI_BASE + 0x0CUL))
#define EXTI_PR                 (*(volatile uint32_t *)(EXTI_BASE + 0x14UL))

/* ===================== NVIC ===================== */

/*
 * NVIC registers are Cortex-M4 core peripherals, not STM32 external
 * peripheral registers.
 *
 * PM0214:
 * - p. 208: NVIC register summary
 * - p. 210: NVIC_ISERx
 * - p. 215: NVIC_IPRx
 * - p. 219: NVIC register map
 *
 * NVIC_ISER block base is 0xE000E100.
 * NVIC_ISER1 is at 0xE000E104 and controls IRQ numbers 32..63.
 * NVIC_IPR priority byte array starts at 0xE000E400.
 */
#define NVIC_ISER1              (*(volatile uint32_t *)0xE000E104UL)
#define NVIC_IPR_BASE           0xE000E400UL

/*
 * STM32F401 interrupt vector table.
 *
 * RM0368, p. 204:
 * - EXTI15_10 position / IRQ number = 40
 * - description: EXTI Line[15:10] interrupts
 */
#define EXTI15_10_IRQn          40UL

/*
 * Enable IRQ in NVIC.
 *
 * PM0214, p. 210:
 * - NVIC_ISER1 bit 0 controls IRQ32
 * - NVIC_ISER1 bit n controls IRQ(32+n)
 *
 * For EXTI15_10_IRQn = 40:
 * - 40 - 32 = 8
 * - write 1 to NVIC_ISER1 bit 8
 */
#define NVIC_ENABLE_IRQ(irqn)                                           \
    do {                                                                \
        NVIC_ISER1 = (1UL << ((irqn) - 32UL));                          \
    } while (0)

/*
 * Set IRQ priority.
 *
 * PM0214, p. 215:
 * - NVIC_IPRx priority fields are byte-accessible.
 * - STM32 Cortex-M4 implements only upper priority bits [7:4].
 * - With configPRIO_BITS = 4, logical priority must be shifted left by 4.
 *
 * PM0214 also states that lower numerical priority value means higher
 * interrupt priority.
 */
#define NVIC_SET_PRIORITY(irqn, priority)                               \
    do {                                                                \
        (*(volatile uint8_t *)(NVIC_IPR_BASE + (irqn))) =               \
            (uint8_t)((priority) << (8U - configPRIO_BITS));            \
    } while (0)

/* ======================== FreeRTOS objects ======================== */

static SemaphoreHandle_t button_sem = NULL;

/* ========================= Hardware init ========================== */

static void gpio_init(void);

static void button_exti_init(void);

/* ========================== FreeRTOS task ========================= */

static void button_task(void *argument);

/* ===================== EXTI interrupt handler ===================== */

void EXTI15_10_IRQHandler(void);

/* ======================= CLOCK config 84Mhz ======================= */

static void system_clock_config_84mhz(void);

/* =========================== main ================================= */

int main(void)
{
    /*
     * Configure system clock to 84 MHz before starting FreeRTOS.
     *
     * This must match:
     * #define configCPU_CLOCK_HZ 84000000UL
     * in FreeRTOSConfig.h.
     */
    system_clock_config_84mhz();

    /*
     * Configure GPIOA PA5/PA6 and GPIOC PC13.
     */
    gpio_init();

    /*
     * Binary semaphore used for ISR-to-task synchronization.
     * Initially empty; task blocks until ISR gives it.
     */
    button_sem = xSemaphoreCreateBinary();

    if (button_sem == NULL) {
        while (1) {}
    }

    /*
     * Create the task that waits for the button semaphore.
     *
     * Stack depth here is 128 words, not bytes.
     * Priority 2 is above idle task priority, so the task can run as soon
     * as it is unblocked by the ISR.
     */
    xTaskCreate(button_task,
                "button",
                128,
                NULL,
                2,
                NULL);

    /*
     * Configure SYSCFG/EXTI and set the NVIC priority here. The NVIC
     * enable itself happens inside button_task, after the scheduler
     * has started, to avoid a FromISR call before FreeRTOS is running.
     */
    button_exti_init();

    /*
     * Start FreeRTOS scheduler.
     * From this point, the created task and interrupt-driven semaphore path
     * perform the measurement.
     */
    vTaskStartScheduler();

    /*
     * Execution reaches this only if scheduler start fails.
     */
    while (1) { }
}

static void gpio_init(void)
{
    /*
     * Enable GPIOA and GPIOC peripheral clocks before accessing their
     * registers.
     *
     * RM0368, p. 118-119:
     * - RCC_AHB1ENR bit 0 GPIOAEN enables IO port A clock.
     * - RCC_AHB1ENR bit 2 GPIOCEN enables IO port C clock.
     */
    RCC_AHB1ENR |= RCC_AHB1ENR_GPIOAEN | RCC_AHB1ENR_GPIOCEN;

    /*
     * Dummy read after enabling peripheral clocks.
     *
     * This is a common bare-metal precaution: it ensures that the write to
     * the RCC enable register has completed before GPIO registers are used.
     */
    (void)RCC_AHB1ENR;

    /*
     * Configure PA5 and PA6 as general purpose outputs.
     *
     * RM0368, p. 158:
     * GPIOx_MODER has two bits per pin:
     * - 00 = input mode
     * - 01 = general purpose output mode
     * - 10 = alternate function mode
     * - 11 = analog mode
     *
     * For pin n, MODER bits are [2n+1 : 2n].
     */
    GPIOA_MODER &= ~((3UL << (LED_PIN * 2U)) |
                     (3UL << (MARKER_PIN * 2U)));

    GPIOA_MODER |=  ((1UL << (LED_PIN * 2U)) |
                     (1UL << (MARKER_PIN * 2U)));

    /*
     * Initial output states.
     *
     * PA5 LED OFF:
     * UM1724, p. 24: LD2 is ON when PA5 is HIGH, OFF when PA5 is LOW.
     *
     * PA6 marker LOW:
     * measurement marker starts from inactive LOW state.
     */
    LED_OFF();
    MARKER_LOW();

    /*
     * Configure PC13 as input for USER button.
     *
     * UM1724, p. 24:
     * B1 USER button is connected to PC13.
     *
     * RM0368, p. 158:
     * MODER = 00 selects input mode.
     */
    GPIOC_MODER &= ~(3UL << (BUTTON_PIN * 2U));

    /*
     * Disable internal pull-up / pull-down on PC13.
     *
     * RM0368, p. 159-160:
     * GPIOx_PUPDR has two bits per pin:
     * - 00 = no pull-up, no pull-down
     * - 01 = pull-up
     * - 10 = pull-down
     * - 11 = reserved
     *
     * The USER button signal behavior is defined by the Nucleo board
     * hardware, so the internal pull resistors are left disabled here.
     */
    GPIOC_PUPDR &= ~(3UL << (BUTTON_PIN * 2U));
}

static void button_exti_init(void)
{
    /*
     * Enable SYSCFG clock.
     *
     * RM0368, p. 122:
     * RCC_APB2ENR bit 14 SYSCFGEN enables the System configuration
     * controller.
     *
     * SYSCFG is needed because EXTI line source selection is done through
     * SYSCFG_EXTICR registers.
     */
    RCC_APB2ENR |= RCC_APB2ENR_SYSCFGEN;
    (void)RCC_APB2ENR;

    /*
     * Connect EXTI13 to GPIOC.
     *
     * RM0368, p. 143:
     * - SYSCFG_EXTICR4 controls EXTI12..EXTI15.
     * - EXTI13 field is EXTI13[3:0].
     * - field position for EXTI13 is bits [7:4].
     * - code 0010 selects PC[x] as EXTIx source.
     *
     * Therefore:
     * - clear bits [7:4]
     * - write 0b0010 into bits [7:4]
     */
    SYSCFG_EXTICR4 &= ~(0xFUL << 4U);
    SYSCFG_EXTICR4 |=  (0x2UL << 4U);

    /*
     * Configure EXTI13 interrupt.
     *
     * RM0368, p. 206-207:
     * To generate an interrupt:
     * - enable the line in EXTI_IMR
     * - select edge in EXTI_RTSR and/or EXTI_FTSR
     * - enable the mapped NVIC IRQ channel
     *
     * USER button on NUCLEO-F401RE is treated as active-low in this test:
     * - button press = falling edge on PC13
     * - rising edge disabled
     * - falling edge enabled
     */
    EXTI_IMR  |=  BUTTON_MASK;
    EXTI_RTSR &= ~BUTTON_MASK;
    EXTI_FTSR |=  BUTTON_MASK;

    /*
     * Clear any old pending EXTI13 interrupt before enabling NVIC.
     *
     * RM0368, p. 211:
     * EXTI_PR pending bit is cleared by writing 1 to it.
     */
    EXTI_PR = BUTTON_MASK;

    /*
     * Set EXTI15_10 interrupt priority.
     *
     * PM0214:
     * - p. 208 and p. 215: NVIC supports programmable priorities.
     * - p. 215: only upper priority bits are implemented on STM32 Cortex-M4.
     *
     * FreeRTOS rule:
     * Any ISR that calls FreeRTOS FromISR API must use a priority value that
     * is numerically greater than or equal to
     * configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY.
     *
     * This ISR calls xSemaphoreGiveFromISR(), so the priority is set to that
     * allowed FreeRTOS boundary.
     */
    NVIC_SET_PRIORITY(EXTI15_10_IRQn,
                      configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY);
}

static void button_task(void *argument)
{
    (void)argument;

    /*
     * Enable EXTI15_10_IRQn in NVIC.
     *
     * RM0368, p. 204:
     * EXTI15_10_IRQn = 40.
     *
     * PM0214, p. 210:
     * IRQ40 is enabled by writing bit 8 in NVIC_ISER1.
     */
    NVIC_ENABLE_IRQ(EXTI15_10_IRQn);

    while (1) {
        /*
         * Wait until EXTI ISR gives the binary semaphore.
         *
         * This is the end part of the measured ISR-to-task path:
         * EXTI interrupt -> xSemaphoreGiveFromISR() -> scheduler -> task.
         */
        xSemaphoreTake(button_sem, portMAX_DELAY);

        /*
         * Measurement end point.
         *
         * PA6 is cleared immediately after the task wakes up, so the HIGH
         * pulse width on PA6 represents the measured ISR-to-task latency.
         */
        MARKER_LOW();

        /*
         * Toggle LD2 so that button handling is also visible on the board.
         * This is not the primary measurement signal; PA6 is.
         */
        LED_TOGGLE();
    }
}

/* ===================== EXTI interrupt handler ===================== */

void EXTI15_10_IRQHandler(void)
{
    BaseType_t higher_priority_task_woken = pdFALSE;

    /*
     * Check whether EXTI13 is pending.
     *
     * RM0368, p. 211:
     * EXTI_PR bit x is set when the selected trigger event occurs on
     * EXTI line x.
     */
    if (EXTI_PR & BUTTON_MASK) {
        /*
         * Clear EXTI13 pending flag.
         *
         * RM0368, p. 211:
         * EXTI_PR bit is cleared by writing 1 to it.
         */
        EXTI_PR = BUTTON_MASK;

        /*
         * Measurement start point.
         *
         * PA6 goes HIGH as soon as the interrupt handler confirms EXTI13.
         * The logic analyzer measures from here until MARKER_LOW() in the
         * FreeRTOS task.
         */
        MARKER_HIGH();

        /*
         * Give semaphore from ISR context.
         *
         * This wakes the task that is blocked in xSemaphoreTake().
         */
        xSemaphoreGiveFromISR(button_sem,
                              &higher_priority_task_woken);

        /*
         * Request a context switch at ISR exit if the unblocked task has
         * higher priority than the currently running task.
         */
        portYIELD_FROM_ISR(higher_priority_task_woken);
    }
}

static void system_clock_config_84mhz(void)
{
    /*
     * Clock target for NUCLEO-F401RE.
     *
     * UM1724, p. 25:
     * NUCLEO-F401RE can use an 8 MHz clock from ST-LINK MCO as HSE input,
     * in bypass mode.
     *
     * PLL settings:
     * - HSE   = 8 MHz
     * - PLLM  = 8
     * - PLLN  = 336
     * - PLLP  = 4
     *
     * RM0368, p. 105:
     * f(VCO clock) = f(PLL input) * PLLN / PLLM
     * f(SYSCLK)    = f(VCO clock) / PLLP
     *
     * Calculation:
     * VCO input = 8 MHz / 8 = 1 MHz
     * VCO clock = 1 MHz * 336 = 336 MHz
     * SYSCLK    = 336 MHz / 4 = 84 MHz
     */

    /*
     * Enable PWR peripheral clock.
     *
     * RM0368, p. 119-120:
     * RCC_APB1ENR bit 28 PWREN enables the power interface clock.
     *
     * In this code PWR_CR is not modified after enabling PWREN.
     * The default PWR_CR reset value keeps VOS[1:0] = 10, which corresponds
     * to Scale 2 mode on this device family.
     */
    RCC_APB1ENR |= RCC_APB1ENR_PWREN;
    (void)RCC_APB1ENR;

    /*
     * Configure Flash access for 84 MHz operation.
     *
     * RM0368, p. 60:
     * FLASH_ACR controls flash memory access time and acceleration features:
     * - LATENCY = 0010: two wait states
     * - PRFTEN  = 1: prefetch enabled
     * - ICEN    = 1: instruction cache enabled
     * - DCEN    = 1: data cache enabled
     *
     * STM32F401xE datasheet:
     * for operation at 84 MHz in the 2.7 V to 3.6 V supply range, two
     * Flash wait states are required.
     */
    FLASH_ACR &= ~FLASH_ACR_LATENCY_MASK;
    FLASH_ACR |= FLASH_ACR_LATENCY_2WS |
                 FLASH_ACR_PRFTEN |
                 FLASH_ACR_ICEN |
                 FLASH_ACR_DCEN;

    /*
     * RM0368, "Increasing the CPU frequency", step 2:
     * confirm by read-back that the new latency is effective
     * before SYSCLK is switched to the PLL.
     */
    while ((FLASH_ACR & FLASH_ACR_LATENCY_MASK) != FLASH_ACR_LATENCY_2WS) {}

    /*
     * Enable HSE bypass and then enable HSE.
     *
     * RM0368, p. 104:
     * - HSEBYP = 1: HSE oscillator is bypassed with an external clock.
     * - HSEON  = 1: HSE clock enabled.
     * - HSERDY = 1: HSE oscillator/clock is ready.
     *
     * Important:
     * HSEBYP can be written only if HSE is disabled. This function is called
     * once after reset, where HSE is normally still disabled.
     */
    RCC_CR |= RCC_CR_HSEBYP;
    RCC_CR |= RCC_CR_HSEON;

    uint32_t hse_timeout = 1000000UL;

    while ((RCC_CR & RCC_CR_HSERDY) == 0U) {
        if (--hse_timeout == 0U) {
            while (1) {
            }
        }
    }

    /*
     * Configure PLL.
     *
     * RM0368, p. 105-106:
     * - PLLM bits [5:0]
     * - PLLN bits [14:6]
     * - PLLP bits [17:16]
     * - PLLSRC bit 22
     * - PLLQ bits [27:24]
     *
     * Read-modify-write: clear only the defined fields
     * PLLM [5:0], PLLN [14:6], PLLP [17:16], PLLSRC (bit 22),
     * PLLQ [27:24]; reserved bits keep their reset values
     * (RM0368: "Reserved, must be kept at reset value").
     */
    uint32_t pllcfgr = RCC_PLLCFGR;

    pllcfgr &= ~0x0F437FFFUL;

    pllcfgr |= (8UL << 0U) |
               (336UL << 6U) |
               (1UL << 16U) |
               RCC_PLLCFGR_PLLSRC_HSE |
               (7UL << 24U);

    RCC_PLLCFGR = pllcfgr;

    /*
     * Configure bus prescalers.
     *
     * RM0368, p. 107-108:
     * - HPRE  bits [7:4]   control AHB prescaler
     * - PPRE1 bits [12:10] control APB1 prescaler
     * - PPRE2 bits [15:13] control APB2 prescaler
     *
     * Datasheet / RM0368 limits for STM32F401:
     * - AHB max  = 84 MHz
     * - APB1 max = 42 MHz
     * - APB2 max = 84 MHz
     *
     * Configuration:
     * - AHB  = SYSCLK / 1 = 84 MHz
     * - APB1 = HCLK / 2   = 42 MHz
     * - APB2 = HCLK / 1   = 84 MHz
     */
    RCC_CFGR &= ~((0xFUL << 4U) |
                  (0x7UL << 10U) |
                  (0x7UL << 13U));

    RCC_CFGR |= (0x4UL << 10U);   /* APB1 prescaler /2 */

    /*
     * Enable PLL and wait until it locks.
     *
     * RM0368, p. 103:
     * - PLLON  bit 24 enables PLL.
     * - PLLRDY bit 25 becomes 1 when PLL is locked.
     */
    RCC_CR |= RCC_CR_PLLON;

    while ((RCC_CR & RCC_CR_PLLRDY) == 0U) {}

    /*
     * Select PLL as system clock.
     *
     * RM0368, p. 107:
     * - SW[1:0] selects system clock source.
     * - SWS[1:0] reports selected system clock source.
     *
     * For STM32F4 RCC_CFGR:
     * - SW = 10 selects PLL as system clock.
     * - SWS = 10 confirms PLL is used as system clock.
     */
    RCC_CFGR &= ~0x3UL;
    RCC_CFGR |=  0x2UL;

    while (((RCC_CFGR >> 2U) & 0x3UL) != 0x2UL) {}
}
