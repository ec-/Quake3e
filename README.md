
                ,---------------------------------------------.
                |   ____              _             _  _____  |
                |  / __ \            | |           | |/ ____| |
                | | |  | |_   _  __ _| | _____     | | (___   |
                | | |  | | | | |/ _` | |/ / _ \_   | |\___ \  |
                | | |__| | |_| | (_| |   |  __| |__| |____) | |
                |  \___\_\\__,_|\__,_|_|\_\___|\____/|_____/  |
                |                                             |
                '----------- https://quake.games -------------'
                                        
This project derives from https://github.com/ec-/Quake3e and https://github.com/inolen/quakejs.
It is not the intention of this project to add features that are not compatible with
either of those two forks or the original Quake 3 source code release by id Software.
The intention of this project is to give developers the tools needed to run Quake 3
in a modern web browser, including Android Mobile Chrome.

QuakeJS is a port of [ioquake3](https://github.com/ioquake/ioq3) to JavaScript with the help of [Emscripten](https://emscripten.org/index.html).

To see a live demo, check out https://quake.games or http://www.quakejs.com

Some of the major features currently implemented are:

  * A working repack script to convert game assets to be more web compatible
  * Deferred (lazy) loading of all game content, entities, models, textures, etc
  * [NippleJS](https://github.com/yoannmoinet/nipplejs) mobile support
  * A content server (NodeJS + express) to repack and live-reload the game as you develop
  * A SOCKS5 server to convert web-socket connections to UDP and connect to any standard Quake 3 server
  * Various mod disassemblies for associating hard-coded shaders with files even if the mod isn't open source
  * Various graphs of mods, including file names for repacked content
  * PNG support
  * Docker support
  * Many, many bug fixes

Coming soon!
  * Huffman decoding for proxy, Man-In-The-Middle always on twitch.tv streaming at no expense to the game server
  * Repacking-as-a-service, uploader for repacking game content
  * Procedurally generated game content and maps
  * Many mod support, compiling and playing lots of different game types, capture the flag with 3+ teams
  * Campaign mode, playing older engine content and playing as enemy characters, new AI for old enemies
  * Remove SDL usage because EMSCRIPTEN uses SDL1, but the engine expects SDL2
  * Updated WebGL renderer

The map editor and associated compiling tools are not included. We suggest you
use a modern copy from http://icculus.org/gtkradiant/.

# Compilation and installation
As a prerequisite, you will need to install the dependencies specific to your
 operating system from ioq3 https://github.com/ioquake/ioq3#compilation-and-installation

```
git clone --recurse-submodules --remote-submodules git@github.com:briancullinan/planet_quake.git
cd planet_quake
```
or 
```
git submodule update --init
```
then
```
./misc/quakejs/lib/emsdk/emsdk install latest-upstream
./misc/quakejs/lib/emsdk/emsdk activate latest
make PLATFORM=js
```

Binaries will be placed in `planet_quake/build/release-js-js/`.

For instructions on how to build a native dedicated server please see the
 requirements on ioq3 https://github.com/ioquake/ioq3#compilation-and-installation

# Console

See the console commands from ioq3 https://github.com/ioquake/ioq3#console

Some client variables have been set by default for compatibility, those are listed here:
https://github.com/briancullinan/planet_quake/blob/ioq3-quakejs/code/sys/sys_browser.js

# Docker

Build the image from this repository:

`docker build -t quake3e .`

`docker run -ti -v .:/tmp/Quake3e -v /Applications/ioquake/baseq3:/tmp/baseq3 -p 8080:8080 -p 1081:1081 -p 27960:27960/udp --name quake3e quake3e`

After the image is built and running, you can skip repeating the conversion process:

`docker commit -t quake3e quake3e`

# Running content server and repacking

TODO: https://github.com/inolen/quakejs/issues/61#issuecomment-583676811

# README for Developers

## pk3dir

ioquake3 has a useful new feature for mappers. Paths in a game directory with
the extension ".pk3dir" are treated like pk3 files. This means you can keep
all files specific to your map in one directory tree and easily zip this
folder for distribution.

## 64bit mods

If you wish to compile external mods as shared libraries on a 64bit platform,
and the mod source is derived from the id Q3 SDK, you will need to modify the
interface code a little. Open the files ending in `_syscalls.c` and change
every instance of int to intptr_t in the declaration of the syscall function
pointer and the dllEntry function. Also find the vmMain function for each
module (usually in cg_main.c g_main.c etc.) and similarly replace the return
value in the prototype with intptr_t (arg0, arg1, ...stay int).

Add the following code snippet to q_shared.h:

    #ifdef Q3_VM
    typedef int intptr_t;
    #else
    #include <stdint.h>
    #endif

Note if you simply wish to run mods on a 64bit platform you do not need to
recompile anything since by default Q3 uses a virtual machine system.

## Creating mods compatible with Q3 1.32b

If you're using this package to create mods for the last official release of
Q3, it is necessary to pass the commandline option '-vq3' to your invocation
of q3asm. This is because by default q3asm outputs an updated qvm format that
is necessary to fix a bug involving the optimizing pass of the x86 vm JIT
compiler.

## Creating standalone games

Have you finished the daunting task of removing all dependencies on the Q3
game data? You probably now want to give your users the opportunity to play
the game without owning a copy of Q3, which consequently means removing cd-key
and authentication server checks. In addition to being a straightforward Q3
client, ioquake3 also purports to be a reliable and stable code base on which
to base your game project.

However, before you start compiling your own version of ioquake3, you have to
ask yourself: Have we changed or will we need to change anything of importance
in the engine?

If your answer to this question is "no", it probably makes no sense to build
your own binaries. Instead, you can just use the pre-built binaries on the
website. Just make sure the game is called with:

    +set com_basegame <yournewbase>

in any links/scripts you install for your users to start the game. The
binary must not detect any original quake3 game pak files. If this
condition is met, the game will set com_standalone to 1 and is then running
in stand alone mode.

If you want the engine to use a different directory in your homepath than
e.g. "Quake3" on Windows or ".q3a" on Linux, then set a new name at startup
by adding

    +set com_homepath <homedirname>

to the command line. You can also control which game name to use when talking
to the master server:

    +set com_gamename <gamename>

So clients requesting a server list will only receive servers that have a
matching game name.

Example line:

    +set com_basegame basefoo +set com_homepath .foo
    +set com_gamename foo

If you really changed parts that would make vanilla ioquake3 incompatible with
your mod, we have included another way to conveniently build a stand-alone
binary. Just run make with the option BUILD_STANDALONE=1. Don't forget to edit
the PRODUCT_NAME and subsequent #defines in qcommon/q_shared.h with
information appropriate for your project.

## PNG support

ioquake3 supports the use of PNG (Portable Network Graphic) images as
textures. It should be noted that the use of such images in a map will
result in missing placeholder textures where the map is used with the id
Quake 3 client or earlier versions of ioquake3.

Recent versions of GtkRadiant and q3map2 support PNG images without
modification. However GtkRadiant is not aware that PNG textures are supported
by ioquake3. To change this behaviour open the file 'q3.game' in the 'games'
directory of the GtkRadiant base directory with an editor and change the
line:

    texturetypes="tga jpg"

to

    texturetypes="tga jpg png"

Restart GtkRadiant and PNG textures are now available.

## Building with MinGW for pre Windows XP

IPv6 support requires a header named "wspiapi.h" to abstract away from
differences in earlier versions of Windows' IPv6 stack. There is no MinGW
equivalent of this header and the Microsoft version is obviously not
redistributable, so in its absence we're forced to require Windows XP.
However if this header is acquired separately and placed in the qcommon/
directory, this restriction is lifted.


# Contributing

# Credits

Maintainers

  * Brian J. Cullinan <megamindbrian@gmail.com>
  * Anyone else? Looking for volunteers

Significant contributions from

  * @klaussilveira, @inolen, @NTT123 (JS build mode)
  * Ryan C. Gordon <icculus@icculus.org>
  * Andreas Kohn <andreas@syndrom23.de>
  * Joerg Dietrich <Dietrich_Joerg@t-online.de>
  * Stuart Dalton <badcdev@gmail.com>
  * Vincent S. Cojot <vincent at cojot dot name>
  * optical <alex@rigbo.se>
  * Aaron Gyes <floam@aaron.gy>
