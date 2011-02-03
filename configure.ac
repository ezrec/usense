#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

AC_PREREQ([2.61])
AC_INIT([usense], [1.5.0], [jason.mcmullan@gmail.com])
AM_INIT_AUTOMAKE([foreign])
AC_CONFIG_HEADERS([config.h])
AC_CONFIG_MACRO_DIR([m4])

# Checks for programs.
AC_PROG_CC
AC_PROG_LIBTOOL

# Checks for libusb
AC_CHECK_HEADERS([usb.h],
		 ,
		 AC_MSG_ERROR([Please install the libusb development package]))
AC_CHECK_LIB([usb], [usb_init],
	     ,
	     AC_MSG_ERROR([Please install the libusb development package]))

# Checks for header files.
AC_CHECK_HEADERS([fcntl.h stdlib.h string.h strings.h syslog.h unistd.h])


# Checks for typedefs, structures, and compiler characteristics.
AC_TYPE_INT16_T

# Checks for library functions.
AC_FUNC_STRTOD
AC_CHECK_FUNCS([localtime_r strchr])

AC_OUTPUT([
	Makefile
	src/Makefile
])
