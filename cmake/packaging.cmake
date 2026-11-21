# Release packages: cpack -G "TGZ;DEB" in the build directory produces
#   irlingo-<version>-<IRLINGO_PACKAGE_ARCH>.tar.gz
#   irlingo_<version>_<IRLINGO_PACKAGE_ARCH>.deb
#
# Packages are laid out relative to CMAKE_INSTALL_PREFIX, which is also where
# irlingo looks for its remotes, so configure release builds with
# -DCMAKE_INSTALL_PREFIX=/usr.

if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(aarch64|arm64)$")
  set(_irlingo_deb_arch arm64)
elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "^arm")
  # Also used for the ARMv6 build: it is static, and Raspberry Pi OS calls
  # every 32-bit Pi armhf, including ARMv6 Pi 1 and Zero.
  set(_irlingo_deb_arch armhf)
elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|AMD64)$")
  set(_irlingo_deb_arch amd64)
else()
  set(_irlingo_deb_arch ${CMAKE_SYSTEM_PROCESSOR})
endif()

set(IRLINGO_PACKAGE_ARCH "${CMAKE_SYSTEM_PROCESSOR}" CACHE STRING
  "Architecture label used in release package file names, e.g. armv6, armv7, aarch64")
set(IRLINGO_DEB_ARCH "${_irlingo_deb_arch}" CACHE STRING
  "Debian architecture written into the .deb package")

set(CPACK_PACKAGE_NAME irlingo)
set(CPACK_PACKAGE_VENDOR "Morten Mathiasen")
set(CPACK_PACKAGE_CONTACT "Morten Mathiasen <https://github.com/mortenmathiasen/irlingo>")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "${PROJECT_DESCRIPTION}")
set(CPACK_PACKAGE_HOMEPAGE_URL "${PROJECT_HOMEPAGE_URL}")
set(CPACK_RESOURCE_FILE_LICENSE "${PROJECT_SOURCE_DIR}/LICENSE")
set(CPACK_RESOURCE_FILE_README "${PROJECT_SOURCE_DIR}/README.md")
set(CPACK_PACKAGE_FILE_NAME "irlingo-${PROJECT_VERSION}-${IRLINGO_PACKAGE_ARCH}")
set(CPACK_PACKAGING_INSTALL_PREFIX "${CMAKE_INSTALL_PREFIX}")
set(CPACK_GENERATOR "TGZ;DEB")
set(CPACK_STRIP_FILES ON)

set(CPACK_DEBIAN_FILE_NAME "irlingo_${PROJECT_VERSION}_${IRLINGO_PACKAGE_ARCH}.deb")
set(CPACK_DEBIAN_PACKAGE_ARCHITECTURE "${IRLINGO_DEB_ARCH}")
set(CPACK_DEBIAN_PACKAGE_SECTION utils)
# irlingo was called PIIR before 3.0.0: installing it removes the piir package
set(CPACK_DEBIAN_PACKAGE_CONFLICTS piir)
set(CPACK_DEBIAN_PACKAGE_REPLACES piir)
set(CPACK_DEBIAN_PACKAGE_DESCRIPTION
  "irlingo is a command line tool that transmits any configured infrared signal, e.g. to control TVs and air conditioners.")
if(NOT IRLINGO_STATIC AND NOT CMAKE_CROSSCOMPILING)
  set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
endif()

include(CPack)
