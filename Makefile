#
# Aquaria Maintainance System
#
# Copyright 2007, Jason S. McMullan <jason.mcmullan@gmail.com>
#

INSTALL_PATH=$(HOME)

all: gotemp Aquaria

install: all
	cp gotemp $(INSTALL_PATH)/bin/gotemp
	cp Aquaria $(INSTALL_PATH)/bin/Aquaria

gotemp: gotemp.c
	$(CC) -o gotemp gotemp.c -lusb
