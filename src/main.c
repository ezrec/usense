/*
 * Copyright 2009, Netronome Systems
 * Author: Jason McMullan <jason.mcmullan@netronome.com>
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

#include "usense.h"

int list_devices(void)
{
	struct usense *usense;
	struct usense_device *dev;

	usense = usense_start(0, NULL);
	if (usense == NULL) {
		return EXIT_SUCCESS;
	}

	for (dev = usense_first(usense); dev != NULL; dev = usense_next(usense, dev)) {
		printf("%s\n", usense_device_name(dev));
	}

	return 0;

	usense_stop(usense);
}

int list_device_props(const char *dev_name)
{
	struct usense_device *dev;
	char value[PATH_MAX];
	const char *key;
	int err;

	dev = usense_open(dev_name);
	if (dev == NULL) {
		fprintf(stderr, "%s: No such sensor\n", dev_name);
		return EXIT_FAILURE;
	}

	for (key = usense_prop_first(dev); key != NULL; key = usense_prop_next(dev, key)) {
		err = usense_prop_get(dev, key, value, sizeof(value));
		if (err < 0) {
			continue;
		}
		printf("%s=%s\n", key, value);
	}

	return EXIT_SUCCESS;
}

int show_device_prop(const char *dev_name, const char *prop_name)
{
	struct usense_device *dev;
	char value[PATH_MAX];
	int err;

	dev = usense_open(dev_name);
	if (dev == NULL) {
		fprintf(stderr, "%s: No such sensor\n", dev_name);
		return EXIT_FAILURE;
	}

	err = usense_prop_get(dev, prop_name, value, sizeof(value));
	if (err < 0) {
		fprintf(stderr, "%s: No such property \"%s\"\n", dev_name, prop_name);
		return EXIT_FAILURE;
	}
	printf("%s\n", value);

	return EXIT_SUCCESS;
}

int set_device_prop(const char *dev_name, const char *prop_name, const char *value)
{
	struct usense_device *dev;
	int err;

	dev = usense_open(dev_name);
	if (dev == NULL) {
		fprintf(stderr, "%s: No such sensor\n", dev_name);
		return EXIT_FAILURE;
	}

	err = usense_prop_set(dev, prop_name, value);
	if (err < 0) {
		fprintf(stderr, "%s: Can't set property \"%s\"\n", dev_name, prop_name);
		return EXIT_FAILURE;
	}

	return EXIT_SUCCESS;
}

int main(int argc, char **argv)
{
	if (argc == 1) {
		/* List all */
		return list_devices();
	}

	if (argc == 2) {
		/* List props of a device */
		return list_device_props(argv[1]);
	}

	if (argc == 3) {
		/* List single prop of a device */
		return show_device_prop(argv[1], argv[2]);
	}

	if (argc == 4) {
		/* Set prop of a device */
		return set_device_prop(argv[1], argv[2], argv[3]);
	}

	return EXIT_FAILURE;
}
