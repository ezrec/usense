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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ch341.h"
#include "i2c.h"
#include "i2c-algo-bit.h"

static void temper_setsda(void *data, int state)
{
	struct ch341 *ch = data;
	int val;
	val = ch341_tiocmget(ch);
	if (state) {
		val |= TIOCM_RTS;
	} else {
		val &= ~TIOCM_RTS;
	}
	ch341_tiocmset(ch, val);
}

static void temper_setscl(void *data, int state)
{
	struct ch341 *ch = data;
	int val;

	val = ch341_tiocmget(ch);
	if (state) {
		val |= TIOCM_DTR;
	} else {
		val &= ~TIOCM_DTR;
	}
	ch341_tiocmset(ch, val);
}

static int  temper_getsda(void *data)
{
	struct ch341 *ch = data;
	int val;

	temper_setsda(data, 1);
	usleep(100);
	val = ch341_tiocmget(ch);

	return ((val & TIOCM_CTS) != 0);
}

#define REG_TEMP	0
#define REG_CONFIG	1
#define REG_THYST	2
#define REG_TOS		3

static int temp_cfg_read(struct i2c_adapter *adap, uint8_t *val)
{
	uint8_t ptr = REG_CONFIG;
	struct i2c_msg msg[2];

	msg[0].addr = 0x4f;
	msg[0].flags = 0;
	msg[0].len = 1;
	msg[0].buf = &ptr;

	msg[1].addr = 0x4f;
	msg[1].flags = I2C_M_RD;
	msg[1].len = 1;
	msg[1].buf = val;

	return i2c_xfer(adap, msg, 2);
}

static inline int temp_cfg_write(struct i2c_adapter *adap, uint8_t val)
{
	uint8_t buff[2];
	struct i2c_msg msg[2];

	buff[0] = REG_CONFIG;
	buff[1] = val;

	msg[0].addr = 0x4f;
	msg[0].flags = 0;
	msg[0].len = 2;
	msg[0].buf = &buff[0];

	return i2c_xfer(adap, msg, 1);
}

static int temp_read(struct i2c_adapter *adap, int reg, int16_t *val)
{
	int err;
	uint8_t ptr = reg;
	uint8_t buff[2];
	struct i2c_msg msg[2];

	msg[0].addr = 0x4f;
	msg[0].flags = 0;
	msg[0].len = 1;
	msg[0].buf = &ptr;

	msg[1].addr = 0x4f;
	msg[1].flags = I2C_M_RD;
	msg[1].len = 2;
	msg[1].buf = &buff[0];

	err = i2c_xfer(adap, msg, 2);
	if (err >= 0) {
		*val = ((uint16_t)buff[0] << 8) | buff[1];
	}

	return err;
}

int main(int argc, char **argv)
{
	/* Connect to ch341 */
	int err;
	struct ch341 *ch;
	struct i2c_adapter adap;
	struct i2c_algo_bit_data i2c_bit;
	uint8_t cfg;
	int16_t temp;

	ch = ch341_open(0);
	if (ch == NULL) {
		return EXIT_FAILURE;
	}

	i2c_bit.data = ch;
	i2c_bit.setsda = temper_setsda;
	i2c_bit.setscl = temper_setscl;
	i2c_bit.getsda = temper_getsda;
	i2c_bit.getscl = NULL;
	i2c_bit.udelay = 50;	/* in ms */
	i2c_bit.timeout = 1000;	/* in ms */

	adap.timeout = 1000;
	adap.retries = 3;
	strncpy(&adap.name[0], "ch341-i2c", sizeof(adap.name));
	adap.algo_data = &i2c_bit;
	i2c_bit_add_bus(&adap);

	/* Read config */
	cfg = 0;
	err = temp_cfg_read(&adap, &cfg);
	if (err < 0) {
		fprintf(stderr, "%s: Can't get current configuration.\n", argv[0]);
	}

	if (cfg != 0x60) {
		err = temp_cfg_write(&adap, 0x60);
	}
	if (err < 0) {
		fprintf(stderr, "%s: Can't configure 12bit resolution\n", argv[0]);
	}

	/* Dump temps */
	err = temp_read(&adap, REG_TEMP, &temp);
	if (err < 0) {
		fprintf(stderr, "%s: Can't read temperator\n", argv[0]);
	} else {
		/* Gotemp style */
		double celsius = temp / 256.0;
		printf("%f\n", celsius);
	}


	ch341_close(ch);

	return EXIT_SUCCESS;
}
