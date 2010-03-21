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
#include <errno.h>

#include "usense.h"
#include "units.h"

#include "ch341.h"
#include "i2c.h"
#include "i2c-algo-bit.h"

struct temper {
	struct ch341 *ch;
	struct i2c_adapter adap;
	struct i2c_algo_bit_data i2c_bit;
};

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

/* Many I2C devices can get into weird states.
 * The recommened technique by Phillips is
 * to 'clock out' 9 clocks.
 *
 * Improvided a little by framing between
 * a START, repeated START, and a STOP condition.
 *
 * Call this before any set of operations.
 */
static int temp_reset(struct i2c_adapter *adap)
{
	struct i2c_algo_bit_data *bit = adap->algo_data;
	int i;

	/* Send a START condition */
	bit->setscl(bit->data, 1);
	bit->setsda(bit->data, 1);
	usleep(500);
	bit->setsda(bit->data, 0);
	usleep(500);
	bit->setscl(bit->data, 0);

	/* Send out a 1Khz waveform of
	 * 9 clock pulses
	 */
	bit->setsda(bit->data, 1);
	for (i = 0; i < 9; i++) {
		bit->setscl(bit->data, 1);
		usleep(500);
		bit->setscl(bit->data, 0);
		usleep(500);
	}

	/* Send out START condition again */
	bit->setscl(bit->data, 1);
	usleep(500);
	bit->setsda(bit->data, 0);
	usleep(500);
	bit->setscl(bit->data, 0);
	usleep(500);

	/* And send STOP condition */
	bit->setscl(bit->data, 1);
	usleep(500);
	bit->setsda(bit->data, 1);
	usleep(500);
}

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

static int TEMPer_update(struct usense_device *dev, void *priv)
{
	int16_t temp;
	struct temper *temper = priv;
	int err;

	/* Reset device */
	temp_reset(&temper->adap);

	/* Dump temp */
	err = temp_read(&temper->adap, REG_TEMP, &temp);
	if (err < 0) {
		fprintf(stderr, "%s: Can't read temperature\n", usense_device_name(dev));
		return -EINVAL;
	} else {
		/* microKelvin */
		char buff[48];
		double kelvin = C_TO_K(temp / 256.0);
		snprintf(buff, sizeof(buff), "%.2f", kelvin);
		usense_prop_set(dev, "reading", buff);
	}

	return 0;
}

static int TEMPer_attach(struct usense_device *dev, struct usb_dev_handle *usb, void **priv)
{
	/* Connect to ch341 */
	int err;
	struct temper *temper;
	struct ch341 *ch;
	uint8_t cfg;
	int16_t temp;

	ch = ch341_open(usb);
	if (ch == NULL) {
		return -ENODEV;
	}

	temper = calloc(1, sizeof(*temper));
	temper->ch = ch;

	temper->i2c_bit.data = ch;
	temper->i2c_bit.setsda = temper_setsda;
	temper->i2c_bit.setscl = temper_setscl;
	temper->i2c_bit.getsda = temper_getsda;
	temper->i2c_bit.getscl = NULL;
	temper->i2c_bit.udelay = 50;	/* in ms */
	temper->i2c_bit.timeout = 1000;	/* in ms */

	temper->adap.timeout = 1000;
	temper->adap.retries = 3;
	strncpy(&temper->adap.name[0], "ch341-i2c", sizeof(temper->adap.name));
	temper->adap.algo_data = &temper->i2c_bit;
	i2c_bit_add_bus(&temper->adap);

	/* Reset device */
	temp_reset(&temper->adap);

	/* Read config */
	cfg = 0;
	err = temp_cfg_read(&temper->adap, &cfg);
	if (err < 0) {
		fprintf(stderr, "%s: Can't get current configuration.\n", usense_device_name(dev));
		ch341_close(ch);
		return -EINVAL;
	}

	if (cfg != 0x60) {
		err = temp_cfg_write(&temper->adap, 0x60);
	}
	if (err < 0) {
		fprintf(stderr, "%s: Can't configure 12bit resolution\n", usense_device_name(dev));
		ch341_close(ch);
		return -EINVAL;
	} else {
		usense_prop_set(dev, "TEMPer.resolution","12");
	}

	/* Set the device and type */
	usense_prop_set(dev, "device", "TEMPer");
	usense_prop_set(dev, "type", "temp");

	TEMPer_update(dev, temper);

	*priv = temper;

	return 0;
}

static void TEMPer_release(void *priv)
{
	struct temper *temper = priv;

	ch341_close(temper->ch);
	free(temper);
}

static int TEMPer_match(struct usb_device_descriptor *desc)
{
	return (desc->idVendor == 0x4348 &&
		desc->idProduct == 0x5523 &&
		desc->iManufacturer == 0 &&
		desc->iProduct == 2 &&
		desc->iSerialNumber == 0 &&
		desc->bNumConfigurations == 1);
}

const struct usense_probe _usense_probe_TEMPer = {
	.type = USENSE_PROBE_USB,
	.probe = { .usb = { .match = TEMPer_match, .attach = TEMPer_attach, } },
	.release = TEMPer_release,
	.update = TEMPer_update,
};
