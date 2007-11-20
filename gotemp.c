/* Go! Temp reader
 * written by Jeff Sadowski <jeff.sadowski@gmail.com>
 * with information gathered from
 * David L. Vernier
 * and Greg KH
 * This Program is Under the terms of the GPL http://www.gnu.org/copyleft/gpl.html
 * Any questions feel free to email me :-)
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


/* This is close to the structure I found in Greg's Code */
struct packet {
    unsigned char measurements;
    unsigned char counter;
    int16_t measurement0;
    int16_t measurement1;
    int16_t measurement2;
};

/* Function to convert Celsius to Fahrenheit */
float CtoF(float C)
{
    return (C * 9.0 / 5.0) + 32;
}

int main(int argc, char **argv)
{
    char *fileName = "/dev/ldusb0";
    struct stat buf;
    struct packet temp;
    /* I got this number from the GoIO_SDK and it matched what David L. Vernier got from his Engineer */
    float convertion = 0.0078125;
    int fd;
    enum { TEMP_F, TEMP_C } temp_mode = TEMP_F;

    if (argc > 1) {
	if (argc==2 && strcmp(argv[1],"-F")==0) {
		temp_mode = TEMP_F;
	} else if (argc==2 && strcmp(argv[1],"-C")==0) {
		temp_mode = TEMP_C;
	}
    }

    if (stat(fileName, &buf)) {
        if (mknod(fileName, S_IFCHR | S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH, makedev(180, 176))) {
            fprintf(stderr, "Problems creating device %s\nrun this command once as root", fileName);
            return 1;
        }
    }
    if ((fd = open(fileName, O_RDONLY)) == -1) {
        fprintf(stderr, "Could not read %s check its permissions. Also check to see that it is plugged in\n", fileName);
        return 1;
    }
    if (read(fd, &temp, sizeof(temp)) != 8) {
        fprintf(stderr, "Error reading %s check to see that Go! Temp is plugged in.\n", fileName);
        return 1;
    }
    close(fd);

    switch (temp_mode) {
	case TEMP_F:
		printf("%.2f\n", CtoF(((float) temp.measurement0) * convertion));
		break;
        case TEMP_C:
		printf("%.2f\n", ((float) temp.measurement0) * convertion);
		break;
    }

    return 0;
}
