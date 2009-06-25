#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

AC_PREREQ([2.63])
AC_INIT([Aquaria], [0.2], [jason.mcmullan@gmail.com])
AM_INIT_AUTOMAKE
AC_CONFIG_HEADERS([config.h])
AC_CONFIG_MACRO_DIR([m4])

# Checks for programs.
AC_PROG_CC
AC_PROG_LIBTOOL

# Checks for libraries.
AC_CHECK_LIB([usb], [usb_init])

# Checks for header files.
AC_CHECK_HEADERS([fcntl.h stdlib.h string.h strings.h syslog.h unistd.h usb.h])

# Checks for typedefs, structures, and compiler characteristics.
AC_TYPE_INT16_T

# Checks for library functions.
AC_FUNC_STRTOD
AC_CHECK_FUNCS([localtime_r strchr])

AC_OUTPUT([
	Makefile
	src/Makefile
])
