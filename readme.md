# Project build instructions

Ensure the following dependencies are installed:

``` bash
sudo apt install gcc make build-essential fdisk grub-pc qemu-system
```

The following sequence will compile the OS and run it in QEMU:

``` bash
make
make disk
make shell
make binstall
make run
```

You can compile each binary separately by specifying its name. Remember to then run ```make binstall``` to copy it to the virtual hard drive.

For more specific information, please consult the Makefile.

------------------------------------------------------------------------

## Screenshots

### Command prompt

<img width="720" height="400" alt="Command prompt" src="https://github.com/user-attachments/assets/b1043630-eb6c-4014-bd7c-ecb07de0988e" />

### ELF executable binaries

<img width="720" height="400" alt="neofetch" src="https://github.com/user-attachments/assets/91fedd22-ba1e-466d-a471-7d67906d3c1a" />

### File manager

<img width="720" height="400" alt="File manager" src="https://github.com/user-attachments/assets/14e17034-b19b-4780-863f-73aca585b15f" />

### Text editor

<img width="720" height="400" alt="Text editor" src="https://github.com/user-attachments/assets/9cf3921f-ec42-43d5-869d-f97cc4a1501e" />

### VGA graphics

<img width="640" height="480" alt="VGA demo" src="https://github.com/user-attachments/assets/db726fe7-633d-4976-a9bc-7732406eaaa5" />

<img width="640" height="480" alt="Bitmap" src="https://github.com/user-attachments/assets/57ffbe21-6603-4ad4-b2b9-6b4e2b14adf8" />

### Desktop

<img width="640" height="480" alt="Desktop" src="https://github.com/user-attachments/assets/1c494309-787b-4fed-9497-b3a0eb7a6d79" />


------------------------------------------------------------------------
