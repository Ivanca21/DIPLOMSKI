# Poređenje latencije FreeRTOS-a i Zephyr-a na STM32 NUCLEO-F401RE

Ovaj repozitorijum sadrži prateći kod za diplomski rad:

**Postupak portovanja operativnog sistema Zephyr na ciljnu platformu i poređenje performansi sa operativnim sistemom FreeRTOS**

U radu su implementirane dve verzije istog mernog testa na istoj hardverskoj platformi:

1. **FreeRTOS implementacija**
2. **Zephyr implementacija**

Cilj je poređenje praktične interrupt-to-task/thread signalne putanje, kao i poređenje zauzeća Flash i RAM memorije.

---

## Platforma

Korišćena razvojna ploča:

- **STM32 NUCLEO-F401RE**
- Mikrokontroler: **STM32F401RE**
- CPU jezgro: **ARM Cortex-M4**
- Sistemski takt: **84 MHz**
- Alat za merenje: **USB logic analyzer**

Korišćeni pinovi:

| Funkcija | Pin | Opis |
|---|---:|---|
| USER button | PC13 | Ulazni taster koji generiše prekid |
| USER LED | PA5 | Vizuelna indikacija da je task/thread izvršen |
| Marker pin | PA6 | Digitalni signal za merenje latencije |
| GND | GND | Zajednička masa sa logic analyzer-om |

---

## Merni princip

Latencija se meri pomoću impulsa na marker pinu **PA6**.

U interrupt/callback delu programa marker pin se postavlja na visok logički nivo. Kada se odgovarajući task/thread odblokira, marker pin se odmah vraća na nizak logički nivo. Širina tog impulsa predstavlja merenu latenciju.

Osnovni tok događaja:

```text
USER button PC13
    ↓
ISR / GPIO callback
    ↓
PA6 = HIGH
    ↓
semaphore give
    ↓
scheduler
    ↓
task/thread resume
    ↓
PA6 = LOW
```

Marker pin se u obe implementacije kontroliše direktnim upisom u STM32 registar `GPIOA_BSRR`, kako bi overhead samog marker signala bio minimalan i uporediv u oba sistema.

---

## Struktura repozitorijuma

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

---

## FreeRTOS implementacija

FreeRTOS projekat je realizovan u STM32CubeIDE okruženju kao minimalan bare-metal STM32 projekat.

Kod direktno konfiguriše:

- RCC
- GPIOA i GPIOC
- SYSCFG
- EXTI
- NVIC
- sistemski takt na 84 MHz

FreeRTOS implementacija koristi:

- binarni semafor
- `xSemaphoreCreateBinary()`
- `xSemaphoreGiveFromISR()`
- `xSemaphoreTake()`
- `portYIELD_FROM_ISR()`

Signalna putanja u FreeRTOS implementaciji:

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
portYIELD_FROM_ISR()
    ↓
FreeRTOS scheduler
    ↓
button_task izlazi iz xSemaphoreTake()
    ↓
PA6 = LOW
```

### FreeRTOS clock

FreeRTOS projekat konfiguriše sistemski takt na 84 MHz:

```text
HSE = 8 MHz
PLLM = 8
PLLN = 336
PLLP = 4

SYSCLK = (8 MHz / 8) × 336 / 4 = 84 MHz
```

Zato u `FreeRTOSConfig.h` mora biti podešeno:

```c
#define configCPU_CLOCK_HZ 84000000UL
```

---

## Zephyr implementacija

Zephyr projekat je realizovan kao sopstveni out-of-tree board port za NUCLEO-F401RE pod nazivom:

```text
my_nucleo_f401re
```

Port se nalazi u okviru aplikacionog projekta `button_latency`, bez izmene glavnog Zephyr stabla izvornog koda.

Zephyr implementacija koristi:

- custom board definition
- Devicetree opis hardvera
- Kconfig konfiguraciju
- GPIO driver API
- GPIO callback
- Zephyr semafor
- `k_sem_give()`
- `k_sem_take()`

Signalna putanja u Zephyr implementaciji:

```text
PC13 button press
    ↓
Zephyr GPIO interrupt/callback path
    ↓
PA6 = HIGH
    ↓
k_sem_give()
    ↓
Zephyr scheduler
    ↓
main thread izlazi iz k_sem_take()
    ↓
PA6 = LOW
```

### Zephyr build

Primer build komande iz root foldera repozitorijuma:

```bat
west build -p always -b my_nucleo_f401re button_latency -- -DBOARD_ROOT=%cd%\zephyr
```

Primer flash komande:

```bat
west flash
```

Ako se build pokreće iz druge lokacije, putanje treba prilagoditi konkretnoj strukturi Zephyr workspace-a.

---

## Optimizacije

Testirane su dve konfiguracije:

1. optimizacija za veličinu koda
2. optimizacija za brzinu

Za FreeRTOS:

- `-Os` za optimizaciju za veličinu
- `-O2` za optimizaciju za brzinu

Za Zephyr:

- `CONFIG_SIZE_OPTIMIZATIONS=y`
- `CONFIG_SPEED_OPTIMIZATIONS=y`

---

## Rezultati merenja latencije

### Optimizacija za veličinu koda

| RTOS | Optimizacija | Min [µs] | Max [µs] | Srednja vrednost [µs] | Standardna devijacija [µs] |
|---|---:|---:|---:|---:|---:|
| Zephyr | `CONFIG_SIZE_OPTIMIZATIONS` | 15.125 | 15.208 | 15.192 | 0.023 |
| FreeRTOS | `-Os` | 5.542 | 5.583 | 5.549 | 0.016 |

Odnos srednjih vrednosti:

```text
Zephyr / FreeRTOS = 15.192 / 5.549 ≈ 2.74
```

### Optimizacija za brzinu

| RTOS | Optimizacija | Min [µs] | Max [µs] | Srednja vrednost [µs] | Standardna devijacija [µs] |
|---|---:|---:|---:|---:|---:|
| Zephyr | `CONFIG_SPEED_OPTIMIZATIONS=y` | 12.833 | 12.917 | 12.857 | 0.024 |
| FreeRTOS | `-O2` | 5.458 | 5.500 | 5.463 | 0.013 |

Odnos srednjih vrednosti:

```text
Zephyr / FreeRTOS = 12.857 / 5.463 ≈ 2.35
```

---

## Zauzeće memorije

| Konfiguracija | text [B] | data [B] | bss [B] | Flash [B] | RAM [B] |
|---|---:|---:|---:|---:|---:|
| Zephyr, veličina | 15000 | 60 | 4387 | 15060 | 4447 |
| FreeRTOS, `-Os` | 6396 | 4 | 12084 | 6400 | 12088 |
| Zephyr, brzina | 18552 | 60 | 4390 | 18612 | 4450 |
| FreeRTOS, `-O2` | 6824 | 4 | 12084 | 6828 | 12088 |

Napomena: FreeRTOS RAM zauzeće uključuje statički rezervisan heap:

```c
#define configTOTAL_HEAP_SIZE (10 * 1024)
```

Ako se iz prikazanog RAM zauzeća izuzme statički rezervisan heap, preostalo statičko RAM zauzeće FreeRTOS implementacije iznosi približno:

```text
12088 B - 10240 B = 1848 B
```
---

## Zaključak

Na istoj STM32F401RE platformi i pri istoj frekvenciji od 84 MHz, implementirana FreeRTOS putanja ima manju srednju latenciju od Zephyr putanje.

Rezultati pokazuju:

- FreeRTOS ima manju latenciju u merenoj signalnoj putanji.
- FreeRTOS zauzima manje Flash memorije.
- Zephyr daje veći nivo apstrakcije, portabilnost i jasnije razdvajanje aplikacionog koda od opisa hardvera.
- Poređenje RAM zauzeća zavisi od izabrane veličine FreeRTOS heap-a i treba ga tumačiti uz metodološke napomene.

Osnovni zaključak je da izbor između FreeRTOS-a i Zephyr-a zavisi od prioriteta projekta: za minimalnu latenciju i manje Flash zauzeće pogodniji je FreeRTOS, dok je za portabilnost, standardizovan drajverski model i sistemsku organizaciju pogodniji Zephyr.

---

## Napomena

Repozitorijum je namenjen kao prateći kod za diplomski rad. Rezultati zavise od konkretne konfiguracije projekta, kompajlerske optimizacije, takta procesora i primenjene metodologije merenja.
