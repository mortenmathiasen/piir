# Raspberry Pi 2/3/4/Zero 2 running 32-bit Raspberry Pi OS.
# Compiler: sudo apt install gcc-arm-linux-gnueabihf
# Debian's armhf compiler targets ARMv7, so binaries will not run on the
# ARMv6 Pi 1 and Pi Zero/Zero W.
set(RPI_TRIPLE arm-linux-gnueabihf)
set(CMAKE_SYSTEM_PROCESSOR arm)
include(${CMAKE_CURRENT_LIST_DIR}/rpi-common.cmake)
