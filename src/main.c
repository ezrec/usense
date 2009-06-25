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

#include "usense.h"

static const char *program;

static int list_devices(void)
{
	struct usense *usense;
	struct usense_device *dev;

	usense = usense_start(0, NULL);
	if (usense == NULL) {
		fprintf(stderr, "%s: Can't create a new usense monitor\n", program);
		return EXIT_FAILURE;
	}

	for (dev = usense_first(usense); dev != NULL; dev = usense_next(usense, dev)) {
		printf("%s\n", usense_device_name(dev));
	}

	usense_stop(usense);

	return 0;
}

static int list_device_props(struct usense_device *dev)
{
	char value[PATH_MAX];
	const char *key;
	int err;

	for (key = usense_prop_first(dev); key != NULL; key = usense_prop_next(dev, key)) {
		err = usense_prop_get(dev, key, value, sizeof(value));
		if (err < 0) {
			continue;
		}
		printf("%s=%s\n", key, value);
	}

	return EXIT_SUCCESS;
}

int show_device_prop(struct usense_device *dev, const char *prop_name)
{
	char value[PATH_MAX];
	int err;

	err = usense_prop_get(dev, prop_name, value, sizeof(value));
	if (err < 0) {
		fprintf(stderr, "%s: No such property \"%s\"\n", usense_device_name(dev), prop_name);
		return EXIT_FAILURE;
	}
	printf("%s\n", value);

	return EXIT_SUCCESS;
}

int set_device_prop(struct usense_device *dev, const char *prop_name, const char *value)
{
	int err;

	err = usense_prop_set(dev, prop_name, value);
	if (err < 0) {
		fprintf(stderr, "%s: Can't set property \"%s\" to \"%s\"\n", usense_device_name(dev), prop_name, value);
		return EXIT_FAILURE;
	}

	return EXIT_SUCCESS;
}

int main(int argc, char **argv)
{
	struct usense_device *dev;
	int i, err = 0;

	program = argv[0];

	if (argc == 1) {
		/* List all */
		return list_devices();
	}

	dev = usense_open(argv[1]);
	if (dev == NULL) {
		fprintf(stderr, "%s: No such sensor '%s'\n", program, argv[1]);
		return EXIT_FAILURE;
	}

	if (argc == 2) {
		/* List props of a device */
		return list_device_props(dev);
	}

	argc -= 1;
	argv += 1;
	for (i = 1; i < argc; i++) {
		char *cp;

		cp = strchr(argv[i],'=');
		if (cp == NULL) {
			/* List single prop of a device */
			err = show_device_prop(dev, argv[i]);
			if (err < 0) {
				break;
			}
		} else {
			*(cp++) = 0;
			/* Set prop of a device */
			err = set_device_prop(dev, argv[i], cp);
			if (err < 0) {
				break;
			}
		}
	}

	usense_close(dev);

	return (err < 0) ? EXIT_FAILURE : EXIT_SUCCESS;
}
