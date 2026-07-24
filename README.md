# xf86-input--gamepad
xf86 driver development for gamepads

I am writing this driver to learn about GNU/Linux, I am aware that new input drivers should be written with `libinput`. This project has taken me to read (some of) the linux kernel, dbus, libudev, systemd, and xserver code to help me understand the overall process by looking at the source.

This driver does not intend to handle analog signals (in the form of events) at this point, for now the driver is meant to handle events for action buttons and direction-pads (dpads).

**Development status**: writing exploratory code to learn about the Linux legacy joystick event handling.

## Development Progress

### Week 1: Getting to know my Gamepad through system calls

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

**Gamepad Mappings**

Wrote an event-loop that processes joystick events as they happen (one at a time). The fields of the `struct js_event` are logged on the console along the button and axes mappings (commit [3ce9d33e](https://github.com/misael-diaz/xf86-input-gamepad/tree/3ce9d33e464258415847b83b1d5aeb47466b2129)).

### Week 2 Reading XServer code

Mostly reading the Xserver source code and also the xf86-input-joystick implementation. This makes sense because this the first driver that I write a driver for the xserver.

### Week 3 Driver Pre-Initialization

- **debugging**: debugging the xserver as it is running is totally a game changer because I can see what the server does just before it calls the driver. The xserver is a massive codebase and so reading alone is not enough, though I am starting to develop a mental model of what the server does when I plugin the gamepad device.

- **mapping device**: udev reports the /dev/input/jsX device when I plug the gamepad and this not what we want because if we used it we would be using the legacy linux input API for joysticks. Instead during PreInit we find the /dev/input/eventX device that matches the device name that udev finds. It's important to take into account that the xserver polls udev socket to know when devices are plugged in and removed. This work in particular was completed in commit [d068c1c5](https://github.com/misael-diaz/xf86-input-gamepad/tree/d068c1c5a48bbbce4d9ebab0f325c50bc489cf27).

- **mod races**: have realized that using the module data for storing device info is not a good idea and can lead to data race issues and even make it impossible to support multiple gamepads at the same time. The proper approach is to use the `private` field of the device record to store the data instead, that's what other developers have used to store the device data and it's the right call. Have realized that myself by thinking about the problem not by asking an LLM about it. This has been done in commit [47cc0181](https://github.com/misael-diaz/xf86-input-gamepad/tree/47cc0181157c804a4ec25aca0092ccd8df02989b)

- **server loads drivers once**: dynamic unloading of input drivers is not supported by the xserver even if the API looks at first like it does. The driver setup is called during startup, period. Removing a device only frees duplicate driver data, and this does not trigger calling the teardown procedure because the xserver is designed to work with the duplicated driver.

- **hotloading**: managed to implement input driver hotloading by changing the driver name just before deleting the input driver. Note that the code is modifying the internal input driver that the xserver uses when looking up input drivers. The code uses the same input-driver lookup function to get direct access: commit [4455fc74](https://github.com/misael-diaz/xf86-input-gamepad/tree/4455fc74be50ea089e7cc898eadaeb0e2329a7f0). I have not seen other drivers implement hotloading because it is not how the xserver operates but this is fine for debugging drivers.

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
	MatchDevicePath "/dev/input/js*"
EndSection
```

note that we use MathDevicePath to identify the gamepad driver only and this is why we use `/dev/input/js*` because that's what `udev` is going to report to the xserver when the gamepad is connected to the computer.

## Build

The following command builds the driver as a shared object (\*.so) that the xserver can load:

```sh
gcc -I/usr/include/pixman-1 -Wall -Wextra -Wformat -Wno-comment -fPIC -shared -O0 -gdwarf-4 -g main.c -o gamepad.so
```

Don't forget to update the module (`*.so`) in the location where the xserver expectes the input driver modules. In my system that location is


```sh
/usr/lib/xorg/modules/input
```

and don't forget to update the owner and file permissions

```sh
chown root.root gamepad.so
chmod 644 gamepad.so
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

GDB is smart enough to know that you have local access and so it will recommend you to use `set sysroot` and so you do to get faster access to the files:

```gdb
set sysroot
```

And that's one step done, more to go.

You will get the symbols but gdb won't be able to find the source files because you don't have the build environment that the maintainer used and so you need to substitute the path; set a breakpoint first such as `dix_main` (this the closest breakpoint after the entry point for Linux) and then ask gdb for the source info

```gdb
b dix_main
```

that sets the breakpoint and then hit continue

```gdb
c
```

and you will see

```gdb
Breakpoint 1 at 0x5555555b4eb0: file ../../../../dix/main.c, line 127.
```

```gdb
info source
```

```gdb
Current source file is ../../../../dix/main.c
Compilation directory is ./debian/build/main/dix
Source language is c.
Producer is GNU C17 9.4.0 -mtune=generic -march=x86-64 -g -O2 -fno-strict-aliasing -fvisibility=hidden -fstack-protector-strong -fPIC -fasynchronous-unwind-tables -fstack-protector-strong -fstack-clash-protection -fcf-protection.
Compiled with DWARF 2 debugging format.
Does not include preprocessor macro info.
```

what you have to do is replace the compilation directory `./debian/build/main/dix` (see second line of output above) with the actual path to the `dix` directory in your machine


```gdb
(gdb) set substitute-path ./debian/build/main/dix /full/path/to/xserver/dix
```

if that worked you should be able to list the source file contents:

```gdb
(gdb) l
warning: Source file is more recent than executable.
122
123	CallbackListPtr RootWindowFinalizeCallback = NULL;
124
125	int
126	dix_main(int argc, char *argv[], char *envp[])
127	{
128	    int i;
129	    HWEventQueueType alwaysCheckForInput[2];
130
131	    display = "0";
```

it's likely that you will see the warning but it's safe. Just make sure that you are looking at the same version of the xserver, the tag should match the version in my case the version 1.20.13:

```sh
git log
```

the output of the `git-log` command shows the matching tag

```
commit 86a72cb1927dd91132d231bb7920b651704601ef (HEAD -> v1.20.13, tag: xorg-server-1.20.13
```

you can see all tags via:

```sh
git tag -l
```

and if you have not done so you can create a branch to that tag

```sh
git branch --no-track v1.20.13 xorg-server-1.20.13
```

where `v1.20.13` is the branch name and `xorg-server-1.20.13` is the tag.

It's likely that you may have to repeat the path-subtitution step a couple of times as you trace the execution of the xserver. Let me know if you find a better way.

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
