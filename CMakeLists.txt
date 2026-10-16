cmake_minimum_required(VERSION 3.13)

project(PIIR
  VERSION 1.0.0
  DESCRIPTION "Raspberry Pi IR transmitter"
  HOMEPAGE_URL "https://github.com/mortenmathiasen/piir"
  LANGUAGES C)

include(GNUInstallDirs)

# GPIO backend: libgpiod (default when found) or pigpio.
set(PIIR_GPIO_BACKEND "auto" CACHE STRING "GPIO library to use: auto, gpiod or pigpio")
set_property(CACHE PIIR_GPIO_BACKEND PROPERTY STRINGS auto gpiod pigpio)

find_path(GPIOD_INCLUDE_DIR gpiod.h)
find_library(GPIOD_LIBRARY gpiod)
find_path(PIGPIO_INCLUDE_DIR pigpio.h)
find_library(PIGPIO_LIBRARY pigpio)

set(PIIR_BACKEND "")
if(PIIR_GPIO_BACKEND STREQUAL "gpiod" OR PIIR_GPIO_BACKEND STREQUAL "auto")
  if(GPIOD_INCLUDE_DIR AND GPIOD_LIBRARY)
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
  message(FATAL_ERROR "Neither libgpiod nor pigpio found; install one of them")
else()
  message(STATUS "PIIR GPIO backend: ${PIIR_BACKEND}")
endif()

add_executable(piir
  src/piir.c
  src/hvac_panasonic_plugin.c
  src/irslinger.c
  src/transmitter.c
  src/log.c
  src/config.c
  src/parson.c)

target_compile_definitions(piir PRIVATE
  DATADIR="${CMAKE_INSTALL_FULL_DATADIR}/piir")

if(PIIR_BACKEND STREQUAL "gpiod")
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
