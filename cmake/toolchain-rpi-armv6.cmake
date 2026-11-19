# Raspberry Pi 1 and Zero/Zero W (ARMv6). The binary also runs on every
# other Pi with 32-bit Raspberry Pi OS.
# Compiler: sudo apt install gcc-arm-linux-gnueabi
#
# Debian's armhf compiler and C library target ARMv7, so this uses the
# soft-float armel ones, which run on ARMv6. Raspberry Pi OS has no armel
# libraries, so the binary is linked statically. That suits the default lirc
# backend; for gpiod or pigpio on ARMv6, build on the Pi instead.
set(RPI_TRIPLE arm-linux-gnueabi)
set(CMAKE_SYSTEM_PROCESSOR arm)
set(CMAKE_C_FLAGS_INIT "-march=armv6 -marm")
set(PIIR_STATIC ON CACHE BOOL "Link piir statically")
include(${CMAKE_CURRENT_LIST_DIR}/rpi-common.cmake)
