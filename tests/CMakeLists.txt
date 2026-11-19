set(FIXTURE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/fixtures)

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
piir_unit_test(test_remote_lookup)

# Every key of every remote, and the test vectors of remotes that have them
piir_test_executable(test_remotes)
add_test(NAME remotes.all
  COMMAND test_remotes --known-failures ${CMAKE_CURRENT_SOURCE_DIR}/known_failures.txt
          ${PROJECT_SOURCE_DIR}/conf/remotes)

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
set(HVAC ${PROJECT_SOURCE_DIR}/conf/remotes/panasonic/hvac_panasonic.json)

# piir_cli_test(name [EXIT 0|nonzero|any] [WORKDIR dir] [PROG program]
#               [ARGS ...] [MATCH regex...] [NOMATCH regex...] [FIXTURES ...])
function(piir_cli_test name)
  cmake_parse_arguments(T "" "EXIT;WORKDIR;PROG" "ARGS;MATCH;NOMATCH;FIXTURES" ${ARGN})
  if(NOT T_WORKDIR)
    set(T_WORKDIR ${CMAKE_CURRENT_BINARY_DIR})
  endif()
  set(emulator ${PIIR_EMULATOR})
  if(T_PROG)
    set(emulator "")   # scripts run on the build machine
  else()
    set(T_PROG $<TARGET_FILE:piir>)
  endif()
  string(REPLACE ";" "~~" args "${T_ARGS}")
  string(REPLACE ";" "~~" match "${T_MATCH}")
  string(REPLACE ";" "~~" nomatch "${T_NOMATCH}")
  add_test(NAME cli.${name}
    COMMAND ${CMAKE_COMMAND}
      -DPROG=${T_PROG}
      -DEMULATOR=${emulator}
      -DARGS=${args}
      -DWORKDIR=${T_WORKDIR}
      -DEXIT=${T_EXIT}
      -DMATCH=${match}
      -DNOMATCH=${nomatch}
      -P ${CMAKE_CURRENT_SOURCE_DIR}/run_cli.cmake)
  if(T_FIXTURES)
    set_tests_properties(cli.${name} PROPERTIES FIXTURES_REQUIRED "${T_FIXTURES}")
  endif()
endfunction()

piir_cli_test(no_arguments EXIT nonzero
  MATCH "Missing configfile setting" "usage:")
piir_cli_test(help_lists_local_remotes EXIT nonzero
  WORKDIR ${FIXTURE_DIR}
  ARGS --help
  MATCH "Available remotes in .*/fixtures/conf/remotes/|remotes available in .*/fixtures/conf/remotes/")
piir_cli_test(help_counts_many_remotes EXIT nonzero
  WORKDIR ${PROJECT_SOURCE_DIR}
  ARGS --help
  MATCH "[0-9]+ remotes available in .*/conf/remotes/ .*--list")
piir_cli_test(list_remotes EXIT 0
  WORKDIR ${PROJECT_SOURCE_DIR}
  ARGS --list
  MATCH "\npanasonic/hvac_panasonic\n" "\nsony/RM-ED011\n")
piir_cli_test(list_fixture_remotes EXIT 0
  WORKDIR ${FIXTURE_DIR}
  ARGS -l
  MATCH "brand1/dup\nbrand1/only\nbrand2/dup\n")
piir_cli_test(ambiguous_remote EXIT nonzero
  WORKDIR ${FIXTURE_DIR}
  ARGS --remote dup --help
  MATCH "Remote 'dup' is ambiguous, use one of:" "    brand1/dup" "    brand2/dup")
piir_cli_test(remote_by_full_name EXIT nonzero
  WORKDIR ${FIXTURE_DIR}
  ARGS --remote brand2/dup --help
  MATCH "Features in config file .*/fixtures/conf/remotes/brand2/dup.json")
piir_cli_test(unknown_plugin EXIT nonzero
  WORKDIR ${FIXTURE_DIR}
  ARGS --remote only --feature power=ON
  MATCH "Unknown plugin 'no_such_plugin'")
piir_cli_test(converted_remote_help EXIT nonzero
  WORKDIR ${PROJECT_SOURCE_DIR}
  ARGS --remote sony/RM-ED011 --help
  MATCH "key:" "KEY_POWER")
piir_cli_test(remote_help EXIT nonzero
  ARGS --remote ${HVAC} --help
  MATCH "Features in config file" "temperature:" "leftright:" "VERYRIGHT")
piir_cli_test(remote_by_name EXIT nonzero
  WORKDIR ${PROJECT_SOURCE_DIR}
  ARGS -r hvac_panasonic -h
  MATCH "Features in config file .*/conf/remotes/panasonic/hvac_panasonic.json")
piir_cli_test(unknown_remote EXIT nonzero
  ARGS --remote no_such_remote
  MATCH "Cannot find remote 'no_such_remote'")
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

  # A remote converted from LIRC
  file(READ ${PROJECT_SOURCE_DIR}/conf/remotes/sony/RM-ED011.json sony_json)
  string(JSON sony_json SET "${sony_json}" transmitter device "\"/nonexistent/piir-test/lirc0\"")
  file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/remotes/RM-ED011.json "${sony_json}")
  string(JSON code GET "${sony_json}" features key KEY_POWER)
  piir_cli_test(transmit_converted_remote EXIT nonzero
    ARGS --remote ${CMAKE_CURRENT_BINARY_DIR}/remotes/RM-ED011.json
         --feature key=KEY_POWER --verbose DEBUG
    MATCH "Transmitting: ${code}" "Cannot open /nonexistent/piir-test/lirc0")
endif()

# piir-remotes: install and remove remotes from the remotes archive
set(ARCHIVE ${PROJECT_BINARY_DIR}/piir-remotes-${PROJECT_VERSION}.tar.gz)
set(INSTALLED ${CMAKE_CURRENT_BINARY_DIR}/installed-remotes)
add_test(NAME remotes_archive.build
  COMMAND ${CMAKE_COMMAND} --build ${PROJECT_BINARY_DIR} --target remotes_archive)
add_test(NAME remotes_archive.clean COMMAND ${CMAKE_COMMAND} -E rm -rf ${INSTALLED})
set_tests_properties(remotes_archive.build remotes_archive.clean
  PROPERTIES FIXTURES_SETUP remotes_archive)
set(TOOL ${PROJECT_BINARY_DIR}/piir-remotes)
set(TOOL_OPTIONS --archive ${ARCHIVE} --dir ${INSTALLED})
piir_cli_test(piir_remotes_available EXIT 0 PROG ${TOOL} FIXTURES remotes_archive
  ARGS ${TOOL_OPTIONS} available RM-ED
  MATCH "sony/RM-ED011" NOMATCH "panasonic/hvac_panasonic")
piir_cli_test(piir_remotes_add EXIT 0 PROG ${TOOL} FIXTURES remotes_archive
  ARGS ${TOOL_OPTIONS} add panasonic/hvac_panasonic sony "lg/AKB*"
  MATCH "Installed [0-9]+ remotes in .*/installed-remotes")
piir_cli_test(piir_remotes_unknown EXIT nonzero PROG ${TOOL} FIXTURES remotes_archive
  ARGS ${TOOL_OPTIONS} add no_such_manufacturer
  MATCH "no remote matches 'no_such_manufacturer'")
piir_cli_test(piir_remotes_installed EXIT 0 PROG ${TOOL} FIXTURES remotes_archive
  ARGS ${TOOL_OPTIONS} installed
  MATCH "\npanasonic/hvac_panasonic\n" "\nsony/RM-ED011\n" "\nlg/AKB[^\n]*\n"
  NOMATCH "\nsamsung/")
piir_cli_test(piir_remotes_remove EXIT 0 PROG ${TOOL} FIXTURES remotes_archive
  ARGS ${TOOL_OPTIONS} remove sony
  MATCH "Removed [0-9]+ remotes")
piir_cli_test(piir_remotes_installed_after_remove EXIT 0 PROG ${TOOL} FIXTURES remotes_archive
  ARGS ${TOOL_OPTIONS} installed
  MATCH "panasonic/hvac_panasonic" NOMATCH "sony/")
set_tests_properties(cli.piir_remotes_installed PROPERTIES DEPENDS cli.piir_remotes_add)
set_tests_properties(cli.piir_remotes_remove PROPERTIES DEPENDS cli.piir_remotes_installed)
set_tests_properties(cli.piir_remotes_installed_after_remove PROPERTIES DEPENDS cli.piir_remotes_remove)

# The LIRC converter, when Python is available
find_package(Python3 COMPONENTS Interpreter)
if(Python3_FOUND)
  set(CONVERTER ${PROJECT_SOURCE_DIR}/tools/lirc2piir.py)
  file(GLOB lirc_fixtures ${CMAKE_CURRENT_SOURCE_DIR}/lirc/*.simsend)
  foreach(expected IN LISTS lirc_fixtures)
    get_filename_component(name ${expected} NAME_WE)
    # The .simsend files are what LIRC 0.10.2's irsimsend -c 2 writes
    add_test(NAME converter.simsend.${name}
      COMMAND ${CMAKE_COMMAND} -DPYTHON=${Python3_EXECUTABLE} -DCONVERTER=${CONVERTER}
        -DCONFIG=${CMAKE_CURRENT_SOURCE_DIR}/lirc/${name}.lircd.conf -DEXPECTED=${expected}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/run_simsend.cmake)
  endforeach()
  set(CONVERTED ${CMAKE_CURRENT_BINARY_DIR}/converted)
  add_test(NAME converter.convert
    COMMAND ${CMAKE_COMMAND} -E env ${Python3_EXECUTABLE} ${CONVERTER}
      convert -o ${CONVERTED} ${CMAKE_CURRENT_SOURCE_DIR}/lirc)
  set_tests_properties(converter.convert PROPERTIES
    FIXTURES_SETUP converted
    PASS_REGULAR_EXPRESSION "5 remotes converted, 2 skipped")
  add_test(NAME converter.remotes COMMAND test_remotes ${CONVERTED})
  set_tests_properties(converter.remotes PROPERTIES FIXTURES_REQUIRED converted)
  piir_cli_test(converter_rc5_help EXIT nonzero FIXTURES converted
    ARGS --remote ${CONVERTED}/rc5.json --help
    MATCH "key:" "KEY_VOLUMEUP")
endif()
