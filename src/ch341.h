/*
 * Copyright 2009, Jason S. McMullan
 * Author: Jason S. McMullan <jason.mcmullan@gmail.com>
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program; if not, write to the Free Software
 *   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 */

#ifndef CH341_H
#define CH341_H

#include <termios.h>

#ifndef TIOCM_LE
/* modem lines  - stolen from Linux asm/termios.h */
#define TIOCM_LE	0x001
#define TIOCM_DTR	0x002
#define TIOCM_RTS	0x004
#define TIOCM_ST	0x008
#define TIOCM_SR	0x010
#define TIOCM_CTS	0x020
#define TIOCM_CAR	0x040
#define TIOCM_RNG	0x080
#define TIOCM_DSR	0x100
#define TIOCM_CD	TIOCM_CAR
#define TIOCM_RI	TIOCM_RNG
#define TIOCM_OUT1	0x2000
#define TIOCM_OUT2	0x4000
#define TIOCM_LOOP	0x8000
#endif

struct ch341;

struct ch341 *ch341_open(int id);
void ch341_close(struct ch341 *priv);

void ch341_set_termios(struct ch341 *priv, struct termios *termios, struct termios *old_termios);

int ch341_tiocmset(struct ch341 *priv, unsigned int val);
int ch341_tiocmget(struct ch341 *priv);

#endif /* CH341_H */
