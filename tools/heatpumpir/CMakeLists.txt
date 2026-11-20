# Builds heatpumpir_capture, which records the codes of arduino-heatpumpir
# (https://github.com/ToniA/arduino-heatpumpir, GPL-2.0) for ir2piir.py.
# The library is not part of PIIR; point HEATPUMPIR_DIR at a checkout:
#   git clone https://github.com/ToniA/arduino-heatpumpir.git
#   cmake -S tools/heatpumpir -B build/heatpumpir -DHEATPUMPIR_DIR=$PWD/arduino-heatpumpir
#   cmake --build build/heatpumpir
cmake_minimum_required(VERSION 3.13)
project(heatpumpir_capture LANGUAGES CXX)

set(HEATPUMPIR_DIR "" CACHE PATH "arduino-heatpumpir checkout")
if(NOT EXISTS "${HEATPUMPIR_DIR}/HeatpumpIRFactory.cpp")
  message(FATAL_ERROR "Set HEATPUMPIR_DIR to an arduino-heatpumpir checkout")
endif()

file(GLOB sources ${HEATPUMPIR_DIR}/*HeatpumpIR*.cpp ${HEATPUMPIR_DIR}/*IR.cpp)
list(FILTER sources EXCLUDE REGEX "IRSender[A-Z]")   # hardware senders
list(APPEND sources ${HEATPUMPIR_DIR}/IRSender.cpp)
list(REMOVE_DUPLICATES sources)

add_executable(heatpumpir_capture capture.cpp ${sources})
set_target_properties(heatpumpir_capture PROPERTIES CXX_STANDARD 14)
target_compile_definitions(heatpumpir_capture PRIVATE ARDUINO=100)
# The Arduino.h stub here, then the library
target_include_directories(heatpumpir_capture PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${HEATPUMPIR_DIR})
target_compile_options(heatpumpir_capture PRIVATE -w)
