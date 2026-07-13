# xf86-input--gamepad
xf86 driver development for gamepads

This driver does not intend to handle analog signals (in the form of events) at this point, for now the driver is meant to handle events for action buttons and direction-pads (dpads).

**Development status**: writing exploratory code to learn about the Linux legacy joystick event handling.

## Development Progress

### Getting to know my Gamepad through system calls

Processing input events from the gamepad is far simpler than I imagined back when I started playing video games in GNU/Linux. Reading events is as simple as a system call `read`. If you want to read a single joystick event well you pass a pointer to a `struct js_event` instance and use `sizeof` to inform how many bytes the application intends to read.

Querying the name of the gamepad is as simple as issuing a `ioctl` system call. The header `linux/joystick.h` provides the macro definitions for each of the operations that you may want to do. For getting the name of the gamepad, there's the following macro:

```c
#define JSIOCGNAME(len)		_IOC(_IOC_READ, 'j', 0x13, len)
```

where `_IOC` is defined in `asm-generic/ioctl.h`:

```c
#define _IOC(dir,type,nr,size) \
        (((dir)  << _IOC_DIRSHIFT) | \
         ((type) << _IOC_TYPESHIFT) | \
         ((nr)   << _IOC_NRSHIFT) | \
         ((size) << _IOC_SIZESHIFT))
```

along with the other macros that you see, what matters is that the macro function is a convenient function for bit manipulation for the `ioctl` system call. It all boils down to the following code snippet:

```c
char name_gamepad[256];
memset(name_gamepad, 0, sizeof(name_gamepad));
rc = ioctl(fd, JSIOCGNAME(sizeof(name_gamepad)), name_gamepad);
```

of course you should check the returned code `rc` and `errno` values for runtime errors.

By looking at this I realized that when I plug in a gamepad the linux kernel probably uses the same `ioctl` call to log the name of the device in the kernel ring buffer. The kernel ring buffer can be obtained by executing the `dmesg` command. And to me this realization was a truly exciting moment (consider that this is the first time I am writing code to process events issued by the Linux kernel).

### Gamepad Mappings

Wrote an event-loop that processes joystick events as they happen (one at a time). The fields of the `struct js_event` are logged on the console along the button and axes mappings (commit [3ce9d33e](https://github.com/misael-diaz/xf86-input-gamepad/tree/3ce9d33e464258415847b83b1d5aeb47466b2129)).

## Requirements

Install the development files

```sh
apt install xorg-dev
```

you might also want to install the debugging symbols for the xserver as well:

```sh
apt install xserver-xorg-core-dbgsym
```

that alone did not work for me so I had to update the pkg list first:

```
printf "deb http://ddebs.ubuntu.com %s main restricted universe multiverse\n" focal{,-updates,-security,-proposed} | tee /etc/apt/sources.list.d/ddebs.list
```

```sh
apt update
```

and try again to obtain the debugging symbols.

The user that executes the code must be in the `input` group

if not you must add the user via (you must be root to do this):

```sh
usermod -aG input username
```

and then for this to take effect immediately (as a regular user):

```sh
newgrp input
```

## Config

minimal driver configuration

```
Section "InputClass"
	Identifier "gamepad"
	Driver "gamepad"
	MatchIsJoystick "on"
	MatchDevicePath "/dev/input/event*"
EndSection
```

## Build

The following command builds the driver as a shared object (\*.so) that the xserver can load:

```sh
gcc -I/usr/include/pixman-1 -Wall -Wextra -Wformat -fPIC -shared -O0 -gdwarf-4 -g main.c -o gamepad.so
```

## Debugging

The way I am debugging the driver is by starting the xserver with `gdbserver`:

```sh
gdbserver :2000 /usr/lib/xorg/Xorg :10 -keeptty
```

for this to work you need to issue that command from the console, which you can access by hitting any of the `Ctrl + Alt + Fx` keys, such as `Ctrl + Alt + F3`. To go back to your desktop environment you can use `Ctrl + Alt + F7`.

Note that `:2000` instructs `gdbserver` to listen on port `2000` and that the commands that follow `Xorg` are the arguments for `Xorg`, namely the display number `:10` and `-keeptty` so that the xserver won't detach from the controlling `tty` and be able to respond to keyboard signals (for instance see xf86ProcessArgument() in `xserver/hw/os-support/linux/lnx_init.c`).

On another machine you will need to run `gdb` to bind to the debugging session on the target machine.

My other Linux box is out of comission so I had to borrow a Window PC, download putty and loging to the target machine.

Once you have logged in via SSH you can run the debugger:

```sh
gdb
```

and from there issue the command

```gdb
target remote localhost:2000
```

here using `localhost` because I am logged in via SSH (from the Windows machine) to the target Linux machine.

from here on you will be able to setup your breakpoints and step through the code in the usual way albeit keep in mind that the code was compiled with `-g -O2` and so some of the code has been optimized away. If you want a closer experience you might have to compile the entire X11 project (which consists of hundreds of packages) yourself. Let's hope you don't need to do that.

## Linux Kernel Reference

A quick reference of the macros defined by the Linux kernel that are relevant for the development of this driver. The links provided below are for the the 5.4 version of the kernel.

### Bit Operations

From the Linux Kernel source blob [`include/linux/bitops.h`](https://github.com/torvalds/linux/blob/219d54332a09e8d8741c1e1982f5eae56099de85/include/linux/bitops.h):

```c
#define BITS_PER_TYPE(type) (sizeof(type) * BITS_PER_BYTE)
#define BITS_TO_LONGS(nr)       DIV_ROUND_UP(nr, BITS_PER_TYPE(long))
```

From the Linux Kernel source tree [`include/linux/bits.h`](https://github.com/torvalds/linux/blob/219d54332a09e8d8741c1e1982f5eae56099de85/include/linux/bits.h);

```c
#define BITS_PER_BYTE 8
```
From the Linux Kernel source tree [`linux/include/linux/kernel.h`](https://github.com/torvalds/linux/blob/219d54332a09e8d8741c1e1982f5eae56099de85/include/linux/kernel.h):

```c
#define DIV_ROUND_UP __KERNEL_DIV_ROUND_UP
```

From the Linux Kernel source tree [`include/uapi/linux/kernel.h`](https://github.com/torvalds/linux/blob/219d54332a09e8d8741c1e1982f5eae56099de85/include/uapi/linux/kernel.h):

```c
#define __KERNEL_DIV_ROUND_UP(n, d) (((n) + (d) - 1) / (d))
```

## X11 Reference

`XF86ModuleVersionInfo` data strucutre is defined in `xserver/hw/xfree86/common/xf86Module.h`

From the xserver source tree `xserver/include/input.h`:

```c
typedef struct _DeviceIntRec *DeviceIntPtr;
```

where `struct _DeviceIntRec` is defined in `xserver/include/inputstr.h` (not shown here for brevity).


`struct _XkbSrvInfo` is defined in `xserver/include/xkbsrv.h`.

`struct _XkbControls` is defined in `xserver/include/xkbstr.h`.

The function `InitKeyboardDeviceStruct()` is defined in `xkb/xkbInit.c`.

`xserver/include/input.h` defines:

```c
typedef struct {
    int click, bell, bell_pitch, bell_duration;
    Bool autoRepeat;
    unsigned char autoRepeats[32];
    Leds leds;
    unsigned char id;
} KeybdCtrl;
```

and

```c
typedef void (*KbdCtrlProcPtr) (DeviceIntPtr /*device */ ,
                                KeybdCtrl * /*ctrl */ );
```

gdb can help find the aliases of types more quickly:

```gdb
info types
```
