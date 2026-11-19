cmake_minimum_required(VERSION 3.13)

project(PIIR
  VERSION 2.1.0
  DESCRIPTION "Raspberry Pi IR transmitter"
  HOMEPAGE_URL "https://github.com/mortenmathiasen/piir"
  LANGUAGES C)

include(GNUInstallDirs)

# Transmitter backend, in order of preference for auto:
#   lirc   - kernel IR driver (dtoverlay=gpio-ir-tx or pwm-ir-tx), accurate timing on all Pi models
#   gpiod  - libgpiod >= 2.0, carrier bit-banged from user space
#   pigpio - pigpio DMA waveforms, not supported on Pi 5
set(PIIR_GPIO_BACKEND "auto" CACHE STRING "Transmitter backend to use: auto, lirc, gpiod or pigpio")
set_property(CACHE PIIR_GPIO_BACKEND PROPERTY STRINGS auto lirc gpiod pigpio)
set(PIIR_LIRC_DEVICE "/dev/lirc0" CACHE STRING "Default LIRC transmitter device for the lirc backend")

include(CheckIncludeFile)
check_include_file(linux/lirc.h HAVE_LINUX_LIRC_H)

find_path(GPIOD_INCLUDE_DIR gpiod.h)
find_library(GPIOD_LIBRARY gpiod)
find_path(PIGPIO_INCLUDE_DIR pigpio.h)
find_library(PIGPIO_LIBRARY pigpio)

set(PIIR_BACKEND "")
if(PIIR_GPIO_BACKEND STREQUAL "lirc" OR PIIR_GPIO_BACKEND STREQUAL "auto")
  if(HAVE_LINUX_LIRC_H)
    set(PIIR_BACKEND lirc)
  elseif(PIIR_GPIO_BACKEND STREQUAL "lirc")
    message(FATAL_ERROR "linux/lirc.h not found")
  endif()
endif()
if(NOT PIIR_BACKEND AND (PIIR_GPIO_BACKEND STREQUAL "gpiod" OR PIIR_GPIO_BACKEND STREQUAL "auto"))
  if(GPIOD_INCLUDE_DIR AND GPIOD_LIBRARY)
    include(CheckSymbolExists)
    set(CMAKE_REQUIRED_INCLUDES ${GPIOD_INCLUDE_DIR})
    set(CMAKE_REQUIRED_LIBRARIES ${GPIOD_LIBRARY})
    check_symbol_exists(gpiod_chip_request_lines gpiod.h HAVE_GPIOD_V2)
    unset(CMAKE_REQUIRED_INCLUDES)
    unset(CMAKE_REQUIRED_LIBRARIES)
    if(NOT HAVE_GPIOD_V2)
      message(FATAL_ERROR "libgpiod >= 2.0 is required (found an older version)")
    endif()
    set(PIIR_BACKEND gpiod)
  elseif(PIIR_GPIO_BACKEND STREQUAL "gpiod")
    message(FATAL_ERROR "libgpiod not found")
  endif()
endif()
if(NOT PIIR_BACKEND AND (PIIR_GPIO_BACKEND STREQUAL "pigpio" OR PIIR_GPIO_BACKEND STREQUAL "auto"))
  if(PIGPIO_INCLUDE_DIR AND PIGPIO_LIBRARY)
    set(PIIR_BACKEND pigpio)
  elseif(PIIR_GPIO_BACKEND STREQUAL "pigpio")
    message(FATAL_ERROR "pigpio not found")
  endif()
endif()
if(NOT PIIR_BACKEND)
  message(FATAL_ERROR "No transmitter backend found; need linux/lirc.h, libgpiod >= 2.0 or pigpio")
else()
  message(STATUS "PIIR GPIO backend: ${PIIR_BACKEND}")
endif()

# Measure test coverage with gcov (gcovr), and of tools/lirc2piir.py with
# Python's coverage module when it is installed
option(PIIR_COVERAGE "Build with coverage instrumentation" OFF)
if(PIIR_COVERAGE)
  add_compile_options(--coverage -O0)
  add_link_options(--coverage)
endif()

# Everything except main() goes into a library shared by piir and the tests
add_library(piir_core STATIC
  src/hvac_panasonic_plugin.c
  src/plugins.c
  src/remotes.c
  src/irslinger.c
  src/transmitter.c
  src/lirc.c
  src/log.c
  src/config.c
  src/parson.c)
target_include_directories(piir_core PUBLIC src)

target_compile_definitions(piir_core PUBLIC
  DATADIR="${CMAKE_INSTALL_FULL_DATADIR}/piir")

if(PIIR_BACKEND STREQUAL "lirc")
  target_compile_definitions(piir_core PUBLIC HAVE_LINUX_LIRC_H=1 LIRC_DEVICE="${PIIR_LIRC_DEVICE}")
elseif(PIIR_BACKEND STREQUAL "gpiod")
  target_compile_definitions(piir_core PUBLIC HAVE_GPIOD_H=1)
  target_include_directories(piir_core PUBLIC ${GPIOD_INCLUDE_DIR})
  target_link_libraries(piir_core PUBLIC ${GPIOD_LIBRARY})
elseif(PIIR_BACKEND STREQUAL "pigpio")
  target_compile_definitions(piir_core PUBLIC HAVE_PIGPIO_H=1)
  target_include_directories(piir_core PUBLIC ${PIGPIO_INCLUDE_DIR})
  target_link_libraries(piir_core PUBLIC ${PIGPIO_LIBRARY})
endif()

add_executable(piir src/piir.c)
target_link_libraries(piir PRIVATE piir_core)

# A static binary runs on any Raspberry Pi OS release regardless of its glibc
option(PIIR_STATIC "Link piir statically" OFF)
if(PIIR_STATIC)
  target_link_options(piir PRIVATE -static)
endif()

install(TARGETS piir RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
include(cmake/remotes.cmake)
install(FILES README.md LICENSE DESTINATION ${CMAKE_INSTALL_DATADIR}/doc/piir)

include(CTest)
if(BUILD_TESTING)
  add_subdirectory(tests)
endif()

include(cmake/packaging.cmake)
