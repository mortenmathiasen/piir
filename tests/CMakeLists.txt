set(FIXTURE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/fixtures)
file(GLOB PIIR_REMOTES ${PROJECT_SOURCE_DIR}/conf/remotes/*.json)

function(piir_test_executable name)
  add_executable(${name} ${name}.c)
  target_link_libraries(${name} PRIVATE piir_core)
  target_compile_definitions(${name} PRIVATE
    FIXTURE_DIR="${FIXTURE_DIR}"
    PROJECT_DIR="${PROJECT_SOURCE_DIR}")
  if(PIIR_STATIC)
    target_link_options(${name} PRIVATE -static)
  endif()
endfunction()

function(piir_unit_test name)
  piir_test_executable(${name})
  add_test(NAME unit.${name} COMMAND ${name})
endfunction()

# Backend independent units
piir_unit_test(test_log)
piir_unit_test(test_config)
piir_unit_test(test_hvac_panasonic)

# Test vectors in every remote configuration
piir_test_executable(test_remotes)
foreach(remote IN LISTS PIIR_REMOTES)
  get_filename_component(remote_name ${remote} NAME_WE)
  add_test(NAME remote.${remote_name}
    COMMAND test_remotes --known-failures ${CMAKE_CURRENT_SOURCE_DIR}/known_failures.txt ${remote})
endforeach()

# Transmitter backend units
if(PIIR_BACKEND STREQUAL "lirc")
  piir_unit_test(test_lirc)
elseif(PIIR_BACKEND STREQUAL "gpiod")
  piir_unit_test(test_transmitter)
elseif(PIIR_BACKEND STREQUAL "pigpio")
  piir_unit_test(test_irslinger)
endif()

# Command line tests of the piir executable
string(REPLACE ";" "~~" PIIR_EMULATOR "${CMAKE_CROSSCOMPILING_EMULATOR}")
set(HVAC ${PROJECT_SOURCE_DIR}/conf/remotes/hvac_panasonic.json)

function(piir_cli_test name)
  cmake_parse_arguments(T "" "EXIT;WORKDIR" "ARGS;MATCH;NOMATCH" ${ARGN})
  if(NOT T_WORKDIR)
    set(T_WORKDIR ${CMAKE_CURRENT_BINARY_DIR})
  endif()
  string(REPLACE ";" "~~" args "${T_ARGS}")
  string(REPLACE ";" "~~" match "${T_MATCH}")
  string(REPLACE ";" "~~" nomatch "${T_NOMATCH}")
  add_test(NAME cli.${name}
    COMMAND ${CMAKE_COMMAND}
      -DPROG=$<TARGET_FILE:piir>
      -DEMULATOR=${PIIR_EMULATOR}
      -DARGS=${args}
      -DWORKDIR=${T_WORKDIR}
      -DEXIT=${T_EXIT}
      -DMATCH=${match}
      -DNOMATCH=${nomatch}
      -P ${CMAKE_CURRENT_SOURCE_DIR}/run_cli.cmake)
endfunction()

piir_cli_test(no_arguments EXIT nonzero
  MATCH "Missing configfile setting" "usage:")
piir_cli_test(help_lists_local_remotes EXIT nonzero
  WORKDIR ${PROJECT_SOURCE_DIR}
  ARGS --help
  MATCH "Available configuration files in .*/conf/remotes/:" "    hvac_panasonic")
piir_cli_test(remote_help EXIT nonzero
  ARGS --remote ${HVAC} --help
  MATCH "Features in config file" "temperature:" "leftright:" "VERYRIGHT")
piir_cli_test(remote_by_name EXIT nonzero
  WORKDIR ${PROJECT_SOURCE_DIR}
  ARGS -r hvac_panasonic -h
  MATCH "Features in config file .*/conf/remotes/hvac_panasonic.json")
piir_cli_test(unknown_remote EXIT nonzero
  ARGS --remote no_such_remote
  MATCH "Cannot load remote 'no_such_remote'")
piir_cli_test(feature_before_remote EXIT nonzero
  ARGS --feature temperature=21
  MATCH "Missing configfile before feature parameter")
piir_cli_test(unknown_feature_value EXIT nonzero
  ARGS --remote ${HVAC} --feature temperature=99
  MATCH "Feature 'temperature'='99' does not match configuration file")
piir_cli_test(malformed_feature EXIT nonzero
  ARGS --remote ${HVAC} --feature temperature
  MATCH "Feature must be given as NAME=VALUE")
piir_cli_test(unknown_option EXIT nonzero
  ARGS --bogus
  MATCH "Unknown argument")

# Full run up to the transmitter, using a copy of the remote that points the
# lirc backend at a device that does not exist, so nothing is sent even on a Pi.
if(PIIR_BACKEND STREQUAL "lirc" AND NOT CMAKE_VERSION VERSION_LESS 3.19)
  file(READ ${HVAC} hvac_json)
  string(JSON hvac_json SET "${hvac_json}" transmitter device "\"/nonexistent/piir-test/lirc0\"")
  file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/remotes/hvac_panasonic.json "${hvac_json}")
  string(JSON outcome GET "${hvac_json}" tests 0 outcome)

  piir_cli_test(transmit_hvac_panasonic EXIT nonzero
    ARGS --remote ${CMAKE_CURRENT_BINARY_DIR}/remotes/hvac_panasonic.json
         --feature temperature=21 --feature mode=AUTO --feature option=AUTO
         --feature fan=AUTO --feature updown=AUTO --feature leftright=AUTO
         --verbose DEBUG
    MATCH "Transmitting: ${outcome}" "Cannot open /nonexistent/piir-test/lirc0")

  # Options override the fan speed even when the fan is given later, and
  # repeating features more often than there are features is fine
  string(JSON outcome GET "${hvac_json}" tests 17 outcome)
  piir_cli_test(transmit_option_overrides_fan EXIT nonzero
    ARGS --remote ${CMAKE_CURRENT_BINARY_DIR}/remotes/hvac_panasonic.json
         --feature temperature=30 --feature temperature=21 --feature mode=COOL
         --feature mode=AUTO --feature option=QUIET --feature fan=FAST
         --feature fan=AUTO --feature updown=AUTO --feature leftright=AUTO
         --verbose DEBUG
    MATCH "Transmitting: ${outcome}")
endif()
