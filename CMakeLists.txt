cmake_minimum_required(VERSION 3.13)

project(PIIR
  VERSION 1.0.0
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

add_executable(piir
  src/piir.c
  src/hvac_panasonic_plugin.c
  src/irslinger.c
  src/transmitter.c
  src/lirc.c
  src/log.c
  src/config.c
  src/parson.c)

target_compile_definitions(piir PRIVATE
  DATADIR="${CMAKE_INSTALL_FULL_DATADIR}/piir")

if(PIIR_BACKEND STREQUAL "lirc")
  target_compile_definitions(piir PRIVATE HAVE_LINUX_LIRC_H=1 LIRC_DEVICE="${PIIR_LIRC_DEVICE}")
elseif(PIIR_BACKEND STREQUAL "gpiod")
  target_compile_definitions(piir PRIVATE HAVE_GPIOD_H=1)
  target_include_directories(piir PRIVATE ${GPIOD_INCLUDE_DIR})
  target_link_libraries(piir PRIVATE ${GPIOD_LIBRARY})
elseif(PIIR_BACKEND STREQUAL "pigpio")
  target_compile_definitions(piir PRIVATE HAVE_PIGPIO_H=1)
  target_include_directories(piir PRIVATE ${PIGPIO_INCLUDE_DIR})
  target_link_libraries(piir PRIVATE ${PIGPIO_LIBRARY})
endif()

install(TARGETS piir RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES conf/remotes/hvac_panasonic.json
  DESTINATION ${CMAKE_INSTALL_DATADIR}/piir/conf/remotes)
