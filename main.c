#include <linux/joystick.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>

int main()
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
					fprintf(stdout, "BUTTON: bytes: %ld time: %ld value: %ld type: %ld number: %ld\n", bytes_read, time, value, type, number);
				}
			}
			else if (JS_EVENT_AXIS == (type & JS_EVENT_AXIS)) {
				if (type & JS_EVENT_INIT) {
					fprintf(stdout, "INIT-AXIS: bytes: %ld time: %ld value: %ld type: %ld number: %ld\n", bytes_read, time, value, type, number);
				}
				else {
					fprintf(stdout, "AXIS: bytes: %ld time: %ld value: %ld type: %ld number: %ld\n", bytes_read, time, value, type, number);
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
