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
#include <stddef.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>
#include <assert.h>

#include <usb.h>

#include "usense.h"
#include "units.h"

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(x)	(sizeof(x)/sizeof(x[0]))
#endif

#ifndef container_of
#define container_of(var, obj, field) \
	 ((obj *)((char *)(var) - offsetof(obj, field)))
#endif

struct usense_prop {
	const char *key;
	char *value;
};

/* Powers of 10 from 10^-16 to 10^15 */
#define USENSE_UNITS_of(x)		((x) & ~0x1f)
#define USENSE_UNITS_POW_10(x)		((uint32_t)(x) & 0x1f)
#define USENSE_UNITS_POW_10_of(x)	(((x) & 0x10) ? (-16+(int)((x) & 0xf)) : ((x) & 0xf))

#define USENSE_UNITS_UNITLESS		(1 << 5)	/* For counters */
#define USENSE_UNITS_CELSIUS		(2 << 5)
#define USENSE_UNITS_KELVIN		(3 << 5)
#define USENSE_UNITS_FAHRENHEIT		(4 << 5)

struct usense_device {
	struct usense_device *next, **pprev;
	enum { USENSE_MODE_READ, USENSE_MODE_UPDATE } mode;
	char name[PATH_MAX];
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

extern const struct usense_probe _usense_probe_gotemp;
extern const struct usense_probe _usense_probe_TEMPer;
extern const struct usense_probe _usense_probe_PCsensor_Temper;

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
		usense_probe_register(&_usense_probe_PCsensor_Temper);
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

struct usense *usense_start(void)
{
	struct usense *usense;

	usense = usense_new();

	usense_detect(usense);

	/* TODO */

	return usense;
}

static void usense_device_free(struct usense_device *dev)
{
	free(dev->prop);
	free(dev);
}

void usense_stop(struct usense *usense)
{
	struct usense_device *dev, *tmp;

	for (dev = usense->devices; dev != NULL; ) {
		tmp = dev->next;
		usense_close(dev);
		usense_device_free(dev);
		dev = tmp;
	}
	free(usense);
}

static struct usense_device *usense_device_new(struct usense *usense, const char *name, const struct usense_probe *probe, void *handle)
{
	struct usense_device *dev;

	dev = calloc(1, sizeof(*dev));

	dev->mode = USENSE_MODE_UPDATE;
	dev->next = usense->devices;
	strncpy(dev->name, name, sizeof(dev->name));
	dev->name[sizeof(dev->name)-1]=0;
	dev->pprev = &usense->devices;
	usense->devices = dev;
	if (dev->next != NULL) {
		dev->next->pprev = &dev->next;
	}
	dev->handle = handle;
	dev->probe = probe;

	usense_prop_set(dev, "calibrate.add", "0.0");
	usense_prop_set(dev, "calibrate.mult", "1.0");
	usense_prop_set(dev, "reading", "unknown");
	usense_prop_set(dev, "name", dev->name);

	return dev;
}

static int usense_prop_cmp(const void *a, const void *b)
{
	const struct usense_prop *prop_a = a, *prop_b = b;

	return strcmp(prop_a->key, prop_b->key);
}

static int usense_check_for(struct usense_device *dev, const char *type, const char **arr, size_t len)
{
	char buff[USENSE_PROP_MAX];
	int i;

	/* Check for generics */
	for (i = 0; i < len; i++) {
		int err;
		err = usense_prop_get(dev, arr[i], buff, sizeof(buff));
		if (err < 0) {
			fprintf(stderr, "%s: Missing %s property '%s'\n",
					dev->name, type, arr[i]);
			return -EINVAL;
		}
	}

	return 0;
}

static uint32_t units_is_valid(const char *type, const char *value)
{
	const struct {
		const char *type;
		const struct valid_map {
			const char *name;
			int unit;
		} *map;
	} valid[] = {
		{ .type = "temp",
		  .map = (const struct valid_map []){
		          { "C", USENSE_UNITS_CELSIUS },
		          { "Celsius", USENSE_UNITS_CELSIUS },
		          { "K", USENSE_UNITS_KELVIN },
	                  { "Kelvin", USENSE_UNITS_KELVIN },
	                  { "F", USENSE_UNITS_FAHRENHEIT },
	                  { "Fahrenheit", USENSE_UNITS_FAHRENHEIT },
	                  { NULL, 0 } } } };
	int i;
	uint32_t unit = USENSE_UNITS_POW_10(0);

	switch (value[0]) {
	case 'm': unit = USENSE_UNITS_POW_10(-3); value++; break;
	case 'u': unit = USENSE_UNITS_POW_10(-6); value++; break;
	case 'n': unit = USENSE_UNITS_POW_10(-9); value++; break;
	default:
		  break;
	}

	for (i = 0; i < ARRAY_SIZE(valid); i++) {
		if (strcmp(type, valid[i].type) == 0) {
			int j;
			for (j = 0; valid[i].map[j].name != NULL; j++) {
				if (strcmp(value, valid[i].map[j].name) == 0) {
					return unit | valid[i].map[j].unit;
				}
			}
		}
	}

	/* Failed to match */
	return 0;
}

static int usense_prop_validate(struct usense_device *dev)
{
	const char *generic[] = {
		"device",
		"type",
		"units",
		"reading",
		"name",
		"calibrate.add",
		"calibrate.mult",
	};
	const char *type_usb[] = {
		"usb.vendor",
		"usb.product",
	};
	int err;
	char buff[USENSE_PROP_MAX];

	/* Force default units based upon type */
	err = usense_prop_get(dev, "type", buff, sizeof(buff));
	if (err < 0) {
		fprintf(stderr,"%s: Device has no 'type' property\n", dev->name);
		return -EINVAL;
	}

	/* Temperature sensors are in 'C'elsius by default.
	 */
	if (strcmp(buff, "temp") == 0) {
		err = usense_prop_set(dev, "units", "C");
		assert(err >= 0);
	}

	/* Validate that all generic properties are valid */
	err = usense_check_for(dev, "generic", generic, ARRAY_SIZE(generic));
	if (err < 0) {
		return err;
	}

	if (dev->probe->type == USENSE_PROBE_USB) {
		err = usense_check_for(dev, "USB", type_usb, ARRAY_SIZE(type_usb));
		if (err < 0) {
			return err;
		}
	}

	return 0;
}

static struct usense_device *usense_probe_usb(struct usense *usense, struct usb_device *dev)
{
	int i,j;
	struct usense_device *udev = NULL;
	char name[PATH_MAX];

	if (dev->config == NULL) {
		return NULL;
	}

	snprintf(name, sizeof(name), "usb:%s.%d", dev->bus->dirname, dev->devnum);
	for (i = 0; i < dev_probes; i++) {
		int err;

		if (dev_probe[i]->type != USENSE_PROBE_USB)
			continue;
		if (!dev_probe[i]->probe.usb.match(&dev->descriptor))
			continue;

		udev = usense_device_new(usense, name, dev_probe[i], dev);

		/* Set the USB properties */
		snprintf(name, sizeof(name), "%04x", dev->descriptor.idVendor);
		usense_prop_set(udev, "usb.vendor", name);
		snprintf(name, sizeof(name), "%04x", dev->descriptor.idProduct);
		usense_prop_set(udev, "usb.product", name);

		break;
	}

	return udev;
}

static int usense_attach_usb(struct usense_device *udev)
{
	struct usb_device *dev = udev->handle;
	struct usb_dev_handle *usb;
	int j, err;

	usb = usb_open(dev);
	if (usb == NULL)
		return -EPERM;

	for (j = 0; j < dev->config->bNumInterfaces; j++) {
		int timeout = 5;
		err = usb_detach_kernel_driver_np(usb, j);
		if ((err == -ENOENT) || (err == -ENODATA))
			err = 0;
		else if (err < 0)
			break;
		do {
			err = usb_claim_interface(usb, j);
			if (err == -EBUSY) {
				sleep(1);
				timeout--;
			}
		} while (err == -EBUSY && timeout > 0);
		if (err < 0)
			break;
	}

	if (err < 0) {
		usb_close(usb);
		return err;
	}

	err = udev->probe->probe.usb.attach(udev, usb, &udev->priv);
	if (err < 0) {
		usb_close(usb);
		return err;
	}

	/* Validate properties */
	err = usense_prop_validate(udev);
	if (err < 0)
		return err;

	udev->mode = USENSE_MODE_READ;

	return 0;
}

static int usb_is_initted = 0;

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
const char *usense_next(struct usense *usense, const char *prev_name)
{
	struct usense_device *dev;

	if (prev_name == NULL) {
		if (usense->devices == NULL)
			return NULL;
		else
			return &usense->devices->name[0];
	}

	dev = container_of(prev_name, struct usense_device, name);

	if (dev->next == NULL)
		return NULL;

	return &dev->next->name[0];
}

/*
 * fd to use with poll(2) for monitoring when device
 * properties have changed
 */
int usense_monitor_fd(struct usense *usense)
{
	return usense->fd;
}

/************** Open a device ****************
 */
struct usense_device *usense_open(struct usense *usense, const char *device_name)
{
	struct usense_device *dev;

	if (usense == NULL)
		return NULL;

	for (dev = usense->devices; dev != NULL; dev = dev->next) {
		if (strcmp(dev->name, device_name) == 0)
			break;
	}
	
	if (dev == NULL)
		return NULL;

	if (dev->probe->type == USENSE_PROBE_USB)
		return (usense_attach_usb(dev) == 0) ? dev : NULL;
	else
		return NULL;
}


void usense_close(struct usense_device *dev)
{
#if 0
	if (dev->probe->type == USENSE_PROBE_USB)
		usense_detach_usb(dev);
#endif
}

/************** General get/set **************/

const char *usense_device_name(struct usense_device *dev)
{
	return dev->name;
}

/* We know 'power' can only be from -16 to 15,
 * so we use this instead of having to link in the
 * full -lm math library.
 */
static inline double power10(int power)
{
	double x = 1.0;
	while (power < 0) {
		x /= 10.0;
		power++;
	}
	while (power > 0) {
		x *= 10.0;
		power--;
	}

	return x;
}


static void convert_reading(struct usense_device *dev, const char *val, char *buff, size_t len)
{
	uint32_t units;
	char ubuff[USENSE_PROP_MAX];
	char tbuff[USENSE_PROP_MAX];
	char abuff[USENSE_PROP_MAX];
	char mbuff[USENSE_PROP_MAX];
	char *tmp;
	int err,n;
	double reading, d, offset_add, offset_mult;

	err = sscanf(val, "%lg%n", &reading, &n);
	if (err != 1 || n != strlen(val)) {
		/* Evidently the device knows better than we do.
		 */
		strncpy(buff, val, len);
		return;
	}

	err = usense_prop_get(dev, "type", tbuff, sizeof(tbuff));
	assert(err > 0);

	err = usense_prop_get(dev, "units", ubuff, sizeof(ubuff));
	assert(err > 0);

	offset_add = 0.0;
	err = usense_prop_get(dev, "calibrate.add", abuff, sizeof(abuff));
	if (err > 0) {
		d = strtod(abuff, &tmp);
		if (tmp != abuff && *tmp == 0)
			offset_add = d;
	}

	offset_mult = 1.0;
	err = usense_prop_get(dev, "calibrate.mult", abuff, sizeof(abuff));
	if (err > 0) {
		d = strtod(abuff, &tmp);
		if (tmp != abuff && *tmp == 0)
			offset_mult = d;
	}

	units = units_is_valid(tbuff, ubuff);
	assert(units != 0);

	reading = (reading + offset_add) * offset_mult;

	switch (USENSE_UNITS_of(units)) {
	case USENSE_UNITS_KELVIN:     break;	/* Kelvin is temp native */
	case USENSE_UNITS_CELSIUS:    reading = K_TO_C(reading); break;
	case USENSE_UNITS_FAHRENHEIT: reading = K_TO_F(reading); break;
	default:
		assert(USENSE_UNITS_of(units) == USENSE_UNITS_UNITLESS );
		break;
	}

	n = USENSE_UNITS_POW_10_of(units);
	reading /= power10(n);

	/* MilliUnits and MicroUnits are integer */
	if (n < -1)
		snprintf(buff, len, "%lld", (long long int)reading);
	else
		snprintf(buff, len, "%g", reading);
}


/* Get property from device
 * (always returns in UTF8z format)
 */
int usense_prop_get(struct usense_device *dev, const char *key, char *buff, size_t len)
{
	struct usense_prop *prop, match;

	match.key = key;
	prop = bsearch(&match, dev->prop, dev->props, sizeof(*prop), usense_prop_cmp);

	if (prop == NULL) {
		return -ENOENT;
	}

	if (len < 1) {
		return 0;
	}

	if (strcmp(key, "reading") == 0) {
		dev->probe->update(dev, dev->priv);
		convert_reading(dev, prop->value, buff, len);
	} else {
		strncpy(buff, prop->value, len);
	}
	buff[len - 1] = 0;

	return strlen(buff);
}

int usense_prop_set(struct usense_device *dev, const char *key, const char *value)
{
	struct usense_prop *prop, match;
	int writable = 0;

	if (key == NULL || value == NULL || strlen(value) >= USENSE_PROP_MAX) {
		return -EINVAL;
	}

	/* If it's 'units', validate it.
	 */
	if (strcmp(key,"units") == 0) {
		int err;
		uint32_t units;
		char buff[USENSE_PROP_MAX];

		err = usense_prop_get(dev, "type", buff, sizeof(buff));
		assert(err > 0);

		units = units_is_valid(buff, value);
		if (units == 0) {
			return -EINVAL;
		}

		writable = 1;
	}
	
	if ((strcmp(key,"calibrate.add") == 0) ||
            (strcmp(key,"calibrate.mult") == 0))
		writable = 1;

	/* Does the device says it's writable? */
	if (!writable
	    && dev->probe->on_prop_set != NULL
	    && dev->mode != USENSE_MODE_UPDATE) {
		int err;

		err = dev->probe->on_prop_set(dev, dev->priv, key, value);
		if (err < 0) {
			return -EINVAL;
		}
		writable = 1;
	}

	if (!writable && dev->mode != USENSE_MODE_UPDATE) {
		return -EROFS;
	}

	match.key = key;
	prop = bsearch(&match, dev->prop, dev->props, sizeof(*prop), usense_prop_cmp);
	if (prop == NULL) {
		dev->prop = realloc(dev->prop, sizeof(*prop) * (dev->props+1));
		prop = &dev->prop[dev->props++];
		prop->key = strdup(key);
		prop->value = strdup(value);
		qsort(dev->prop, dev->props, sizeof(*prop), usense_prop_cmp);
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
	if ((prop - dev->prop) >= dev->props) {
		return NULL;
	}

	return prop->key;
}
