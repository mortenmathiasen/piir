# Compares "lirc2piir.py simsend -c 2 CONFIG" with the EXPECTED irsimsend output.
# PYTHON is the interpreter command, its parts separated by ~~
string(REPLACE "~~" ";" PYTHON "${PYTHON}")
execute_process(
  COMMAND ${PYTHON} ${CONVERTER} simsend -c 2 ${CONFIG}
  RESULT_VARIABLE result
  OUTPUT_VARIABLE output
  ERROR_VARIABLE error)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "simsend failed: ${error}")
endif()
file(READ ${EXPECTED} expected)
if(NOT output STREQUAL expected)
  file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/simsend.out "${output}")
  message(FATAL_ERROR "output differs from ${EXPECTED}")
endif()
