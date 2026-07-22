# ako korisnik korisit nizu verziju => cmake ce vratiti error
cmake_minimum_required(VERSION 3.20.0)

# pronadji zephyr paket i ukljuci kompletan zephyr build sistem
# nakon ovoga zephyr ubacuje :
# kernel
# drivers
# devicetree obradu
# Kconfig obradu
# linker script generisanje
# board konfiguraciju
# compiler flags
# startup kod
# interrupt setup
# system init mehanizam
# => zato u ZEPHYR aplikaciji ne dodajes rucni kernel fajlove kao za freertos!!!!!!!!!!!(tasks.c, queue.c, lists.c ...)
# drugim recima ova linija pokrece sistem KOJ SAM ZNA STA TREBA DA UKLJUCI!!!
# ZEPHYR_BASE -> pokazuje na ZEPHYR SOURCE folder(C:\zephyyrproject\zephyr) (definisano u cmake-u u zephyr source-u)
# => zephyr zna da trazi ovde : C:\zephyyrproject\zephyr
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})

# button_latency je sada zvanicno ime projekta
# cmake sada automatski kreira set promenljivih koje mozes da koristis preko ${PROJECT_NAME} -> sadrzi "button_latency".
# ili ${PROJECT_BINARY_DIR} -> putanja do build foldera za ovaj projekat.(npr -> C:\zephyyrproject\build).
# ili  ${PROJECT_SOURCE_DIR} -> putanja do foldera gde je projekat.
project(button_latency)

# dodaj src/main.c u  application target
# app je vec unapred definisan od strane zephyra -> zephyr je vec napravio target koj predstavlja moju aplikaciju
# "u moju aplikaciju ubaci ovaj c fajl"
# src/main.c treba kompajlirati i linkovati kao deo finalnog firmware-a
# PRIVATE -> ovaj source fajl(main.c) pripada samo targetu app.
# src/main.c se kompajlira kao deo tvoje aplikacije
target_sources(app PRIVATE src/main.c)

#ZEPHYR DALJE SMA GENERISE FIRMWARE
