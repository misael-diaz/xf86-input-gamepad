#include <linux/input.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <dirent.h>
#include <string.h>
#include <errno.h>

// TODO: in read() check the BTN_GAMEPAD bit first if it's not set that means that the user unplugged the device and so we must disable it. Check if the xserver will try to enable us later so that we can probe for gamepad devices, open it, and start reading events from it.

// NOTE: borrowed this macro function from /usr/include/xorg/xorgVersion.h, I am assuming that the freedesktop developers left this to inform the next developers about their versioning logic
#define __XORG_VERSION_NUMERIC(major,minor,patch,snap,dummy) \
        (((major) * 10000000) + ((minor) * 100000) + ((patch) * 1000) + snap)

// NOTE: had to run Xorg -version on the console to find the current version
#define XORG_VERSION_CURRENT __XORG_VERSION_NUMERIC(1, 20, 13, 0, 0)

#include <xorg/xf86.h>
#include <xorg/xf86Xinput.h>
#include <xorg/xf86Module.h>
#include <xorg/xf86Modes.h>
#include <xorg/xf86Opt.h>
#include <xorg/xkbsrv.h>

#define GAMEPAD_DRIVER_NAME "gamepad"
#define GAMEPAD_MODULE_NAME (GAMEPAD_DRIVER_NAME)
#define PACKAGE_VERSION_MAJOR 1
#define PACKAGE_VERSION_MINOR 0
#define PACKAGE_VERSION_PATCHLEVEL 0

// REFS:
// https://github.com/torvalds/linux/tree/master/Documentation/input/event-codes.rst
// https://github.com/torvalds/linux/tree/master/Documentation/input/gamepad.rst
// https://gitlab.freedesktop.org/libevdev/evtest
// https://gitlab.freedesktop.org/xorg/xserver
// https://gitlab.freedesktop.org/xorg/driver/xf86-input-joystick

// NOTE: this way we know the lowerbounds of the strings that we concatenate into
_Static_assert(sizeof(struct dirent) > 256);
_Static_assert(sizeof(long) == sizeof(int64_t));
_Static_assert(sizeof(int64_t) == 8);

#define __LINUX_KERNEL_BITS_TO_LONGS(n) (((n) + 63) >> 6)
#define NBITS(x) __LINUX_KERNEL_BITS_TO_LONGS(x)
#define NBYTES(x) ((NBITS(x)) << 3)
#define OFF(x) ((x) & 63)
#define LONG(x) ((x) >> 6)
#define test_bit(bit, array) (((array[LONG(bit)] >> OFF(bit))) & 1)

struct _ModuleDesc {
    struct module_desc *child;
    struct module_desc *sib;
    struct module_desc *parent;
    void *handle;
    ModuleSetupProc SetupProc;
    ModuleTearDownProc TearDownProc;
    void *TearDownData;         /* returned from SetupProc */
    const XF86ModuleVersionInfo *VersionInfo;
};

struct _KeybdCtrl {
	KeybdCtrl ctrl;
};

enum _GAMEPADEVENT {
    EVENT_NONE,
    EVENT_BUTTON,
    EVENT_AXIS
};

typedef int (*GamepadOpenFn)(
	struct _GamepadDevRec *gamepad,
	Bool probe
);

typedef void (*GamepadCloseFn)(struct _GamepadDevRec *gamepad);

typedef int (*GamepadReadFn)(
	struct _GamepadDevRec *gamepad,
	enum _GAMEPADEVENT *event,
	int *number
);

// KbRMLVO: Keyboard Rules Model Layout Variant Options

// TODO: instead of a char* for devname use a suitable offset for holding the device name, this is far better than using a pointer and having to free() the memory of devname
struct _GamepadDevRec {
	struct _GamepadDriverRec *driver;
	GamepadOpenFn open;
	GamepadCloseFn close;
	GamepadReadFn read;
	struct _InputInfoRec *gamepad;
	struct _InputInfoRec *keyboard;
	struct _XkbRMLVOSet *options;
	int fd;
	uint8_t btno;
	uint8_t axno;
	// TODO: research what buttons and axes data do you need for this driver and that means reading the xf86 code, don't want to make the mistake of adding features the driver does not really need without understanding first
};

struct _GamepadDriverRec {
    uintptr_t base;
    uint64_t size;
    uint64_t offset_devname;
    uint64_t size_devname;
    struct _GamepadDevRec device;
};

static int GamepadOpen(
	struct _InputInfoRec *info
) {
	// TODO: research probing in joystick device
	if (info->flags & XI86_SERVER_FD) {
		if (-1 == info->fd) {
			xf86Msg(X_ERROR, "[%s] error: xserver should have openned the device by now", GAMEPAD_DRIVER_NAME);
			return BadImplementation;
		}
	}


}

static struct _InputInfoRec *GamepadKeyboardHotplug(
	struct _InputInfoRec *info,
	int flags
) {
	return NULL;
}

static int GamepadCorePreInit(
	struct _InputDriverRec *driver_gamepad,
	struct _InputInfoRec *info_gamepad,
	int flags
) {
	struct _InputInfoRec *info_keyboard = NULL;
	// NOTE: catches PreInit for the underlying keyboard device (happens when we call NewInputDeviceRequest() ourselves in the call stack of this PreInit function)
	char *src = xf86CheckStrOption(info_gamepad->options, "_source", NULL);
	if (src) {
		if (!strcmp(src, "_driver/joystick")) {
			// TODO implement PreInit for keyboard device
			xf86Msg(X_NOT_IMPLEMENTED, "[%s] error: kdbPreInit not implemented\n", GAMEPAD_DRIVER_NAME);
			return BadImplementation;
		}
		else {
			xf86Msg(X_DEBUG, "[%s] _source: %s\n", GAMEPAD_DRIVER_NAME, src);
		}
	}

	info_gamepad->device_control = NULL; // TODO impl GamepadCoreControl
	info_gamepad->read_input = NULL; // TODO impl GamepadCoreRead
	info_gamepad->control_proc = NULL; // NOTE: `control_proc` only needed if we intend to support DEVICE_CORE, DEVICE_ABS_CALIB, DEVICE_ABS_AREA (this is what I know from reading the xserver source code but what modes are these are still elusive TODO: research this)
	info_gamepad->switch_mode = NULL; // NOTE: only needed for devices that can switch reporting relative to absolute position
	info_gamepad->dev = NULL; // NOTE: set by AddInputDevice()
	info_gamepad->private = NULL;
	info_gamepad->type_name = XI_JOYSTICK;
	if (info_gamepad->drv) {
		xf86Msg(X_DEBUG, "[%s] driver: %s\n", GAMEPAD_DRIVER_NAME, info_gamepad->drv->driverName);
	} else {
		// NOTE: should have been set by xf86AddInput() right before calling this PreInit function; see call stack: NewInputDeviceRequest() -> xf86NewInputDevice() -> xf86AddInput() -> drv->PreInit()
		xf86Msg(X_DEBUG, "[%s] driver: %s\n", GAMEPAD_DRIVER_NAME, "UNKNOWN");
		return BadImplementation;
	}
	struct _ModuleDesc *module = (typeof(module)) info_gamepad->drv->module;
	struct _GamepadDriverRec *data = module->TearDownData;

	char *devname = (char*) (data->base + data->offset_devname);

	if (!data->size_devname) {
		xf86Msg(X_DEBUG, "[%s] error: device name was not set during driver setup %s\n", GAMEPAD_DRIVER_NAME);
		return BadImplementation;
	}
	else {
		xf86Msg(X_DEBUG, "[%s] device: %s\n", GAMEPAD_DRIVER_NAME, devname);
	}
	if (!(info_gamepad->flags & XI86_SERVER_FD)) {
		if (-1 == info_gamepad->fd) {
			// NOTE: this is surprising because xf86AllocateInput() sets fd = -1 on the xserver side and also we want to know if this get called with a valid file descriptor
			xf86Msg(X_DEBUG, "[%s] fd: %d\n", GAMEPAD_DRIVER_NAME, info_gamepad->fd);
			// TODO lookup /dev/input/eventX for a gamepad and then open file descriptor to the device; this is needed because at this point the caller has tried openning the device but was unable to do so because we are trying to support platforms without systemd. One last thing you don't need to set the options yet that is private to the gamepad (the keyboard probably does not need to know this since it is private data)
			return BadImplementation;
		}
	}

	info_keyboard = GamepadKeyboardHotplug(info_gamepad, flags);
	return BadImplementation;
}

// TODO: impl Core functions
_X_EXPORT struct _InputDriverRec GAMEPAD = {
    .driverVersion = 1,
    .driverName = GAMEPAD_DRIVER_NAME,
    .Identify = NULL,
    .PreInit = NULL,
    .UnInit = NULL,
    .module = NULL,
    .default_options = NULL,
#ifdef XI86_DRV_CAP_SERVER_FD
    .capabilities = 0
#endif
};

// TODO make sure that you first check that `p` is a valid pointer and that `base` and `size` are not zero
static void GamepadDriverTeardown(void *p)
{
	errno = 0;
	struct _GamepadDriverRec *priv = (typeof(priv)) p;
	void *base = (typeof(base)) priv->base;
	uint64_t size = (typeof(size)) priv->size;
	int rc = munmap(base, size);
	if (-1 == rc) {
		xf86Msg(X_ERROR, "[%s] error: failed to unmap driver data\n", GAMEPAD_DRIVER_NAME);
		if (errno) {
			xf86Msg(X_ERROR, "[%s] error: %s\n", GAMEPAD_DRIVER_NAME, strerror(errno));
		}
	}
	return;
}

static XF86ModuleVersionInfo ModuleVersionGamepad = {
	.modname = GAMEPAD_MODULE_NAME,
	.vendor = MODULEVENDORSTRING,
	._modinfo1_ = MODINFOSTRING1,
	._modinfo2_ = MODINFOSTRING2,
	.xf86version = XORG_VERSION_CURRENT,
	.majorversion = PACKAGE_VERSION_MAJOR,
	.minorversion = PACKAGE_VERSION_MINOR,
	.patchlevel = PACKAGE_VERSION_PATCHLEVEL,
	.abiclass = ABI_CLASS_XINPUT,
	.abiversion = ABI_XINPUT_VERSION,
	.moduleclass = MOD_CLASS_XINPUT,
	.checksum = {}
};

static int IsEventDevice(struct dirent const *dir)
{
	int rc = 0;
	char event[] = "event";
	uint64_t const len = (sizeof(event) - 1);
	rc = strncmp(dir->d_name, event, len);
	return ((0 == rc)? 1 : 0);
}

static int GamepadGetDeviceName(struct _GamepadDriverRec *drv)
{
	errno = 0;
	int64_t rc = 0;
	struct dirent **namelist = NULL;
	char *gamepad = NULL;
	rc = scandir("/dev/input", &namelist, IsEventDevice, alphasort);
	if (-1 == rc) {
		xf86Msg(X_ERROR, "[%s] error: scandir failed to find events\n", GAMEPAD_DRIVER_NAME);
		if (errno) {
			xf86Msg(X_ERROR, "[%s] %s\n", GAMEPAD_DRIVER_NAME, strerror(errno));
		}
		return BadRequest;
	}

	int const devno = rc;
	for (int i = 0; i != devno; ++i) {
		errno = 0;
		char devname[sizeof(struct dirent) << 1] = "/dev/input/";
		strcat(devname, namelist[i]->d_name);
		int const fd = open(devname, O_RDONLY);
		if (-1 == fd) {
			xf86Msg(X_ERROR, "[%s] error: failed to open: %s\n", GAMEPAD_DRIVER_NAME, devname);
			if (errno) {
				xf86Msg(X_ERROR, "[%s] %s\n", GAMEPAD_DRIVER_NAME, strerror(errno));
			}

			for (int i = 0; i != devno; ++i) {
				free(namelist[i]);
			}
			free(namelist);
			return BadRequest;
		}

		errno = 0;
		char dev[sizeof(struct dirent) << 1];
		memset(dev, 0, sizeof(dev));
		uint64_t const len = sizeof(dev);
		rc = ioctl(fd, EVIOCGNAME(len), dev);
		if (-1 == rc) {
			xf86Msg(X_ERROR, "[%s] error: failed to query name of: %s\n", GAMEPAD_DRIVER_NAME, devname);
			if (errno) {
				xf86Msg(X_ERROR, "[%s] %s\n", GAMEPAD_DRIVER_NAME, strerror(errno));
			}

			for (int i = 0; i != devno; ++i) {
				free(namelist[i]);
			}
			free(namelist);
			return BadRequest;
		}

		errno = 0;
		uint64_t bit[NBITS(KEY_CNT)];
		uint64_t code[NBITS(KEY_CNT)];
		memset(bit, 0, sizeof(bit));
		memset(code, 0, sizeof(code));
		rc = ioctl(fd, EVIOCGBIT(0, NBYTES(KEY_CNT)), bit);
		if (-1 == rc) {
			xf86Msg(X_ERROR, "[%s] error: failed to probe the bits of: %s\n", GAMEPAD_DRIVER_NAME, devname);
			if (errno) {
				xf86Msg(X_ERROR, "[%s] %s\n", GAMEPAD_DRIVER_NAME, strerror(errno));
			}

			for (int i = 0; i != devno; ++i) {
				free(namelist[i]);
			}
			free(namelist);
			return BadRequest;
		}

		if (test_bit(EV_KEY, bit)) {
			errno = 0;
			rc = ioctl(fd, EVIOCGBIT(EV_KEY, NBYTES(KEY_CNT)), code);
			if (-1 == rc) {
				xf86Msg(X_ERROR, "[%s] error: failed to probe the bits of gamepad: %s\n", GAMEPAD_DRIVER_NAME, devname);
				if (errno) {
					xf86Msg(X_ERROR, "[%s] %s\n", GAMEPAD_DRIVER_NAME, strerror(errno));
				}

				for (int i = 0; i != devno; ++i) {
					free(namelist[i]);
				}
				free(namelist);
				return BadRequest;
			}
			if (test_bit(BTN_GAMEPAD, code)) {
				xf86Msg(X_DEBUG, "[%s] detected gamepad: %s\n", GAMEPAD_DRIVER_NAME, devname);
				char *gamepad = (char*) (drv->base + drv->offset_devname);
				uint64_t const len = strlen(devname);
				drv->size_devname = (1 + len);
				if (PATH_MAX <= len) {
					xf86Msg(X_DEBUG, "[%s] device name length is greater than or equal to PATH_MAX: %d\n", GAMEPAD_DRIVER_NAME, PATH_MAX);
					return BadRequest;
				}
				strncpy(dev, devname, len);
				gamepad[len] = 0;
				close(fd);
				break;
			}
		}

		close(fd);
	}

	for (int i = 0; i != devno; ++i) {
		free(namelist[i]);
	}
	free(namelist);

	return Success;
}

// the pointer type is defined as typedef void* pointer in /usr/include/X11/Xdefs.h
static void *GamepadDriverSetup(
	void *module,
	void *options,
	int *errmaj,
	int *errmin
) {
    errno = 0;
    int64_t rc = sysconf(_SC_PAGESIZE);
    if (-1 == rc) {
	xf86Msg(X_ERROR, "[%s] error: failed to query the pagesize", GAMEPAD_DRIVER_NAME);
	if (errno) {
	    xf86Msg(X_ERROR, "[%s] error: %s\n", GAMEPAD_DRIVER_NAME, strerror(errno));
	}
	return NULL;
    }
    uint64_t const pagesz = (typeof(pagesz)) rc;
    void *base = mmap(NULL, pagesz, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    errno = 0;
    if (MAP_FAILED == base) {
	xf86Msg(X_ERROR, "[%s] error: failed to map driver data", GAMEPAD_DRIVER_NAME);
	if (errno) {
	    xf86Msg(X_ERROR, "[%s] error: %s\n", GAMEPAD_DRIVER_NAME, strerror(errno));
	}
        return NULL;
    }

    struct _GamepadDriverRec *priv = (typeof(priv)) base;
    priv->base = (uintptr_t) base;
    priv->size = (typeof(priv->size)) pagesz;
    priv->device.driver = priv;
    priv->offset_devname = ((sizeof(*priv) + 63) + ~63);
    priv->size_devname = 0;

    if ((sizeof(*priv) + PATH_MAX) > pagesz) {
	    xf86Msg(X_ERROR, "[%s] error: insufficient memory-map size\n", GAMEPAD_DRIVER_NAME);
	    return NULL;
    }

    rc = GamepadGetDeviceName(priv);
    if (Success != rc) {
	    xf86Msg(X_ERROR, "[%s] error: failed to get device name\n", GAMEPAD_DRIVER_NAME);
	    return NULL;
    }

    if (!priv->size_devname) {
	    xf86Msg(X_ERROR, "[%s] error: failed to get the size of the name of the device\n", GAMEPAD_DRIVER_NAME);
	    return NULL;
    }
    else if (PATH_MAX < priv->size_devname) {
	    xf86Msg(X_ERROR, "[%s] error: size of the name of the device is greater than PATH_MAX: %d\n", GAMEPAD_DRIVER_NAME, PATH_MAX);
	    return NULL;
    }

    void *TearDownData = base;
    xf86AddInputDriver(&GAMEPAD, module, 0);
    return TearDownData;
}

static void GamepadKbdCtrl(
	struct _DeviceIntRec *DevGamepad,
	KeybdCtrl *ctrl
) {
	struct _KeybdCtrl *p = (typeof(p)) ctrl;
	return;
}

// TODO: void *priv_gamepad -> struct _GamepadDev *priv_gamepad, you must define `struct _GamepadDev` yourself based on xf86-input-joystick's `jstk.h`
static int GamepadInitKeys(
	struct _DeviceIntRec *DevGamepad,
	void *PrivDevGamepad
) {
	int rc = 0;
	struct _XkbSrvInfo *info = NULL;
	struct _XkbControls ctrls = {};
	rc = InitKeyboardDeviceStruct(DevGamepad, NULL, NULL, GamepadKbdCtrl);
	return 0;
}

_X_EXPORT XF86ModuleData gamepadModuleData = {
    .vers = &ModuleVersionGamepad,
    .setup = GamepadDriverSetup,
    .teardown = GamepadDriverTeardown
};

/*
int main()
{
	int64_t rc = 0;
	char *devname_gamepad = GamepadGetDeviceName();
	if (!devname_gamepad) {
		fprintf(stdout, "%s\n", "no gamepad devices were found");
		_exit(0);
	}

	fprintf(stdout, "%s\n", devname_gamepad);
	int fd = open(devname_gamepad, O_RDONLY);
	if (-1 == fd) {
		fprintf(stderr, "error: failed to open gamepad: %s\n", devname_gamepad);
		if (errno) {
			fprintf(stderr, "%s\n", strerror(errno));
		}
		free(devname_gamepad);
		_exit(1);
	}

	int version = -1;
	rc = ioctl(fd, EVIOCGVERSION, &version);
	int const major_version = ((version >> 16) & 0xff);
	int const minor_version = ((version >>  8) & 0xff);
	int const patch_version = ((version >>  0) & 0xff);
	fprintf(stdout, "driver-version: %d.%d.%d\n", major_version, minor_version, patch_version);

	struct input_id iid = {};
	rc = ioctl(fd, EVIOCGID, &iid);
	if (-1 == rc) {
		fprintf(stderr, "%s\n", "error: failed to get gamepad input ID");
		if (errno) {
			fprintf(stderr, "%s\n", strerror(errno));
		}
		free(devname_gamepad);
		_exit(1);
	}

	uint64_t bits[NBITS(KEY_CNT)];
	memset(bits, 0, sizeof(bits));
	rc = ioctl(fd, EVIOCGBIT(EV_KEY, NBYTES(KEY_CNT)), bits);
	if (-1 == rc) {
		fprintf(stderr, "%s\n", "error: failed to probe key events");
		if (errno) {
			fprintf(stderr, "%s\n", strerror(errno));
		}
		free(devname_gamepad);
		_exit(1);
	}

	int buttons = 0;
	for (int32_t code = BTN_GAMEPAD; code != BTN_DIGI; ++code) {
		if (test_bit(code, bits)) {
			++buttons;
		}
	}
	fprintf(stdout, "buttons: %d\n", buttons);

	int dpad_buttons = 0;
	for (int32_t code = BTN_DPAD_UP; code != KEY_ALS_TOGGLE; ++code) {
		if (test_bit(code, bits)) {
			++dpad_buttons;
		}
	}
	fprintf(stdout, "dpad-buttons: %d\n", dpad_buttons);

	memset(bits, 0, sizeof(bits));
	rc = ioctl(fd, EVIOCGBIT(EV_ABS, NBYTES(ABS_CNT)), bits);
	int axes = 0;
	for (int32_t code = ABS_X; code != ABS_PRESSURE; ++code) {
		if (
			(ABS_THROTTLE == code) ||
			(ABS_RUDDER == code) ||
			(ABS_WHEEL == code) ||
			(ABS_GAS == code) ||
			(ABS_BRAKE == code) ||
			0
		   ) {
			continue;
		}
		if (test_bit(code, bits)) {
			++axes;
		}
	}
	fprintf(stdout, "axes: %d\n", axes);

	// NOTES: disables unsupported events for the xf86 driver
	uint64_t codes[NBITS(ABS_CNT)];
	memset(codes, 0, sizeof(codes));
	codes[0] |= (1 << ABS_HAT0X) | (1 << ABS_HAT0Y);
	struct input_mask im = {};
	im.type = EV_ABS;
	im.codes_ptr = (uint64_t) codes;
	im.codes_size = NBYTES(ABS_CNT);
	rc = ioctl(fd, EVIOCSMASK, &im);
	if (-1 == rc) {
		fprintf(stderr, "%s\n", "error: failed to set input-masks");
		if (errno) {
			fprintf(stderr, "%s\n", strerror(errno));
		}
		free(devname_gamepad);
		_exit(1);
	}

	struct input_event ev = {};
	while (1) {
		rc = read(fd, &ev, sizeof(ev));
		if (-1 == rc) {
			fprintf(stderr, "%s\n", "error: failed to read event");
			if (errno) {
				fprintf(stderr, "%s\n", strerror(errno));
			}
			free(devname_gamepad);
			_exit(1);
		}
		int64_t code = ev.code;
		int64_t type = ev.type;
		int64_t value = ev.value;
		if ((type & EV_ABS) == EV_ABS) {
			fprintf(stdout, "AXIS: type: 0x%lx code: 0x%lx value: 0x%lx\n", type, code, value);
		}
		else if ((type & EV_KEY) == EV_KEY) {
			fprintf(stdout, "BUTTON: type: 0x%lx code: 0x%lx value: 0x%lx\n", type, code, value);
		}
		else {
			fprintf(stdout, "OTHER: type: 0x%lx code: 0x%lx value: 0x%lx\n", type, code, value);
		}
		if (BTN_MODE == code) {
			fprintf(stdout, "%s\n", "quitting upon user request");
			break;
		}
	}

	// NOTE: the following code is experimental and belongs in its own function
	// TODO: impl PreInit and UnInit functions and see if you need any of the others
	struct _InputDriverRec driver = {};
	driver.driverVersion = ((1 << 16) & 0xff) | ((0 << 8) & 0xff) | (0 & 0xff);
	driver.driverName = "gamepad";
	driver.PreInit = NULL;
	driver.UnInit = NULL;
	driver.module = NULL;
	driver.default_options = NULL;
	driver.capabilities = 0;
	fprintf(stdout, "xf86-driver-gamepad-version: %d\n", driver.driverVersion >> 16);
	free(devname_gamepad);
	return 0;
}

// sample code for creating a symbolink to the /dev/input/eventX device
// maybe it would useful to be able to create a symbolic link to the gamepad so here's a sample code that shows how to do that

#include <linux/input.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>

#define TARGET "TARGET_DEVICE"

int main()
{
	char buf[1024];
	memset(buf, 0, sizeof(buf));

	errno = 0;
	int rc = readlink(TARGET, buf, sizeof(buf));
	if (-1 == rc) {
		fprintf(stderr, "%s\n", strerror(errno));
		_exit(1);
	}

	char *match = NULL;
	match = strstr(buf, "event");
	if (!match) {
		fprintf(stderr, "error: no 'event' substring in device name\n");
		_exit(1);
	}

	char devname[1024] = "/dev/input/";
	strcat(devname, match);
	fprintf(stdout, "device: %s\n", devname);

	errno = 0;
	int fd = open(devname, O_RDONLY);
	if (-1 == fd) {
		fprintf(stderr, "%s\n", strerror(errno));
		_exit(1);
	}

	char name[1024];
	memset(name, 0, sizeof(name));

	errno = 0;
	memset(name, 0, sizeof(name));
	rc = ioctl(fd, EVIOCGPHYS(sizeof(name)), name);
	if (-1 == rc) {
		fprintf(stderr, "%s\n", strerror(errno));
		_exit(1);
	}
	fprintf(stdout, "%s\n", name);

	match = strstr(name, "/");
	if (!match) {
		fprintf(stderr, "error: no '/' in string\n");
		_exit(1);
	}

	char link[1024] = "/dev/input/by-path/";
	strncat(link, name, match - name);
	fprintf(stdout, "link: %s\n", link);

	errno = 0;
	rc = symlink(devname, link);
	if (-1 == rc) {
		fprintf(stderr, "%s\n", strerror(errno));
		_exit(1);
	}

	close(fd);
	return 0;
}

*/

// XF86 Driver Notes:
// from the xf86-input-joystick we know that the we have to impl GamepadKeyboardDeviceControlProc() function to handle the different levels of operation: DEVICE_INIT, DEVICE_ON, DEVICE_OFF, and DEVICE_CLOSE
//
// DEVICE_INIT is the most critical one to have a functional driver (not that the other modes are not important but are trivial to implement in comparison). First we have to intialize focus for the device and then initialize the keys. However from reading at the impl of InitKeyboardDeviceStruct it calls InitFocusClassDeviceStruct() if dev->focus is NULL. Need to understand if initializing focus ahead is a requirement for our driver or if we can simply let the InitKeyboardDeviceStruct() handle this to simplify our end if the effect is the same. So the question I need to answer is if order is crucial here.
//
// InitFocusDeviceStruct is device-independent (a dix) and so it is implemented in xserver/dix/devices.c.
//
// InitFocusClassDeviceStruct() requires a device so (dev cannot be NULL)  it gets a pointer to the root window, the current time, and binds the focus to the device after allocating it. I think it's safe to let InitKeyboardDeviceStruct() call this for us. Why I think this way because the xserver is a huge codebase that has changed over many years and there are about 20 years between what I see in this code and the current state of the xserver. And so I find it worthwhile to try to let InitKeyboardDeviceStruct() initialize the device focus (dev->focus). Most importantly initializing the device focus must happen only once; the impl checks for that.
//
//
// Find out how they set the joystick->device (string) and this is important to know because what you are doing is searching for a gamepad in /dev/input and I want to know what they do instead. Do they use /dev/input/js0? probably but what if there are other joysticks?

// TRACE:
// jstkDeviceControlProc() calls jstkOpenDevice() on initialization and jstkOpenDevice calls priv->open_proc which is the open function according to the backend (evdev, /dev/input/js0, or BSD). But interestingly this happens if probe is False. Nevertheless jstkOpenDevice() function either calls the priv->open_proc or the backend which are the same. If probe is false then no call xf86Msg() (a logger) is done. This means the following:
//
// device (name of the joystick device must be known then)
// if the file descriptor data member is not set it's set by this call along with the open, close, and read function pointers.
//
// It seems that jstkCorePreInit is the one function that sets the device name `device`
// That functions tries first to set the device name via xf86SetStrOption() with string "Dvice" then fallsback to calling it with string "Path". If that fails the driver aborts.
//
// Diving into xf86, xf86SetStrOption() -> LookupStrOption() -> ParseOptionValue(scrnIndex= -1, options = optlist, OptionInfo p = &o, markUsed = True) -> xf86findOptionValue() -> xf86findOption()
//
// o.type = OPTV_STRING o.name = name (either "Device" or "Path")
//
// at the deepest level xf86findOption() does a case insensitive character comparison as it traverses the given option list (a linked-list and its type is XF86OptionPtr).
//
// I verified that pInfo->options is of type XF86OptionPtr (of course it would not have compiled):
//
//typedef struct _XF86OptionRec {
//    GenericListRec list;
//    const char *opt_name;
//    const char *opt_val;
//    int opt_used;
//    const char *opt_comment;
//} XF86OptionRec;
//
//typedef struct _InputOption *XF86OptionPtr;
//
// and also found this which freedesktop developers strongly discourage the use of the generic list (a singly linked-list talk about tech debt it's use by xf86 at its core and they could not get rid of it)
//
//typedef struct generic_list_rec {
//    void *next;
//} GenericListRec, *GenericListPtr, *glp;
//
//
// Nevertheless, the parser checks the returned string could be an empty string and if it is a debug warning message is logged (possibly to the console) about not having a valid string. And then it return False to indicate that it was not found. If we have a valid string then it will be equal to either "Device" or "Path" depending on the attempt that succeeds.
//
// noted that ParseOptionValue() returns as it names suggest the value that correspond to the option in this case it could be /dev/input/jsX or /dev/input/eventX
//
//
// FINDING OUT WHAT LOADS OUR DRIVER
//
// dix_main() -> InitOutput -> ? -> xf86LoadModules() -> LoadModule()
//
// InitOutput() calls the following functions that I should read:
//
// - xf86ModulelistFromConfig()
// - xf86DriverlistFromConfig()
// - xf86InputDriverlistFromConfig()
//
// both of these functions return a (char**) modulelist
//
// an important discovery is the modreq module-requirements is optional so if you pass NULL the xserver won't complain about version or ABI compatibility.
//
//
//
// We probably want to look at InitInput() for input devices
//
// the important global is the `inputs` data member of (struct _serverlayoutrec) xf86ConfigLayout:
//
// typedef struct _serverlayoutrec {
//     const char *id;
//     screenLayoutPtr screens;
//     GDevPtr inactives;
//     InputInfoRec **inputs;
//     void *options;
// } serverLayoutRec, *serverLayoutPtr;
//
//
//
//
// There's a global configuration file which populates the (struct _serverlayoutrec) xf86ConfigLayout. This is done in xf86HandleConfigFile()
//
// Interestingly it is called by InitOutput() and InitOuput() is called before InitInput() in dix_main().
//
//
// xf86HandleConfigFile() it is also called by DoConfigure()
//
// both functions set autoconfig to False
//
//
//
// xf86HandleConfigFile() call stack:
//
// - xf86initConfigFiles()
// - xf86openConfigDirFiles() and xf86openConfigFile()
//
//
// xf86openConfigFile() calls OpenConfigFile(path, cmdline, projroot, XCONFIGFILE)
// where XCONFIGFILE apparently is defined in xorg-config.h when compiling it but if that's not available (because this was done by maintainer in most cases) you can look at the system header: /usr/include/xorg/xorg-server.h but you will have to install the development files for xorg to get it:
//
//
// Location of configuration file
// #define XCONFIGFILE "xorg.conf"
//
//
// don't forget the xf86openConfigDirFiles() was called with path set to look in all the standard locations for the xorg configuration files in the system (this is a string of paths delimited by commas)
//
// this one xf86openConfigDirFiles() calls OpenConfigDir(path, cmdline, projroot, XCONFIGDIR)
//
// where XCONFIGDIR is probably /etc/X11/xorg.conf.d or maybe just `xorg.conf.d` unfortunately this is only known at build time but `xorg.conf.d` is a safebet.
//
//
// In my system the DATADIR is set to `/usr/share/` (I deduced this from reading the source code and the meson.build files and by locating the file 10-quirks.conf and this led the solution)
//
// /usr/share/X11/xorg.conf.d/10-quirks.conf
//
// so /usr/share must be the DATADIR which is used when %D escape sequence is found in DoSubstitution()
//
// and `xorg.conf.d` must be the XCONFIGDIR which may be replaced when %X escape sequence is found, note that encoutering %X does not mean that it resolves to XCONFIGDIR it could also resolve to XCONFIGFILE because this is an argument of DoSubstitution()
//
//
// After DoSubstitution() it returns a string, and if called by OpenConfigDir() the following happens:
//
// - calls scandir() to search for *.conf files (like the 10-quirks.conf file; you may want to look at the impl but it essentially uses a strcmp by looking at the bytes that correspond to the file extenstion or suffix .conf and that's how they do this without the need of regular expressions)
// - if scandir() returns at least one matching .conf file the `dirpath` is added via AddConfigDirFiles(); however the AddConfigDirFiles() function appears to do more than just adding it because the caller checks the return value in anticipation that it could fail (probably because this function is used in other contexts where it is not known in advance)
//
//
//
// AddConfigDirFiles() interestingly opens all the .conf files in the config directory and keeps a record of them (both the path and FILE*) in `configFiles` struct (not a global in the sense that other source files can access it but it is in the global scope of scan.c)
//
//static struct {
//    FILE *file;
//    char *path;
//} configFiles[CONFIG_MAX_FILES];
//
//
// what OpenConfigDir() returns is the `dirpath` that hosts the .conf files. This is what xf86openConfigDirFiles() also returns to the caller. The result is stored in sysdirname and `dirname` of course these placeholders store different locations. I am certain that `sysdir` should resolve to /usr/share/X11/xorg.conf.d/ in my system.
//
//
// xf86HandleConfigFile() checks for elevated privileges, if running as root it means that the root user started the xserver, otherwise it was a regular user that used sudo to elevate its privileges. In the latter case the xserver only allows a smaller set of paths to configure the server. The ALL_CONFIGPATH and ALL_CONFIGDIRPATH are defined in xf86Config.c.
//
// calls xf86initConfigFiles() which basically sets indexes to zero
// xf86openConfigDirFiles(SYS_CONFIGDIRPATH, NULL, PROJECTROOT) both parameters are defined in the xf86Config.c source file.
//
// it calls many open functions but this one caught my attention:
//
// xf86openConfigFile() -> OpenConfigFile() to open the xorg config file and this is stored in the global variable (char*) xf86ConfigFile. It's worth mentioning that as OpenConfigDir() the search stops on the first match and the search is hierarchical meaning that the firs paths is more important than those that follow. It is also important to know that the command-line searchpaths both absolute (%A) and relative (%R espcape sequence) take precedence over standard system locations such as /etc/X11/xorg.conf.
//
//
// eventually xf86HandleConfigFile() calls xf86readConfigFile().
//
// xf86readConfigFile() starts by processing tokens, and to the best of my understanding, a token could be an entire line (for example even a comment in the config file for these are stored in the server). This work is done by the function xf86getToken()
//
// xf86getToken() returns an enum to indicate what the token is and there are many. At least in the context of readConfigFile we are looking for the SECTION token. And this means that the COMMENT section of the config files *.conf is stored until we hit the SECTION. And this were I still have work to do.
//
// It is important to bear in mind that xf86getToken() returns numbers right away but there is reading to do because there's more to just returning early. In fact as soon as a token is identified it is returned. We know that the caller expects to process the comment section and then the section SECTION (example "inputclass").
//
// It is also important to know that as xf86getToken() is called the config files are read. These are all the config files that we found earlier either at the standard or commad-line specified paths.
//
// constrary to my expectation xf86addComment() function appends the comments to a string which gets reallocated as more space is needed. If you were to print this string on the console it would look like the comments in the .conf file but without leading whitespace.
//
// at the level of xf86readConfigFile() the tokens can be COMMENT or SECTION otherwise it generates a parsing error message. So what happens is that if the token itself could be a single word as well and this happens when it's not a COMMENT or STRING or a NUMBER.
//
// Ok so when you find the word Section it returns SECTION and then it needs to check if the word Section is followed by a string (example Section "inputclass"). If it's not followed by a string literally it checks for "\"" then there is a parse error. And this check is necessary to know if it makes sense to continue processing the .conf file. There might be more to just this but at least this is who they implemented this check.
//
//
// This is where the interesting things probably happen (implemented by the parser) in xf86readConfigFile() but have yet to read the code in between.
//
//            else if (xf86nameCompare(xf86_lex_val.str, "inputclass") == 0) {
//                free(xf86_lex_val.str);
//                xf86_lex_val.str = NULL;
//                HANDLE_LIST(conf_inputclass_lst,
//                            xf86parseInputClassSection, XF86ConfInputClassPtr);
//            }
//
// where inputclass is the class for input devices.
//
// the macro fun:
//
//#define HANDLE_LIST(field,func,type)\
//{\
//type p = func ();\
//if (p == NULL)\
//{\
//        CLEANUP (ptr);\
//        return NULL;\
//}\
//else\
//{\
//        ptr->field = (type) xf86addListItem ((glp) ptr->field, (glp) p);\
//}\
//}
//
// NOTE conf_input_lst is tied to the global xf86configptr, if you look closely to the HANDLE_LIST() function macro you will see that it dereferences the pointer `ptr` and that pointer is allocated by xf86readConfigFile().
//
// calls xf86addListItem() and this as the name suggests add item to the list () conf_input_lst which is probably a data member of (XF86ConfigPtr) xf86configptr.
//
// This is the typedef that tells us the definition of xf86configptr:
//
//typedef struct {
//    XF86ConfFilesPtr conf_files;
//    XF86ConfModulePtr conf_modules;
//    XF86ConfFlagsPtr conf_flags;
//    XF86ConfVideoAdaptorPtr conf_videoadaptor_lst;
//    XF86ConfModesPtr conf_modes_lst;
//    XF86ConfMonitorPtr conf_monitor_lst;
//    XF86ConfDevicePtr conf_device_lst;
//    XF86ConfScreenPtr conf_screen_lst;
//    XF86ConfInputPtr conf_input_lst;
//    XF86ConfInputClassPtr conf_inputclass_lst;
//    XF86ConfOutputClassPtr conf_outputclass_lst;
//    XF86ConfLayoutPtr conf_layout_lst;
//    XF86ConfVendorPtr conf_vendor_lst;
//    XF86ConfDRIPtr conf_dri;
//    XF86ConfExtensionsPtr conf_extensions;
//    char *conf_comment;
//} XF86ConfigRec, *XF86ConfigPtr;
//
//
// finally we have reached the point where we will know what the xserver does with inputclass devices.
//
// xf86parseInputClassSection(): in this section the xserver populates the conf_inputclass_lst and returns it to the caller.
//
//
// so interestingly xf86parseInputClassSection() uses a macro function to declare a pointer and this is why it looks at first as if working on global data but it's not:
//
//
//#define parsePrologue(typeptr,typerec) typeptr ptr; \
//if( (ptr=calloc(1,sizeof(typerec))) == NULL ) { return NULL; }
//
// where  typeptr = XF86ConfInputClassPtr typerec = XF86ConfInputClassRec,
//
// and as mentioned before this is the type of `conf_inputclass_lst` so we are reading a key function here related to our driver.
//
// then it starts reading tokens from the inputclass config file
//
// if the token is of kind DRIVER
// case DRIVER:
//     if (xf86getSubToken(&(ptr->comment)) != STRING)
//         Error(QUOTE_MSG, "Driver");
//     if (strcmp(xf86_lex_val.str, "keyboard") == 0) {
//         ptr->driver = strdup("kbd");
//         free(xf86_lex_val.str);
//     }
//     else
//         ptr->driver = xf86_lex_val.str;
//     break;
//
// this means that in our case ptr->driver = "joystick"
//
// it also sets the identifier ptr->identifier and sets has_ident(ifier) to True
//
// in our case ptr->identifier is set to "joystick-all" in accordance to the config file
//
// this is also where the options are set as well (all the configuration file is parsed);
// in particular it sets `match_device` field in our case "/dev/input/event*" and I suppose that matching supports globbing or regex (have yet to look at this). So far what I have seen is that it uses strstr() to determine a device match and other (such as product, vendor, etc.).
//
// TODO DEVICE MATCHING
// determine how the xserver does device matching
//
// Probably you may want to look into setting `major` and `minor` fields of pInfo to zero to use xf86stat(); however this only sets the major and minor fields using linux `stat()` and these are both set to the Device ID (assuming that it's a special device which it should be). Nevertheless, this still means that "Device" must be set option. If it's not set by the time this function is called if the device has capabilities set to XI86_DRV_CAP_SERVER_FD, as in the joystick driver that you are using as reference it uses systemd tools.
//
//
// The reason that we in our driver code do not set the pInfo->name field is because it is set by NewInputDeviceRequest(), it will use whatever we supplied in options (either the "name" or "identifier" option). If we supply a `name` field to pInfo (before calling NewInputDeviceRequest) it will return a BadRequest.
//
//
// TODO
// Read the code again to see if it sets `xf86ConfigLayout.inputs` because it is needed when checking the count of input-drivers. Note that the "inputclass" registers a driver and so it should be in the list unless I am missing the mark here.
//
// I did not get to read any specifics. I did see that the xserver checks for the keyboard and pointer and if not configured (properly) the xserver might not find any. You are encouraged to read checkCoreInputDevices() because this is where these checks are done.
//
//
// ActivateDevice() calls control procedure function with DEVICE_INIT
//
// EnableDevice() calls control procedure function with DEVICE_ON
//
// both of these functions are implemented in dix/devices.c
//
//
// WHAT FOLLOWS NEEDS TO BE MERGED WITH WHAT I WROTE ABOVE
// evntually the xf86HandleConfigFile() checks the config files among other things:
//
// we are going to study configFiles() to see if we find somethig that gives us a clue here; not what I expected.
//
//
//
// The most misleading name for a function macro (does not parse but allocates instead):
//
//#define parsePrologue(typeptr,typerec) typeptr ptr; \
//if( (ptr=calloc(1,sizeof(typerec))) == NULL ) { return NULL; }
//
//
//struct xorg_list {
//   struct xorg_list *next, *prev;
//};
//
//
//static inline void
//xorg_list_init(struct xorg_list *list)
//{
//    list->next = list->prev = list;
//}
//
// this just sets the lits to empty lists
//
//
// TODO write about getToken since you have read the source code.
// Understand getToken to understand how inputclass configs are parsed (impl in parser/scan.c)
//
//
// ABI
//
// The xserver can ignore ABI mismatches if configured to do this via:
//
// xf86Info.ignoreABI
//
// if you get into trouble you know you may try to tell the xserver to ignore it. This is checked in the InitOutput() function, however if needed I would have to find where this flag is set. Probably via command-line and so you need to read the man pages for the xserver probably or xinit or another tool that starts the xserver.
//
// LoaderSetPath() overrides the default module path and so you know that you can probably override that via command line. This is done in InitOutput() function.
//
//
//
// xf86ModulelistFromConfig(void ***optlist) -> modulelist (char **) loads configuration modules `conf_modules` for example it replaces the obsolete keyboard with kbd (linux) driver
//
// the caller function does pass &optlst where optlst is (void **) and so optlst is set by this function call

//
// xf86InputDriverlistFromConfig()
//
// this function checks the (InputInfoRec **inputs) `inputs` field of (struct _serverlayoutrec or serverLayoutRec) xf86ConfigLayout and simply copies the driver names into the (char **) modulearray.
//
// So here is where my Gamepad driver would be marked as a module.
//
// Then, xf86LoadModules would be called this way xf86LoadModules(modulelist, NULL) without an optlist because it's NULL. Because of this the function calls LoadModule(name, NULL, NULL, &errmaj), where (char *) `name` is the normalized name (maybe lowercase would have to read xf86NormalizeName())
//
// so it boils down to what LoadModule does:
//
// checks the module name, even though it would accept a filename (non-canonical), it expects a simple name or a fullpath name.
//
// These are the standard patterns in linux:
// {"^lib(.*)\\.so$",},
// {"(.*)_drv\\.so$",},
// {"(.*)\\.so$",},
//
// the driver sample provides a noncanonical name and that's okay because eventually the xserver searches for the module by doing a simple string comparison for any of the standard patterns. For example, if driver name is "gamepad" the module must be "gamepad.so" or gam"gamepad_drv.so" or "libgamepad.so". This work is done by FindModule(). Eventually the function calls LoaderOpen() which returns a a void* to the driver handle (*.so file).
//
//
// NOTE: LoaderSymbolFromModule() maybe expects the symbol to be gamepadModuleData;
//       if it fails you know that you must use `gamepadModuleData` instead.
//
// at the moment you have:
//
// nm gamepad.so | grep -i moduledata
// 0000000000004150 D GamepadModuleData
//
// from the module data it will check the versions and binds the DriverSetup and DriverTeardown functions, set the TearDownData and the VersionInfo.
//
// This is where the xserver calls the `SetupProc` function (in this case DriverSetup) to set the `TearDownData`, see the struct definition:
//
//typedef struct module_desc {
//    struct module_desc *child;
//    struct module_desc *sib;
//    struct module_desc *parent;
//    void *handle;
//    ModuleSetupProc SetupProc;
//    ModuleTearDownProc TearDownProc;
//    void *TearDownData;
//    const XF86ModuleVersionInfo *VersionInfo;
//} ModuleDesc, *ModuleDescPtr;
//
//
// Interestingly the SetupProc for the joystick driver simply returns the pointer to the struct module_desc (first input argument).
//
//
// xf86NewInputDevice() calls PreInit
//
// NewInputDeviceRequest() calls xf86NewInputDevice()
// also xf86InputEnableVTProbe calls xf86NewInputDevice()
//
// InitInput() -> NewInputDeviceRequest() -> xf86NewInputDevice()
//
// So this means that the device Plugin procedure function is probably called before the PreInit procedure function. The Plugin adds the driver
//
//
//
// Ok so reading the joystick driver again and noticed the following:
//
// It looks (as the author mentioned) that the PreInit procedure is run more than once. The first time it is run the PreInit code sets `device_control` and `read_input` function pointers and a couple of things. Then the hotplug function jstkKeyboardHotPlug() function is called. That's when the "_source", "_device/joystick" key-value pair option is set. And so when PreInit runs a second time it checks for that option to call jstkKeyboardPreInit() function. The hotplug procedure returns a keyboard_device.
//
//
// NewInputDeviceRequest():
//
// - allocates InputInfoPtr `pInfo` and initializes its fd = -1 and name = "UNKNOWN"
// - checks for the following options: "driver", "name" or "identifier", and "_source" among other possibilities but only listing the one that the joystick driver defines; if any of these options are missing NewInputDeviceRequest() fails.
// - because NewInputDeviceRequest() merges the attributes it makes sense to create a duplicate to pass to this function (this function has the side effect of destroying some of the attributes when merging and so the more reason for duplicating them)
// - checks if the inputclass should be ignored by traversing the XF86ConfInputClassPtr, it does not seem to me that this would trigger
// - checks that `name` and `driver` have been defined in `pInfo` and this is where the loop needs to be closed (meaning you should be able to answer this based on what you read).
// - then it calls xf86NewInputDevice()
//
// xf86NewInputDevice():
//
// - loads the driver by calling xf86LoadInputDriver()
// - it checks if the driver has a PreInit procedure (returns BadImplementation if not)
// - it checks for the device path in the "Device" option and this is done before calling the PreInit procedure
// - maybe I have found a BUG in the xserver invalid free() call if path is not set. And indeed if "Device" is not set path would be NULL. I wrote a simple test program that checks if trying to free(NULL) crashes the program but it did not (checked with valgrind also for memory errors and none were detected). So to my relief it is OK if "Device" is not set so that we can do that during PreInit. In fact we have to do that in PreInit. It is important to know what does the xserver need to know to determine if it is to manage the device file descriptor fd. Thus check for the flag XI86_SERVER_FD because your driver would be the one managing the file descriptor maybe. Have found that if the capabilities is set to XI86_DRV_CAP_SERVER_FD then that means that the server manages the file descriptor. Nothing else enables this flag.
// - deep in the callstack the it reports about core events in xf86ProcessCommonOptions() this is where "Floating" option in .conf appears and so you need to know what it means to send core events or not.
//
//
//
// xf86LoadInputDriver()
//
// - calls xf86LookupInputDriver() -- a simple lookup algorithm for the input-driver
//
// from InitInput() we see both `driver` and `name` are already set
//
//    for (pInfo = xf86ConfigLayout.inputs; pInfo && *pInfo; pInfo++) {
//        (*pInfo)->options =
//            xf86AddNewOption((*pInfo)->options, "driver", (*pInfo)->driver);
//        (*pInfo)->options =
//            xf86AddNewOption((*pInfo)->options, "identifier", (*pInfo)->name);
//        if (NewInputDeviceRequest((*pInfo)->options, NULL, &dev) == BadAlloc)
//            break;
//    }
//
// and we see that probably this is the first time the driver code starts being processed by the xserver due to the NewInputDeviceRequest() call.
//
// NOTES about xf86optionListMerge()
//
// to understand this function you must bear in mind that `ap` stands for the previous element of the list `a`, and similarly `bp` stands for the previous element of list `b`. If the `a` list runs out of elements it rewinds in some sense until the other list is fully traversed and checked for duplicate options. Priority is given to the inputclass options (the `a` list). It's useful to draw diagrams of the two lists as the algorithm does its job to not miss a thing of what the algorithm does (this is understanding by first principles).
//
// This is why it's important to read the xserver source code so that you can catch subtle errors.
//
//
// The last thing done during InitInput() is config_init() which probably calls config_udev() on my system. There you will see the creation of "device" option this does not mean that you have solved the mystery though you need to continue reading and understanding how all of this works. And surely you need to know about libudev, there are man pages but this is reference info only.
//
//
// Good news, some progress has been done.
// - It's not necessary to debug the Xserver during it startup sequence for troubleshooting the gamepad device driver.
// - when a device is plugged in the udev `socket_handler` handler adds the device, yes the xserver in linux probably uses epoll to check for udev events and so simply plugging in the gamepad to the USB will trigger a read (I have verified this).
// - during the call to `socket_handler` it will call NewInputDeviceRequest() and if we have added some configuration for our gamepad it will check the device attributes against match options in the gamepad configuration file. Here is were you need to be careful what you put in there because that alone might be enough for the device to be ignored. Meaning that the our driver won't be assigned to the device if there's a mismatch in any of those options. Recommendation is to run another GDB session to see what attributes you get and then decide what matches should you use. Have confirmed that udev properly detects the gamepad as a joystick device by checking the ATTR_JOYSTICK flag being set (flag is defined xserver/include/input.h).
//
//
// Have determined that the `device` field of the gamepad device attributes has been set to "/dev/input/js0" and so the MatchDevicePath pattern value must be set to "/dev/input/js*". This is probably the one reason the gamepad driver was not matched. Other than this there are no other matches in the config file that would make the xserver to ignore the gamepad driver.
//
// Other things that have been cleared out.
// - as mentioned earlier during class attributes merging the driver is going to be set to "gamepad", this is when pInfo->driver is set (we get a duplicate string of the drivername)
// - after the merge the code checks the pInfo->driver, assuming that the merge was successful (see previous comments) the xserver won't ignore the device.
// - calls xf86NewInputDevice() if form some reason it fails it must be because auto configuration is not enabled. see the passed argument is_auto which should be equal to one (truthfty value) becasue this device was discovered by udev.
// - within the driver you will need to replace the device string option to /dev/input/eventX so that you can use the event API instead of the legacy joystick API or keep it unchange but disable driver capabilities so that you own the file descriptor not the xserver.
