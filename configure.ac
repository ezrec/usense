#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

AC_PREREQ([2.63])
AC_INIT([Aquaria], [0.2], [jason.mcmullan@gmail.com])
AM_INIT_AUTOMAKE
AC_CONFIG_HEADERS([config.h])

# Checks for programs.
AC_PROG_CC

# Checks for libraries.
AC_CHECK_LIB([usb], [usb_init])
AC_CHECK_LIB([ip-usbph], [ip_usbph_init])

# Checks for header files.
AC_CHECK_HEADERS([fcntl.h stdlib.h string.h strings.h syslog.h unistd.h ip-usbph.h])

# Checks for typedefs, structures, and compiler characteristics.
AC_TYPE_INT16_T

# Checks for library functions.
AC_FUNC_STRTOD
AC_CHECK_FUNCS([localtime_r strchr])

AC_OUTPUT([
	Makefile
	src/Makefile
])