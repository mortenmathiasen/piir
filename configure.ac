#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

AC_PREREQ([2.69])
AC_INIT([PIIR], [1.0.0], [https://github.com/mortenmathiasen/piir/issues])
AC_CONFIG_SRCDIR([src/parson.c])
AC_CONFIG_HEADERS([config.h])

# AutoMake
AM_INIT_AUTOMAKE ([-Wall -Werror foreign])

# Checks for programs.
AC_PROG_CC

# Checks for libraries.

# Checks for header files.
AC_CHECK_HEADERS([stddef.h stdlib.h string.h unistd.h])

# Checks for typedefs, structures, and compiler characteristics.
AC_CHECK_HEADER_STDBOOL
AC_TYPE_SIZE_T
AC_TYPE_UINT32_T
AC_TYPE_UINT8_T

# Checks for library functions.
AC_FUNC_STRTOD
AC_CHECK_FUNCS([memmove strchr strdup strstr])

AC_CONFIG_FILES([Makefile])
AC_OUTPUT
