## Build Instructions

### windows/msvc

Install Visual Studio Community Edition 2017 or later and compile `quake3e` project from solution

`code/win32/msvc2017/quake3e.sln`

Copy resulting exe from `code/win32/msvc2017/output` directory

To compile with Vulkan backend - clean solution, right click on `quake3e` project, find `Project Dependencies` and select `renderervk` instead of `renderer`

---

### windows/mingw

All build dependencies (libraries, headers) are bundled-in

Build with either `make ARCH=x86` or `make ARCH=x86_64` commands depending on your target system, then copy resulting binaries from created `build` directory or use command:

`make install DESTDIR=<path_to_game_files>`

---

### generic/ubuntu linux/bsd

You may need to run the following commands to install packages (using fresh ubuntu-18.04 installation as example):

* sudo apt install make gcc libcurl4-openssl-dev mesa-common-dev
* sudo apt install libxxf86dga-dev libxrandr-dev libxxf86vm-dev libasound-dev
* sudo apt install libsdl2-dev

Build with: `make`

Copy the resulting binaries from created `build` directory or use command:

`make install DESTDIR=<path_to_game_files>`

---

### Arch Linux

The package `quake3e-git` can either be installed through your favourite AUR helper, or manually using these commands:

Download the snapshot from AUR:

`curl -O https://aur.archlinux.org/cgit/aur.git/snapshot/quake3e-git.tar.gz`

Extract the snapshot:

`tar xfz quake3e-git.tar.gz`

Enter the extracted directory:

`cd quake3e-git`

Build and install `quake3e-git`:

`makepkg -risc`

---

### raspberry pi os

Install the build dependencies:

* apt install libsdl2-dev libxxf86dga-dev libcurl4-openssl-dev

Build with: `make`

Copy the resulting binaries from created `build` directory or use command:

`make install DESTDIR=<path_to_game_files>`

---

### macos

* install the official SDL2 framework to /Library/Frameworks
* `brew install molten-vk` or install Vulkan SDK to use MoltenVK library

Build with: `make`

Copy the resulting binaries from created `build` directory

---

Several Makefile options are available for linux/mingw/macos builds:

`BUILD_CLIENT=1` - build unified client/server executable, enabled by default

`BUILD_SERVER=1` - build dedicated server executable, enabled by default

`USE_SDL=0`- use SDL2 backend for video, audio, input subsystems, enabled by default, enforced for macos

`USE_VULKAN=1` - build vulkan modular renderer, enabled by default

`USE_SYSTEM_JPEG=0` - use current system JPEG library, disabled by default

Example:

`make BUILD_SERVER=0 USE_RENDERER_DLOPEN=0 RENDERER_DEFAULT=vulkan` - which means do not build dedicated binary, build client with single static vulkan renderer
