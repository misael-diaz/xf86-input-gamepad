#include <linux/joystick.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <dirent.h>
#include <string.h>
#include <errno.h>

// NOTE: this way we know the lowerbounds of the strings that we concatenate into
_Static_assert(sizeof(struct dirent) > 256);
_Static_assert(sizeof(long) == sizeof(int64_t));
_Static_assert(sizeof(int64_t) == 8);

#define BITS_PER_LONG 64
#define NBITS(x) (((x) >> 6))
#define NBYTES(x) ((NBITS(x)) << 3)
#define OFF(x) ((x) & 63)
#define test_bit(bit, array) (((array[NBITS(bit)] >> OFF(bit))) & 1)

int IsEventDevice(struct dirent const *dir)
{
	int rc = 0;
	char event[] = "event";
	uint64_t const len = (sizeof(event) - 1);
	rc = strncmp(dir->d_name, event, len);
	return ((0 == rc)? 1 : 0);
}

int main()
{
	errno = 0;
	int64_t rc = 0;
	struct dirent **namelist = NULL;
	rc = scandir("/dev/input", &namelist, IsEventDevice, alphasort);
	if (-1 == rc) {
		fprintf(stderr, "%s\n", "error: scandir failed to find events");
		if (errno) {
			fprintf(stderr, "%s\n", strerror(errno));
		}
		_exit(1);
	}

	int const devno = rc;
	for (int i = 0; i != devno; ++i) {
		errno = 0;
		char devname[sizeof(struct dirent) << 1] = "/dev/input/";
		strcat(devname, namelist[i]->d_name);
		int const fd = open(devname, O_RDONLY);
		if (-1 == fd) {
			fprintf(stderr, "error: failed to open: %s\n", devname);
			if (errno) {
				fprintf(stderr, "%s\n", strerror(errno));
			}

			for (int i = 0; i != devno; ++i) {
				free(namelist[i]);
			}
			free(namelist);
			_exit(1);
		}

		errno = 0;
		char dev[sizeof(struct dirent) << 1];
		memset(dev, 0, sizeof(dev));
		uint64_t const len = sizeof(dev);
		rc = ioctl(fd, EVIOCGNAME(len), dev);
		if (-1 == rc) {
			fprintf(stderr, "error: failed to query name of: %s\n", devname);
			if (errno) {
				fprintf(stderr, "%s\n", strerror(errno));
			}

			for (int i = 0; i != devno; ++i) {
				free(namelist[i]);
			}
			free(namelist);
			_exit(1);
		}

		errno = 0;
		uint64_t bit[NBITS(KEY_MAX) + BITS_PER_LONG];
		uint64_t code[NBITS(KEY_MAX) + BITS_PER_LONG];
		memset(bit, 0, sizeof(bit));
		memset(code, 0, sizeof(code));
		rc = ioctl(fd, EVIOCGBIT(0, NBYTES((KEY_MAX - 1)) + 8), bit);
		if (-1 == rc) {
			fprintf(stderr, "error: failed to query bits of: %s\n", devname);
			if (errno) {
				fprintf(stderr, "%s\n", strerror(errno));
			}

			for (int i = 0; i != devno; ++i) {
				free(namelist[i]);
			}
			free(namelist);
			_exit(1);
		}

		if (test_bit(EV_KEY, bit)) {
			errno = 0;
			rc = ioctl(fd, EVIOCGBIT(EV_KEY, NBYTES((KEY_MAX - 1)) + 8), code);
			if (-1 == rc) {
				fprintf(stderr, "error: failed to query bits of: %s\n", devname);
				if (errno) {
					fprintf(stderr, "%s\n", strerror(errno));
				}

				for (int i = 0; i != devno; ++i) {
					free(namelist[i]);
				}
				free(namelist);
				_exit(1);
			}
			if (test_bit(BTN_GAMEPAD, code)) {
				fprintf(stderr, "gamepad detected: %s\n", devname);
			}
		}

		close(fd);
	}

	for (int i = 0; i != devno; ++i) {
		free(namelist[i]);
	}
	free(namelist);
	return 0;
}
/*
{
	errno = 0;
	struct js_event ev = {};
	int fd = open("/dev/input/js0", O_RDONLY);
	if (-1 == fd) {
		fprintf(stderr, "%s\n", "error: failed to open gamepad");
		if (errno) {
			fprintf(stderr, "%s\n", strerror(errno));
		}
		_exit(1);
	}

	errno = 0;
	int version = -1;
	int64_t rc = ioctl(fd, JSIOCGVERSION, &version);
	if (-1 == rc) {
		fprintf(stderr, "%s\n", "error: failed to obtain joystick driver version");
		if (errno) {
			fprintf(stderr, "%s\n", strerror(errno));
		}
		_exit(1);
	}
	int const major_version = ((version >> 16) & 0xff);
	int const minor_version = ((version >>  8) & 0xff);
	int const patch_version = ((version >>  0) & 0xff);
	fprintf(stdout, "driver-version: %d.%d.%d\n", major_version, minor_version, patch_version);
	if (major_version < 1) {
		fprintf(stderr, "%s\n", "error: unsupported version");
		_exit(1);
	}

	char axes = -1;
	rc = ioctl(fd, JSIOCGAXES, &axes);
	if (-1 == rc) {
		fprintf(stderr, "%s\n", "error: failed to obtain number of axes");
		if (errno) {
			fprintf(stderr, "%s\n", strerror(errno));
		}
		_exit(1);
	}
	fprintf(stdout, "axes: %d\n", axes);

	char buttons = -1;
	rc = ioctl(fd, JSIOCGBUTTONS, &buttons);
	if (-1 == rc) {
		fprintf(stderr, "%s\n", "error: failed to obtain number of buttons");
		if (errno) {
			fprintf(stderr, "%s\n", strerror(errno));
		}
		_exit(1);
	}
	fprintf(stdout, "buttons: %d\n", buttons);

	uint8_t axmap[ABS_CNT];
	int const len_axmap = ABS_CNT;
	memset(axmap, 0, sizeof(axmap));
	rc = ioctl(fd, JSIOCGAXMAP, &axmap);
	if (-1 == rc) {
		fprintf(stderr, "%s\n", "error: failed to obtain axes mappings");
		if (errno) {
			fprintf(stderr, "%s\n", strerror(errno));
		}
		_exit(1);
	}
	fprintf(stdout, "axesmap-length: %d\n", len_axmap);
	for (int i = 0; i != axes; ++i) {
		fprintf(stdout, "axmap[%d]: 0x%02x\n", i, axmap[i]);
	}

	uint16_t btnmap[KEY_MAX - BTN_MISC + 1];
	int const len_btnmap = (KEY_MAX - BTN_MISC + 1);
	memset(btnmap, 0, sizeof(btnmap));
	rc = ioctl(fd, JSIOCGBTNMAP, &btnmap);
	if (-1 == rc) {
		fprintf(stderr, "%s\n", "error: failed to obtain button mappings");
		if (errno) {
			fprintf(stderr, "%s\n", strerror(errno));
		}
		_exit(1);
	}
	fprintf(stdout, "buttonmap-length: %d\n", len_btnmap);
	for (int i = 0; i != buttons; ++i) {
		fprintf(stdout, "btnmap[%d]: 0x%04x\n", i, btnmap[i]);
	}

	errno = 0;
	char name_gamepad[256];
	memset(name_gamepad, 0, sizeof(name_gamepad));
	rc = ioctl(fd, JSIOCGNAME(sizeof(name_gamepad)), name_gamepad);
	if (-1 == rc) {
		fprintf(stderr, "%s\n", "error: failed to query gamepad name");
		if (errno) {
			fprintf(stderr, "%s\n", strerror(errno));
		}
		_exit(1);
	}
	fprintf(stdout, "gamepad: %s\n", name_gamepad);

	errno = 0;

	while (1) {
		rc = read(fd, &ev, sizeof(ev));
		if (-1 == rc) {
			fprintf(stderr, "%s\n", "error: failed to read joystick event");
			if (errno) {
				fprintf(stderr, "%s\n", strerror(errno));
			}
			_exit(1);
		}
		else if (sizeof(ev) != rc) {
			fprintf(stderr, "%s\n", "warning: partial reading of joystick event");
		}
		else {
			int64_t bytes_read = rc;
			int64_t time = ev.time;
			int64_t value = ev.value;
			int64_t type = ev.type;
			int64_t number = ev.number;
			//fprintf(stdout, "bytes: %ld time: %ld value: %ld type: %ld number: %ld\n", bytes_read, time, value, type, number);
			if (JS_EVENT_BUTTON == (type & JS_EVENT_BUTTON)) {
				if (type & JS_EVENT_INIT) {
					fprintf(stdout, "INIT-BUTTON: bytes: %ld time: %ld value: %ld type: %ld number: %ld\n", bytes_read, time, value, type, number);
				}
				else {
					switch (btnmap[number]) {
					case BTN_START: {
						fprintf(stdout, "BUTTON START: bytes: %ld time: %ld value: %ld type: %ld number: %ld\n", bytes_read, time, value, type, number);
					}
					break;
					case BTN_SELECT: {
						fprintf(stdout, "BUTTON SELECT: bytes: %ld time: %ld value: %ld type: %ld number: %ld\n", bytes_read, time, value, type, number);
					}
					break;
					case BTN_A: {
						fprintf(stdout, "BUTTON A: bytes: %ld time: %ld value: %ld type: %ld number: %ld\n", bytes_read, time, value, type, number);
					}
					break;
					case BTN_B: {
						fprintf(stdout, "BUTTON B: bytes: %ld time: %ld value: %ld type: %ld number: %ld\n", bytes_read, time, value, type, number);
					}
					break;
					case BTN_C: {
						fprintf(stdout, "BUTTON C: bytes: %ld time: %ld value: %ld type: %ld number: %ld\n", bytes_read, time, value, type, number);
					}
					break;
					case BTN_X: {
						fprintf(stdout, "BUTTON X: bytes: %ld time: %ld value: %ld type: %ld number: %ld\n", bytes_read, time, value, type, number);
					}
					break;
					case BTN_Y: {
						fprintf(stdout, "BUTTON Y: bytes: %ld time: %ld value: %ld type: %ld number: %ld\n", bytes_read, time, value, type, number);
					}
					break;
					case BTN_Z: {
						fprintf(stdout, "BUTTON Z: bytes: %ld time: %ld value: %ld type: %ld number: %ld\n", bytes_read, time, value, type, number);
					}
					break;
					case BTN_TL: {
						fprintf(stdout, "BUTTON TL: bytes: %ld time: %ld value: %ld type: %ld number: %ld\n", bytes_read, time, value, type, number);
					}
					break;
					case BTN_TR: {
						fprintf(stdout, "BUTTON TR: bytes: %ld time: %ld value: %ld type: %ld number: %ld\n", bytes_read, time, value, type, number);
					}
					break;
					case BTN_TL2: {
						fprintf(stdout, "BUTTON TL2: bytes: %ld time: %ld value: %ld type: %ld number: %ld\n", bytes_read, time, value, type, number);
					}
					break;
					case BTN_TR2: {
						fprintf(stdout, "BUTTON TR2: bytes: %ld time: %ld value: %ld type: %ld number: %ld\n", bytes_read, time, value, type, number);
					}
					break;
					case BTN_THUMBL: {
						fprintf(stdout, "BUTTON THUMBL: bytes: %ld time: %ld value: %ld type: %ld number: %ld\n", bytes_read, time, value, type, number);
					}
					break;
					case BTN_THUMBR: {
						fprintf(stdout, "BUTTON THUMBR: bytes: %ld time: %ld value: %ld type: %ld number: %ld\n", bytes_read, time, value, type, number);
					}
					break;
					case BTN_MODE: {
						fprintf(stdout, "BUTTON MODE: bytes: %ld time: %ld value: %ld type: %ld number: %ld\n", bytes_read, time, value, type, number);
					}
					break;
					default: {
						fprintf(stdout, "BUTTON PENDING: bytes: %ld time: %ld value: %ld type: %ld number: %ld mapping: 0x%04x\n", bytes_read, time, value, type, number, btnmap[number]);
					}
					}
				}
			}
			else if (JS_EVENT_AXIS == (type & JS_EVENT_AXIS)) {
				if (type & JS_EVENT_INIT) {
					fprintf(stdout, "INIT-AXIS: bytes: %ld time: %ld value: %ld type: %ld number: %ld\n", bytes_read, time, value, type, number);
				}
				else {
					switch (axmap[number]) {
					case ABS_HAT0X: {
						fprintf(stdout, "AXIS HAT0X: bytes: %ld time: %ld value: %ld type: %ld number: %ld\n", bytes_read, time, value, type, number);
					}
					break;
					case ABS_HAT0Y: {
						fprintf(stdout, "AXIS HAT0Y: bytes: %ld time: %ld value: %ld type: %ld number: %ld\n", bytes_read, time, value, type, number);
					}
					break;
					default: {
						 fprintf(stdout, "AXIS: bytes: %ld time: %ld value: %ld type: %ld number: %ld\n", bytes_read, time, value, type, number);
					}
					}
				}
			}
			if ((0 == value) && (1 == type) && (9 == number)) {
				fprintf(stdout, "%s\n", "quitting upon user request");
				break;
			}
		}
	}

	close(fd);
	return 0;
}
*/
