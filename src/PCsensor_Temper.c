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
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>

#include "usense.h"
#include "units.h"

struct temper {
	struct usb_dev_handle *usb;
};

#define TEMPER_TIMEOUT	1000

static int send_command(struct usb_dev_handle *usb, unsigned int a, unsigned int b, unsigned int c, unsigned int d, unsigned int e, unsigned int f, unsigned int g, unsigned int h)
{
	unsigned char buf[32];
	int rc;

	bzero(buf, 32);
	buf[0] = a;
	buf[1] = b;
	buf[2] = c;
	buf[3] = d;
	buf[4] = e;
	buf[5] = f;
	buf[6] = g;
	buf[7] = h;

	rc = usb_control_msg(usb, 0x21, 9, 0x200, 0x01,
			    (char *) buf, 32, TEMPER_TIMEOUT);
	if(rc != 32) {
		perror("send_command failed");
		return -1;
	}
	return 0;
}


static int temp_read(struct usb_dev_handle *usb, int16_t *val)
{
	uint8_t buff[256];
	uint16_t tmp;
	int err, i;

	memset(buff, 0, sizeof(buff));
	buff[0] = 10;
	buff[1] = 11;
	buff[2] = 12;
	buff[3] = 13;
	buff[6] = 2;

	err = usb_control_msg(usb, 0x21, 9, 0x200, 0x01,
	                      (void *)buff, 32, TEMPER_TIMEOUT);
	if (err < 0) return err;

	memset(buff, 0, 32);
	buff[0] = 0x54;

	err = usb_control_msg(usb, 0x21, 9, 0x200, 0x01,
	                      (void *)buff, 32, TEMPER_TIMEOUT);
	if (err < 0) return err;

	memset(buff, 0, 32);
	for (i = 0; i < 7; i++) {
		err = usb_control_msg(usb, 0x21, 9, 0x200, 0x01,
				      (void *)buff, 32, TEMPER_TIMEOUT);
		if (err < 0) return err;
	}

	memset(buff, 0, 32);
	buff[0] = 10;
	buff[1] = 11;
	buff[2] = 12;
	buff[3] = 13;
	buff[6] = 1;

	err = usb_control_msg(usb, 0x21, 9, 0x200, 0x01,
	                      (void *)buff, 32, TEMPER_TIMEOUT);
	if (err < 0) return err;

	memset(buff, 0, sizeof(buff));
	err = usb_control_msg(usb, 0xa1, 1, 0x300, 0x01,
	                      (void *)buff, 8, TEMPER_TIMEOUT);
	if (err < 0) return err;

	/* First byte is Degrees C
	 * Second byte is 256ths Degrees C
	 * Third byte is 0x31 - Unknown at this time.
	 */
	tmp = ((uint16_t)((buff[0]<<8) | buff[1]));

	if (tmp == 0 || tmp == ~0)
		return -EAGAIN;

	/* msb means the temperature is negative -- less than 0 Celsius -- and in 2'complement form.
	 * We can't be sure that the host uses 2's complement to store negative numbers
	 * so if the temperature is negative, we 'manually' get its magnitude
	 * by explicity getting it's 2's complement and then we return the negative of that.
	 */

	if ((buff[0] & 0x80)!=0) {
		/* return the negative of magnitude of the temperature */
		*val = -((tmp ^ 0xffff)+1);
	} else {
		*val = tmp;
	}

	return 0;
}

static int PCsensor_Temper_update(struct usense_device *dev, void *priv)
{
	int16_t temp;
	struct temper *temper = priv;
	int err;

	/* Dump temp */
	err = temp_read(temper->usb, &temp);
	if (err < 0) {
		fprintf(stderr, "%s: Can't read temperature\n", usense_device_name(dev));
		return err;
	} else {
		/* Kelvin */
		char buff[48];
		double celsius = (double)temp/ 256.0;
		double kelvin = C_TO_K(celsius);
		snprintf(buff, sizeof(buff), "%g", kelvin);
		usense_prop_set(dev, "reading", buff);
	}

	return 0;
}

static int PCsensor_Temper_attach(struct usense_device *dev, struct usb_dev_handle *usb, void **priv)
{
	int err;
	struct temper *temper;

	temper = calloc(1, sizeof(*temper));
	temper->usb = usb;

	/* Set the device and type */
	usense_prop_set(dev, "device", "PCsensor_Temper");
	usense_prop_set(dev, "type", "temp");

	/* Issue the commands to set the device to 12-bit mode */

	send_command(usb, 10, 11, 12, 13, 0, 0, 2, 0);
	send_command(usb, 0x43, 0, 0, 0, 0, 0, 0, 0);
	send_command(usb, 0, 0, 0, 0, 0, 0, 0, 0);
	send_command(usb, 0, 0, 0, 0, 0, 0, 0, 0);
	send_command(usb, 0, 0, 0, 0, 0, 0, 0, 0);
	send_command(usb, 0, 0, 0, 0, 0, 0, 0, 0);
	send_command(usb, 0, 0, 0, 0, 0, 0, 0, 0);
	send_command(usb, 0, 0, 0, 0, 0, 0, 0, 0);

	PCsensor_Temper_update(dev, temper);

	*priv = temper;

	return 0;
}

static void PCsensor_Temper_release(void *priv)
{
	struct temper *temper = priv;

	free(temper);
}

static int PCsensor_Temper_match(struct usb_device_descriptor *desc)
{
	return (desc->idVendor == 0x1130 &&
		desc->idProduct == 0x660c &&
		desc->bNumConfigurations == 1);
}

const struct usense_probe _usense_probe_PCsensor_Temper = {
	.type = USENSE_PROBE_USB,
	.probe = { .usb = {
		.match = PCsensor_Temper_match,
		.attach = PCsensor_Temper_attach, } },
	.release = PCsensor_Temper_release,
	.update = PCsensor_Temper_update,
};
