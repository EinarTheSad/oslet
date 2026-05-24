# osLET – a 32-bit educational operating system

<img width="640" height="480" alt="osLET desktop" src="https://github.com/user-attachments/assets/27ac81d3-59a4-4739-8781-b4564ab4ecc7" />

------------------------------------------------------------------------

## Project build instructions

Ensure the following dependencies are installed (built in Ubuntu 24.04 LTS, may vary depending on your distro):

``` bash
sudo apt install build-essential gcc-multilib binutils grub-pc grub-common dosfstools util-linux qemu-system-x86
```

To create a fresh disk image, compile the OS with all its programs, install everything, and run it in QEMU (`sudo` is required):

``` bash
make full
```

For development, update the existing disk and then boot it:

``` bash
make clean update run
```

`make update` builds the kernel and programs, then copies the kernel, resources, apps, INI files, and groups into the existing disk image. `make run` only starts QEMU with the current `disk.img`; it does not mount or modify the disk.

For smaller updates:

``` bash
make kernel install      # kernel/resources only
make binaries binstall   # user programs only
make fileman binstall    # one program, then copy programs to disk
make run                 # boot without writing to disk
```

Use the one-program form only when you changed that program and no shared ABI or kernel-side code. If you changed `src/syscall.h`, `src/syscall.c`, `src/win/`, or another shared interface, use `make update` so the kernel and all user programs are rebuilt together.

Please note that while the system runs in **QEMU** and **PcEM**, it does not work in VirtualBox. VMware, Bochs, and actual hardware are yet to be tested.

------------------------------------------------------------------------

<img width="720" height="400" alt="Command line" src="https://github.com/user-attachments/assets/774ecf4a-e429-4028-9359-e1fa29bf5889" />
