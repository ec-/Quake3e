# Quake3e

This is a modern Quake III Arena engine aimed to be fast, secure and compatible with all existing Q3A mods.
It is based on last non-SDL source dump of ioquake3 with latest upstream fixes applied.

**Key features**:

* optimized OpenGL renderer with per-pixel dynamic lights, bloom, antialiasing, greyscale options, etc.
* optimized Vulkan renderer
* significally reworked QVM (Quake Virtual Machine)
* improved server-side DoS protection, much reduced memory usage
* raized filesystem limits (up to 20.000 maps can be handled in a single directory)
* reworked Zone memory allocator, no more out-of-memory errors
* non-intrusive support for SDL2 backend (video,audio,input), selectable at compile time
* tons of bugfixes and other improvements

*This repository do not contains any game content so in order to play you must copy resulting binaries into your existing Quake III Arena installation*

## Build Instructions

### windows/msvc 

Install Visual Studio Community Edition 2017 or later and compile `quake3e` project from solution

`code/win32/msvc2017/quake3e.sln`

Copy resulting exe from `code/win32/msvc2017/output` directory

To compile with Vulkan backend - clean solution, right click on `quake3e` project, find `Project Dependencies` and select `renderervk` instead of `renderer`

### windows/mingw

All build dependencies (libraries, headers) are bundled-in

Build with either `make ARCH=x86` or `make ARCH=x86_64` commands depending from your target system, then copy resulting binaries from created `build` directory or use command: 

`make install DESTDIR=<path_to_game_files>`

### linux/bsd

You may need to run following commands to install packages (using fresh ubuntu-18.04 installation as example):

* sudo apt install make gcc libcurl4-openssl-dev mesa-common-dev
* sudo apt install libxxf86dga-dev libxrandr-dev libxxf86vm-dev libasound-dev
* sudo apt install libsdl2-dev

Build with: `make`

Copy resulting binaries from created `build` directory or use command: 

`make install DESTDIR=<path_to_game_files>`

Several make options available for linux/mingw builds:

`BUILD_CLIENT=1` - build unified client/server executable, enabled by default

`BUILD_SERVER=1` - build dedicated server executable, enabled by default

`USE_SDL=0`- use SDL2 backend for video, audio, input subsystems, disabled by default

`USE_VULKAN=0` - link client with vulkan renderer instead of OpenGL, disabled by default

`USE_RENDERER_DLOPEN=0` - do not link single renderer into client binary, compile all renderers as dynamic libraries and allow to switch them on the fly via `\cl_renderer` cvar, disabled by default - *not recommented due to not stable renderer API*

Example:

`make BUILD_SERVER=0 USE_VULKAN=1` - which means do not build dedicated binary, build client with static vulkan renderer

## Links

* https://bitbucket.org/CPMADevs/cnq3
* https://github.com/ioquake/ioq3
* https://github.com/kennyalive/Quake-III-Arena-Kenny-Edition
* https://github.com/OpenArena/engine
