# Shared settings for cross compiling PIIR to Raspberry Pi.
# Included by the toolchain-rpi-*.cmake files, which set RPI_TRIPLE and
# CMAKE_SYSTEM_PROCESSOR first.
#
# Target libraries (libgpiod or pigpio) are found either:
#   - in a sysroot copied from the Pi: pass -DRPI_SYSROOT=/path/to/sysroot
#     (or set the RPI_SYSROOT environment variable), or
#   - from Debian multiarch packages on the build host, e.g.
#     dpkg --add-architecture arm64 && apt install libgpiod-dev:arm64

set(CMAKE_SYSTEM_NAME Linux)

set(RPI_TOOLCHAIN_PREFIX "${RPI_TRIPLE}-" CACHE STRING "Cross compiler prefix")
set(CMAKE_C_COMPILER "${RPI_TOOLCHAIN_PREFIX}gcc")

# Toolchain files are evaluated more than once; pass these on to try_compile.
list(APPEND CMAKE_TRY_COMPILE_PLATFORM_VARIABLES RPI_SYSROOT RPI_TOOLCHAIN_PREFIX)

if(NOT RPI_SYSROOT AND DEFINED ENV{RPI_SYSROOT})
  set(RPI_SYSROOT "$ENV{RPI_SYSROOT}")
endif()

if(RPI_SYSROOT)
  set(CMAKE_SYSROOT "${RPI_SYSROOT}")
  set(CMAKE_FIND_ROOT_PATH "${RPI_SYSROOT}")
  set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
  set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
  set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
  set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
endif()

# Search lib/<triple> (multiarch) on the host or in the sysroot.
set(CMAKE_LIBRARY_ARCHITECTURE "${RPI_TRIPLE}")
