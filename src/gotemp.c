/* Go! Temp reader
 * written by Jason S. McMullan <jason.mcmullan@gmail.com>
 *
 * with information gathered from
 * David L. Vernier and Greg KH
 *
 * This Program is Under the terms of the GPL http://www.gnu.org/copyleft/gpl.html
 */

#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <string.h>
#include <strings.h>
#include <errno.h>
#include <assert.h>

#include <usb.h>

#include "usense.h"
#include "units.h"

struct gotemp {
	usb_dev_handle *usb;

	/* This is close to the structure I found in Greg's Code
	 * NOTE: This is in little endian format!
	 */
	struct packet {
		unsigned char measurements;
		unsigned char counter;
		int16_t measurement0;
		int16_t measurement1;
		int16_t measurement2;
	} __attribute__((packed)) packet;

	struct {
		double add;
		double mult;
	} calibrate;
};

static int gotemp_on_prop_set(struct usense_device *dev, void *priv, const char *key, const char *val)
{
	struct gotemp *gotemp = priv;
	char buff[PATH_MAX];
	const char *cp;
	int err, len;
	double d;

	if (strcmp(key, "gotemp.cal.add") == 0) {
		err = sscanf(val, "%lf%n", &d, &len);
		if (err != 1 || len != strlen(val)) {
			return -EINVAL;
		}
		gotemp->calibrate.add = d;
		return 0;
	}

	if (strcmp(key, "gotemp.cal.mul") == 0) {
		err = sscanf(val, "%lf%n", &d, &len);
		if (err != 1 || len != strlen(val)) {
			return -EINVAL;
		}
		gotemp->calibrate.mult = d;
		return 0;
	}

	return -EINVAL;
}

void gotemp_release(void *priv)
{
	struct gotemp *gotemp = priv;
	free(gotemp);
}


int gotemp_update(struct usense_device *dev, void *priv)
{
	/* From the GoIO_SDK */
	const double conversion = 0.0078125;
	struct gotemp *gotemp = priv;
	double kelvin;
	char buff[64];
	int len;

	assert(sizeof(gotemp->packet) == 8);

	do {
		len = usb_interrupt_read(gotemp->usb, 0x81, (void *)&gotemp->packet, sizeof(gotemp->packet), 1000);
		if (len < 0 || len != sizeof(gotemp->packet)) {
			if (len == -EAGAIN) {
				continue;
			}
			return -EINVAL;
		}
	} while (0);

	kelvin = C_TO_K((((double) gotemp->packet.measurement0) * conversion * gotemp->calibrate.mult) + gotemp->calibrate.add);
	snprintf(buff, sizeof(buff), "%.2f", kelvin);
	return usense_prop_set(dev, "reading", buff);
}

static int gotemp_attach(struct usense_device *dev, struct usb_dev_handle *usb, void **priv)
{
	int err;
	struct gotemp *gotemp;
	char buff[USENSE_PROP_MAX];

	gotemp = calloc(1, sizeof(*gotemp));
	if (gotemp == NULL) {
		return -ENODEV;
	}

	gotemp->usb = usb;
	gotemp->calibrate.add = 0.0;
	gotemp->calibrate.mult = 1.0;

	/* Set the device and type */
	usense_prop_set(dev, "device", "gotemp");
	usense_prop_set(dev, "type", "temp");

	snprintf(buff, sizeof(buff), "%g", gotemp->calibrate.add);
	usense_prop_set(dev, "gotemp.cal.add", buff);
	snprintf(buff, sizeof(buff), "%g", gotemp->calibrate.mult);
	usense_prop_set(dev, "gotemp.cal.mul", buff);

	/* Do a dummy update first */
	gotemp_update(dev, gotemp);

	err = gotemp_update(dev, gotemp);
	if (err < 0) {
		gotemp_release(gotemp);
		return err;
	}

	*priv = gotemp;
	return 0;
}

static int gotemp_match(struct usb_device_descriptor *desc)
{
	return (desc->idVendor == 0x08f7 &&
		desc->idProduct == 0x0002 &&
		desc->iManufacturer == 1 &&
		desc->iProduct == 2 &&
		desc->iSerialNumber == 0 &&
		desc->bNumConfigurations == 1);
}

const struct usense_probe _usense_probe_gotemp = {
	.type = USENSE_PROBE_USB,
	.probe = { .usb = { .match = gotemp_match, .attach = gotemp_attach, } },
	.release = gotemp_release,
	.update = gotemp_update,
	.on_prop_set = gotemp_on_prop_set,
};
