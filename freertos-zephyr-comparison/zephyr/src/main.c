#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>

/*
 * Zephyr latency measurement example for STM32 NUCLEO-F401RE.
 *
 * Measurement idea:
 * - PC13 USER button generates an EXTI interrupt.
 * - GPIO callback sets PA6 marker HIGH and gives a semaphore.
 * - The main thread wakes up, clears PA6 marker LOW and toggles PA5 LED.
 * - Logic analyzer measures the HIGH pulse width on PA6.
 *
 * Board references (UM1724):
 * - p. 24: LD2 user LED on PA5 (devicetree alias led0).
 * - p. 24: B1 USER button on PC13 (devicetree alias sw0).
 * - p. 45: Arduino D12 = PA6, used as latency marker (alias latency0,
 *          defined in the custom board devicetree).
 */

 /* ===================== Devicetree bindings ===================== */

/*
 * Devicetree aliases resolved at build time.
 *
 * led0, sw0 and latency0 are all defined in the custom board devicetree
 * (my_nucleo_f401re.dts), so the application code never refers to a
 * concrete GPIO port or pin number.
 */
#define LED_NODE    DT_ALIAS(led0)
#define BUTTON_NODE DT_ALIAS(sw0)
#define MARKER_NODE  DT_ALIAS(latency0)

/*
 * Each gpio_dt_spec bundles the port device, pin number and devicetree
 * flags. Polarity lives in the flags: on this board sw0 is ACTIVE_LOW
 * (external pull-up), so the "active" level
 * used by the GPIO API below already accounts for that.
 */
static const struct gpio_dt_spec led = GPIO_DT_SPEC_GET(LED_NODE, gpios);
static const struct gpio_dt_spec button = GPIO_DT_SPEC_GET(BUTTON_NODE, gpios);
static const struct gpio_dt_spec marker = GPIO_DT_SPEC_GET(MARKER_NODE, gpios);

/* ======================== Kernel objects ======================== */

/*
 * The callback container must have static storage duration: the GPIO
 * driver keeps it linked in its callback list for the lifetime of the
 * registration.
 */
static struct gpio_callback button_cb_data;

/*
 * ISR-to-thread synchronization. Initialized in main() as a binary
 * (count 0, limit 1) semaphore; k_sem_give() is ISR-safe, Zephyr has
 * no separate FromISR API variants like FreeRtos.
 */
static struct k_sem button_sem;

/* ===================== Raw marker access ===================== */

/*
 * Direct register access only for a clean measurement marker.
 *
 * The marker edges are kept as single register stores so that both
 * edges cost the same and no driver overhead leaks into the measured
 * pulse. The LED, which is not part of the measurement, goes through
 * the regular GPIO API instead.
 *
 * STM32F401xE datasheet, Memory mapping, pp. 51-54:
 * - GPIOA base = 0x40020000
 *
 * RM0368, p. 152 and p. 161:
 * - GPIOx_BSRR offset = 0x18
 * - writing 1 to BSRR bit y sets ODRy
 * - writing 1 to BSRR bit y+16 resets ODRy
 * - BSRR provides atomic bitwise handling, no read-modify-write needed.
 */
#define GPIOA_BASE   0x40020000UL
#define GPIOA_BSRR   (*(volatile uint32_t *)(GPIOA_BASE + 0x18UL))
#define MARKER_PIN   (1UL << 6)

#define MARKER_HIGH()   (GPIOA_BSRR = MARKER_PIN)
#define MARKER_LOW()    (GPIOA_BSRR = (MARKER_PIN << 16U))


/*
 * Runs in interrupt context, invoked from Zephyr's shared EXTI ISR
 * through the GPIO driver's callback list. By the time this function
 * executes, the driver has already cleared the EXTI pending flag.
 */
static void button_pressed_callback(const struct device *dev,
                                    struct gpio_callback *cb,
                                    uint32_t pins)
{
    ARG_UNUSED(dev);
    ARG_UNUSED(cb);
    ARG_UNUSED(pins);

    /*
     * Measurement start point.
     *
     * PA6 goes HIGH as soon as the application-level callback runs.
     * The logic analyzer measures from here until MARKER_LOW() in the
     * main thread.
     */
    MARKER_HIGH();

    /*
     * Wake the main thread blocked in k_sem_take(). If that thread has
     * higher priority than whatever was preempted, the context switch
     * happens on interrupt exit. Main thread is set to have the highest
     * priority in zephyr.config via CONFIG_MAIN_THREAD_PRIORITY=0.
     */
    k_sem_give(&button_sem);
}

int main(void)
{
    /*
     * Devices are initialized by the kernel before main(); this only
     * verifies that their init succeeded.
     */
    if (!gpio_is_ready_dt(&led) || !gpio_is_ready_dt(&button) || !gpio_is_ready_dt(&marker) ) {
        return 0;
    }

    /*
     * devicetree polarity flags are applied by the driver.
     */
    gpio_pin_configure_dt(&led, GPIO_OUTPUT_INACTIVE);
    gpio_pin_configure_dt(&button, GPIO_INPUT);
    gpio_pin_configure_dt(&marker, GPIO_OUTPUT_INACTIVE);

    /*
     * Binary semaphore: initial count 0 (empty, the thread blocks
     * until the ISR gives it), maximum count 1.
     */
    k_sem_init(&button_sem, 0, 1);

    /*
     * Register the callback before enabling the interrupt, so no edge
     * can arrive while the driver has nowhere to deliver it.
     */
    gpio_init_callback(&button_cb_data,
                       button_pressed_callback,
                       BIT(button.pin));

    gpio_add_callback(button.port, &button_cb_data);

    /*
     * Enable the interrupt last, once the semaphore and callback exist.
     *
     * GPIO_INT_EDGE_TO_ACTIVE = edge toward the logical active level.
     * With sw0 declared ACTIVE_LOW in the board devicetree this is the
     * falling physical edge on PC13, i.e. the button press.
     */
    gpio_pin_interrupt_configure_dt(&button, GPIO_INT_EDGE_TO_ACTIVE);

    while (1) {
        /*
         * Wait until the GPIO callback gives the semaphore.
         */
        k_sem_take(&button_sem, K_FOREVER);

        /*
         * Measurement end point.
         *
         * PA6 is cleared immediately after the thread wakes up, so the
         * HIGH pulse width on PA6 represents the callback-to-thread
         * latency.
         */
        MARKER_LOW();

        /*
         * Toggle LD2 so button handling is also visible on the board
         */
        gpio_pin_toggle_dt(&led);
    }
}
