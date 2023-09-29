# SidecarT

A cartridge emulator for the Atari ST, STE, and Mega series on Raspberry Pi Pico steroids.

## What is SidecarT?

SidecarT is a state-of-the-art cartridge ROM emulator crafted for the Atari ST, STE, and Mega series. It leverages the robust capabilities of the RP2040 microcontroller found in the Raspberry Pi Pico, enabling it to:

- Emulate both 64Kbyte and 128Kbyte ROMs by simply loading the binary files from a microSD card or via a Wi-Fi connection.

- Interact with the cartridge bus in real-time for data reading and writing, which allows for seamless emulation of devices such as hard disks, floppy disks, real-time clocks, keyboards, mouse devices, and more.

- Dive into a realm of possibilities, limited only by your creativity. Thanks to its open architecture and the open-source code available here, SidecarT can evolve to meet any challenge you envision.

## Features

- Versatile ROM Emulation: Easily emulate 64Kbyte and 128Kbyte ROMs. With SidecarT, switching between ROMs is a breeze—simply load the binary files either from a microSD card or directly via a Wifi connection.

- Real-time Cartridge Bus Interaction: SidecarT isn't just for ROMs. It's built to interact with the cartridge bus on-the-fly, making data reading and writing seamless. This real-time integration allows for an enriched experience, especially when emulating devices.

- Diverse Device Emulation: From hard disks and floppy disks to real-time clocks, keyboards, and mouse devices, SidecarT has you covered. Its advanced capabilities ensure you get an authentic emulation experience across a range of devices.

- Open Source & Customizable: At its core, SidecarT is designed for innovation. With open-source code and architecture, you have the freedom to tweak, modify, and expand its functionalities. It's not just an emulator—it's a canvas for all your tech endeavors.

- Powered by Raspberry Pi Pico: Thanks to the prowess of the RP2040 microcontroller in the Raspberry Pi Pico, SidecarT delivers exceptional performance and reliability. It's the perfect blend of old-school charm and modern-day tech.

- Ever-evolving Capabilities: The world of Atari ST and its series is vast, a major retro platform. SidecarT is built to evolve, ensuring that you're always at the forefront of emulation technology.

## Binary Releases

The rest of the document is intended for developers willing to customize on their own the software and also the hardware of the SidecarT board. If you are not interested in that, you can download the latest binary release from the [Releases page](https://github.com/diegoparrilla/atarist-sidecart-raspberry-pico/releases). The latest stable release is always recommended.

If you need help or have any questions, please visit the website of the project [SidecarT](https://sidecart.xyz).


## Requirements

### If you only want to use SidecarT

- A SidecarT board. You can build your own or purchase one from the [SidecarT website](https://sidecart.xyz).

- A Raspberry Pi Pico W with Headers.

- An Atari ST / STE / Mega computer. Forget about emulators, this project needs real retro hardware. You can find Atari ST computers in eBay for less than 100€.

### If you want to contribute to the project

- If you are a developer, you'll need a another Raspberry Pi Pico or a RP2040 based board that will act as a picoprobe hardware debugger. 

- The [atarist-toolkit-docker](https://github.com/diegoparrilla/atarist-toolkit-docker) is pivotal. Familiarize yourself with its installation and usage.

- A `git` client, command line or GUI, your pick.

- A Makefile attuned with GNU Make.

- Visual Studio Code is highly recommended if you want to debug the code. You can find the configuration files in the [.vscode](.vscode) folder. 

## Building your own SidecarT board

You can build your own SidecarT board. The hardware design is open source and you can find the EasyEDA schematics project in the [schematics](schematics) folder. Please read the [README](/schematics/README.md) file for more information.


## Set up the development environment

### Setup a Raspberry Pi Pico development environment from scratch

If you are not familiar with the development on the Raspberry Pi Pico or RP2040, we recommend you to follow the [Getting Started with Raspberry Pi Pico](https://datasheets.raspberrypi.org/pico/getting-started-with-pico.pdf) guide. This guide will help you to setup the Raspberry Pi Pico development environment from scratch.

We also think it's very important to setup the picoprobe debugging hardware. The picoprobe is a hardware debugger that will allow you to debug the code running on the Raspberry Pi Pico. You can find more information in this two excellent tutorials from Shawn Hymel:
- [Raspberry Pi Pico and RP2040 - C/C++ Part 1: Blink and VS Code](https://www.digikey.es/en/maker/projects/raspberry-pi-pico-and-rp2040-cc-part-1-blink-and-vs-code/7102fb8bca95452e9df6150f39ae8422)
- [Raspberry Pi Pico and RP2040 - C/C++ Part 2 Debugging with VS Code](https://www.digikey.es/en/maker/projects/raspberry-pi-pico-and-rp2040-cc-part-2-debugging-with-vs-code/470abc7efb07432b82c95f6f67f184c0)

To support the debugging the SidecarT has four pins that are connected to the picoprobe hardware debugger. These pins are:
- UART TX: This pin is used to send the debug information from the RP2040 to the picoprobe hardware debugger.
- UART RX: This pin is used to send the debug information from the picoprobe hardware debugger to the RP2040.
- GND: Two ground pins. One MUST connect to the GND of the Raspberry Pi Pico W (the middle connector between DEBUG and SWCLK and SWDIO) and the other MUST connect to the GND of the picoprobe hardware debugger. Don't let this pins floating, otherwise the debugging will not work.

Also a good tutorial about setting up a debugging environment with the picoprobe can be found in the [Raspberry Pi Debug Probe](https://www.raspberrypi.com/documentation/microcontrollers/debug-probe.html) tutorial.

Trying to develop software for this microcontroller without the right environment is frustrating and a waste of time. So please, take your time to setup the development environment properly. It will save you a lot of time in the future.

#### Configure environment variables

The following environment variables are required to be set:

- `PICO_SDK_PATH`: The path to the Raspberry Pi Pico SDK.
- `PICO_EXTRAS_PATH`: The path to the Raspberry Pi Pico Extras SDK.
- `FATFS_SDK_PATH`: The path to the FatFS SDK.

This repository contains subrepos pointing to these SDKs in root folder.

All the compile, debug and build scripts use these environment variables to locate the SDKs. So it's very important to set them properly. An option would be to set them in your `.bashrc` file if you are using Bash, or in your `.zshrc` file if you are using ZSH. 

#### Configure Visual Studio Code

To configure Visual Studio Code to work with the Raspberry Pi Pico, please follow the [Raspberry Pi Pico and RP2040 - C/C++ Part 2 Debugging with VS Code](https://www.digikey.es/en/maker/projects/raspberry-pi-pico-and-rp2040-cc-part-2-debugging-with-vs-code/470abc7efb07432b82c95f6f67f184c0) tutorial.

The `.vscode` folder contains the configuration files for Visual Studio Code. **Please modify them as follows**:

- `launch.json`: Modify the `gdbPath` property to point to the `arm-none-eabi-gdb` file in your computer.
- `launch.json`: Modify the `searchDir` property to point to the `/tcl` folder inside the `openocd` project in your computer.
- `settings.json`: Modify the `cortex-debug.gdbPath` property to point to the `arm-none-eabi-gdb` file in your computer.


#### Install Visual Studio Code extensions

- [C/C++ Extension Pack](https://marketplace.visualstudio.com/items?itemName=ms-vscode.cpptools-extension-pack)
- [CMake](https://marketplace.visualstudio.com/items?itemName=twxs.cmake)
- [CMake Tools](https://marketplace.visualstudio.com/items?itemName=ms-vscode.cmake-tools)
- [Cortex-Debug](https://marketplace.visualstudio.com/items?itemName=marus25.cortex-debug)

## Building the firmware
### From the command line

To build a production ready firmware for the SidecarT board, follow these steps:

1. Clone this repository:

```
$ git clone https://github.com/diegoparrilla/atarist-sidecart-raspberry-pico.git
```

2. Navigate to the cloned repository:

```
cd atarist-sidecart-raspberry-pico
```

3. Trigger the `build.sh` script to build the firmware:

```
./build.sh
```

4. The `dist` folder now houses the file: `sidecart-pico_w.uf2`, which needs to be copied to the RP2040 in the Raspberry Pi Pico W. This can be done by connecting the Raspberry Pi Pico W to your computer via USB and copying the file to the `RPI-RP2` drive.


### From a Github Action

In the folder `/.github` of the root directory of the project you can find the file `build.yml`. This file contains the configuration of the Github Action that will build the firmware.

The `release.yml` will create a release in Github with the firmware binary file.

### From Visual Studio Code

It's also possible to build the firmware from Visual Studio Code, but probably it's a better idea to build it in Debug mode and launch it from Visual Studio Code. Please read the section about setting up the debugging environment above.

## Development

The development on the RP2040 of the Raspberry Pi Pico is straightforward if you have setup the environment previously. The RP2040 is a Cortex-M0+ microcontroller and the development is done in C. The RP2040 is a very powerful microcontroller and it's possible to develop very complex applications for it. Apart from the C code have also to use the PIO (Programmable IO) to develop the I/O section with the Atari ST cartridge. The PIO is a very powerful peripheral that allows you to develop very complex applications in a very easy way.

Here goes a list of things to take into account when developing:

1. The source code is inside the `romemul` folder. This name could change in the future.
2. `memmap_romemul.ld` is the linker script used to link the code. It contains the different memory sections that the Sidecart needs. Please don't change these values if you don't know what you are doing. The RAM for the RP2040 has been reduced to 128Kbytes to keep the Atari ST ROMs in the RAM for performance reasons. Also, don't modify the space needed for configuration data. This data is used to store the configuration of the SidecarT board and it's used by the `CONFIGURATOR` tool.
3. CMakelists.txt is the file used by the CMake tool to build the project.

A special note about the `firmware.c` file. This file is an array generated with the python script `download_firmware.py`. This script downloads the latest version of the Atari ST firmware contained in the repository [atarist-sidecart-firmware](https://github.com/diegoparrilla/atarist-sidecart-firmware). Hence, the code embeds the Atari ST firmware in the SidecarT firmware. This is done to simplify the development and to avoid the need to flash the Atari ST firmware in the RP2040. **As a rule of thumb, if you modify the Atari ST firmware, you have to regenerate the `firmware.c` file. To do that, just run the `download_firmware.py` script.**

## Releases

For releases, head over to the [Releases page](https://github.com/diegoparrilla/atarist-sidecart-raspberry-pico/releases). The latest release is always recommended.

For a quick tutorial about how to flash the firmware in the Raspberry Pi Pico, please read the [Quickstart](https://sidecart.xyz/quickstart).

## Changelog

The full changelog is available in the [CHANGELOG.md](CHANGELOG.md) file. 

## Resources 

- [Sidecart ROM Emulator website](https://sidecart.xyz)
- [Sidecart Atari ST Firmware](https://github.com/diegoparrilla/atarist-sidecart-firmware).
- [Sidecart Atari ST Test ROM](https://github.com/diegoparrilla/atarist-sidecart-test-rom).

## Contribute

Thank you for your interest in contributing to this repository! We welcome and appreciate contributions from the community. Here are a few ways you can contribute:

- Report bugs: If you find a bug in the code, please let us know by opening an issue. Be sure to include details about the error, how to reproduce it, and any possible workarounds.

- Suggest new features: Have an idea for a new feature or improvement? We'd love to hear about it. Open an issue to start a discussion.

- Contribute code: If you're a developer and want to contribute code to this project, here are a few steps to get started:
    * Fork the repository and clone it to your local machine.
    * Create a new branch for your changes.
    * Make your changes, including tests to cover your changes.
    * Run the tests to ensure everything is working.
    * Commit your changes and push them to your fork.
    * Open a pull request to this repository.


- Write documentation: If you're not a developer, you can still contribute by writing documentation, such as improved usage examples or tutorials.

- Share feedback: Even if you don't have any specific changes in mind, we welcome your feedback and thoughts on the project. You can share your feedback by opening an issue or by joining the repository's community.

We appreciate any and all contributions, and we'll do our best to review and respond to your submissions in a timely manner. Please note that all contributors are expected to follow our code of conduct (one day we will have one!). Thank you for your support!

## Licenses

The source code of the project is licensed under the GNU General Public License v3.0. The full license is accessible in the [LICENSE](LICENSE) file. 

The design and schematics of the hardware are licensed under the [Attribution-NonCommercial-ShareAlike 4.0 International (CC BY-NC-SA 4.0)](https://creativecommons.org/licenses/by-nc-sa/4.0/), specifically all the designs and schematics in every single file in the `/schematics` folder and its subfolders.

