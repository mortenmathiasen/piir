# Remote configurations in conf/remotes/<manufacturer>/<remote>.json
#
# IRLINGO_REMOTES selects the remotes to install:
#   all                 every remote (default)
#   native              the remotes made for irlingo (IRLINGO_NATIVE_REMOTES)
#   none                no remotes
#   sony                every remote of a manufacturer
#   sony/RM-ED009       one remote
#   samsung/AA59-*      remotes matching a pattern (* and ?)
# Combine them with semicolons, e.g. -DIRLINGO_REMOTES="native;sony;lg/AKB*".
#
# The target remotes_archive packs every remote into
# irlingo-remotes-<version>.tar.gz for the irlingo-remotes tool.

set(IRLINGO_REMOTES_DIR ${PROJECT_SOURCE_DIR}/conf/remotes)
set(IRLINGO_NATIVE_REMOTES panasonic/hvac_panasonic nibe/hvac_nibe carrier/hvac_carrier_nqv)
set(IRLINGO_REMOTES "all" CACHE STRING
  "Remotes to install: all, native, none, or manufacturers, remotes and patterns below conf/remotes")

file(GLOB_RECURSE _irlingo_all_remotes RELATIVE ${IRLINGO_REMOTES_DIR} ${IRLINGO_REMOTES_DIR}/*.json)
list(SORT _irlingo_all_remotes)

set(_irlingo_selected)
foreach(_pattern IN LISTS IRLINGO_REMOTES)
  if(_pattern STREQUAL "all")
    list(APPEND _irlingo_selected ${_irlingo_all_remotes})
  elseif(_pattern STREQUAL "native")
    foreach(_remote IN LISTS IRLINGO_NATIVE_REMOTES)
      list(APPEND _irlingo_selected ${_remote}.json)
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
    foreach(_remote IN LISTS _irlingo_all_remotes)
      if(_remote MATCHES "^${_regex}\\.json$")
        list(APPEND _irlingo_selected ${_remote})
        set(_found TRUE)
      endif()
    endforeach()
    if(NOT _found)
      message(WARNING "IRLINGO_REMOTES: no remote matches '${_pattern}'")
    endif()
  endif()
endforeach()
if(_irlingo_selected)
  list(REMOVE_DUPLICATES _irlingo_selected)
endif()
list(LENGTH _irlingo_selected _count)
message(STATUS "irlingo remotes to install: ${_count}")

foreach(_remote IN LISTS _irlingo_selected)
  get_filename_component(_dir ${_remote} DIRECTORY)
  install(FILES ${IRLINGO_REMOTES_DIR}/${_remote}
    DESTINATION ${CMAKE_INSTALL_DATADIR}/irlingo/conf/remotes/${_dir})
endforeach()

# Archive with every remote, attached to releases
file(GLOB _irlingo_remote_dirs RELATIVE ${IRLINGO_REMOTES_DIR} ${IRLINGO_REMOTES_DIR}/*)
set(IRLINGO_REMOTES_ARCHIVE ${PROJECT_BINARY_DIR}/irlingo-remotes-${PROJECT_VERSION}.tar.gz)
add_custom_target(remotes_archive
  COMMAND ${CMAKE_COMMAND} -E tar czf ${IRLINGO_REMOTES_ARCHIVE} -- ${_irlingo_remote_dirs}
  WORKING_DIRECTORY ${IRLINGO_REMOTES_DIR}
  COMMENT "Packing all remotes into ${IRLINGO_REMOTES_ARCHIVE}"
  VERBATIM)

# irlingo-remotes adds and removes remotes from that archive on an installation
configure_file(${PROJECT_SOURCE_DIR}/tools/irlingo-remotes.in ${PROJECT_BINARY_DIR}/irlingo-remotes @ONLY)
install(PROGRAMS ${PROJECT_BINARY_DIR}/irlingo-remotes DESTINATION ${CMAKE_INSTALL_BINDIR})
