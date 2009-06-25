
include_HEADERS = usense.h

bin_PROGRAMS = usense

usense_SOURCES = main.c
usense_LDADD = libusense.la

lib_LTLIBRARIES = libusense.la

libusense_la_SOURCES = \
		usense.h usense.c \
		gotemp.c \
		TEMPer.c \
		ch341.c ch341.h \
		i2c-algo-bit.c i2c-algo-bit.h i2c.h
