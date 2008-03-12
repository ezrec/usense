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

struct usb_dev_handle *gotemp_acquire(int index)
{
	struct usb_bus *busses, *bus;
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
					return usb;
				}
				index--;
			}
		}
	}

	return NULL;
}

void gotemp_release(struct usb_dev_handle *usb)
{
	assert(usb != NULL);

	usb_reset(usb);
	usb_close(usb);
}


/* This is close to the structure I found in Greg's Code
 * NOTE: This is in little endian format!
 */
struct packet {
    unsigned char measurements;
    unsigned char counter;
    int16_t measurement0;
    int16_t measurement1;
    int16_t measurement2;
} __attribute__((packed));

int gotemp_read(struct usb_dev_handle *usb, struct packet *pack)
{
	int len;

	assert(sizeof(*pack) == 8);

	do {
		len = usb_interrupt_read(usb, 0x81, (void *)pack, sizeof(*pack), 100000000);
		if (len < 0 || len != sizeof(*pack)) {
			if (len == -EAGAIN) {
				continue;
			}
			return -1;
		}
	} while (0);

	return 0;
}

/* Function to convert Celsius to Fahrenheit */
float CtoF(float C)
{
    return (C * 9.0 / 5.0) + 32;
}

int main(int argc, char **argv)
{
    struct stat buf;
    struct packet temp;
    /* From the GoIO_SDK */
    float conversion = 0.0078125;
    int err;
    enum { TEMP_F, TEMP_C } temp_mode = TEMP_F;
    struct usb_dev_handle *usb;

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
		printf("%.2f\n", CtoF(((float) temp.measurement0) * conversion));
		break;
        case TEMP_C:
		printf("%.2f\n", ((float) temp.measurement0) * conversion);
		break;
    }

    return 0;
}
