# Runs piir and checks its exit code and output.
#   PROG        path to piir
#   EMULATOR    optional emulator command (e.g. qemu-aarch64), parts separated by ~~
#   ARGS        arguments separated by ~~
#   WORKDIR     working directory
#   EXIT        expected exit code: 0, nonzero or any
#   MATCH       regular expressions the output must match, separated by ~~
#   NOMATCH     regular expressions the output must not match, separated by ~~

foreach(var EMULATOR ARGS MATCH NOMATCH)
  string(REPLACE "~~" ";" ${var} "${${var}}")
endforeach()

execute_process(
  COMMAND ${EMULATOR} ${PROG} ${ARGS}
  WORKING_DIRECTORY "${WORKDIR}"
  RESULT_VARIABLE result
  OUTPUT_VARIABLE output
  ERROR_VARIABLE output)

string(REPLACE ";" " " cmdline "${PROG} ${ARGS}")
message("$ ${cmdline}\n${output}exit: ${result}")

if(NOT result MATCHES "^[0-9]+$")
  message(FATAL_ERROR "piir did not exit normally: ${result}")
endif()
if(EXIT STREQUAL "0" AND NOT result EQUAL 0)
  message(FATAL_ERROR "expected exit code 0")
elseif(EXIT STREQUAL "nonzero" AND result EQUAL 0)
  message(FATAL_ERROR "expected a non-zero exit code")
endif()

# Sanitizer reports fail a test even when it expects a non-zero exit code
if(output MATCHES "ERROR: AddressSanitizer|ERROR: LeakSanitizer|runtime error:")
  message(FATAL_ERROR "sanitizer error")
endif()

foreach(regex IN LISTS MATCH)
  if(NOT output MATCHES "${regex}")
    message(FATAL_ERROR "output does not match: ${regex}")
  endif()
endforeach()
foreach(regex IN LISTS NOMATCH)
  if(output MATCHES "${regex}")
    message(FATAL_ERROR "output unexpectedly matches: ${regex}")
  endif()
endforeach()
