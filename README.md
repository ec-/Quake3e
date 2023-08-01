# Quake3xe

This is a [Quake3e](https://github.com/ec-/Quake3e) client with [xq3e](https://github.com/xq3e/engine) features and some my fixes and changes:


- **\x_con_chat_antispam_all 0..1**
- **\x_con_chat_antispam_team 0..1**
- **\x_con_chat_antispam_private 0..1**

Block messages for common, team or private chat. Messages will be blocked if user and text same as in the previous message.

- **\x_snd_kill_sound 0..4**

Play sound if got a frag. 0 - sound is disbled, 1-4 different variants of the sound.
**\cg_centertime** command is cheat unprotected now (osp mode only) so you can disable frag text messages by set it to zero.

- **\x_wp_autoswitch 0..9**

Automatically switch to the specified weapon if the player was dead. 0 - disabled, 1-9 weapon number. 

For **Quake3e** information see [README.q3e.md](README.q3e.md).

For **xq3e** information see [README.xq3e.md](README.xq3e.md).


## Build Instructions

### windows/msvc

#### msvc2017

Build is similar to q3e:

Compile `quake3e` project from solution

`code/win32/msvc2017/quake3e.sln`

Copy resulting exe from `code/win32/msvc2017/output` directory

To compile with Vulkan backend - clean solution, right click on `quake3e` project, find `Project Dependencies` and select `renderervk` instead of `renderer`

---

### ubuntu

You may need to run the following commands to install packages (using fresh ubuntu-18.04 installation as example):

* sudo apt install make gcc libcurl4-openssl-dev mesa-common-dev
* sudo apt install libxxf86dga-dev libxrandr-dev libxxf86vm-dev libasound-dev
* sudo apt install libsdl2-dev
* sudo apt install 7z

Build with: `make`

Copy the resulting binaries from created `build` directory or use command:

`make install DESTDIR=<path_to_game_files>`

Change dir to `xq3e_pak`, run `pack.cmd` and copy `xq3e.pak` into your `baseq3` directory.

For more information about Makefile options see [BUILD.md](BUILD.md)

---

### Other platforms

Not tested, see [BUILD.md](BUILD.md). 


