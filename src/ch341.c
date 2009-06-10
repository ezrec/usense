/*
 * Copyright 2007, Frank A Kingswood <frank@kingswood-consulting.co.uk>
 * Copyright 2007, Werner Cornelius <werner@cornelius-consult.de>
 * Copyright 2009, Boris Hajduk <boris@hajduk.org>
 * Copyright 2009, Jason S. McMullan <jason.mcmullan@gmail.com>
 *
 * ch341.c implements a serial port driver for the Winchiphead CH341.
 *
 * Modified from Linux kernel to libusb use by Jason S. McMullan
 *
 * The CH341 device can be used to implement an RS232 asynchronous
 * serial port, an IEEE-1284 parallel printer port or a memory-like
 * interface. In all cases the CH341 supports an I2C interface as well.
 * This driver only supports the asynchronous serial interface.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License version
 * 2 as published by the Free Software Foundation.
 */

#include <stdio.h>
#include <termios.h>
#include <errno.h>
#include <unistd.h>
#include <stdint.h>

#include <usb.h>

#include "ch341.h"

#define DEFAULT_BAUD_RATE 9600
#define DEFAULT_TIMEOUT   1000

/* flags for IO-Bits */
#define CH341_BIT_RTS (1 << 6)
#define CH341_BIT_DTR (1 << 5)

/******************************/
/* interrupt pipe definitions */
/******************************/
/* always 4 interrupt bytes */
/* first irq byte normally 0x08 */
/* second irq byte base 0x7d + below */
/* third irq byte base 0x94 + below */
/* fourth irq byte normally 0xee */

/* second interrupt byte */
#define CH341_MULT_STAT 0x04 /* multiple status since last interrupt event */

/* status returned in third interrupt answer byte, inverted in data
   from irq */
#define CH341_BIT_CTS 0x01
#define CH341_BIT_DSR 0x02
#define CH341_BIT_RI  0x04
#define CH341_BIT_DCD 0x08
#define CH341_BITS_MODEM_STAT 0x0f /* all bits */

/*******************************/
/* baudrate calculation factor */
/*******************************/
#define CH341_BAUDBASE_FACTOR 1532620800
#define CH341_BAUDBASE_DIVMAX 3

struct ch341 {
	struct usb_dev_handle *dev;
	unsigned baud_rate; /* set baud rate */
	uint8_t line_control; /* set line control value RTS/DTR */
	uint8_t line_status; /* active status of modem control inputs */
	uint8_t multi_status_change; /* status changed multiple since last call */
};

static int ch341_control_out(struct ch341 *priv, uint8_t request,
			     uint16_t value, uint16_t index)
{
	int r;

	r = usb_control_msg(priv->dev,
			    USB_TYPE_VENDOR | USB_RECIP_DEVICE | USB_ENDPOINT_OUT,
			    request,
			    value, index, NULL, 0, DEFAULT_TIMEOUT);
	usleep(100);
	return r;
}

static int ch341_control_in(struct ch341 *priv,
			    uint8_t request, uint16_t value, uint16_t index,
			    char *buf, unsigned bufsize)
{
	int r;

	r = usb_control_msg(priv->dev,
			    USB_TYPE_VENDOR | USB_RECIP_DEVICE | USB_ENDPOINT_IN,
			    request,
			    value, index, buf, bufsize, DEFAULT_TIMEOUT);
	usleep(100);
	return r;
}

static int ch341_set_baudrate(struct ch341 *priv)
{
	short a, b;
	int r;
	unsigned long factor;
	short divisor;

	if (!priv->baud_rate)
		return -EINVAL;
	factor = (CH341_BAUDBASE_FACTOR / priv->baud_rate);
	divisor = CH341_BAUDBASE_DIVMAX;

	while ((factor > 0xfff0) && divisor) {
		factor >>= 3;
		divisor--;
	}

	if (factor > 0xfff0)
		return -EINVAL;

	factor = 0x10000 - factor;
	a = (factor & 0xff00) | divisor;
	b = factor & 0xff;

	r = ch341_control_out(priv, 0x9a, 0x1312, a);
	if (!r)
		r = ch341_control_out(priv, 0x9a, 0x0f2c, b);

	return r;
}

static int ch341_set_handshake(struct ch341 *priv, uint8_t control)
{
	return ch341_control_out(priv, 0xa4, ~control, 0);
}

static int ch341_get_status(struct ch341 *priv)
{
	char buffer[8];
	int r;

	r = ch341_control_in(priv, 0x95, 0x0706, 0, buffer, sizeof(buffer));
	if (r < 0)
		goto out;

	/* setup the private status if available */
	if (r == 2) {
		r = 0;
		priv->line_status = (~(*buffer)) & CH341_BITS_MODEM_STAT;
		priv->multi_status_change = 0;
	} else {
		r = -EPROTO;
	}

out:
	return r;
}

/* -------------------------------------------------------------------------- */

static int ch341_configure(struct ch341 *priv)
{
	char buffer[8];
	int r;

	/* expect two bytes 0x27 0x00 */
	r = ch341_control_in(priv, 0x5f, 0, 0, buffer, sizeof(buffer));
	if (r < 0)
		goto out;

	r = ch341_control_out(priv, 0xa1, 0, 0);
	if (r < 0)
		goto out;

	r = ch341_set_baudrate(priv);
	if (r < 0)
		goto out;

	/* expect two bytes 0x56 0x00 */
	r = ch341_control_in(priv, 0x95, 0x2518, 0, buffer, sizeof(buffer));
	if (r < 0)
		goto out;

	r = ch341_control_out(priv, 0x9a, 0x2518, 0x0050);
	if (r < 0)
		goto out;

	/* expect 0xff 0xee */
	r = ch341_get_status(priv);
	if (r < 0)
		goto out;

	r = ch341_control_out(priv, 0xa1, 0x501f, 0xd90a);
	if (r < 0)
		goto out;

	r = ch341_set_baudrate(priv);
	if (r < 0)
		goto out;

	r = ch341_set_handshake(priv, priv->line_control);
	if (r < 0)
		goto out;

	/* expect 0x9f 0xee */
	r = ch341_get_status(priv);

out:
	return r;
}

static struct ch341 *ch341_acquire(int index)
{
	struct usb_bus *busses, *bus;
	struct ch341 *priv;
	int err;

	usb_init();
	usb_find_busses();
	usb_find_devices();

	busses = usb_get_busses();

	for (bus = busses; bus != NULL; bus = bus->next) {
		struct usb_device *dev;
		struct usb_dev_handle *usb;
		for (dev = bus->devices; dev != NULL; dev = dev->next) {
			if (dev->descriptor.idVendor == 0x4348 &&
				dev->descriptor.idProduct == 0x5523 &&
				dev->descriptor.iManufacturer == 0 &&
				dev->descriptor.iProduct == 2 &&
				dev->descriptor.iSerialNumber == 0 &&
				dev->descriptor.bNumConfigurations == 1) {

				usb = usb_open(dev);
				if (usb == NULL) {
					fprintf(stderr, "Can't open device: %s\n", usb_strerror());
					continue;
				}
				if (index <= 0) {
					err = usb_clear_halt(usb, 0);
					err = usb_detach_kernel_driver_np(usb, 0);
					err = usb_claim_interface(usb, 0);
					if (err < 0) {
						fprintf(stderr, "Can't claim device: %s\n", usb_strerror());
						usb_close(usb);
						return NULL;
					}
					priv = calloc(1, sizeof(*priv));
					priv->dev = usb;
					return priv;
				}
				index--;
			}
		}
	}

	return NULL;
}

static void ch341_release(struct ch341 *priv)
{
	usb_reset(priv->dev);
	usb_close(priv->dev);
	free(priv);
}

void ch341_close(struct ch341 *priv)
{
	/* drop DTR and RTS */
	priv->line_control = 0;
	ch341_set_handshake(priv, 0);
	ch341_release(priv);
}


/* open this device, set default parameters */
struct ch341 *ch341_open(int id)
{
	struct ch341 *priv;
	int r;

	priv = ch341_acquire(id);
	if (priv == NULL) {
		return NULL;
	}

	priv->baud_rate = DEFAULT_BAUD_RATE;
	priv->line_control = CH341_BIT_RTS | CH341_BIT_DTR;

	r = ch341_configure(priv);
	if (r < 0) {
		ch341_release(priv);
		return NULL;
	}


	priv->baud_rate = DEFAULT_BAUD_RATE;
	priv->line_control = CH341_BIT_RTS | CH341_BIT_DTR;

	r = ch341_configure(priv);
	if (r)
		goto out;

	r = ch341_set_handshake(priv, priv->line_control);
	if (r)
		goto out;

	r = ch341_set_baudrate(priv);
out:
	if (r) {
		ch341_close(priv);
		return NULL;
	}

	return priv;
}

/* Old_termios contains the original termios settings and
 * termios contains the new setting to be used.
 */
void ch341_set_termios(struct ch341 *priv, struct termios *termios, struct termios *old_termios)
{
	speed_t baud_rate;

	if (!termios)
		return;

	baud_rate = cfgetispeed(termios);

	priv->baud_rate = baud_rate;

	if (baud_rate) {
		priv->line_control |= (CH341_BIT_DTR | CH341_BIT_RTS);
		ch341_set_baudrate(priv);
	} else {
		priv->line_control &= ~(CH341_BIT_DTR | CH341_BIT_RTS);
	}

	ch341_set_handshake(priv, priv->line_control);

	/* Unimplemented:
	 * (cflag & CSIZE) : data bits [5, 8]
	 * (cflag & PARENB) : parity {NONE, EVEN, ODD}
	 * (cflag & CSTOPB) : stop bits [1, 2]
	 */
}

int ch341_tiocmset(struct ch341 *priv, unsigned int val)
{
	uint8_t control;

	if (val & TIOCM_RTS)
		priv->line_control |= CH341_BIT_RTS;
	else
		priv->line_control &= ~CH341_BIT_RTS;

	if (val & TIOCM_DTR)
		priv->line_control |= CH341_BIT_DTR;
	else
		priv->line_control &= ~CH341_BIT_DTR;

	control = priv->line_control;

	return ch341_set_handshake(priv, control);
}

static void ch341_poll(struct ch341 *priv)
{
	char data[256];
	int status;

	status = usb_interrupt_read(priv->dev, 0x81, data, sizeof(data), 1);
	if (status >= 4) {
		priv->line_status = (~(data[2])) & CH341_BITS_MODEM_STAT;
	}
}

int ch341_tiocmget(struct ch341 *priv)
{
	uint8_t mcr;
	uint8_t status;
	unsigned int result;

	ch341_poll(priv);
	mcr = priv->line_control;
	status = priv->line_status;

	result = ((mcr & CH341_BIT_DTR)		? TIOCM_DTR : 0)
		  | ((mcr & CH341_BIT_RTS)	? TIOCM_RTS : 0)
		  | ((status & CH341_BIT_CTS)	? TIOCM_CTS : 0)
		  | ((status & CH341_BIT_DSR)	? TIOCM_DSR : 0)
		  | ((status & CH341_BIT_RI)	? TIOCM_RI  : 0)
		  | ((status & CH341_BIT_DCD)	? TIOCM_CD  : 0);

	return result;
}
