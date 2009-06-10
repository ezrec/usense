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
		double offset;
		double ratio;
	} calibrate;
};

static void gotemp_calibrate(struct gotemp *gotemp)
{
	FILE *inf;
	char buff[PATH_MAX];

	gotemp->calibrate.offset = 0.0;
	gotemp->calibrate.ratio = 1.0;

	snprintf(buff, sizeof(buff), "%s/.gotemprc", getenv("HOME"));
	inf = fopen(buff, "r");
	if (inf == NULL) {
		inf = fopen("/etc/gotemp", "r");
	}

	if (inf == NULL) {
		return;
	}

	while (fgets(buff, sizeof(buff), inf) != NULL) {
		char *cp;

		cp = strchr(buff, '=');
		if (cp == NULL) {
			continue;
		}
		*(cp++) = 0;
		if (strcmp(buff, "calibrate.offset") == 0) {
			gotemp->calibrate.offset = strtod(cp, NULL);
		} else if (strcmp(buff, "calibrate.ratio") == 0) {
			gotemp->calibrate.ratio = strtod(cp, NULL);
		}
	}

	fclose(inf);
}

struct gotemp *gotemp_acquire(int index)
{
	struct usb_bus *busses, *bus;
	struct gotemp *gotemp;
	int err;

	usb_init();
	usb_find_busses();
	usb_find_devices();

	busses = usb_get_busses();

	for (bus = busses; bus != NULL; bus = bus->next) {
		struct usb_device *dev;
		struct usb_dev_handle *usb;

		for (dev = bus->devices; dev != NULL; dev = dev->next) {
			if (dev->descriptor.idVendor == 0x08f7 &&
				dev->descriptor.idProduct == 0x0002 &&
				dev->descriptor.iManufacturer == 1 &&
				dev->descriptor.iProduct == 2 &&
				dev->descriptor.iSerialNumber == 0 &&
				dev->descriptor.bNumConfigurations == 1) {

				usb = usb_open(dev);
				if (usb == NULL) {
					fprintf(stderr, "Can't open Vernier EasyTemp (GoTemp) device: %s\n", usb_strerror());
					continue;
				}
				if (index <= 0) {
					err = usb_clear_halt(usb, 0);
					err = usb_detach_kernel_driver_np(usb, 0);
					err = usb_claim_interface(usb, 0);
					if (err < 0) {
						fprintf(stderr, "Can't claim Vernier EasyTemp (GoTemp) device: %s\n", usb_strerror());
						usb_close(usb);
						return NULL;
					}
					gotemp = calloc(1, sizeof(*gotemp));
					gotemp->usb = usb;
					gotemp_calibrate(gotemp);
					return gotemp;
				}
				index--;
			}
		}
	}

	return NULL;
}

void gotemp_release(struct gotemp *gotemp)
{
	assert(gotemp != NULL);
	assert(gotemp->usb != NULL);

	usb_reset(gotemp->usb);
	usb_close(gotemp->usb);
	free(gotemp);
}


int gotemp_read(struct gotemp *gotemp, double *temp)
{
	/* From the GoIO_SDK */
	const double conversion = 0.0078125;
	int len;

	assert(sizeof(gotemp->packet) == 8);

	do {
		len = usb_interrupt_read(gotemp->usb, 0x81, (void *)&gotemp->packet, sizeof(gotemp->packet), 1000);
		if (len < 0 || len != sizeof(gotemp->packet)) {
			if (len == -EAGAIN) {
				continue;
			}
			return -1;
		}
	} while (0);

	*temp = (((double) gotemp->packet.measurement0) * conversion * gotemp->calibrate.ratio) + gotemp->calibrate.offset;
	return 0;
}

/* Function to convert Celsius to Fahrenheit */
float CtoF(float C)
{
    return (C * 9.0 / 5.0) + 32;
}

int main(int argc, char **argv)
{
    double temp;
    int err;
    enum { TEMP_F, TEMP_C } temp_mode = TEMP_F;
    struct gotemp *usb;

    if (argc > 1) {
	if (argc==2 && strcmp(argv[1],"-F")==0) {
		temp_mode = TEMP_F;
	} else if (argc==2 && strcmp(argv[1],"-C")==0) {
		temp_mode = TEMP_C;
	}
    }

    usb = gotemp_acquire(0);
    if (usb == NULL) {
        return EXIT_FAILURE;
    }

    err = gotemp_read(usb, &temp);
    err = gotemp_read(usb, &temp);
    gotemp_release(usb);

    if (err < 0) {
        fprintf(stderr, "Can't get a reading.\n");
        return EXIT_FAILURE;
    }

    switch (temp_mode) {
	case TEMP_F:
		printf("%.2f\n", CtoF(temp));
		break;
        case TEMP_C:
		printf("%.2f\n", temp);
		break;
    }

    return 0;
}
