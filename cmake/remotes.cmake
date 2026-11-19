# Remote configurations in conf/remotes/<manufacturer>/<remote>.json
#
# PIIR_REMOTES selects the remotes to install:
#   all                 every remote (default)
#   native              the remotes made for PIIR (PIIR_NATIVE_REMOTES)
#   none                no remotes
#   sony                every remote of a manufacturer
#   sony/RM-ED009       one remote
#   samsung/AA59-*      remotes matching a pattern (* and ?)
# Combine them with semicolons, e.g. -DPIIR_REMOTES="native;sony;lg/AKB*".
#
# The target remotes_archive packs every remote into
# piir-remotes-<version>.tar.gz for the piir-remotes tool.

set(PIIR_REMOTES_DIR ${PROJECT_SOURCE_DIR}/conf/remotes)
set(PIIR_NATIVE_REMOTES panasonic/hvac_panasonic)
set(PIIR_REMOTES "all" CACHE STRING
  "Remotes to install: all, native, none, or manufacturers, remotes and patterns below conf/remotes")

file(GLOB_RECURSE _piir_all_remotes RELATIVE ${PIIR_REMOTES_DIR} ${PIIR_REMOTES_DIR}/*.json)
list(SORT _piir_all_remotes)

set(_piir_selected)
foreach(_pattern IN LISTS PIIR_REMOTES)
  if(_pattern STREQUAL "all")
    list(APPEND _piir_selected ${_piir_all_remotes})
  elseif(_pattern STREQUAL "native")
    foreach(_remote IN LISTS PIIR_NATIVE_REMOTES)
      list(APPEND _piir_selected ${_remote}.json)
    endforeach()
  elseif(_pattern STREQUAL "none" OR _pattern STREQUAL "")
  else()
    string(REGEX REPLACE "\\.json$" "" _pattern "${_pattern}")
    if(NOT _pattern MATCHES "[/*?]")
      set(_pattern "${_pattern}/*")   # a manufacturer
    endif()
    string(REGEX REPLACE "([][+.^$(){}|\\\\])" "\\\\\\1" _regex "${_pattern}")
    string(REPLACE "*" "[^/]*" _regex "${_regex}")
    string(REPLACE "?" "[^/]" _regex "${_regex}")
    set(_found FALSE)
    foreach(_remote IN LISTS _piir_all_remotes)
      if(_remote MATCHES "^${_regex}\\.json$")
        list(APPEND _piir_selected ${_remote})
        set(_found TRUE)
      endif()
    endforeach()
    if(NOT _found)
      message(WARNING "PIIR_REMOTES: no remote matches '${_pattern}'")
    endif()
  endif()
endforeach()
if(_piir_selected)
  list(REMOVE_DUPLICATES _piir_selected)
endif()
list(LENGTH _piir_selected _count)
message(STATUS "PIIR remotes to install: ${_count}")

foreach(_remote IN LISTS _piir_selected)
  get_filename_component(_dir ${_remote} DIRECTORY)
  install(FILES ${PIIR_REMOTES_DIR}/${_remote}
    DESTINATION ${CMAKE_INSTALL_DATADIR}/piir/conf/remotes/${_dir})
endforeach()

# Archive with every remote, attached to releases
file(GLOB _piir_remote_dirs RELATIVE ${PIIR_REMOTES_DIR} ${PIIR_REMOTES_DIR}/*)
set(PIIR_REMOTES_ARCHIVE ${PROJECT_BINARY_DIR}/piir-remotes-${PROJECT_VERSION}.tar.gz)
add_custom_target(remotes_archive
  COMMAND ${CMAKE_COMMAND} -E tar czf ${PIIR_REMOTES_ARCHIVE} -- ${_piir_remote_dirs}
  WORKING_DIRECTORY ${PIIR_REMOTES_DIR}
  COMMENT "Packing all remotes into ${PIIR_REMOTES_ARCHIVE}"
  VERBATIM)

# piir-remotes adds and removes remotes from that archive on an installation
configure_file(${PROJECT_SOURCE_DIR}/tools/piir-remotes.in ${PROJECT_BINARY_DIR}/piir-remotes @ONLY)
install(PROGRAMS ${PROJECT_BINARY_DIR}/piir-remotes DESTINATION ${CMAKE_INSTALL_BINDIR})
