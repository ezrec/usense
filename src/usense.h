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

	/* Update the usense_device */
	int (*update)(struct usense_device *dev, void *priv);
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
struct usense *usense_start(int devices, const char **device_names);
void usense_stop(struct usense *usense);

/*
 * Rescan for new devices.
 */
void usense_detect(struct usense *usense);

/* Walk the device list.
 */
struct usense_device *usense_first(struct usense *usense);
struct usense_device *usense_next(struct usense *usense, struct usense_device *curr_dev);

/*
 * fd to use with poll(2) for monitoring when device
 * properties have changed
 */
int usense_monitor_fd(struct usense *usense);

/************** Use-once mode ****************
 */
struct usense_device *usense_open(const char *device_name);

void usense_close(struct usense_device *dev);

/************** General get/set **************/

const char *usense_device_name(struct usense_device *dev);

/* Get property from device
 * (always returns in UTF8z format)
 *
 * The only 'guaranteed' to exists property is "reading", which is returned in
 * microkelvin units.
 */
int usense_prop_get(struct usense_device *dev, const char *prop, char *buff, size_t len);

int usense_prop_set(struct usense_device *dev, const char *prop, const char *value);

/* Property walking
 */
const char *usense_prop_first(struct usense_device *dev);
const char *usense_prop_next(struct usense_device *dev, const char *curr_prop);


#endif /* USENSE_H */