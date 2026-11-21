cmake_minimum_required(VERSION 3.13)

project(irlingo
  VERSION 3.0.0
  DESCRIPTION "Infrared remote control transmitter with a database of remotes"
  HOMEPAGE_URL "https://github.com/mortenmathiasen/irlingo"
  LANGUAGES C)

include(GNUInstallDirs)

# Transmitter backend, in order of preference for auto:
#   lirc   - kernel IR driver (dtoverlay=gpio-ir-tx or pwm-ir-tx), accurate timing on all Pi models
#   gpiod  - libgpiod >= 2.0, carrier bit-banged from user space
#   pigpio - pigpio DMA waveforms, not supported on Pi 5
set(IRLINGO_GPIO_BACKEND "auto" CACHE STRING "Transmitter backend to use: auto, lirc, gpiod or pigpio")
set_property(CACHE IRLINGO_GPIO_BACKEND PROPERTY STRINGS auto lirc gpiod pigpio)
set(IRLINGO_LIRC_DEVICE "/dev/lirc0" CACHE STRING "Default LIRC transmitter device for the lirc backend")

include(CheckIncludeFile)
check_include_file(linux/lirc.h HAVE_LINUX_LIRC_H)

find_path(GPIOD_INCLUDE_DIR gpiod.h)
find_library(GPIOD_LIBRARY gpiod)
find_path(PIGPIO_INCLUDE_DIR pigpio.h)
find_library(PIGPIO_LIBRARY pigpio)

set(IRLINGO_BACKEND "")
if(IRLINGO_GPIO_BACKEND STREQUAL "lirc" OR IRLINGO_GPIO_BACKEND STREQUAL "auto")
  if(HAVE_LINUX_LIRC_H)
    set(IRLINGO_BACKEND lirc)
  elseif(IRLINGO_GPIO_BACKEND STREQUAL "lirc")
    message(FATAL_ERROR "linux/lirc.h not found")
  endif()
endif()
if(NOT IRLINGO_BACKEND AND (IRLINGO_GPIO_BACKEND STREQUAL "gpiod" OR IRLINGO_GPIO_BACKEND STREQUAL "auto"))
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
    set(IRLINGO_BACKEND gpiod)
  elseif(IRLINGO_GPIO_BACKEND STREQUAL "gpiod")
    message(FATAL_ERROR "libgpiod not found")
  endif()
endif()
if(NOT IRLINGO_BACKEND AND (IRLINGO_GPIO_BACKEND STREQUAL "pigpio" OR IRLINGO_GPIO_BACKEND STREQUAL "auto"))
  if(PIGPIO_INCLUDE_DIR AND PIGPIO_LIBRARY)
    set(IRLINGO_BACKEND pigpio)
  elseif(IRLINGO_GPIO_BACKEND STREQUAL "pigpio")
    message(FATAL_ERROR "pigpio not found")
  endif()
endif()
if(NOT IRLINGO_BACKEND)
  message(FATAL_ERROR "No transmitter backend found; need linux/lirc.h, libgpiod >= 2.0 or pigpio")
else()
  message(STATUS "irlingo GPIO backend: ${IRLINGO_BACKEND}")
endif()

# Measure test coverage with gcov (gcovr), and of tools/lirc2irlingo.py with
# Python's coverage module when it is installed
option(IRLINGO_COVERAGE "Build with coverage instrumentation" OFF)
if(IRLINGO_COVERAGE)
  add_compile_options(--coverage -O0)
  add_link_options(--coverage)
endif()

# The portable core in components/irlingo/: loading remotes, features,
# overrides, variants, checksums, plugins and turning codes into pulses. The
# ESPHome component compiles the same files, so they must not use anything
# specific to Linux.
add_library(irlingo_core STATIC
  components/irlingo/hvac_panasonic_plugin.c
  components/irlingo/plugins.c
  components/irlingo/checksum.c
  components/irlingo/pulses.c
  components/irlingo/log.c
  components/irlingo/config.c
  components/irlingo/parson.c)
target_include_directories(irlingo_core PUBLIC components/irlingo)

# The Linux parts: finding installed remotes, the toggle state file and the
# transmitter backends. Everything except main() is shared by irlingo and
# the tests.
add_library(irlingo_linux STATIC
  src/remotes.c
  src/state.c
  src/irslinger.c
  src/transmitter.c
  src/lirc.c)
target_include_directories(irlingo_linux PUBLIC src)
target_link_libraries(irlingo_linux PUBLIC irlingo_core)

target_compile_definitions(irlingo_linux PUBLIC
  DATADIR="${CMAKE_INSTALL_FULL_DATADIR}/irlingo")

if(IRLINGO_BACKEND STREQUAL "lirc")
  target_compile_definitions(irlingo_linux PUBLIC HAVE_LINUX_LIRC_H=1 LIRC_DEVICE="${IRLINGO_LIRC_DEVICE}")
elseif(IRLINGO_BACKEND STREQUAL "gpiod")
  target_compile_definitions(irlingo_linux PUBLIC HAVE_GPIOD_H=1)
  target_include_directories(irlingo_linux PUBLIC ${GPIOD_INCLUDE_DIR})
  target_link_libraries(irlingo_linux PUBLIC ${GPIOD_LIBRARY})
elseif(IRLINGO_BACKEND STREQUAL "pigpio")
  target_compile_definitions(irlingo_linux PUBLIC HAVE_PIGPIO_H=1)
  target_include_directories(irlingo_linux PUBLIC ${PIGPIO_INCLUDE_DIR})
  target_link_libraries(irlingo_linux PUBLIC ${PIGPIO_LIBRARY})
endif()

add_executable(irlingo src/irlingo.c)
target_link_libraries(irlingo PRIVATE irlingo_linux)

# A static binary runs on any Raspberry Pi OS release regardless of its glibc
option(IRLINGO_STATIC "Link irlingo statically" OFF)
if(IRLINGO_STATIC)
  target_link_options(irlingo PRIVATE -static)
endif()

install(TARGETS irlingo RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
include(cmake/remotes.cmake)
install(FILES README.md LICENSE DESTINATION ${CMAKE_INSTALL_DATADIR}/doc/irlingo)
install(DIRECTORY doc/ DESTINATION ${CMAKE_INSTALL_DATADIR}/doc/irlingo/doc
  FILES_MATCHING PATTERN "*.md" PATTERN "*.json")

include(CTest)
if(BUILD_TESTING)
  add_subdirectory(tests)
endif()

include(cmake/packaging.cmake)
