# Raspberry Pi 3/4/5/Zero 2 running 64-bit Raspberry Pi OS.
# Compiler: sudo apt install gcc-aarch64-linux-gnu
set(RPI_TRIPLE aarch64-linux-gnu)
set(CMAKE_SYSTEM_PROCESSOR aarch64)
include(${CMAKE_CURRENT_LIST_DIR}/rpi-common.cmake)
