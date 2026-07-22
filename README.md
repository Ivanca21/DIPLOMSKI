# Diplomski rad — FreeRTOS i Zephyr na STM32 NUCLEO-F401RE

Ovaj repozitorijum sadrži prateći kod za diplomski rad na temu:

**Roadmap za portovanje Zephyra i poređenje performansi Zephyra i FreeRTOS-a**

Cilj rada je implementacija istog real-time testa na istoj hardverskoj platformi korišćenjem dva RTOS-a: **FreeRTOS** i **Zephyr**. Kao test platforma korišćena je razvojna ploča **STM32 NUCLEO-F401RE** sa mikrokontrolerom **STM32F401RE**.

## Sadržaj repozitorijuma

Repozitorijum je podeljen na dva glavna dela:

```text
FreeRTOS/
└── FreeRTOS implementacija testa

button_latency/
└── Zephyr implementacija testa
