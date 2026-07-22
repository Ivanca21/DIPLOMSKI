# FreeRTOS vs Zephyr Latency Comparison on STM32 NUCLEO-F401RE

Ovaj repozitorijum sadrži dva projekta za istu STM32 NUCLEO-F401RE platformu:

1. **FreeRTOS implementaciju**
2. **Zephyr implementaciju**

Cilj je poređenje real-time ponašanja dva RTOS-a kroz merenje latencije od eksternog interrupt-a do izvršavanja task/thread koda.

Tema rada:

> Roadmap za portovanje Zephyra i poređenje performansi Zephyra i FreeRTOS-a

---

## Platforma

Korišćena razvojna ploča:

- **STM32 NUCLEO-F401RE**
- Mikrokontroler: **STM32F401RE**
- CPU jezgro: **ARM Cortex-M4**
- Sistemski takt: **84 MHz**
- Alat za merenje: **USB logic analyzer 24 MHz**

Korišćeni pinovi:

| Funkcija | Pin | Opis |
|---|---:|---|
| USER button | PC13 | Ulazni taster, koristi EXTI13 interrupt |
| LD2 LED | PA5 | Vizuelna potvrda obrade događaja |
| Latency marker | PA6 | Signal koji meri logic analyzer |

---

## Ideja merenja

Merenje se zasniva na širini HIGH impulsa na pinu **PA6**.

Tok događaja:

1. Korisnik pritisne taster B1.
2. PC13 generiše eksterni interrupt.
3. ISR/callback postavlja PA6 na HIGH.
4. ISR/callback signalizira task/thread preko semaphore mehanizma.
5. Task/thread se budi.
6. Task/thread spušta PA6 na LOW.
7. Logic analyzer meri trajanje HIGH stanja na PA6.

Izmereni impuls predstavlja vreme od početka interrupt obrade do nastavka izvršavanja task/thread koda.


---

## Struktura repozitorijuma

Predložena struktura:

```text
freertos-zephyr-comparison/
├── freertos/
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
├── zephyr/
│   ├── boards/
│   ├── src/
│   ├── CMakeLists.txt
│   └── prj.conf
│
├── README.md
```

---

## FreeRTOS projekat

FreeRTOS projekat je realizovan u STM32CubeIDE okruženju, uz ručno podešen bare-metal kod za:

- GPIO konfiguraciju
- EXTI interrupt
- NVIC konfiguraciju
- clock konfiguraciju na 84 MHz
- PA6 latency marker
- PA5 LED toggle

FreeRTOS deo koristi:

- binary semaphore
- `xSemaphoreGiveFromISR()`
- `xSemaphoreTake()`
- `portYIELD_FROM_ISR()`

Osnovna signalna putanja:

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
FreeRTOS context switch
    ↓
button_task()
    ↓
PA6 = LOW
```

### FreeRTOS clock

FreeRTOS projekat podešava sistemski takt na 84 MHz:

```text
HSE = 8 MHz
PLLM = 8
PLLN = 336
PLLP = 4

SYSCLK = (8 MHz / 8) × 336 / 4 = 84 MHz
```

Zato je u `FreeRTOSConfig.h`:

```c
#define configCPU_CLOCK_HZ 84000000UL
```
---

## Zephyr projekat

Zephyr projekat je realizovan kao custom board port za NUCLEO-F401RE.

Korišćeni elementi:

- custom board definition
- Devicetree opis za LED, button i marker pin
- `prj.conf`
- GPIO driver API
- Zephyr semaphore
- GPIO callback

Osnovna signalna putanja:

```text
PC13 button press
    ↓
Zephyr GPIO interrupt callback
    ↓
PA6 = HIGH
    ↓
k_sem_give()
    ↓
Zephyr scheduler
    ↓
main thread wakes up
    ↓
PA6 = LOW
```

### Build komanda za Zephyr

Primer build komande iz root foldera repozitorijuma:

```bat
west build -p always -b my_nucleo_f401re zephyr -- -DBOARD_ROOT=%cd%/zephyr
```

Ukoliko se build pokreće iz Zephyr workspace-a, putanje treba prilagoditi konkretnoj lokaciji projekta.

Primer flash komande:

```bat
west flash
```

---

## Optimizacije

Testirane su dve vrste optimizacije:

1. optimizacija za veličinu
2. optimizacija za brzinu

Za FreeRTOS:

- `-Os` za size optimized build
- `-O2` za speed optimized build

Za Zephyr:

- `CONFIG_SIZE_OPTIMIZATIONS=y`
- `CONFIG_SPEED_OPTIMIZATIONS=y`

---

## Rezultati merenja latencije

### Size optimized build

| RTOS | Optimizacija | Prosečna latencija | Minimum | Maksimum | Jitter |
|---|---:|---:|---:|---:|---:|
| FreeRTOS | `-Os` | 4.829 µs | 4.708 µs | 5.750 µs | 1.042 µs |
| Zephyr | size optimization | 15.1916 µs | 15.167 µs | 15.208 µs | 0.041 µs |

Odnos:

```text
Zephyr / FreeRTOS = 15.1916 / 4.829 ≈ 3.15
```

U ovom testu, Zephyr signalna putanja je bila približno 3.15 puta sporija od FreeRTOS signalne putanje.

### Speed optimized build

| RTOS | Optimizacija | Prosečna latencija | Minimum | Maksimum | Jitter |
|---|---:|---:|---:|---:|---:|
| FreeRTOS | `-O2` | 4.7251 µs | 4.625 µs | 5.542 µs | 0.917 µs |
| Zephyr | `CONFIG_SPEED_OPTIMIZATIONS=y` | 12.9128 µs | 12.875 µs | 12.917 µs | 0.042 µs |

Odnos:

```text
Zephyr / FreeRTOS = 12.9128 / 4.7251 ≈ 2.73
```

U speed optimized konfiguraciji, Zephyr signalna putanja je bila približno 2.73 puta sporija od FreeRTOS signalne putanje.

---

## Veličina programa

### Size optimized build

| RTOS | Flash | RAM |
|---|---:|---:|
| FreeRTOS `-Os` | 5192 B | 12080 B |
| Zephyr size optimized | 15060 B | 4447 B |

Napomena: FreeRTOS RAM uključuje statički rezervisan heap:

```c
#define configTOTAL_HEAP_SIZE (10 * 1024)
```

Zato je korisno navesti i aproksimaciju:

```text
FreeRTOS RAM bez statički rezervisanog heap-a ≈ 1840 B
```

### Speed optimized build

| RTOS | Flash | RAM |
|---|---:|---:|
| FreeRTOS `-O2` | 5624 B | 12080 B |
| Zephyr speed optimized | 18612 B | 4450 B |

---

## Zaključak

Na istoj STM32F401RE platformi i pri istoj frekvenciji od 84 MHz, FreeRTOS implementacija je pokazala manju prosečnu latenciju u merenoj putanji od Zephyr implementacije.

Glavna razlika je u nivou apstrakcije:

- FreeRTOS implementacija koristi direktan EXTI interrupt handler i direktan pristup registrima.
- Zephyr implementacija koristi GPIO driver, callback mehanizam i dodatne slojeve apstrakcije.


---

## Napomena

Repozitorijum je namenjen kao prateći kod za diplomski rad. Rezultati zavise od konkretne konfiguracije projekta, kompajlerske optimizacije, takta procesora itd...
