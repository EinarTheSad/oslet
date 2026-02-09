# osLET – a 32-bit educational operating system

<img width="640" height="480" alt="osLET desktop" src="https://github.com/user-attachments/assets/3b8cb53b-7f56-439d-be80-4ce3384ef6f6" />

------------------------------------------------------------------------

## Project build instructions

Ensure the following dependencies are installed (built in Ubuntu 24.04 LTS, may vary depending on your distro):

``` bash
sudo apt install build-essential gcc-multilib binutils grub-pc grub-common dosfstools util-linux qemu-system-x86
```

The following sequence will compile the OS and run it in QEMU:

``` bash
make disk shell desktop binstall run
```

You can compile each binary separately. Remember to then run ```make binstall``` to copy it to the virtual hard drive. For more specific information, please consult the Makefile.

Please note that while the system runs in **QEMU** and **PcEM**, it experiences VGA driver problems in VirtualBox. VMware, Bochs, and actual hardware are yet to be tested.

------------------------------------------------------------------------

<img width="720" height="400" alt="Command line" src="https://github.com/user-attachments/assets/774ecf4a-e429-4028-9359-e1fa29bf5889" />