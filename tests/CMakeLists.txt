set(FIXTURE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/fixtures)

function(irlingo_test_executable name)
  add_executable(${name} ${name}.c)
  target_link_libraries(${name} PRIVATE irlingo_linux)
  target_compile_definitions(${name} PRIVATE
    FIXTURE_DIR="${FIXTURE_DIR}"
    PROJECT_DIR="${PROJECT_SOURCE_DIR}")
  if(IRLINGO_STATIC)
    target_link_options(${name} PRIVATE -static)
  endif()
endfunction()

function(irlingo_unit_test name)
  irlingo_test_executable(${name})
  add_test(NAME unit.${name} COMMAND ${name})
endfunction()

# Backend independent units
irlingo_unit_test(test_log)
irlingo_unit_test(test_config)
irlingo_unit_test(test_hvac_panasonic)
irlingo_unit_test(test_checksum)
irlingo_unit_test(test_pulses)
irlingo_unit_test(test_remote_lookup)
irlingo_unit_test(test_state)

# Every key of every remote, and the test vectors of remotes that have them
irlingo_test_executable(test_remotes)
add_test(NAME remotes.all
  COMMAND test_remotes --known-failures ${CMAKE_CURRENT_SOURCE_DIR}/known_failures.txt
          ${PROJECT_SOURCE_DIR}/conf/remotes)

# Transmitter backend units
if(IRLINGO_BACKEND STREQUAL "lirc")
  irlingo_unit_test(test_lirc)
  irlingo_unit_test(test_lirc_device)
  target_link_options(test_lirc_device PRIVATE
    -Wl,--wrap=ioctl -Wl,--wrap=__ioctl_time64 -Wl,--wrap=write)
elseif(IRLINGO_BACKEND STREQUAL "gpiod")
  irlingo_unit_test(test_transmitter)
  irlingo_unit_test(test_transmitter_device)
  set(gpiod_wrapped opendir gpiod_is_gpiochip_device gpiod_chip_open gpiod_chip_close
    gpiod_chip_get_line_offset_from_name gpiod_line_settings_new gpiod_line_settings_free
    gpiod_line_settings_set_direction gpiod_line_settings_set_output_value
    gpiod_line_config_new gpiod_line_config_free gpiod_line_config_add_line_settings
    gpiod_request_config_new gpiod_request_config_free gpiod_request_config_set_consumer
    gpiod_chip_request_lines gpiod_line_request_release gpiod_line_request_set_value)
  list(TRANSFORM gpiod_wrapped PREPEND "-Wl,--wrap=")
  target_link_options(test_transmitter_device PRIVATE ${gpiod_wrapped})
elseif(IRLINGO_BACKEND STREQUAL "pigpio")
  irlingo_unit_test(test_irslinger)
  irlingo_unit_test(test_irslinger_device)
  set(pigpio_wrapped gpioCfgDMAchannels gpioInitialise gpioSetMode gpioWaveClear
    gpioWaveAddGeneric gpioWaveCreate gpioWaveTxSend gpioWaveTxBusy gpioWaveDelete
    gpioTerminate time_sleep)
  list(TRANSFORM pigpio_wrapped PREPEND "-Wl,--wrap=")
  target_link_options(test_irslinger_device PRIVATE ${pigpio_wrapped})
endif()

# Command line tests of the irlingo executable
string(REPLACE ";" "~~" IRLINGO_EMULATOR "${CMAKE_CROSSCOMPILING_EMULATOR}")
set(HVAC ${PROJECT_SOURCE_DIR}/conf/remotes/panasonic/hvac_panasonic.json)

# irlingo_cli_test(name [EXIT 0|nonzero|any] [WORKDIR dir] [PROG program]
#               [TARGET target] [STATE dir] [ARGS ...] [MATCH regex...]
#               [NOMATCH regex...] [FIXTURES ...])
# PROG is a script run on the build machine, TARGET an executable built here
# (irlingo by default), run with the cross compiling emulator if there is one.
# Each test keeps toggle states in its own directory (STATE) instead of the
# user's.
function(irlingo_cli_test name)
  cmake_parse_arguments(T "" "EXIT;WORKDIR;PROG;TARGET;STATE" "ARGS;MATCH;NOMATCH;FIXTURES" ${ARGN})
  if(NOT T_STATE)
    set(T_STATE ${CMAKE_CURRENT_BINARY_DIR}/state/${name})
  endif()
  if(NOT T_WORKDIR)
    set(T_WORKDIR ${CMAKE_CURRENT_BINARY_DIR})
  endif()
  set(emulator ${IRLINGO_EMULATOR})
  if(T_PROG)
    set(emulator "")   # scripts run on the build machine
  elseif(T_TARGET)
    set(T_PROG $<TARGET_FILE:${T_TARGET}>)
  else()
    set(T_PROG $<TARGET_FILE:irlingo>)
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
  set_tests_properties(cli.${name} PROPERTIES ENVIRONMENT IRLINGO_STATE_DIR=${T_STATE})
  if(T_FIXTURES)
    set_tests_properties(cli.${name} PROPERTIES FIXTURES_REQUIRED "${T_FIXTURES}")
  endif()
endfunction()

irlingo_cli_test(no_arguments EXIT nonzero
  MATCH "Missing configfile setting" "usage:")
irlingo_cli_test(help_lists_local_remotes EXIT nonzero
  WORKDIR ${FIXTURE_DIR}
  ARGS --help
  MATCH "Available remotes in .*/fixtures/conf/remotes/|remotes available in .*/fixtures/conf/remotes/")
irlingo_cli_test(help_counts_many_remotes EXIT nonzero
  WORKDIR ${PROJECT_SOURCE_DIR}
  ARGS --help
  MATCH "[0-9]+ remotes available in .*/conf/remotes/ .*--list")
irlingo_cli_test(list_remotes EXIT 0
  WORKDIR ${PROJECT_SOURCE_DIR}
  ARGS --list
  MATCH "\npanasonic/hvac_panasonic\n" "\nsony/RM-ED011\n")
irlingo_cli_test(list_fixture_remotes EXIT 0
  WORKDIR ${FIXTURE_DIR}
  ARGS -l
  MATCH "brand1/dup\nbrand1/only\nbrand2/dup\n")
irlingo_cli_test(ambiguous_remote EXIT nonzero
  WORKDIR ${FIXTURE_DIR}
  ARGS --remote dup --help
  MATCH "Remote 'dup' is ambiguous, use one of:" "    brand1/dup" "    brand2/dup")
irlingo_cli_test(remote_by_full_name EXIT nonzero
  WORKDIR ${FIXTURE_DIR}
  ARGS --remote brand2/dup --help
  MATCH "Features in config file .*/fixtures/conf/remotes/brand2/dup.json")
irlingo_cli_test(unknown_plugin EXIT nonzero
  WORKDIR ${FIXTURE_DIR}
  ARGS --remote only --feature power=ON
  MATCH "Unknown plugin 'no_such_plugin'")
irlingo_cli_test(invalid_checksum EXIT nonzero
  ARGS --remote ${FIXTURE_DIR}/bad_checksum.json --feature power=ON
  MATCH "Invalid checksum in .*/bad_checksum.json")
irlingo_cli_test(converted_remote_help EXIT nonzero
  WORKDIR ${PROJECT_SOURCE_DIR}
  ARGS --remote sony/RM-ED011 --help
  MATCH "key:" "KEY_POWER")
irlingo_cli_test(remote_help EXIT nonzero
  ARGS --remote ${HVAC} --help
  MATCH "Features in config file" "temperature:" "leftright:" "VERYRIGHT")
irlingo_cli_test(remote_by_name EXIT nonzero
  WORKDIR ${PROJECT_SOURCE_DIR}
  ARGS -r hvac_panasonic -h
  MATCH "Features in config file .*/conf/remotes/panasonic/hvac_panasonic.json")
irlingo_cli_test(second_remote_ignored EXIT nonzero
  ARGS --remote ${HVAC} --remote ${FIXTURE_DIR}/simple.json --help
  MATCH "Features in config file .*/hvac_panasonic.json" "temperature:")
irlingo_cli_test(invalid_remote_file EXIT nonzero
  ARGS --remote ${FIXTURE_DIR}/invalid.json
  MATCH "Cannot load remote '.*/invalid.json'")
irlingo_cli_test(unknown_remote EXIT nonzero
  ARGS --remote no_such_remote
  MATCH "Cannot find remote 'no_such_remote'")
irlingo_cli_test(feature_before_remote EXIT nonzero
  ARGS --feature temperature=21
  MATCH "Missing configfile before feature parameter")
irlingo_cli_test(unknown_feature_value EXIT nonzero
  ARGS --remote ${HVAC} --feature temperature=99
  MATCH "Feature 'temperature'='99' does not match configuration file")
irlingo_cli_test(malformed_feature EXIT nonzero
  ARGS --remote ${HVAC} --feature temperature
  MATCH "Feature must be given as NAME=VALUE")
irlingo_cli_test(unknown_option EXIT nonzero
  ARGS --bogus
  MATCH "Unknown argument")

# Full run up to the transmitter, using a copy of the remote that points the
# lirc backend at a device that does not exist, so nothing is sent even on a Pi.
if(IRLINGO_BACKEND STREQUAL "lirc" AND NOT CMAKE_VERSION VERSION_LESS 3.19)
  file(READ ${HVAC} hvac_json)
  string(JSON hvac_json SET "${hvac_json}" transmitter device "\"/nonexistent/irlingo-test/lirc0\"")
  file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/remotes/hvac_panasonic.json "${hvac_json}")
  string(JSON outcome GET "${hvac_json}" tests 0 outcome)

  irlingo_cli_test(transmit_hvac_panasonic EXIT nonzero
    ARGS --remote ${CMAKE_CURRENT_BINARY_DIR}/remotes/hvac_panasonic.json
         --feature temperature=21 --feature mode=AUTO --feature option=AUTO
         --feature fan=AUTO --feature updown=AUTO --feature leftright=AUTO
         --verbose DEBUG
    MATCH "Transmitting: ${outcome}" "Cannot open /nonexistent/irlingo-test/lirc0")

  # Options override the fan speed even when the fan is given later, and
  # repeating features more often than there are features is fine
  string(JSON outcome GET "${hvac_json}" tests 17 outcome)
  irlingo_cli_test(transmit_option_overrides_fan EXIT nonzero
    ARGS --remote ${CMAKE_CURRENT_BINARY_DIR}/remotes/hvac_panasonic.json
         --feature temperature=30 --feature temperature=21 --feature mode=COOL
         --feature mode=AUTO --feature option=QUIET --feature fan=FAST
         --feature fan=AUTO --feature updown=AUTO --feature leftright=AUTO
         --verbose DEBUG
    MATCH "Transmitting: ${outcome}")

  # A remote file from PIIR 1.x without "plugin" or "checksums" settings gets the
  # hvac_panasonic checksum from its file name
  string(JSON legacy_json REMOVE "${hvac_json}" checksums)
  file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/remotes/legacy/hvac_panasonic.json "${legacy_json}")
  # Test vector No 2 (mode OFF): its checksum differs from the template's
  string(JSON outcome GET "${hvac_json}" tests 1 outcome)
  irlingo_cli_test(transmit_legacy_hvac_panasonic EXIT nonzero
    ARGS --remote ${CMAKE_CURRENT_BINARY_DIR}/remotes/legacy/hvac_panasonic.json
         --feature temperature=21 --feature mode=OFF --feature option=AUTO
         --feature fan=AUTO --feature updown=AUTO --feature leftright=AUTO
         --verbose DEBUG
    MATCH "Transmitting: ${outcome}")

  # A remote converted from LIRC
  file(READ ${PROJECT_SOURCE_DIR}/conf/remotes/sony/RM-ED011.json sony_json)
  string(JSON sony_json SET "${sony_json}" transmitter device "\"/nonexistent/irlingo-test/lirc0\"")
  file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/remotes/RM-ED011.json "${sony_json}")
  string(JSON code GET "${sony_json}" features key KEY_POWER)
  irlingo_cli_test(transmit_converted_remote EXIT nonzero
    ARGS --remote ${CMAKE_CURRENT_BINARY_DIR}/remotes/RM-ED011.json
         --feature key=KEY_POWER --verbose DEBUG
    MATCH "Transmitting: ${code}" "Cannot open /nonexistent/irlingo-test/lirc0")
endif()

# Toggle bits: the state file decides which code is sent, and a failed
# transmission (the device does not exist) leaves the state unchanged
if(IRLINGO_BACKEND STREQUAL "lirc")
  get_filename_component(toggle_remote ${FIXTURE_DIR}/toggle.json REALPATH)
  set(toggled_state ${CMAKE_CURRENT_BINARY_DIR}/state/toggled)
  file(WRITE ${toggled_state}/toggle-state "1 /other/remote.json\n1 ${toggle_remote}\n")
  irlingo_cli_test(toggle_first_press EXIT nonzero
    ARGS --remote ${toggle_remote} --feature key=KEY_POWER --verbose DEBUG
    MATCH "Toggle state 0" "Transmitting: AAAA")
  irlingo_cli_test(toggle_second_press EXIT nonzero STATE ${toggled_state}
    ARGS --remote ${toggle_remote} --feature key=KEY_POWER --verbose DEBUG
    MATCH "Toggle state 1 from .*/state/toggled/toggle-state" "Transmitting: BAAA")
  irlingo_cli_test(toggle_state_kept_after_failure EXIT nonzero STATE ${toggled_state}
    ARGS --remote ${toggle_remote} --feature key=KEY_POWER --verbose DEBUG
    MATCH "Toggle state 1" "Transmitting: BAAA")
  set_tests_properties(cli.toggle_state_kept_after_failure PROPERTIES
    DEPENDS cli.toggle_second_press)

  # Complete runs with a simulated kernel driver: every successful press
  # flips the toggle state
  add_executable(irlingo_fake_lirc ${PROJECT_SOURCE_DIR}/src/irlingo.c fake_lirc_driver.c)
  target_link_libraries(irlingo_fake_lirc PRIVATE irlingo_linux)
  target_link_options(irlingo_fake_lirc PRIVATE -Wl,--wrap=ioctl -Wl,--wrap=__ioctl_time64)
  if(IRLINGO_STATIC)
    target_link_options(irlingo_fake_lirc PRIVATE -static)
  endif()
  file(READ ${FIXTURE_DIR}/toggle.json toggle_json)
  string(JSON toggle_json SET "${toggle_json}" transmitter device
    "\"${CMAKE_CURRENT_BINARY_DIR}/fake-lirc0\"")
  file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/remotes/fake/toggle.json "${toggle_json}")
  set(fake_remote ${CMAKE_CURRENT_BINARY_DIR}/remotes/fake/toggle.json)
  set(fake_state ${CMAKE_CURRENT_BINARY_DIR}/state/fake)
  add_test(NAME toggle.clean COMMAND ${CMAKE_COMMAND} -E rm -rf ${fake_state})
  add_test(NAME toggle.device COMMAND ${CMAKE_COMMAND} -E touch ${CMAKE_CURRENT_BINARY_DIR}/fake-lirc0)
  set_tests_properties(toggle.clean toggle.device PROPERTIES FIXTURES_SETUP toggle_presses)
  set(previous "")
  foreach(press 1 2 3)
    if(press EQUAL 2)
      set(code "BAAA")
    else()
      set(code "AAAA")
    endif()
    irlingo_cli_test(toggle_press_${press} EXIT 0 TARGET irlingo_fake_lirc STATE ${fake_state}
      FIXTURES toggle_presses
      ARGS --remote ${fake_remote} --feature key=KEY_POWER --verbose DEBUG
      MATCH "Transmitting: ${code}")
    if(previous)
      set_tests_properties(cli.toggle_press_${press} PROPERTIES DEPENDS ${previous})
    endif()
    set(previous cli.toggle_press_${press})
  endforeach()
  # The state belongs to the remote, not the key
  irlingo_cli_test(toggle_press_other_key EXIT 0 TARGET irlingo_fake_lirc STATE ${fake_state}
    FIXTURES toggle_presses
    ARGS --remote ${fake_remote} --feature key=KEY_MUTE --verbose DEBUG
    MATCH "Toggle state 1" "Transmitting: ABBB")
  set_tests_properties(cli.toggle_press_other_key PROPERTIES DEPENDS ${previous})

  irlingo_cli_test(no_toggle_state_without_toggle EXIT nonzero
    ARGS --remote ${HVAC} --feature temperature=21 --verbose DEBUG
    NOMATCH "Toggle state")
endif()

# irlingo-remotes: install and remove remotes from the remotes archive
set(ARCHIVE ${PROJECT_BINARY_DIR}/irlingo-remotes-${PROJECT_VERSION}.tar.gz)
set(INSTALLED ${CMAKE_CURRENT_BINARY_DIR}/installed-remotes)
add_test(NAME remotes_archive.build
  COMMAND ${CMAKE_COMMAND} --build ${PROJECT_BINARY_DIR} --target remotes_archive)
add_test(NAME remotes_archive.clean COMMAND ${CMAKE_COMMAND} -E rm -rf ${INSTALLED})
set_tests_properties(remotes_archive.build remotes_archive.clean
  PROPERTIES FIXTURES_SETUP remotes_archive)
set(TOOL ${PROJECT_BINARY_DIR}/irlingo-remotes)
set(TOOL_OPTIONS --archive ${ARCHIVE} --dir ${INSTALLED})
irlingo_cli_test(irlingo_remotes_available EXIT 0 PROG ${TOOL} FIXTURES remotes_archive
  ARGS ${TOOL_OPTIONS} available RM-ED
  MATCH "sony/RM-ED011" NOMATCH "panasonic/hvac_panasonic")
irlingo_cli_test(irlingo_remotes_add EXIT 0 PROG ${TOOL} FIXTURES remotes_archive
  ARGS ${TOOL_OPTIONS} add panasonic/hvac_panasonic sony "lg/AKB*"
  MATCH "Installed [0-9]+ remotes in .*/installed-remotes")
irlingo_cli_test(irlingo_remotes_unknown EXIT nonzero PROG ${TOOL} FIXTURES remotes_archive
  ARGS ${TOOL_OPTIONS} add no_such_manufacturer
  MATCH "no remote matches 'no_such_manufacturer'")
irlingo_cli_test(irlingo_remotes_installed EXIT 0 PROG ${TOOL} FIXTURES remotes_archive
  ARGS ${TOOL_OPTIONS} installed
  MATCH "\npanasonic/hvac_panasonic\n" "\nsony/RM-ED011\n" "\nlg/AKB[^\n]*\n"
  NOMATCH "\nsamsung/")
irlingo_cli_test(irlingo_remotes_remove EXIT 0 PROG ${TOOL} FIXTURES remotes_archive
  ARGS ${TOOL_OPTIONS} remove sony
  MATCH "Removed [0-9]+ remotes")
irlingo_cli_test(irlingo_remotes_installed_after_remove EXIT 0 PROG ${TOOL} FIXTURES remotes_archive
  ARGS ${TOOL_OPTIONS} installed
  MATCH "panasonic/hvac_panasonic" NOMATCH "sony/")
set_tests_properties(cli.irlingo_remotes_installed PROPERTIES DEPENDS cli.irlingo_remotes_add)
set_tests_properties(cli.irlingo_remotes_remove PROPERTIES DEPENDS cli.irlingo_remotes_installed)
set_tests_properties(cli.irlingo_remotes_installed_after_remove PROPERTIES DEPENDS cli.irlingo_remotes_remove)

# The LIRC converter, when Python is available
find_package(Python3 COMPONENTS Interpreter)
if(Python3_FOUND)
  set(CONVERTER ${PROJECT_SOURCE_DIR}/tools/lirc2irlingo.py)
  set(PYTHON ${Python3_EXECUTABLE})
  if(IRLINGO_COVERAGE)
    execute_process(COMMAND ${Python3_EXECUTABLE} -c "import coverage"
      RESULT_VARIABLE no_coverage OUTPUT_QUIET ERROR_QUIET)
    if(no_coverage EQUAL 0)
      set(PYTHON ${Python3_EXECUTABLE} -m coverage run --parallel-mode
        --data-file=${PROJECT_BINARY_DIR}/python.coverage
        --include=${PROJECT_SOURCE_DIR}/tools/*)
    endif()
  endif()
  string(REPLACE ";" "~~" PYTHON_ARGS "${PYTHON}")
  file(GLOB lirc_fixtures ${CMAKE_CURRENT_SOURCE_DIR}/lirc/*.simsend)
  foreach(expected IN LISTS lirc_fixtures)
    get_filename_component(name ${expected} NAME_WE)
    # The .simsend files are what LIRC 0.10.2's irsimsend -c 2 writes
    add_test(NAME converter.simsend.${name}
      COMMAND ${CMAKE_COMMAND} -DPYTHON=${PYTHON_ARGS} -DCONVERTER=${CONVERTER}
        -DCONFIG=${CMAKE_CURRENT_SOURCE_DIR}/lirc/${name}.lircd.conf -DEXPECTED=${expected}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/run_simsend.cmake)
  endforeach()
  set(CONVERTED ${CMAKE_CURRENT_BINARY_DIR}/converted)
  add_test(NAME converter.convert
    COMMAND ${PYTHON} ${CONVERTER} convert -o ${CONVERTED} ${CMAKE_CURRENT_SOURCE_DIR}/lirc)
  set_tests_properties(converter.convert PROPERTIES
    FIXTURES_SETUP converted
    PASS_REGULAR_EXPRESSION "5 remotes converted, 2 skipped")
  add_test(NAME converter.remotes COMMAND test_remotes ${CONVERTED})
  set_tests_properties(converter.remotes PROPERTIES FIXTURES_REQUIRED converted)
  add_test(NAME converter.unit
    COMMAND ${PYTHON} -m unittest -v test_lirc2irlingo
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
  set_tests_properties(converter.unit PROPERTIES
    ENVIRONMENT PYTHONPATH=${PROJECT_SOURCE_DIR}/tools)
  add_test(NAME ir2irlingo.unit
    COMMAND ${PYTHON} -m unittest -v test_ir2irlingo
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
  set_tests_properties(ir2irlingo.unit PROPERTIES
    ENVIRONMENT PYTHONPATH=${PROJECT_SOURCE_DIR}/tools)
  # ir2irlingo: derive the Panasonic remote again from the codes it sends
  irlingo_test_executable(encode_remote)
  add_test(NAME ir2irlingo.panasonic
    COMMAND ${PYTHON} ${CMAKE_CURRENT_SOURCE_DIR}/ir2irlingo_roundtrip.py
      --encoder $<TARGET_FILE:encode_remote>
      --test-remotes $<TARGET_FILE:test_remotes>
      --ir2irlingo ${PROJECT_SOURCE_DIR}/tools/ir2irlingo.py
      --out ${CMAKE_CURRENT_BINARY_DIR}/ir2irlingo/panasonic
      --expect-checksums [{"type":"sum","from":8,"to":25,"at":26}]
      ${HVAC})
  irlingo_cli_test(converter_rc5_help EXIT nonzero FIXTURES converted
    ARGS --remote ${CONVERTED}/rc5.json --help
    MATCH "key:" "KEY_VOLUMEUP")
endif()
