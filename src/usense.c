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
#include <assert.h>

#include <usb.h>

#include "usense.h"

struct usense_prop {
	const char *key;
	char *value;
};

struct usense_device {
	struct usense_device *next, **pprev;
	char *name;
	const struct usense_probe *probe;
	void *priv;
	struct usense_prop *prop;	/* bsearch */
	int props;
	void *handle;	/* device type handle */
};

struct usense {
	int fd;		/* Reading FD */
	struct usense_device *devices;
};

static const struct usense_probe **dev_probe;
static int dev_probes;

extern const struct usense_probe _usense_probe_gotemp, _usense_probe_TEMPer;

/* Register device to look for
 */
int usense_probe_register(const struct usense_probe *probe)
{
	int i;
	for (i = 0; i < dev_probes; i++) {
		if (dev_probe[i] == probe) {
			return -EEXIST;
		}
	}

	dev_probe = realloc(dev_probe, sizeof(*dev_probe) * (dev_probes + 1));
	assert(dev_probe != NULL);
	dev_probe[dev_probes++] = probe;
}

void usense_probe_unregister(const struct usense_probe *probe)
{
	int i;
	for (i = 0; i < dev_probes; i++) {
		if (dev_probe[i] == probe) {
			break;
		}
	}

	if (i == dev_probes) {
		return;
	}

	memcpy(&dev_probe[i], &dev_probe[i+1], sizeof(*dev_probe) * (dev_probes - i - 1));
	dev_probes--;
}

static int usense_init(void)
{
	if (dev_probes == 0) {
		usense_probe_register(&_usense_probe_gotemp);
		usense_probe_register(&_usense_probe_TEMPer);
	}
}


/************** Persistent daemon mode ****************
 *
 * Set devices to -1 for auto-detection.
 *
 * Device names are of the form:
 *
 *  usb:<bus>.<device>
 *
 */
static struct usense *usense_new(void)
{
	struct usense *usense;

	usense_init();

	usense = calloc(1, sizeof(*usense));
	usense->fd = -1;

	return usense;
}

static void usense_free(struct usense *usense)
{
	free(usense);
}

struct usense *usense_start(int devices, const char **device_names)
{
	struct usense *usense;

	usense = usense_new();

	usense_detect(usense);

	/* TODO */

	return usense;
}

void usense_stop(struct usense *usense)
{
	usense_free(usense);
}

static struct usense_device *usense_device_new(struct usense *usense, const char *name, const struct usense_probe *probe, void *handle)
{
	struct usense_device *dev;

	dev = calloc(1, sizeof(*dev));

	dev->next = usense->devices;
	dev->name = strdup(name);
	dev->pprev = &usense->devices;
	usense->devices = dev;
	if (dev->next != NULL) {
		dev->next->pprev = &dev->next;
	}
	dev->handle = handle;
	dev->probe = probe;

	usense_prop_set(dev, "reading", "unknown");

	return dev;
}

static int usense_prop_cmp(const void *a, const void *b)
{
	const struct usense_prop *prop_a = a, *prop_b = b;

	return strcmp(prop_a->key, prop_b->key);
}

static void usense_device_free(struct usense_device *dev)
{
	if (dev->next != NULL) {
		dev->next->pprev = dev->pprev;
	}
	*dev->pprev = dev->next;

	switch (dev->probe->type) {
	case USENSE_PROBE_INVALID: break;
	case USENSE_PROBE_USB: usb_close(dev->handle); break;
	}

	free(dev->prop);
	free(dev->name);
	free(dev);
}

static struct usense_device *usense_probe_usb(struct usense *usense, struct usb_device *dev)
{
	int i;
	struct usense_device *udev = NULL;
	char name[PATH_MAX];

	for (i = 0; i < dev_probes; i++) {
		struct usb_dev_handle *usb;
		int err;

		if (dev_probe[i]->type != USENSE_PROBE_USB)
			continue;
		if (!dev_probe[i]->probe.usb.match(&dev->descriptor))
			continue;

		usb = usb_open(dev);
		if (usb == NULL) {
			continue;
		}
		err = usb_clear_halt(usb, 0);
		err = usb_detach_kernel_driver_np(usb, 0);
		err = usb_claim_interface(usb, 0);
		if (err < 0) {
			usb_close(usb);
			continue;
		}

		snprintf(name, sizeof(name), "usb:%s.%d", dev->bus->dirname, dev->devnum);
		udev = usense_device_new(usense, name, dev_probe[i], usb);
		err = dev_probe[i]->probe.usb.attach(udev, usb, &udev->priv);
		if (err < 0) {
			usense_device_free(udev);
			continue;
		}

		break;
	}

	return udev;
}

static int usb_is_initted = 0;

static struct usb_device *usb_find(const char *bus_name, int dev_id)
{
	struct usb_bus *busses, *bus;

	if (!usb_is_initted) {
		usb_init();
		usb_is_initted = 1;
	}

	usb_find_busses();
	usb_find_devices();

	busses = usb_get_busses();

	for (bus = busses; bus != NULL; bus = bus->next) {
		struct usb_device *dev;

		if (strcmp(bus->dirname, bus_name) != 0)
			continue;

		for (dev = bus->devices; dev != NULL; dev = dev->next) {
			if (dev->devnum  == dev_id)
				return dev;
		}
	}

	return NULL;
}

/*
 * Rescan for new devices.
 */
void usense_detect(struct usense *usense)
{
	struct usb_bus *busses, *bus;

	if (!usb_is_initted) {
		usb_init();
		usb_is_initted = 1;
	}

	usb_find_busses();
	usb_find_devices();

	busses = usb_get_busses();

	for (bus = busses; bus != NULL; bus = bus->next) {
		struct usb_device *dev;
		struct usb_dev_handle *usb;
		for (dev = bus->devices; dev != NULL; dev = dev->next) {
			usense_probe_usb(usense, dev);
		}
	}
}

/* Walk the device list.
 */
struct usense_device *usense_first(struct usense *usense)
{
	return usense->devices;
}

struct usense_device *usense_next(struct usense *usense, struct usense_device *curr_dev)
{
	return curr_dev->next;
}

/*
 * fd to use with poll(2) for monitoring when device
 * properties have changed
 */
int usense_monitor_fd(struct usense *usense)
{
	return usense->fd;
}

/************** Use-once mode ****************
 */
struct usense_device *usense_open(const char *device_name)
{
	static struct usense *usense = NULL;

	if (usense == NULL) {
		usense = usense_new();
	}

	if (strncmp(device_name, "usb:", 4) == 0) {
		struct usb_device *udev;
		int err, dev_no, len;
		char bus_name[256];

		err = sscanf(device_name,"usb:%256[^.].%d%n", bus_name, &dev_no, &len);
		if (err != 2 || len != strlen(device_name)) {
			return NULL;
		}

		udev = usb_find(bus_name, dev_no);
		if (udev == NULL)
			return NULL;

		return usense_probe_usb(usense, udev);
	}

	return NULL;
}


void usense_close(struct usense_device *dev)
{
	usense_device_free(dev);
}

/************** General get/set **************/

const char *usense_device_name(struct usense_device *dev)
{
	return dev->name;
}


/* Get property from device
 * (always returns in UTF8z format)
 *
 * The only 'guaranteed' to exists property is "reading", which is returned in
 * microkelvin units.
 */
int usense_prop_get(struct usense_device *dev, const char *key, char *buff, size_t len)
{
	struct usense_prop *prop, match;

	match.key = key;
	prop = bsearch(&match, dev->prop, dev->props, sizeof(*prop), usense_prop_cmp);

	if (prop == NULL) {
		return -ENOENT;
	}

	strncpy(buff, prop->value, len);
	if (len > 0) {
		buff[len - 1] = 0;
	}

	return 0;
}

int usense_prop_set(struct usense_device *dev, const char *key, const char *value)
{
	struct usense_prop *prop, match;

	match.key = key;
	prop = bsearch(&match, dev->prop, dev->props, sizeof(*prop), usense_prop_cmp);
	if (prop == NULL) {
		dev->prop = realloc(dev->prop, sizeof(*prop) * (dev->props+1));
		prop = &dev->prop[dev->props++];
		prop->key = strdup(key);
		prop->value = strdup(value);
		return 1;
	}

	if (strcmp(prop->value, value) != 0) {
		free(prop->value);
		prop->value = strdup(value);
		return 1;
	}

	return 0;
}

/* Property walking
 */
const char *usense_prop_first(struct usense_device *dev)
{
	if (dev->props == 0) {
		return NULL;
	}

	return dev->prop[0].key;
}

const char *usense_prop_next(struct usense_device *dev, const char *curr_prop)
{
	struct usense_prop *prop, match;

	match.key = curr_prop;
	prop = bsearch(&match, dev->prop, dev->props, sizeof(*prop), usense_prop_cmp);
	if (prop == NULL) {
		return NULL;
	}

	prop++;
	if ((prop - dev->prop) > dev->props) {
		return NULL;
	}

	return prop->key;
}
