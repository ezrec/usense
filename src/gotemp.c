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
	struct usense_device *dev;
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

static void gotemp_calibrate(struct gotemp *gotemp)
{
	char buff[PATH_MAX];
	const char *cp;
	int err;

	gotemp->calibrate.add = 0.0;
	gotemp->calibrate.mult = 1.0;

	err = usense_prop_get(gotemp->dev, "calibrate.add", buff, sizeof(buff));
	if (err >= 0) {
		gotemp->calibrate.add = strtod(cp, NULL);
	}

	err = usense_prop_get(gotemp->dev, "calibrate.mult", buff, sizeof(buff));
	if (err >= 0) {
		gotemp->calibrate.mult = strtod(cp, NULL);
	}
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
	uint64_t mkelvin;
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

	mkelvin = (uint64_t)(C_TO_K((((double) gotemp->packet.measurement0) * conversion * gotemp->calibrate.mult) + gotemp->calibrate.add) * 1000000);
	snprintf(buff, sizeof(buff), "%llu", (unsigned long long)mkelvin);
	return usense_prop_set(dev, "reading", buff);
}

static int gotemp_attach(struct usense_device *dev, struct usb_dev_handle *usb, void **priv)
{
	int err;
	struct gotemp *gotemp;

	gotemp = calloc(1, sizeof(*gotemp));
	gotemp->usb = usb;
	gotemp->dev = dev;
	gotemp_calibrate(gotemp);

	if (gotemp == NULL) {
		return -ENODEV;
	}

	/* Do a dummy update first */
	gotemp_update(dev, gotemp);

	err = gotemp_update(dev, gotemp);
	if (err < 0) {
		gotemp_release(gotemp);
		return err;
	}
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
};
