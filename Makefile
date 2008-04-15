#
# Aquaria Maintainance System
#
# Copyright 2007, Jason S. McMullan <jason.mcmullan@gmail.com>
#

INSTALL_PATH=/opt/Aquaria

all: gotemp Aquaria

install: all
	mkdir -p /etc/Aquaria
	cp gotemp $(INSTALL_PATH)/bin/gotemp
	cp Aquaria $(INSTALL_PATH)/bin/Aquaria

gotemp: gotemp.c
	$(CC) -o gotemp gotemp.c -lusb
