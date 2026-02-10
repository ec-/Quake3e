## Build Instructions

### windows/msvc

Install Visual Studio Community Edition 2017 or later and compile `quake3e` project from solution

`code/win32/msvc2017/quake3e.sln`

Copy resulting exe from `code/win32/msvc2017/output` directory

To compile with Vulkan backend - clean solution, right click on `quake3e` project, find `Project Dependencies` and select `renderervk` instead of `renderer`

---

### windows/msys2

Install the build dependencies:

`MSYS2 MSYS`

* pacman -Syu
* pacman -S make mingw-w64-x86_64-gcc mingw-w64-i686-gcc

Use `MSYS2 MINGW32` or `MSYS2 MINGW64` depending on your target system, then copy resulting binaries from created `build` directory or use command:

`make install DESTDIR=<path_to_game_files>`

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

### ppc64le / ppc64 (PowerPC 64-bit)

Install the build dependencies (same as generic linux above), then build with:

`make`

The JIT compiler (`vm_powerpc.c`) supports optional ISA-level optimizations that are enabled automatically based on compiler target flags:

* **ISA 2.07 (POWER8)**: Uses direct-move instructions (`mtvsrwa`, `mfvsrwz`, `xscvdpsxws`) to eliminate memory round-trips in float/int conversions (`OP_CVIF`, `OP_CVFI`)
* **ISA 3.0 (POWER9)**: Uses hardware modulo instructions (`modsw`, `moduw`) to replace 3-instruction sequences for `OP_MODI` and `OP_MODU`

To enable these optimizations, pass the appropriate `-mcpu` flag:

`make CFLAGS='-mcpu=power8'` - enable ISA 2.07 optimizations

`make CFLAGS='-mcpu=power9'` - enable ISA 2.07 + ISA 3.0 optimizations

`make CFLAGS='-mcpu=native'` - auto-detect based on build machine (note: resulting binary may not be portable to older hardware)

Without explicit `-mcpu`, the optimizations depend on the compiler/distro defaults. The JIT gracefully falls back to baseline instruction sequences when the target ISA level is not available.

---

Several Makefile options are available for linux/mingw/macos builds:

`BUILD_CLIENT=1` - build unified client/server executable, enabled by default

`BUILD_SERVER=1` - build dedicated server executable, enabled by default

`USE_SDL=0`- use SDL2 backend for video, audio, input subsystems, enabled by default, enforced for macos

`USE_VULKAN=1` - build vulkan modular renderer, enabled by default

`USE_OPENGL=1` - build opengl modular renderer, enabled by default

`USE_OPENGL2=0` - build opengl2 modular renderer, disabled by default

`USE_RENDERER_DLOPEN=1` - do not link single renderer into client binary, compile all enabled renderers as dynamic libraries and allow to switch them on the fly via `\cl_renderer` cvar, enabled by default

`RENDERER_DEFAULT=opengl` - set default value for `\cl_renderer` cvar or use selected renderer for static build for `USE_RENDERER_DLOPEN=0`, valid options are `opengl`, `opengl2`, `vulkan`

`USE_SYSTEM_JPEG=0` - use current system JPEG library, disabled by default

Example:

`make BUILD_SERVER=0 USE_RENDERER_DLOPEN=0 RENDERER_DEFAULT=vulkan` - which means do not build dedicated binary, build client with single static vulkan renderer
