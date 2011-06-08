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

#ifndef USENSE_H
#define USENSE_H

#include <usb.h>

struct usense;
struct usense_device;

#define USENSE_PROP_MAX		256	/* Maximum property length, including ASCIIz */

struct usense_probe {
	enum {
		USENSE_PROBE_INVALID=0,
		USENSE_PROBE_USB,
		USENSE_PROBE_SERIAL,
	} type;

	union {
		struct {	/* USB probe functions */
			/* Does it look like one? 0 = no, 1 = yes */
			int (*match)(struct usb_device_descriptor *desc);

			/* Set up '*priv' to point to any drive data you need.
			 */
			int (*attach)(struct usense_device *dev, struct usb_dev_handle *usb, void **priv);
		} usb;
		struct {	/* Serial probe functions */
			/* Does it look like one? 0 = no, 1 = yes */
			int (*match)(int tty_fd);

			/* Set up '*priv' to point to any drive data you need.
			 */
			int (*attach)(struct usense_device *dev, int tty_fd, void **priv);
		} serial;
	} probe;

	/* Free private data */
	void (*release)(void *priv);

	/* Update the device properties
	 *
	 * NOTE: Each device 'type' has a native reading units.
	 *       temp  -> Kelvin
	 *
	 * Therefore, when updating "reading", it must report in Kelvin.
	 * Use the '%g' format string.
	 * The 'usense_prop_get()' remaps the reading for the user.
	 */
	int (*update)(struct usense_device *dev, void *priv);

	/* Optional: Validate before setting
	 *
	 * Return >= 0 if 'value' is valid, -1 if not.
	 */
	int (*on_prop_set)(struct usense_device *dev, void *priv, const char *prop, const char *val);
};

/* Register devices to look for
 */
int usense_probe_register(const struct usense_probe *ops);
void usense_probe_unregister(const struct usense_probe *ops);

/************** Persistent daemon mode ****************
 *
 * Set devices to -1 for auto-detection.
 *
 * Device names are of the form:
 *
 *  usb:<bus>.<device>
 *
 */
struct usense *usense_start(void);
void usense_stop(struct usense *usense);

/*
 * Rescan for new devices.
 */
void usense_detect(struct usense *usense);

/* Walk the device list.
 * Use prev_name = NULL for the first device
 */
const char *usense_next(struct usense *usense, const char *prev_name);

/*
 * fd to use with poll(2) for monitoring when device
 * properties have changed
 */
int usense_monitor_fd(struct usense *usense);

/************** Open and close devices ****************
 */
struct usense_device *usense_open(struct usense *usense, const char *device_name);

void usense_close(struct usense_device *dev);

/************** General get/set **************/

const char *usense_device_name(struct usense_device *dev);

/* Get property from device
 * (always returns in UTF8z format)
 *
 * Guaranteed properties:
 *   device:	Device type (ie gotemp, TEMPer)
 *   type:	temp	(for now)
 *   units:	C, F, K, optionally prefixed by m(milli), u(micro) or n(nano)
 *   reading:	Reading of the device in 'units'
 *   name:	Unique name (ie usb:003.2)
 *
 * Guaranteed USB device info
 *   usb.vendor:	USB vendor ID
 *   usb.product:	USB product ID
 *
 * Device specific properties should be device_type.prop_name, ie:
 *
 *   TEMPer.precision	9 or 12 bits
 */
int usense_prop_get(struct usense_device *dev, const char *prop, char *buff, size_t len);

int usense_prop_set(struct usense_device *dev, const char *prop, const char *value);

/* Property walking
 */
const char *usense_prop_first(struct usense_device *dev);
const char *usense_prop_next(struct usense_device *dev, const char *curr_prop);


#endif /* USENSE_H */
