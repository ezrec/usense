#!/bin/sh
#
#  Copyright 2009, Jason S. McMullan
#  Author: Jason S. McMullan <jason.mcmullan@gmail.com>
#
# Re-generate the automake and autoconf files
# Useful when checking out from the git repo.

aclocal
autoheader
automake --add-missing --foreign
autoconf
